#include <AiPlugin/AiPluginPCH.h>
#include <AiPlugin/Navigation/Components/NavLinkComponent.h>
#include <AiPlugin/Navigation/Components/NavigationComponent.h>
#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <AiPlugin/Navigation/Navigation.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Configuration/CVar.h>
#include <GameEngine/Gameplay/BlackboardComponent.h>
#include <GameEngine/Physics/CharacterControllerComponent.h>
#include <RendererCore/Debug/DebugRenderer.h>

extern plCVarBool cvar_AiCrowdEnable; // defined in CrowdWorldModule.cpp

plCVarFloat cvar_AiNavDriftCorrection("AI.Navigation.DriftCorrection", 3.0f, plCVarFlags::Default, "RootMotion mode: how strongly (per second) the agent is pulled back onto its path corridor.");

// clang-format off
PL_BEGIN_STATIC_REFLECTED_BITFLAGS(plAiNavigationDebugFlags, 1)
  PL_BITFLAGS_CONSTANTS(plAiNavigationDebugFlags::PrintState, plAiNavigationDebugFlags::VisPathCorridor, plAiNavigationDebugFlags::VisPathLine, plAiNavigationDebugFlags::VisTarget)
PL_END_STATIC_REFLECTED_BITFLAGS;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiNavigationComponentState, 1)
  PL_ENUM_CONSTANTS(plAiNavigationComponentState::Idle, plAiNavigationComponentState::Moving, plAiNavigationComponentState::Turning, plAiNavigationComponentState::Falling, plAiNavigationComponentState::Fallen, plAiNavigationComponentState::Failed, plAiNavigationComponentState::TraversingLink)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiMovementMode, 1)
  PL_ENUM_CONSTANTS(plAiMovementMode::DirectTransform, plAiMovementMode::PhysicsCharacter, plAiMovementMode::RootMotion)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_COMPONENT_TYPE(plAiNavigationComponent, 3, plComponentMode::Dynamic)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("NavmeshConfig", m_sNavmeshConfig)->AddAttributes(new plDynamicStringEnumAttribute("AiNavmeshConfig")),
    PL_MEMBER_PROPERTY("PathSearchConfig", m_sPathSearchConfig)->AddAttributes(new plDynamicStringEnumAttribute("AiPathSearchConfig")),
    PL_MEMBER_PROPERTY("Speed", m_fSpeed)->AddAttributes(new plDefaultValueAttribute(5.0f)),
    PL_MEMBER_PROPERTY("Acceleration", m_fAcceleration)->AddAttributes(new plDefaultValueAttribute(3.0f)),
    PL_MEMBER_PROPERTY("Deceleration", m_fDecceleration)->AddAttributes(new plDefaultValueAttribute(8.0f)),
    PL_MEMBER_PROPERTY("FootRadius", m_fFootRadius)->AddAttributes(new plDefaultValueAttribute(0.15f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("ReachedDistance", m_fReachedDistance)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10.0f)),
    PL_MEMBER_PROPERTY("CollisionLayer", m_uiCollisionLayer)->AddAttributes(new plDynamicEnumAttribute("PhysicsCollisionLayer")),
    PL_MEMBER_PROPERTY("FallHeight", m_fFallHeight)->AddAttributes(new plDefaultValueAttribute(1.0f)),
    PL_BITFLAGS_MEMBER_PROPERTY("DebugFlags", plAiNavigationDebugFlags , m_DebugFlags),
    PL_MEMBER_PROPERTY("ApplySteering", m_bApplySteering)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("CrowdAvoidance", m_bCrowdAvoidance)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("AgentRadius", m_fAgentRadius)->AddAttributes(new plDefaultValueAttribute(0.3f), new plClampValueAttribute(0.05f, 5.0f)),
    PL_ENUM_MEMBER_PROPERTY("AvoidanceQuality", plAiCrowdAvoidanceQuality, m_AvoidanceQuality),
    PL_ENUM_MEMBER_PROPERTY("MovementMode", plAiMovementMode, m_MovementMode),
    PL_MEMBER_PROPERTY("LinkTraverseSpeed", m_fLinkTraverseSpeed)->AddAttributes(new plDefaultValueAttribute(3.0f), new plClampValueAttribute(0.1f, 50.0f)),
    PL_MEMBER_PROPERTY("CCWalkSpeed", m_fCCWalkSpeed)->AddAttributes(new plDefaultValueAttribute(1.5f)),
    PL_MEMBER_PROPERTY("CCRunSpeed", m_fCCRunSpeed)->AddAttributes(new plDefaultValueAttribute(3.5f)),
    PL_MEMBER_PROPERTY("CCRotateSpeed", m_CCRotateSpeed)->AddAttributes(new plDefaultValueAttribute(plAngle::MakeFromDegree(90))),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Navigation"),
  }
  PL_END_ATTRIBUTES;
  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(SetDestination, In, "Destination", In, "AllowPartialPaths"),
    PL_SCRIPT_FUNCTION_PROPERTY(CancelNavigation),
    PL_SCRIPT_FUNCTION_PROPERTY(GetState),
    PL_SCRIPT_FUNCTION_PROPERTY(StopWalking, In, "WithinDistance"),
    PL_SCRIPT_FUNCTION_PROPERTY(TurnTowards, In, "TargetPosition"),
    PL_SCRIPT_FUNCTION_PROPERTY(GetTurnAngleTowards, In, "TargetPosition"),
    PL_SCRIPT_FUNCTION_PROPERTY(EnsureNavMeshSectorAvailable, In, "vCenter", In, "fRadius"),
    PL_SCRIPT_FUNCTION_PROPERTY(FindRandomPointAroundCircle, In, "vCenter", In, "fRadius", Out, "out_vPoint"),
    PL_SCRIPT_FUNCTION_PROPERTY(RaycastNavMesh, In, "vStart", In, "vDirection", In, "fDistance", Out, "out_vPoint", Out, "out_fDistance"),
    PL_SCRIPT_FUNCTION_PROPERTY(GetSteeringPosition),
    PL_SCRIPT_FUNCTION_PROPERTY(GetSteeringRotation),
    PL_SCRIPT_FUNCTION_PROPERTY(FinishLinkTraversal),
  }
  PL_END_FUNCTIONS;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiNavigationComponent::plAiNavigationComponent() = default;
