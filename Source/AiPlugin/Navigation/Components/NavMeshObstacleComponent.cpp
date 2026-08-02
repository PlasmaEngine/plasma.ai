#include <AiPlugin/AiPluginPCH.h>
#include <AiPlugin/Navigation/Components/NavMeshObstacleComponent.h>
#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/World/World.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Configuration/CVar.h>

plCVarBool cvar_NavMeshDynamicObstacles("AI.Navmesh.DynamicObstacles", true, plCVarFlags::Default, "Rebuild navmesh sectors when obstacle components move.");

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plNavMeshObstacleComponent, 3, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Mode", plAiNavObstacleMode, m_Mode),
    PL_MEMBER_PROPERTY("MoveThreshold", m_fMoveThreshold)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.05f, 100.0f)),
    PL_MEMBER_PROPERTY("ReinvalidateCooldown", m_ReinvalidateCooldown)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(0.5))),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(InvalidateSectors),
  }
  PL_END_FUNCTIONS;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Navigation"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plNavMeshObstacleComponent::plNavMeshObstacleComponent() = default;
plNavMeshObstacleComponent::~plNavMeshObstacleComponent() = default;

void plNavMeshObstacleComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_fMoveThreshold;
  s << m_ReinvalidateCooldown;
  s << m_Mode;
}

void plNavMeshObstacleComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& s = inout_stream.GetStream();

  if (uiVersion >= 2)
  {
    s >> m_fMoveThreshold;
    s >> m_ReinvalidateCooldown;
  }

  if (uiVersion >= 3)
  {
    s >> m_Mode;
  }
}

void plNavMeshObstacleComponent::OnActivated()
{
  SUPER::OnActivated();

  if (IsSimulationStarted())
    InvalidateSectors();
}

void plNavMeshObstacleComponent::OnSimulationStarted()
{
  plComponent::OnSimulationStarted();

  InvalidateSectors();
}

void plNavMeshObstacleComponent::OnDeactivated()
{
  // drop the carve volume BEFORE invalidating, so the rebuild heals the hole
  if (m_uiCarveID != plAiNavMeshWorldModule::InvalidObstacleCarveID)
  {
    if (auto* pNavMeshModule = GetWorld()->GetModule<plAiNavMeshWorldModule>())
    {
      pNavMeshModule->UnregisterObstacleCarve(m_uiCarveID);
    }

    m_uiCarveID = plAiNavMeshWorldModule::InvalidObstacleCarveID;
  }

  // release the previously carved area; rebuild right away for dynamic objects,
  // otherwise keep the lazy unload behavior for scene teardown
  InvalidateObstacleBounds(GetOwner()->IsDynamic());

  SUPER::OnDeactivated();
}

void plNavMeshObstacleComponent::InvalidateSectors()
{
  RefreshCarveVolume();

  // dynamic objects (spawned crates etc.) and avoid volumes must apply immediately - waiting
  // for a re-request would leave agents pathing obliviously; static level geometry keeps the
  // lazy behavior
  InvalidateObstacleBounds(GetOwner()->IsDynamic() || m_Mode == plAiNavObstacleMode::Avoid);
}

void plNavMeshObstacleComponent::RefreshCarveVolume()
{
  // Block mode: navmesh generation only collects STATIC physics geometry - static owners
  // rasterize into the build on their own; dynamic owners must register a carve volume that
  // the build stamps unwalkable.
  // Avoid mode: always needs the registered volume (the soft cost zone), on any owner.
  if (m_Mode == plAiNavObstacleMode::Block && !GetOwner()->IsDynamic())
    return;

  const plBoundingBox bounds = QueryCurrentBounds();
  if (!bounds.IsValid())
    return;

  auto* pNavMeshModule = GetWorld()->GetOrCreateModule<plAiNavMeshWorldModule>();
  if (pNavMeshModule == nullptr)
    return;

  if (m_uiCarveID == plAiNavMeshWorldModule::InvalidObstacleCarveID)
  {
    m_uiCarveID = pNavMeshModule->RegisterObstacleCarve(bounds, m_Mode);
  }
  else
  {
    pNavMeshModule->UpdateObstacleCarve(m_uiCarveID, bounds);
  }
}

