#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/Components/VoxelObstacleComponent.h>
#include <AiPlugin/Navigation/VoxelWorldModule.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/World/World.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiVoxelObstacleComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("CollisionLayer", m_uiCollisionLayer)->AddAttributes(new plDynamicEnumAttribute("PhysicsCollisionLayer")),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(UpdateObstacle),
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

plAiVoxelObstacleComponent::plAiVoxelObstacleComponent() = default;
plAiVoxelObstacleComponent::~plAiVoxelObstacleComponent() = default;

void plAiVoxelObstacleComponent::OnActivated()
{
  SUPER::OnActivated();

  if (IsSimulationStarted())
    InjectIntoGrid();
}

void plAiVoxelObstacleComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  InjectIntoGrid();
}

void plAiVoxelObstacleComponent::OnDeactivated()
{
  RemoveFromGrid();

  SUPER::OnDeactivated();
}

void plAiVoxelObstacleComponent::UpdateObstacle()
{
  RemoveFromGrid();
  InjectIntoGrid();
}

void plAiVoxelObstacleComponent::InjectIntoGrid()
{
  auto* pPhysics = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();
  if (pPhysics == nullptr)
    return;

  auto* pVoxelModule = GetWorld()->GetOrCreateModule<plAiVoxelWorldModule>();
  if (pVoxelModule == nullptr)
    return;

  auto bounds = pPhysics->GetWorldSpaceBounds(GetOwner(), m_uiCollisionLayer, plPhysicsShapeType::Static | plPhysicsShapeType::Dynamic, true);
  if (!bounds.IsValid())
    return;

  const plBoundingBox box = bounds.GetBox();
  pVoxelModule->InjectObstacle(box);
  m_LastInjectedBounds = box;
  m_bInjected = true;
}

void plAiVoxelObstacleComponent::RemoveFromGrid()
{
  if (!m_bInjected)
    return;

  auto* pVoxelModule = GetWorld()->GetModule<plAiVoxelWorldModule>();
  if (pVoxelModule == nullptr)
    return;

  pVoxelModule->RemoveObstacle(m_LastInjectedBounds);
  m_bInjected = false;
}

void plAiVoxelObstacleComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  plStreamWriter& s = inout_stream.GetStream();

  s << m_uiCollisionLayer;
}

void plAiVoxelObstacleComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  plStreamReader& s = inout_stream.GetStream();

  s >> m_uiCollisionLayer;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Components_VoxelObstacleComponent);