plAiNavigationComponent::~plAiNavigationComponent() = default;

void plAiNavigationComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_uiSkipNextFrames = 3; // 2 are needed to have colliders set up at the start of the scene simulation, 3 just to be save
  m_Steering.m_vPosition = GetOwner()->GetGlobalPosition();
  m_Steering.m_qRotation = GetOwner()->GetGlobalRotation();
}

void plAiNavigationComponent::OnDeactivated()
{
  if (m_pCrowdModule != nullptr && m_CrowdAgentID != plAiCrowdWorldModule::InvalidAgentID)
  {
    m_pCrowdModule->UnregisterAgent(m_CrowdAgentID);
    m_CrowdAgentID = plAiCrowdWorldModule::InvalidAgentID;
    m_pCrowdModule = nullptr;
  }

  SUPER::OnDeactivated();
}

void plAiNavigationComponent::SetDestination(const plVec3& vGlobalPos, bool bAllowPartialPath)
{
  m_fStopWalkDistance = plMath::HighValue<float>();
  m_bAllowPartialPath = bAllowPartialPath;
  m_Navigation.SetTargetPosition(vGlobalPos);
  m_State = plAiNavigationComponentState::Moving;
}

void plAiNavigationComponent::CancelNavigation()
{
  m_Navigation.CancelNavigation();

  if (m_State != plAiNavigationComponentState::Falling)
  {
    // if it is still falling, don't reset the state
    m_State = plAiNavigationComponentState::Idle;
  }
}

void plAiNavigationComponent::StopWalking(float fWithinDistance)
{
  m_fStopWalkDistance = plMath::Min(m_fStopWalkDistance, fWithinDistance);
}

void plAiNavigationComponent::TurnTowards(const plVec2& vGlobalPos)
{
  if (m_State == plAiNavigationComponentState::Idle)
  {
    m_State = plAiNavigationComponentState::Turning;

    m_vTurnTowardsPos = vGlobalPos;
  }
}

plAngle plAiNavigationComponent::GetTurnAngleTowards(const plVec2& vGlobalPos) const
{
  plVec3 vOwnPos2D = GetOwner()->GetGlobalPosition();
  vOwnPos2D.z = 0.0f;

  plVec3 vTargetDir = (vGlobalPos.GetAsVec3(0) - vOwnPos2D);
  if (vTargetDir.NormalizeIfNotZero(plVec3::MakeZero()).Failed())
    return plAngle::MakeZero();

  plVec3 vLookDir = GetOwner()->GetGlobalDirForwards();
  vLookDir.z = 0.0f;
  vLookDir.Normalize();

  return vLookDir.GetAngleBetween(vTargetDir, plVec3::MakeAxisZ());
}

