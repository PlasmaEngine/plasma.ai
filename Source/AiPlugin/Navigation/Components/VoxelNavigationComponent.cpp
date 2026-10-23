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
  PL_ENUM_CONSTANTS(plAiVoxelNavigationComponentState::Idle, plAiVoxelNavigationComponentState::Moving, plAiVoxelNavigationComponentState::Failed, plAiVoxelNavigationComponentState::Searching)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_COMPONENT_TYPE(plAiVoxelNavigationComponent, 3, plComponentMode::Dynamic)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("NavigationTarget", DummyGetter, SetNavigationTargetReference)->AddAttributes(new plGameObjectReferenceAttribute()),
    PL_MEMBER_PROPERTY("Speed", m_fSpeed)->AddAttributes(new plDefaultValueAttribute(5.0f)),
    PL_MEMBER_PROPERTY("Acceleration", m_fAcceleration)->AddAttributes(new plDefaultValueAttribute(3.0f)),
    PL_MEMBER_PROPERTY("Deceleration", m_fDeceleration)->AddAttributes(new plDefaultValueAttribute(8.0f)),
    PL_MEMBER_PROPERTY("ReachedDistance", m_fReachedDistance)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10.0f)),
    PL_MEMBER_PROPERTY("AgentRadius", m_fAgentRadius)->AddAttributes(new plClampValueAttribute(0.0f, 10.0f)),
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
  m_vDestination = vGlobalPos;
  m_bHasDestination = true;

  // keep following the current path while the new one is computed
  if (m_State != plAiVoxelNavigationComponentState::Moving)
  {
    m_State = plAiVoxelNavigationComponentState::Searching;
  }

  StartSearch();
}

void plAiVoxelNavigationComponent::StartSearch()
{
  auto* pVoxelModule = GetWorld()->GetOrCreateModule<plAiVoxelWorldModule>();
  if (!pVoxelModule->IsReady())
    return; // the destination is kept; UpdateSearch starts once the grid is ready

  m_Navigation.SetVoxelGrid(pVoxelModule->GetVoxelGrid());
  m_fVoxelSize = pVoxelModule->GetVoxelGrid()->GetVoxelSize();
  m_LastSearchStart = GetWorld()->GetClock().GetAccumulatedTime();

  const plVec3 vFrom = GetOwner()->GetGlobalPosition();
  const plAiVoxelNavigation::State result = m_Navigation.StartPathSearch(vFrom, m_vDestination, m_fAgentRadius);

  if (result != plAiVoxelNavigation::State::Searching)
  {
    ReportFailure(result, vFrom);
  }
}

void plAiVoxelNavigationComponent::UpdateSearch()
{
  auto* pVoxelModule = GetWorld()->GetModule<plAiVoxelWorldModule>();
  if (pVoxelModule == nullptr || !m_bHasDestination)
    return;

  if (!m_Navigation.IsSearching())
  {
    if (m_State == plAiVoxelNavigationComponentState::Searching && pVoxelModule->IsReady())
    {
      StartSearch(); // the destination was set before the grid was ready
    }
    else if (m_State == plAiVoxelNavigationComponentState::Moving && pVoxelModule->IsReady() &&
             m_Navigation.GetPathGeneration() != pVoxelModule->GetVoxelGrid()->GetGeneration() &&
             GetWorld()->GetClock().GetAccumulatedTime() - m_LastSearchStart > plTime::MakeFromSeconds(0.25))
    {
      StartSearch(); // obstacles changed the grid since the path was computed
    }

    if (!m_Navigation.IsSearching())
      return;
  }

  const plUInt32 uiBudget = pVoxelModule->TakeSearchBudget(plMath::MaxValue<plUInt32>());
  if (uiBudget == 0)
    return;

  const plAiVoxelNavigation::State result = m_Navigation.ContinuePathSearch(uiBudget);

  if (result == plAiVoxelNavigation::State::PathFound)
  {
    if (m_State != plAiVoxelNavigationComponentState::Moving)
    {
      m_vVelocity = plVec3::MakeZero();
    }

    m_State = plAiVoxelNavigationComponentState::Moving;
  }
  else if (result != plAiVoxelNavigation::State::Searching)
  {
    ReportFailure(result, GetOwner()->GetGlobalPosition());
  }
}

void plAiVoxelNavigationComponent::ReportFailure(plAiVoxelNavigation::State result, const plVec3& vFrom)
{
  switch (result)
  {
    case plAiVoxelNavigation::State::InvalidStartPosition:
      plLog::Warning("VoxelNavigation: Start position ({}, {}, {}) is outside the grid or inside a solid voxel.", vFrom.x, vFrom.y, vFrom.z);
      break;

    case plAiVoxelNavigation::State::InvalidTargetPosition:
      plLog::Warning("VoxelNavigation: Target position ({}, {}, {}) is outside the grid or inside a solid voxel.", m_vDestination.x, m_vDestination.y, m_vDestination.z);
      break;

    default:
      plLog::Warning("VoxelNavigation: No path found from ({}, {}, {}) to ({}, {}, {}).", vFrom.x, vFrom.y, vFrom.z, m_vDestination.x, m_vDestination.y, m_vDestination.z);
      break;
  }

  m_Navigation.CancelNavigation();
  m_State = plAiVoxelNavigationComponentState::Failed;
  m_bHasDestination = false;
  m_vVelocity = plVec3::MakeZero();
}

void plAiVoxelNavigationComponent::CancelNavigation()
{
  m_Navigation.CancelNavigation();
  m_State = plAiVoxelNavigationComponentState::Idle;
  m_bHasDestination = false;
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
  s << m_fAgentRadius;
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

  if (uiVersion >= 3)
  {
    s >> m_fAgentRadius;
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

  UpdateSearch();

  const float tDiff = GetWorld()->GetClock().GetTimeDiff().AsFloatInSeconds();

  if (m_State == plAiVoxelNavigationComponentState::Moving)
  {
    MoveTowardsWaypoint(tDiff);

    if (m_State == plAiVoxelNavigationComponentState::Idle)
    {
      // arrived: drop the destination and any recomputation still running for it
      m_Navigation.CancelNavigation();
      m_bHasDestination = false;
    }
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
        case plAiVoxelNavigationComponentState::Searching:
          plDebugRenderer::Draw3DText(GetWorld(), "Searching", vPosition, plColor::LightBlue);
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
