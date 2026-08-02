#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/Components/VoxelNavigationComponent.h>
#include <RendererCore/Debug/DebugRendererContext.h>
#include <AiPlugin/Navigation/VoxelWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <RendererCore/Debug/DebugRenderer.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_BITFLAGS(plAiVoxelNavigationDebugFlags, 1)
  PL_BITFLAGS_CONSTANTS(plAiVoxelNavigationDebugFlags::PrintState, plAiVoxelNavigationDebugFlags::VisPath, plAiVoxelNavigationDebugFlags::VisGrid)
PL_END_STATIC_REFLECTED_BITFLAGS;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiVoxelNavigationComponentState, 1)
  PL_ENUM_CONSTANTS(plAiVoxelNavigationComponentState::Idle, plAiVoxelNavigationComponentState::Moving, plAiVoxelNavigationComponentState::Failed)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_COMPONENT_TYPE(plAiVoxelNavigationComponent, 2, plComponentMode::Dynamic)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("NavigationTarget", DummyGetter, SetNavigationTargetReference)->AddAttributes(new plGameObjectReferenceAttribute()),
    PL_MEMBER_PROPERTY("Speed", m_fSpeed)->AddAttributes(new plDefaultValueAttribute(5.0f)),
    PL_MEMBER_PROPERTY("Acceleration", m_fAcceleration)->AddAttributes(new plDefaultValueAttribute(3.0f)),
    PL_MEMBER_PROPERTY("Deceleration", m_fDeceleration)->AddAttributes(new plDefaultValueAttribute(8.0f)),
    PL_MEMBER_PROPERTY("ReachedDistance", m_fReachedDistance)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10.0f)),
    PL_MEMBER_PROPERTY("ApplySteering", m_bApplySteering)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_BITFLAGS_MEMBER_PROPERTY("DebugFlags", plAiVoxelNavigationDebugFlags, m_DebugFlags),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Navigation"),
  }
  PL_END_ATTRIBUTES;
  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(SetDestination, In, "Destination"),
    PL_SCRIPT_FUNCTION_PROPERTY(CancelNavigation),
    PL_SCRIPT_FUNCTION_PROPERTY(GetState),
    PL_SCRIPT_FUNCTION_PROPERTY(GetSteeringPosition),
    PL_SCRIPT_FUNCTION_PROPERTY(GetSteeringRotation),
  }
  PL_END_FUNCTIONS;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiVoxelNavigationComponent::plAiVoxelNavigationComponent() = default;
plAiVoxelNavigationComponent::~plAiVoxelNavigationComponent() = default;

void plAiVoxelNavigationComponent::SetNavigationTargetReference(const char* szReference)
{
  auto resolver = GetWorld()->GetGameObjectReferenceResolver();

  if (!resolver.IsValid())
    return;

  SetNavigationTarget(resolver(szReference, GetHandle(), "NavigationTarget"));
}

void plAiVoxelNavigationComponent::SetNavigationTarget(plGameObjectHandle hObject)
{
  m_hNavigationTarget = hObject;
}

void plAiVoxelNavigationComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_uiSkipNextFrames = 3;
  m_vSteerPosition = GetOwner()->GetGlobalPosition();
  m_qSteerRotation = GetOwner()->GetGlobalRotation();
  m_vVelocity = plVec3::MakeZero();
}

void plAiVoxelNavigationComponent::SetDestination(const plVec3& vGlobalPos)
{
  auto* pVoxelModule = GetWorld()->GetOrCreateModule<plAiVoxelWorldModule>();
  if (pVoxelModule == nullptr || !pVoxelModule->IsReady())
  {
    // Grid not ready yet, don't treat as failure - caller can retry
    return;
  }

  m_Navigation.SetVoxelGrid(pVoxelModule->GetVoxelGrid());
  m_fVoxelSize = pVoxelModule->GetVoxelGrid()->GetVoxelSize();

  const plVec3 vCurrentPos = GetOwner()->GetGlobalPosition();
  const auto result = m_Navigation.FindPath(vCurrentPos, vGlobalPos);

  switch (result)
  {
    case plAiVoxelNavigation::State::PathFound:
      m_State = plAiVoxelNavigationComponentState::Moving;
      m_vVelocity = plVec3::MakeZero();
      break;

    case plAiVoxelNavigation::State::InvalidStartPosition:
      plLog::Warning("VoxelNavigation: Start position ({}, {}, {}) is outside the grid or inside a solid voxel.",
        vCurrentPos.x, vCurrentPos.y, vCurrentPos.z);
      m_State = plAiVoxelNavigationComponentState::Failed;
      break;

    case plAiVoxelNavigation::State::InvalidTargetPosition:
      plLog::Warning("VoxelNavigation: Target position ({}, {}, {}) is outside the grid or inside a solid voxel.",
        vGlobalPos.x, vGlobalPos.y, vGlobalPos.z);
      m_State = plAiVoxelNavigationComponentState::Failed;
      break;

    default:
      plLog::Warning("VoxelNavigation: No path found from ({}, {}, {}) to ({}, {}, {}).",
        vCurrentPos.x, vCurrentPos.y, vCurrentPos.z,
        vGlobalPos.x, vGlobalPos.y, vGlobalPos.z);
      m_State = plAiVoxelNavigationComponentState::Failed;
      break;
  }
}