bool plAiNavigationComponent::PrepareQueryObject()
{
  if (m_Query.GetNavmesh() == nullptr)
  {
    plAiNavMeshWorldModule* pNavMeshModule = GetWorld()->GetOrCreateModule<plAiNavMeshWorldModule>();
    if (pNavMeshModule == nullptr)
      return false;

    m_Query.SetNavmesh(pNavMeshModule->GetNavMesh(m_sNavmeshConfig));
    m_Query.SetQueryFilter(pNavMeshModule->GetPathSearchFilter(m_sPathSearchConfig));
  }

  return true;
}

bool plAiNavigationComponent::EnsureNavMeshSectorAvailable(const plVec3& vCenter, float fRadius)
{
  if (!PrepareQueryObject())
    return false;

  return m_Query.GetNavmesh()->RequestSector(vCenter.GetAsVec2(), plVec2(fRadius));
}

bool plAiNavigationComponent::FindRandomPointAroundCircle(const plVec3& vCenter, float fRadius, plVec3& out_vPoint)
{
  if (!PrepareQueryObject())
    return false;

  if (!m_Query.PrepareQueryArea(vCenter, fRadius))
    return false;

  return m_Query.FindRandomPointAroundCircle(vCenter, fRadius, GetWorld()->GetRandomNumberGenerator(), out_vPoint);
}

bool plAiNavigationComponent::RaycastNavMesh(const plVec3& vStart, const plVec3& vDirection, float fDistance, plVec3& out_vPoint, float& out_fDistance)
{
  if (!PrepareQueryObject())
    return false;

  // ignore result, even if not everything is loaded, the raycast may still hit an obstacle
  m_Query.PrepareQueryArea(vStart, fDistance);

  plAiNavmeshRaycastHit hit;
  if (!m_Query.Raycast(vStart, vDirection, fDistance, hit))
    return false;

  out_vPoint = hit.m_vHitPosition;
  out_fDistance = hit.m_fHitDistance;
  return true;
}

plVec3 plAiNavigationComponent::GetSteeringPosition() const
{
  return m_vSteerPosition;
}

plQuat plAiNavigationComponent::GetSteeringRotation() const
{
  return m_qSteerRotation;
}

void plAiNavigationComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  plStreamWriter& s = inout_stream.GetStream();

  s << m_sPathSearchConfig;
  s << m_sNavmeshConfig;
  s << m_fReachedDistance;
  s << m_fSpeed;
  s << m_fAcceleration;
  s << m_fDecceleration;
  s << m_fFootRadius;
  s << m_uiCollisionLayer;
  s << m_fFallHeight;
  s << m_DebugFlags;
  s << m_bApplySteering;
  s << m_bCrowdAvoidance;
  s << m_fAgentRadius;
  s << m_AvoidanceQuality;
  s << m_MovementMode;
  s << m_fLinkTraverseSpeed;
  s << m_fCCWalkSpeed;
  s << m_fCCRunSpeed;
  s << m_CCRotateSpeed;
}

void plAiNavigationComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  plStreamReader& s = inout_stream.GetStream();
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());

  s >> m_sPathSearchConfig;
  s >> m_sNavmeshConfig;
  s >> m_fReachedDistance;
  s >> m_fSpeed;
  s >> m_fAcceleration;
  s >> m_fDecceleration;
  s >> m_fFootRadius;
  s >> m_uiCollisionLayer;
  s >> m_fFallHeight;
  s >> m_DebugFlags;

  if (uiVersion >= 2)
  {
    s >> m_bApplySteering;
  }

  if (uiVersion >= 3)
  {
    s >> m_bCrowdAvoidance;
    s >> m_fAgentRadius;
    s >> m_AvoidanceQuality;
    s >> m_MovementMode;
    s >> m_fLinkTraverseSpeed;
    s >> m_fCCWalkSpeed;
    s >> m_fCCRunSpeed;
    s >> m_CCRotateSpeed;
  }
}