plBoundingBox plNavMeshObstacleComponent::QueryCurrentBounds()
{
  plBoundingBox bounds = plBoundingBox::MakeInvalid();

  auto* pPhysics = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();
  if (pPhysics == nullptr)
    return bounds;

  auto* pNavMeshModule = GetWorld()->GetModule<plAiNavMeshWorldModule>();
  if (pNavMeshModule == nullptr)
    return bounds;

  for (const auto& navConfig : pNavMeshModule->GetConfig().m_NavmeshConfigs)
  {
    const auto physicsBounds = pPhysics->GetWorldSpaceBounds(GetOwner(), navConfig.m_uiCollisionLayer, plPhysicsShapeType::Static | plPhysicsShapeType::Dynamic, true);

    if (physicsBounds.IsValid())
    {
      bounds.ExpandToInclude(physicsBounds.GetBox());
    }
  }

  return bounds;
}

void plNavMeshObstacleComponent::InvalidateObstacleBounds(bool bRebuildAsSoonAsPossible)
{
  auto* pPhysics = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();
  if (pPhysics == nullptr)
    return;

  auto* pNavMeshModule = GetWorld()->GetOrCreateModule<plAiNavMeshWorldModule>();
  if (pNavMeshModule == nullptr)
    return;

  plBoundingBox invalidatedBounds = plBoundingBox::MakeInvalid();

  for (const auto& navConfig : pNavMeshModule->GetConfig().m_NavmeshConfigs)
  {
    plAiNavMesh* pNavMesh = pNavMeshModule->GetNavMesh(navConfig.m_sName);
    const plUInt8 uiCollisionLayer = navConfig.m_uiCollisionLayer;

    const auto bounds = pPhysics->GetWorldSpaceBounds(GetOwner(), uiCollisionLayer, plPhysicsShapeType::Static | plPhysicsShapeType::Dynamic, true);
    if (bounds.IsValid())
    {
      pNavMesh->InvalidateSector(bounds.GetBox().GetCenter().GetAsVec2(), bounds.GetBox().GetHalfExtents().GetAsVec2(), bRebuildAsSoonAsPossible);
      invalidatedBounds.ExpandToInclude(bounds.GetBox());
    }
  }

  if (invalidatedBounds.IsValid())
  {
    m_LastInvalidatedBounds = invalidatedBounds;
  }
}

void plNavMeshObstacleComponent::Update()
{
  if (!GetOwner()->IsDynamic() || !cvar_NavMeshDynamicObstacles)
    return;

  const plBoundingBox currentBounds = QueryCurrentBounds();

  if (!currentBounds.IsValid())
    return;

  if (m_LastInvalidatedBounds.IsValid())
  {
    const float fCenterDelta = (currentBounds.GetCenter() - m_LastInvalidatedBounds.GetCenter()).GetLength();
    const float fExtentDelta = (currentBounds.GetHalfExtents() - m_LastInvalidatedBounds.GetHalfExtents()).GetLength();

    if (fCenterDelta > m_fMoveThreshold || fExtentDelta > m_fMoveThreshold)
    {
      m_bPendingMove = true;
    }
  }
  else
  {
    // physics wasn't ready when the component activated - do the initial carve now
    InvalidateSectors();
    return;
  }

  if (!m_bPendingMove)
    return;

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  if (now < m_NextAllowedReinvalidate)
    return; // rate-limited; the pending flag guarantees the rest position is re-baked eventually

  m_bPendingMove = false;
  m_NextAllowedReinvalidate = now + m_ReinvalidateCooldown;

  auto* pNavMeshModule = GetWorld()->GetModule<plAiNavMeshWorldModule>();
  if (pNavMeshModule == nullptr)
    return;

  // move the carve volume first, so the rebuilds below already see the new position
  RefreshCarveVolume();

  // release the previously carved area AND carve the new one
  for (const auto& navConfig : pNavMeshModule->GetConfig().m_NavmeshConfigs)
  {
    plAiNavMesh* pNavMesh = pNavMeshModule->GetNavMesh(navConfig.m_sName);

    if (m_LastInvalidatedBounds.IsValid())
    {
      pNavMesh->InvalidateSector(m_LastInvalidatedBounds.GetCenter().GetAsVec2(), m_LastInvalidatedBounds.GetHalfExtents().GetAsVec2(), true);
    }

    pNavMesh->InvalidateSector(currentBounds.GetCenter().GetAsVec2(), currentBounds.GetHalfExtents().GetAsVec2(), true);
  }

  m_LastInvalidatedBounds = currentBounds;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Components_NavMeshObstacleComponent);