void plAiVoxelNavigationComponent::CancelNavigation()
{
  m_Navigation.CancelNavigation();
  m_State = plAiVoxelNavigationComponentState::Idle;
  m_vVelocity = plVec3::MakeZero();
}

plVec3 plAiVoxelNavigationComponent::GetSteeringPosition() const
{
  return m_vSteerPosition;
}

plQuat plAiVoxelNavigationComponent::GetSteeringRotation() const
{
  return m_qSteerRotation;
}

void plAiVoxelNavigationComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  plStreamWriter& s = inout_stream.GetStream();

  s << m_fSpeed;
  s << m_fAcceleration;
  s << m_fDeceleration;
  s << m_fReachedDistance;
  s << m_bApplySteering;
  s << m_DebugFlags;
  inout_stream.WriteGameObjectHandle(m_hNavigationTarget);
}

void plAiVoxelNavigationComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  plStreamReader& s = inout_stream.GetStream();
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());

  s >> m_fSpeed;
  s >> m_fAcceleration;
  s >> m_fDeceleration;
  s >> m_fReachedDistance;
  s >> m_bApplySteering;
  s >> m_DebugFlags;

  if (uiVersion >= 2)
  {
    m_hNavigationTarget = inout_stream.ReadGameObjectHandle();
  }
}

void plAiVoxelNavigationComponent::Update()
{
  if (m_uiSkipNextFrames > 0)
  {
    m_uiSkipNextFrames--;
    return;
  }

  // If a navigation target is set and we're idle, navigate to it
  if (m_State == plAiVoxelNavigationComponentState::Idle && !m_hNavigationTarget.IsInvalidated())
  {
    plGameObject* pTarget = nullptr;
    if (GetWorld()->TryGetObject(m_hNavigationTarget, pTarget))
    {
      const plVec3 vTargetPos = pTarget->GetGlobalPosition();
      const float fDistToTarget = (vTargetPos - m_vSteerPosition).GetLength();

      // Only re-navigate if we're not already at the target
      const float fArrivalThreshold = plMath::Max(m_fReachedDistance, m_fVoxelSize * 0.5f);
      if (fDistToTarget > fArrivalThreshold)
      {
        SetDestination(vTargetPos);
      }
    }
  }

  const float tDiff = GetWorld()->GetClock().GetTimeDiff().AsFloatInSeconds();

  if (m_State == plAiVoxelNavigationComponentState::Moving)
  {
    MoveTowardsWaypoint(tDiff);
  }

  if (m_DebugFlags.IsAnyFlagSet())
  {
    if (m_DebugFlags.IsSet(plAiVoxelNavigationDebugFlags::PrintState))
    {
      const plVec3 vPosition = GetOwner()->GetGlobalPosition() + plVec3(0, 0, 1.5f);

      switch (m_State)
      {
        case plAiVoxelNavigationComponentState::Idle:
          plDebugRenderer::Draw3DText(GetWorld(), "Idle", vPosition, plColor::Grey);
          break;
        case plAiVoxelNavigationComponentState::Moving:
          plDebugRenderer::Draw3DText(GetWorld(), "Moving", vPosition, plColor::Yellow);
          break;
        case plAiVoxelNavigationComponentState::Failed:
          plDebugRenderer::Draw3DText(GetWorld(), "Failed", vPosition, plColor::Red);
          break;
      }
    }

    if (m_DebugFlags.IsSet(plAiVoxelNavigationDebugFlags::VisPath))
    {
      m_Navigation.DebugDrawPath(GetWorld(), plColor::DeepSkyBlue);
    }

    if (m_DebugFlags.IsSet(plAiVoxelNavigationDebugFlags::VisGrid))
    {
      auto* pVoxelModule = GetWorld()->GetModule<plAiVoxelWorldModule>();
      if (pVoxelModule != nullptr)
      {
        pVoxelModule->GetVoxelGrid()->DebugDraw(GetWorld(), plColor::LimeGreen.WithAlpha(0.1f));
      }
    }
  }
}