void plAiNavigationComponent::Update()
{
  if (m_uiSkipNextFrames > 0)
  {
    // in the very first frame, physics may not be available yet (colliders are not yet set up)
    // so skip that frame to prevent not finding a ground and entering the 'falling' state
    m_uiSkipNextFrames--;
    return;
  }

  plTransform transform = GetOwner()->GetGlobalTransform();
  const float tDiff = GetWorld()->GetClock().GetTimeDiff().AsFloatInSeconds();

  if (m_State == plAiNavigationComponentState::TraversingLink)
  {
    // steering is frozen while crossing a nav link; either game code moves the character
    // (it handled plMsgAiNavLinkTraverse) or the built-in traverser lerps across
    TraverseLink(transform, tDiff);
  }
  else
  {
    Steer(transform, tDiff);
    Turn(transform, tDiff);

    if (m_MovementMode != plAiMovementMode::PhysicsCharacter)
    {
      // a character controller owns grounding and gravity itself
      PlaceOnGround(transform, tDiff);
    }
  }

  m_vSteerPosition = transform.m_vPosition;
  m_qSteerRotation = transform.m_qRotation;

  ApplyMovement(transform, tDiff);

  if (m_DebugFlags.IsAnyFlagSet())
  {
    if (m_DebugFlags.IsSet(plAiNavigationDebugFlags::VisPathCorridor))
    {
      m_Navigation.DebugDrawPathCorridor(GetWorld(), plColor::Aquamarine.WithAlpha(0.15f), 0.2f);
    }

    if (m_DebugFlags.IsSet(plAiNavigationDebugFlags::VisPathLine))
    {
      m_Navigation.DebugDrawPathLine(GetWorld(), plColor::DeepSkyBlue, 0.3f);
    }

    if (m_DebugFlags.IsSet(plAiNavigationDebugFlags::PrintState))
    {
      const plVec3 vPosition = GetOwner()->GetGlobalPosition() + plVec3(0, 0, 1.5f);

      switch (m_State)
      {
        case plAiNavigationComponentState::Idle:
          plDebugRenderer::Draw3DText(GetWorld(), "Idle", vPosition, plColor::Grey);
          break;
        case plAiNavigationComponentState::Moving:
          plDebugRenderer::Draw3DText(GetWorld(), "Moving", vPosition, plColor::Yellow);
          m_Navigation.DebugDrawState(GetWorld(), vPosition - plVec3(0, 0, 0.5f));
          break;
        case plAiNavigationComponentState::Turning:
          plDebugRenderer::Draw3DText(GetWorld(), "Turning", vPosition, plColor::Orange);
          break;
        case plAiNavigationComponentState::Falling:
          plDebugRenderer::Draw3DText(GetWorld(), "Falling...", vPosition, plColor::IndianRed);
          break;
        case plAiNavigationComponentState::Fallen:
          plDebugRenderer::Draw3DText(GetWorld(), "Fallen", vPosition, plColor::IndianRed);
          break;
        case plAiNavigationComponentState::Failed:
          plDebugRenderer::Draw3DText(GetWorld(), "Failed", vPosition, plColor::Red);
          m_Navigation.DebugDrawState(GetWorld(), vPosition - plVec3(0, 0, 0.5f));
          break;
        case plAiNavigationComponentState::TraversingLink:
          plDebugRenderer::Draw3DText(GetWorld(), "TraversingLink", vPosition, plColor::Violet);
          break;
      }
    }

    if (m_DebugFlags.IsSet(plAiNavigationDebugFlags::VisTarget))
    {
      plDebugRenderer::DrawArrow(GetWorld(), 1.0f, plColor::Lime, plTransform(m_Navigation.GetTargetPosition() + plVec3(0, 0, 1.5f)), -plVec3::MakeAxisZ());
    }
  }
}

void plAiNavigationComponent::Steer(plTransform& transform, float tDiff)
{
  if (m_State != plAiNavigationComponentState::Moving)
    return;

  if (plAiNavMeshWorldModule* pNavMeshModule = GetWorld()->GetOrCreateModule<plAiNavMeshWorldModule>())
  {
    m_Navigation.SetNavmesh(pNavMeshModule->GetNavMesh(m_sNavmeshConfig));
    m_Navigation.SetQueryFilter(pNavMeshModule->GetPathSearchFilter(m_sPathSearchConfig));
  }

  m_Navigation.SetCurrentPosition(GetOwner()->GetGlobalPosition());

  m_Navigation.Update();

  switch (m_Navigation.GetState())
  {
    case plAiNavigation::State::Idle:
      m_State = plAiNavigationComponentState::Idle;
      return;

    case plAiNavigation::State::InvalidCurrentPosition:
    case plAiNavigation::State::InvalidTargetPosition:
    case plAiNavigation::State::NoPathFound:
      m_State = plAiNavigationComponentState::Failed;
      return;

    case plAiNavigation::State::StartNewSearch:
    case plAiNavigation::State::Searching:
      return;

    case plAiNavigation::State::FullPathFound:
      break;

    case plAiNavigation::State::PartialPathFound:
      if (m_bAllowPartialPath)
        break;

      m_State = plAiNavigationComponentState::Failed;
      return;
  }

  if (m_fSpeed <= 0)
    return;

  plVec2 vForwardDir = GetOwner()->GetGlobalDirForwards().GetAsVec2();
  vForwardDir.NormalizeIfNotZero(plVec2(1, 0)).IgnoreResult();

  m_Steering.m_fMaxSpeed = m_fSpeed;
  m_Steering.m_vPosition = GetOwner()->GetGlobalPosition();
  m_Steering.m_qRotation = GetOwner()->GetGlobalRotation();
  m_Steering.m_vVelocity = GetOwner()->GetLinearVelocity();
  m_Steering.m_fAcceleration = m_fAcceleration;
  m_Steering.m_fDecceleration = m_fDecceleration;

  // TODO: hard-coded values
  m_Steering.m_MinTurnSpeed = plAngle::MakeFromDegree(180);

  const float fBrakingDistance = 1.2f * (plMath::Square(m_Steering.m_fMaxSpeed) / (2.0f * m_Steering.m_fDecceleration));

  m_Navigation.ComputeSteeringInfo(m_Steering.m_Info, vForwardDir, fBrakingDistance);
  // m_Steering.m_Info.m_vDirectionTowardsWaypoint.Set(1, 0);

  // the next waypoint is a nav link: once close enough, cross it instead of steering into it
  if (m_Steering.m_Info.m_bNextWaypointIsLink && m_Steering.m_Info.m_fDistanceToWaypoint <= plMath::Max(0.3f, m_fAgentRadius))
  {
    StartLinkTraversal(m_Steering.m_Info.m_LinkPolyRef);

    if (m_State == plAiNavigationComponentState::TraversingLink)
      return;

    // traversal could not start (corridor not at the link yet) - steer normally, retry next frame
  }

  if (m_fStopWalkDistance < plMath::HighValue<float>())
  {
    m_Steering.m_Info.m_fArrivalDistance = plMath::Min(m_fStopWalkDistance, m_Steering.m_Info.m_fArrivalDistance);
    m_Steering.m_Info.m_fDistanceToWaypoint = plMath::Min(m_fStopWalkDistance, m_Steering.m_Info.m_fDistanceToWaypoint);
  }

  m_Steering.Calculate(tDiff, GetWorld());

  // ---- crowd avoidance ----
  // Consume the avoidance velocity solved LAST frame (the crowd module solves in PostAsync,
  // after this update ran) and submit this frame's state for the next solve.
  if (m_bCrowdAvoidance && cvar_AiCrowdEnable)
  {
    if (m_pCrowdModule == nullptr)
    {
      m_pCrowdModule = GetWorld()->GetOrCreateModule<plAiCrowdWorldModule>();
    }

    if (m_CrowdAgentID == plAiCrowdWorldModule::InvalidAgentID)
    {
      m_CrowdAgentID = m_pCrowdModule->RegisterAgent();
    }

    plVec3 vAdjustedVelocity;
    if (m_pCrowdModule->TryGetAdjustedVelocity(m_CrowdAgentID, vAdjustedVelocity))
    {
      // override only the position integration; rotation keeps facing the corridor waypoint,
      // so avoidance side-steps read as strafing
      m_Steering.m_vPosition = transform.m_vPosition + vAdjustedVelocity * tDiff;
    }

    plAiCrowdAgentState agentState;
    agentState.m_vPosition = transform.m_vPosition;
    agentState.m_vVelocity = GetOwner()->GetLinearVelocity();
    agentState.m_vDesiredVelocity = m_Steering.m_vDesiredVelocity;
    agentState.m_fRadius = m_fAgentRadius;
    agentState.m_fMaxSpeed = m_fSpeed;
    agentState.m_Quality = m_AvoidanceQuality;
    agentState.m_CurrentPoly = m_Navigation.GetCurrentPolyRef();
    agentState.m_pNavMesh = m_Navigation.GetNavmesh();
    agentState.m_pFilter = m_Navigation.GetFilter();

    m_pCrowdModule->SubmitAgentState(m_CrowdAgentID, agentState);
  }

  const plVec2 vMove = m_Steering.m_vPosition.GetAsVec2() - transform.m_vPosition.GetAsVec2();
  const float fMoveDist = vMove.GetLength();

  if (m_fStopWalkDistance < plMath::HighValue<float>())
  {
    m_fStopWalkDistance = plMath::Max(0.0f, m_fStopWalkDistance - fMoveDist);
  }

  const float fSpeed = fMoveDist / tDiff;

  transform.m_vPosition = m_Steering.m_vPosition;
  transform.m_qRotation = m_Steering.m_qRotation;

  if (fSpeed < 0.2f && (m_fStopWalkDistance <= 0.1f || ((m_Navigation.GetTargetPosition().GetAsVec2() - m_Steering.m_vPosition.GetAsVec2()).GetLengthSquared() < plMath::Square(m_fReachedDistance))))
  {
    // reached the goal
    CancelNavigation();
    m_State = plAiNavigationComponentState::Idle;
    return;
  }
}