void plAiVoxelNavigationComponent::MoveTowardsWaypoint(float fTimeDiff)
{
  if (m_Navigation.IsPathComplete())
  {
    m_State = plAiVoxelNavigationComponentState::Idle;
    m_vVelocity = plVec3::MakeZero();
    return;
  }

  const plVec3 vCurrentPos = m_vSteerPosition;
  const plVec3 vTargetWaypoint = m_Navigation.GetNextWaypoint();
  plVec3 vToWaypoint = vTargetWaypoint - vCurrentPos;
  float fDistanceToWaypoint = vToWaypoint.GetLength();

  // Minimum threshold to consider a waypoint reached, regardless of ReachedDistance setting
  const float fMinReachThreshold = plMath::Max(m_fReachedDistance, m_fVoxelSize * 0.25f);

  if (fDistanceToWaypoint <= fMinReachThreshold)
  {
    // Snap to waypoint to prevent oscillation
    m_vSteerPosition = vTargetWaypoint;

    if (!m_Navigation.AdvanceWaypoint())
    {
      m_State = plAiVoxelNavigationComponentState::Idle;
      m_vVelocity = plVec3::MakeZero();
    }

    if (m_bApplySteering)
    {
      GetOwner()->SetGlobalPosition(m_vSteerPosition);
      GetOwner()->SetGlobalRotation(m_qSteerRotation);
    }
    return;
  }

  const plVec3 vDirection = vToWaypoint / fDistanceToWaypoint;

  // Compute target speed with braking near end of path
  float fTargetSpeed = m_fSpeed;
  const float fBrakingDistance = (m_fSpeed * m_fSpeed) / (2.0f * m_fDeceleration);

  if (m_Navigation.IsPathComplete() || m_Navigation.GetCurrentWaypointIndex() >= m_Navigation.GetWaypoints().GetCount() - 2)
  {
    const plVec3 vFinalTarget = m_Navigation.GetWaypoints()[m_Navigation.GetWaypoints().GetCount() - 1];
    const float fDistToEnd = (vFinalTarget - vCurrentPos).GetLength();

    if (fDistToEnd < fBrakingDistance)
    {
      fTargetSpeed = m_fSpeed * (fDistToEnd / fBrakingDistance);
      fTargetSpeed = plMath::Max(fTargetSpeed, 0.5f);
    }
  }

  // Accelerate or decelerate
  const float fCurrentSpeed = m_vVelocity.GetLength();
  float fNewSpeed;

  if (fCurrentSpeed < fTargetSpeed)
  {
    fNewSpeed = plMath::Min(fCurrentSpeed + m_fAcceleration * fTimeDiff, fTargetSpeed);
  }
  else
  {
    fNewSpeed = plMath::Max(fCurrentSpeed - m_fDeceleration * fTimeDiff, fTargetSpeed);
  }

  // Clamp movement distance so we don't overshoot the waypoint
  float fMoveDistance = fNewSpeed * fTimeDiff;
  if (fMoveDistance >= fDistanceToWaypoint)
  {
    // Would overshoot: snap to waypoint
    m_vSteerPosition = vTargetWaypoint;
    m_vVelocity = vDirection * fNewSpeed;

    if (!m_Navigation.AdvanceWaypoint())
    {
      m_State = plAiVoxelNavigationComponentState::Idle;
      m_vVelocity = plVec3::MakeZero();
    }
  }
  else
  {
    m_vVelocity = vDirection * fNewSpeed;
    m_vSteerPosition = vCurrentPos + vDirection * fMoveDistance;
  }

  // Update rotation to face movement direction
  if (fNewSpeed > 0.1f)
  {
    m_qSteerRotation = plQuat::MakeShortestRotation(plVec3::MakeAxisX(), vDirection);
  }

  if (m_bApplySteering)
  {
    GetOwner()->SetGlobalPosition(m_vSteerPosition);
    GetOwner()->SetGlobalRotation(m_qSteerRotation);
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Components_VoxelNavigationComponent);