void plAiNavigationComponent::StartLinkTraversal(dtPolyRef linkPoly)
{
  plUInt32 uiUserID = plInvalidIndex;

  if (m_Navigation.TraverseOffMeshLink(linkPoly, m_vLinkStart, m_vLinkEnd, uiUserID).Failed())
    return; // corridor not at the link yet - self-heals next frame

  m_TraversingLinkType = plAiNavLinkType::Jump;
  plGameObjectHandle hLinkObject;

  if (plAiNavMeshWorldModule* pNavMeshModule = GetWorld()->GetModule<plAiNavMeshWorldModule>())
  {
    m_TraversingLinkType = pNavMeshModule->GetNavLinkType(uiUserID);

    plComponent* pLinkComponent = nullptr;
    if (GetWorld()->TryGetComponent(pNavMeshModule->ResolveNavLink(uiUserID), pLinkComponent))
    {
      hLinkObject = pLinkComponent->GetOwner()->GetHandle();
    }
  }

  plMsgAiNavLinkTraverse msg;
  msg.m_vStart = m_vLinkStart;
  msg.m_vEnd = m_vLinkEnd;
  msg.m_Type = m_TraversingLinkType;
  msg.m_hLinkObject = hLinkObject;

  // if game code handles this (animation, script), it moves the character and calls
  // FinishLinkTraversal(); otherwise the built-in traverser in TraverseLink() takes over
  m_bLinkHandledExternally = GetOwner()->SendMessage(msg);

  m_fLinkProgress = 0.0f;
  m_State = plAiNavigationComponentState::TraversingLink;
}

void plAiNavigationComponent::TraverseLink(plTransform& transform, float tDiff)
{
  if (m_bLinkHandledExternally)
    return; // waiting for game code to call FinishLinkTraversal()

  const float fLinkLength = plMath::Max(0.01f, (m_vLinkEnd - m_vLinkStart).GetLength());
  m_fLinkProgress = plMath::Min(1.0f, m_fLinkProgress + (tDiff * m_fLinkTraverseSpeed) / fLinkLength);

  plVec3 vPosition = plMath::Lerp(m_vLinkStart, m_vLinkEnd, m_fLinkProgress);

  const plAiNavLinkType::Enum linkType = static_cast<plAiNavLinkType::Enum>(m_TraversingLinkType.GetValue());

  if (linkType == plAiNavLinkType::Jump || linkType == plAiNavLinkType::Vault || linkType == plAiNavLinkType::Drop)
  {
    // parabolic arc
    const float fArcHeight = plMath::Clamp(fLinkLength * 0.3f, 0.3f, 1.2f);
    vPosition.z += 4.0f * fArcHeight * m_fLinkProgress * (1.0f - m_fLinkProgress);
  }

  transform.m_vPosition = vPosition;

  // face the travel direction
  plVec2 vTravelDir = (m_vLinkEnd - m_vLinkStart).GetAsVec2();
  if (vTravelDir.NormalizeIfNotZero(plVec2::MakeZero()).Succeeded())
  {
    transform.m_qRotation = plQuat::MakeShortestRotation(plVec3::MakeAxisX(), vTravelDir.GetAsVec3(0));
  }

  if (m_fLinkProgress >= 1.0f)
  {
    FinishLinkTraversal();
  }
}

void plAiNavigationComponent::FinishLinkTraversal()
{
  if (m_State != plAiNavigationComponentState::TraversingLink)
    return;

  GetOwner()->SetGlobalPosition(m_vLinkEnd);

  // keep the steering model coherent with the new position
  m_Steering.m_vPosition = m_vLinkEnd;
  m_Steering.m_qRotation = GetOwner()->GetGlobalRotation();
  m_Steering.m_vVelocity = plVec3::MakeZero();

  m_bLinkHandledExternally = false;
  m_State = plAiNavigationComponentState::Moving;
}

void plAiNavigationComponent::ApplyMovement(const plTransform& transform, float tDiff)
{
  switch (m_MovementMode.GetValue())
  {
    case plAiMovementMode::DirectTransform:
    {
      if (m_bApplySteering)
      {
        GetOwner()->SetGlobalPosition(transform.m_vPosition);
        GetOwner()->SetGlobalRotation(transform.m_qRotation);
      }

      break;
    }

    case plAiMovementMode::PhysicsCharacter:
    {
      // the character controller moves the owner during the physics update; the corridor tracks
      // reality automatically because Steer() re-reads the owner position every frame
      if (m_State == plAiNavigationComponentState::TraversingLink && !m_bLinkHandledExternally)
      {
        // drive the CC towards the link end
        plVec3 vToEnd = m_vLinkEnd - GetOwner()->GetGlobalPosition();
        vToEnd.z = 0.0f;

        if (vToEnd.GetLengthSquared() < plMath::Square(0.3f))
        {
          FinishLinkTraversal();
        }
        else
        {
          vToEnd.NormalizeIfNotZero(plVec3::MakeAxisX()).IgnoreResult();
          MapVelocityToCharacterInput(vToEnd * m_fLinkTraverseSpeed, tDiff);
        }
      }
      else if (m_State == plAiNavigationComponentState::Moving)
      {
        MapVelocityToCharacterInput(m_Steering.m_vDesiredVelocity, tDiff);
      }

      break;
    }

    case plAiMovementMode::RootMotion:
    {
      // publish movement intent for the animation graph; the anim system moves the mesh,
      // a gentle drift correction keeps the agent on the corridor
      if (auto pBlackboard = plBlackboardComponent::FindBlackboard(GetOwner()))
      {
        const float fSpeedFraction = (m_fSpeed > 0.01f) ? plMath::Clamp(m_Steering.m_vDesiredVelocity.GetLength() / m_fSpeed, 0.0f, 1.0f) : 0.0f;
        pBlackboard->SetEntryValue("MoveForwards", fSpeedFraction);
      }

      GetOwner()->SetGlobalRotation(transform.m_qRotation);

      const float fCorrection = plMath::Min(1.0f, tDiff * cvar_AiNavDriftCorrection);
      const plVec3 vCorrected = plMath::Lerp(GetOwner()->GetGlobalPosition(), transform.m_vPosition, fCorrection);
      GetOwner()->SetGlobalPosition(vCorrected);

      break;
    }
  }
}

void plAiNavigationComponent::MapVelocityToCharacterInput(const plVec3& vDesiredVelocity, float tDiff)
{
  // plMsgMoveCharacterController carries NORMALIZED input [0..1] plus a run flag - not a velocity.
  // The character controller computes: final velocity = rotation * inputDir * configured speed.
  // Map the desired world-space velocity onto that model using the CC speed configuration
  // mirrored in the CCWalkSpeed/CCRunSpeed/CCRotateSpeed properties.
  const plVec3 vLocal = GetOwner()->GetGlobalRotation().GetInverse() * vDesiredVelocity;
  const float fSpeed = vDesiredVelocity.GetLength();

  const bool bRun = fSpeed > m_fCCWalkSpeed * 1.05f;
  const float fReferenceSpeed = plMath::Max(0.1f, bRun ? m_fCCRunSpeed : m_fCCWalkSpeed);

  plMsgMoveCharacterController msg;
  msg.m_fMoveForwards = plMath::Clamp(vLocal.x / fReferenceSpeed, 0.0f, 1.0f);
  msg.m_fMoveBackwards = plMath::Clamp(-vLocal.x / fReferenceSpeed, 0.0f, 1.0f);
  msg.m_fStrafeRight = plMath::Clamp(vLocal.y / fReferenceSpeed, 0.0f, 1.0f);
  msg.m_fStrafeLeft = plMath::Clamp(-vLocal.y / fReferenceSpeed, 0.0f, 1.0f);
  msg.m_bRun = bRun;

  // rotation: the CC rotates by rotateSpeed * input per tick - convert this frame's desired yaw delta
  const plQuat qCurrent = GetOwner()->GetGlobalRotation();
  const plQuat qDesired = m_Steering.m_qRotation;

  const plVec3 vCurrentFwd = qCurrent * plVec3::MakeAxisX();
  const plVec3 vDesiredFwd = qDesired * plVec3::MakeAxisX();

  const float fCross = vCurrentFwd.x * vDesiredFwd.y - vCurrentFwd.y * vDesiredFwd.x;
  const plAngle yawDelta = vCurrentFwd.GetAsVec2().GetAngleBetween(vDesiredFwd.GetAsVec2());

  const float fRotateAmount = plMath::Clamp(yawDelta.GetRadian() / plMath::Max(0.01f, m_CCRotateSpeed.GetRadian() * tDiff), 0.0f, 1.0f);

  if (fCross >= 0.0f)
    msg.m_fRotateLeft = fRotateAmount;
  else
    msg.m_fRotateRight = fRotateAmount;

  GetOwner()->SendMessage(msg);
}

void plAiNavigationComponent::Turn(plTransform& transform, float tDiff)
{
  if (m_State != plAiNavigationComponentState::Turning)
    return;

  plAngle turnSpeed = plAngle::MakeFromDegree(360);

  const plAngle remainingAngle = GetTurnAngleTowards(m_vTurnTowardsPos);
  const plAngle rotateNow = tDiff * turnSpeed;

  plAngle toRotate;

  if (rotateNow >= remainingAngle)
  {
    toRotate = remainingAngle;
    m_State = plAiNavigationComponentState::Idle;
  }
  else
  {
    toRotate = rotateNow;
  }

  const plQuat qRot = plQuat::MakeFromAxisAndAngle(plVec3::MakeAxisZ(), toRotate);
  const plVec3 vNewLookDir = qRot * GetOwner()->GetGlobalDirForwards();

  transform.m_qRotation = plQuat::MakeShortestRotation(plVec3::MakeAxisX(), vNewLookDir);
}

void plAiNavigationComponent::PlaceOnGround(plTransform& transform, float tDiff)
{
  if (m_fFootRadius <= 0.0f)
    return;

  plPhysicsWorldModuleInterface* pPhysicsInterface = GetWorld()->GetOrCreateModule<plPhysicsWorldModuleInterface>();

  if (pPhysicsInterface == nullptr)
    return;

  const plVec3 vDown = -plVec3::MakeAxisZ();
  const float fDistUp = 1.0f;
  const float fDistDown = m_fFallHeight;
  const plVec3 vStartPos = transform.m_vPosition - fDistUp * vDown;

  float fMoveUp = 0.0f;
  bool bHadCollision = false;

  plPhysicsCastResult res;
  plPhysicsQueryParameters params(m_uiCollisionLayer, plPhysicsShapeType::Static);
  if (pPhysicsInterface->SweepTestSphere(res, m_fFootRadius, vStartPos, vDown, fDistUp + fDistDown, params))
  {
    if (res.m_fDistance == 0.0f)
    {
      // ran into an obstacle
      // this shouldn't happen when walking just on the navmesh, as long as foot radius is smaller than the character radius
      // it can easily happen, once outside forces push the NPC around, but then one should really use a proper
      // physics character controller to avoid geometry
      return;
    }

    // found an intersection within the search radius
    bHadCollision = true;
    const float fFloorHeight = vStartPos.z - res.m_fDistance - m_fFootRadius;
    fMoveUp = fFloorHeight - transform.m_vPosition.z;
  }
  else
  {
    // did not find an intersection -> falling down
    fMoveUp = -fDistDown; // will be clamped by gravity
    CancelNavigation();
    m_State = plAiNavigationComponentState::Falling;
  }

  if (fMoveUp > 0.0f)
  {
    // if the character is being pushed up

    // TODO: better lerp up
    transform.m_vPosition.z += fMoveUp * plMath::Min(1.0f, 25.0f * tDiff);

    m_fFallSpeed = 0.0f;
  }
  else
  {
    m_fFallSpeed += pPhysicsInterface->GetGravity().z * tDiff;

    float fFallDist = m_fFallSpeed * tDiff;

    if (fFallDist < fMoveUp)
    {
      // clamp to the maximum speed / or to the floor
      fFallDist = fMoveUp;

      if (bHadCollision)
      {
        m_fFallSpeed = 0.0f;

        if (m_State == plAiNavigationComponentState::Falling)
        {
          // we just landed from a high fall -> starting to walk again probably makes no sense, since we obviously left the navmesh
          m_State = plAiNavigationComponentState::Fallen;
        }
      }
    }

    transform.m_vPosition.z += fFallDist;
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Components_NavigationComponent);
