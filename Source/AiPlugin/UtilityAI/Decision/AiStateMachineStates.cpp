#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/Components/NavigationComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiStateMachineStates.h>
#include <AiPlugin/UtilityAI/Impl/GameAiActions.h>
#include <Core/Utils/Blackboard.h>
#include <Foundation/IO/TypeVersionContext.h>
#include <Foundation/Serialization/ReflectionSerializer.h>
#include <RendererCore/Components/SplineComponent.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiNavigateTo, 1, plRTTIDefaultAllocator<plStateMachineState_AiNavigateTo>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("TargetPositionEntry", m_sTargetPositionEntry),
    PL_MEMBER_PROPERTY("ResultEntry", m_sResultEntry),
    PL_MEMBER_PROPERTY("Speed", m_fSpeed)->AddAttributes(new plDefaultValueAttribute(3.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_MEMBER_PROPERTY("ReachedDistance", m_fReachedDistance)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.01f, plVariant())),
    PL_MEMBER_PROPERTY("AllowPartialPath", m_bAllowPartialPath),
    PL_MEMBER_PROPERTY("EndBehaviorWhenDone", m_bEndBehaviorWhenDone)->AddAttributes(new plDefaultValueAttribute(true)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

namespace
{
  plGameObject* GetOwnerObject(plStateMachineInstance& ref_instance)
  {
    if (plComponent* pComponent = plDynamicCast<plComponent*>(&ref_instance.GetOwner()))
    {
      return pComponent->GetOwner();
    }

    return nullptr;
  }

  void SetBlackboardInt(plStateMachineInstance& ref_instance, const plHashedString& sEntry, plInt32 iValue)
  {
    if (sEntry.IsEmpty())
      return;

    if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
    {
      pBlackboard->SetEntryValue(sEntry, iValue);
    }
  }

  void SetBlackboardBool(plStateMachineInstance& ref_instance, plStringView sEntry, bool bValue)
  {
    if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
    {
      pBlackboard->SetEntryValue(sEntry, bValue);
    }
  }
} // namespace

plStateMachineState_AiNavigateTo::plStateMachineState_AiNavigateTo(plStringView sName)
  : plStateMachineState(sName)
{
  m_sTargetPositionEntry.Assign("Ai_TargetPosition");
  m_sResultEntry.Assign("Ai_NavResult");
}

plStateMachineState_AiNavigateTo::~plStateMachineState_AiNavigateTo() = default;

void plStateMachineState_AiNavigateTo::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);
  pData->m_bNavigationStarted = false;
  pData->m_bFinished = false;

  SetBlackboardInt(ref_instance, m_sResultEntry, 0);

  plGameObject* pOwner = GetOwnerObject(ref_instance);
  if (pOwner == nullptr)
    return;

  plAiNavigationComponent* pNavigation = nullptr;
  if (!pOwner->TryGetComponentOfBaseType(pNavigation))
  {
    plLog::Error("plStateMachineState_AiNavigateTo: owner object '{}' has no plAiNavigationComponent.", pOwner->GetName());
    return;
  }

  plVec3 vTargetPosition = plVec3::MakeZero();

  if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
  {
    const plVariant value = pBlackboard->GetEntryValue(m_sTargetPositionEntry);
    if (value.IsA<plVec3>())
    {
      vTargetPosition = value.Get<plVec3>();
    }
    else
    {
      plLog::Error("plStateMachineState_AiNavigateTo: blackboard entry '{}' does not hold a Vec3.", m_sTargetPositionEntry);
      return;
    }
  }

  pNavigation->m_fSpeed = m_fSpeed;
  pNavigation->m_fReachedDistance = m_fReachedDistance;
  pNavigation->SetDestination(vTargetPosition, m_bAllowPartialPath);

  pData->m_bNavigationStarted = true;
}

void plStateMachineState_AiNavigateTo::OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  if (pData->m_bNavigationStarted && !pData->m_bFinished)
  {
    if (plGameObject* pOwner = GetOwnerObject(ref_instance))
    {
      plAiNavigationComponent* pNavigation = nullptr;
      if (pOwner->TryGetComponentOfBaseType(pNavigation))
      {
        pNavigation->CancelNavigation();
      }
    }
  }
}

void plStateMachineState_AiNavigateTo::Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  if (pData->m_bFinished)
    return;

  plGameObject* pOwner = GetOwnerObject(ref_instance);
  if (pOwner == nullptr)
    return;

  plAiNavigationComponent* pNavigation = nullptr;
  if (!pOwner->TryGetComponentOfBaseType(pNavigation))
    return;

  if (!pData->m_bNavigationStarted)
  {
    // configuration failed on enter
    pData->m_bFinished = true;
    SetBlackboardInt(ref_instance, m_sResultEntry, 2);

    if (m_bEndBehaviorWhenDone)
    {
      SetBlackboardBool(ref_instance, "Ai_BehaviorFailed", true);
    }

    return;
  }

  const plAiNavigationComponentState::Enum state = pNavigation->GetState();

  if (state == plAiNavigationComponentState::Failed || state == plAiNavigationComponentState::Fallen)
  {
    pData->m_bFinished = true;
    SetBlackboardInt(ref_instance, m_sResultEntry, 2);

    if (m_bEndBehaviorWhenDone)
    {
      SetBlackboardBool(ref_instance, "Ai_BehaviorFailed", true);
    }
  }
  else if (state == plAiNavigationComponentState::Idle)
  {
    pData->m_bFinished = true;
    SetBlackboardInt(ref_instance, m_sResultEntry, 1);

    if (m_bEndBehaviorWhenDone)
    {
      SetBlackboardBool(ref_instance, "Ai_BehaviorDone", true);
    }
  }
}

plResult plStateMachineState_AiNavigateTo::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));

  inout_stream << m_sTargetPositionEntry;
  inout_stream << m_sResultEntry;
  inout_stream << m_fSpeed;
  inout_stream << m_fReachedDistance;
  inout_stream << m_bAllowPartialPath;
  inout_stream << m_bEndBehaviorWhenDone;

  return PL_SUCCESS;
}

plResult plStateMachineState_AiNavigateTo::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));

  inout_stream >> m_sTargetPositionEntry;
  inout_stream >> m_sResultEntry;
  inout_stream >> m_fSpeed;
  inout_stream >> m_fReachedDistance;
  inout_stream >> m_bAllowPartialPath;
  inout_stream >> m_bEndBehaviorWhenDone;

  return PL_SUCCESS;
}

bool plStateMachineState_AiNavigateTo::GetInstanceDataDesc(plInstanceDataDesc& out_desc)
{
  out_desc.FillFromType<InstanceData>();
  return true;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiSetBlackboardEntries, 1, plRTTIDefaultAllocator<plStateMachineState_AiSetBlackboardEntries>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ARRAY_MEMBER_PROPERTY("OnEnterEntries", m_OnEnterEntries),
    PL_ARRAY_MEMBER_PROPERTY("OnExitEntries", m_OnExitEntries),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plStateMachineState_AiSetBlackboardEntries::plStateMachineState_AiSetBlackboardEntries(plStringView sName)
  : plStateMachineState(sName)
{
}

plStateMachineState_AiSetBlackboardEntries::~plStateMachineState_AiSetBlackboardEntries() = default;

void plStateMachineState_AiSetBlackboardEntries::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard();
  if (pBlackboard == nullptr)
    return;

  for (const auto& entry : m_OnEnterEntries)
  {
    pBlackboard->SetEntryValue(entry.m_sName, entry.m_InitialValue);
  }
}

void plStateMachineState_AiSetBlackboardEntries::OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const
{
  const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard();
  if (pBlackboard == nullptr)
    return;

  for (const auto& entry : m_OnExitEntries)
  {
    pBlackboard->SetEntryValue(entry.m_sName, entry.m_InitialValue);
  }
}

namespace
{
  // plBlackboardEntry::Serialize is not exported from the GameEngine DLL, so serialize the fields manually
  void WriteBlackboardEntries(plStreamWriter& inout_stream, const plDynamicArray<plBlackboardEntry>& entries)
  {
    inout_stream << entries.GetCount();

    for (const auto& entry : entries)
    {
      inout_stream << entry.m_sName;
      inout_stream << entry.m_InitialValue;
      inout_stream << entry.m_Flags;
    }
  }

  void ReadBlackboardEntries(plStreamReader& inout_stream, plDynamicArray<plBlackboardEntry>& out_entries)
  {
    plUInt32 uiCount = 0;
    inout_stream >> uiCount;

    out_entries.Clear();
    out_entries.Reserve(uiCount);

    for (plUInt32 i = 0; i < uiCount; ++i)
    {
      auto& entry = out_entries.ExpandAndGetRef();
      inout_stream >> entry.m_sName;
      inout_stream >> entry.m_InitialValue;
      inout_stream >> entry.m_Flags;
    }
  }
} // namespace

plResult plStateMachineState_AiSetBlackboardEntries::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));

  WriteBlackboardEntries(inout_stream, m_OnEnterEntries);
  WriteBlackboardEntries(inout_stream, m_OnExitEntries);

  return PL_SUCCESS;
}

plResult plStateMachineState_AiSetBlackboardEntries::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));

  ReadBlackboardEntries(inout_stream, m_OnEnterEntries);
  ReadBlackboardEntries(inout_stream, m_OnExitEntries);

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiWait, 1, plRTTIDefaultAllocator<plStateMachineState_AiWait>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("MinDuration", m_MinDuration)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(1.0)), new plClampValueAttribute(plTime::MakeZero(), plVariant())),
    PL_MEMBER_PROPERTY("MaxDuration", m_MaxDuration)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(1.0)), new plClampValueAttribute(plTime::MakeZero(), plVariant())),
    PL_MEMBER_PROPERTY("ResultEntry", m_sResultEntry),
    PL_MEMBER_PROPERTY("EndBehaviorWhenDone", m_bEndBehaviorWhenDone),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plStateMachineState_AiWait::plStateMachineState_AiWait(plStringView sName)
  : plStateMachineState(sName)
{
  m_sResultEntry.Assign("Ai_WaitResult");
}

plStateMachineState_AiWait::~plStateMachineState_AiWait() = default;

void plStateMachineState_AiWait::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);
  pData->m_bFinished = false;

  const double fMinSeconds = m_MinDuration.GetSeconds();
  const double fMaxSeconds = plMath::Max(fMinSeconds, m_MaxDuration.GetSeconds());

  double fSeconds = fMinSeconds;

  if (fMaxSeconds > fMinSeconds)
  {
    if (plWorld* pWorld = ref_instance.GetOwnerWorld())
    {
      fSeconds = pWorld->GetRandomNumberGenerator().DoubleMinMax(fMinSeconds, fMaxSeconds);
    }
  }

  pData->m_Remaining = plTime::MakeFromSeconds(fSeconds);

  SetBlackboardInt(ref_instance, m_sResultEntry, 0);
}

void plStateMachineState_AiWait::Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  if (pData->m_bFinished)
    return;

  pData->m_Remaining -= deltaTime;

  if (pData->m_Remaining.IsPositive())
    return;

  pData->m_bFinished = true;
  SetBlackboardInt(ref_instance, m_sResultEntry, 1);

  if (m_bEndBehaviorWhenDone)
  {
    SetBlackboardBool(ref_instance, "Ai_BehaviorDone", true);
  }
}

plResult plStateMachineState_AiWait::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));

  inout_stream << m_MinDuration;
  inout_stream << m_MaxDuration;
  inout_stream << m_sResultEntry;
  inout_stream << m_bEndBehaviorWhenDone;

  return PL_SUCCESS;
}

plResult plStateMachineState_AiWait::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));

  inout_stream >> m_MinDuration;
  inout_stream >> m_MaxDuration;
  inout_stream >> m_sResultEntry;
  inout_stream >> m_bEndBehaviorWhenDone;

  return PL_SUCCESS;
}

bool plStateMachineState_AiWait::GetInstanceDataDesc(plInstanceDataDesc& out_desc)
{
  out_desc.FillFromType<InstanceData>();
  return true;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiPatrolPointMode, 1)
  PL_ENUM_CONSTANTS(plAiPatrolPointMode::Waypoints, plAiPatrolPointMode::RandomAroundHome, plAiPatrolPointMode::Spline)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiPickPatrolPoint, 3, plRTTIDefaultAllocator<plStateMachineState_AiPickPatrolPoint>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Mode", plAiPatrolPointMode, m_Mode),
    PL_MEMBER_PROPERTY("TargetEntry", m_sTargetEntry),
    PL_MEMBER_PROPERTY("ResultEntry", m_sResultEntry),
    PL_MEMBER_PROPERTY("RouteGlobalKey", m_sRouteGlobalKey),
    PL_MEMBER_PROPERTY("Radius", m_fRadius)->AddAttributes(new plDefaultValueAttribute(10.0f), new plClampValueAttribute(0.5f, 1000.0f)),
    PL_MEMBER_PROPERTY("HomeEntry", m_sHomeEntry),
    PL_MEMBER_PROPERTY("StepDistance", m_fStepDistance)->AddAttributes(new plDefaultValueAttribute(5.0f), new plClampValueAttribute(0.5f, 1000.0f)),
    PL_MEMBER_PROPERTY("ResumeRadius", m_fResumeRadius)->AddAttributes(new plDefaultValueAttribute(1.5f), new plClampValueAttribute(0.0f, 100.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plStateMachineState_AiPickPatrolPoint::plStateMachineState_AiPickPatrolPoint(plStringView sName)
  : plStateMachineState(sName)
{
  m_sTargetEntry.Assign("Ai_PatrolTarget");
  m_sResultEntry.Assign("Ai_PatrolResult");
  m_sHomeEntry.Assign("Ai_PatrolHome");
}

plStateMachineState_AiPickPatrolPoint::~plStateMachineState_AiPickPatrolPoint() = default;

plGameObject* plStateMachineState_AiPickPatrolPoint::ResolveRouteObject(plWorld* pWorld, InstanceData* pData) const
{
  if (m_sRouteGlobalKey.IsEmpty())
  {
    if (!pData->m_bRouteSearched)
    {
      pData->m_bRouteSearched = true;
      plLog::Warning("AiPickPatrolPoint: no RouteGlobalKey configured.");
    }

    return nullptr;
  }

  plGameObject* pRoute = nullptr;

  if (!pData->m_hRoute.IsInvalidated())
  {
    pWorld->TryGetObject(pData->m_hRoute, pRoute);
  }

  if (pRoute == nullptr)
  {
    const plTempHashedString sRouteName(m_sRouteGlobalKey.GetData());

    // resolve by global key first, by object name as fallback
    if (!pWorld->TryGetObjectWithGlobalKey(sRouteName, pRoute))
    {
      pWorld->Traverse([&](plGameObject* pObject) {
        if (pObject->HasName(sRouteName))
        {
          pRoute = pObject;
          return plVisitorExecution::Stop;
        }
        return plVisitorExecution::Continue;
      });

      if (pRoute != nullptr && !pData->m_bRouteSearched)
      {
        plLog::Info("AiPickPatrolPoint: route '{}' found by object name (no matching global key).", m_sRouteGlobalKey);
      }
    }

    if (pRoute != nullptr)
    {
      pData->m_hRoute = pRoute->GetHandle();
    }
    else if (!pData->m_bRouteSearched)
    {
      plLog::Warning("AiPickPatrolPoint: no object with global key or name '{}' found.", m_sRouteGlobalKey);
    }

    pData->m_bRouteSearched = true; // only log once, but keep retrying (the route may get spawned later)
  }

  return pRoute;
}

namespace
{
  /// Inverse of plSplineComponent::GetKeyAtDistanceHelper(). Distances and keys both increase monotonically.
  float PatrolSplineKeyToDistance(const plArrayMap<float, float>& distanceToKey, float fKey)
  {
    if (distanceToKey.IsEmpty())
      return 0.0f;

    plUInt32 uiLower = 0;
    plUInt32 uiUpper = distanceToKey.GetCount() - 1;

    if (fKey <= distanceToKey.GetValue(uiLower))
      return distanceToKey.GetKey(uiLower);

    if (fKey >= distanceToKey.GetValue(uiUpper))
      return distanceToKey.GetKey(uiUpper);

    while (uiUpper - uiLower > 1)
    {
      const plUInt32 uiMid = (uiLower + uiUpper) / 2;

      if (distanceToKey.GetValue(uiMid) <= fKey)
        uiLower = uiMid;
      else
        uiUpper = uiMid;
    }

    const float fLowerKey = distanceToKey.GetValue(uiLower);
    const float fUpperKey = distanceToKey.GetValue(uiUpper);
    return plMath::Lerp(distanceToKey.GetKey(uiLower), distanceToKey.GetKey(uiUpper), plMath::Saturate(plMath::Unlerp(fLowerKey, fUpperKey, fKey)));
  }
} // namespace

void plStateMachineState_AiPickPatrolPoint::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  plGameObject* pOwner = GetOwnerObject(ref_instance);
  plWorld* pWorld = ref_instance.GetOwnerWorld();

  const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard();

  if (pOwner == nullptr || pWorld == nullptr || pBlackboard == nullptr)
  {
    SetBlackboardInt(ref_instance, m_sResultEntry, 2);
    return;
  }

  const plVec3 vOwnerPosition = pOwner->GetGlobalPosition();
  const float fResumeRadiusSqr = plMath::Square(m_fResumeRadius);

  plVec3 vPickedPosition = plVec3::MakeZero();
  bool bPicked = false;

  switch (m_Mode.GetValue())
  {
    case plAiPatrolPointMode::Waypoints:
    {
      plGameObject* pRoute = ResolveRouteObject(pWorld, pData);
      if (pRoute == nullptr)
        break;

      plHybridArray<const plGameObject*, 16> waypoints;

      for (auto it = pRoute->GetChildren(); it.IsValid(); ++it)
      {
        waypoints.PushBack(&(*it));
      }

      if (waypoints.IsEmpty())
      {
        plLog::Warning("AiPickPatrolPoint: route '{}' has no child waypoints.", m_sRouteGlobalKey);
        break;
      }

      if (!pData->m_bCursorSeeded)
      {
        pData->m_bCursorSeeded = true;

        plUInt32 uiClosest = 0;
        float fClosestDistSqr = plMath::MaxValue<float>();

        for (plUInt32 i = 0; i < waypoints.GetCount(); ++i)
        {
          const float fDistSqr = (waypoints[i]->GetGlobalPosition() - vOwnerPosition).GetLengthSquared();
          if (fDistSqr < fClosestDistSqr)
          {
            fClosestDistSqr = fDistSqr;
            uiClosest = i;
          }
        }

        // already standing on the closest waypoint -> continue with the one after it
        if (fClosestDistSqr <= fResumeRadiusSqr)
        {
          uiClosest = (uiClosest + 1) % waypoints.GetCount();
        }

        pData->m_uiNextWaypoint = uiClosest;
      }

      vPickedPosition = waypoints[pData->m_uiNextWaypoint % waypoints.GetCount()]->GetGlobalPosition();
      pData->m_uiNextWaypoint = (pData->m_uiNextWaypoint + 1) % waypoints.GetCount();
      bPicked = true;
      break;
    }

    case plAiPatrolPointMode::Spline:
    {
      plGameObject* pRoute = ResolveRouteObject(pWorld, pData);
      if (pRoute == nullptr)
        break;

      const plSplineComponent* pSpline = nullptr;
      if (!pRoute->TryGetComponentOfBaseType(pSpline))
      {
        plLog::Warning("AiPickPatrolPoint: route '{}' has no plSplineComponent.", m_sRouteGlobalKey);
        break;
      }

      const float fTotalLength = pSpline->GetTotalLength();

      if (fTotalLength <= 0.01f)
      {
        plLog::Warning("AiPickPatrolPoint: spline on route '{}' has no length.", m_sRouteGlobalKey);
        break;
      }

      float fStep = m_fStepDistance;

      if (!pData->m_bCursorSeeded)
      {
        pData->m_bCursorSeeded = true;

        float fLocalDistance = 0.0f;
        const float fKey = pSpline->FindKeyClosestToPoint(vOwnerPosition, fLocalDistance, plSplineComponentSpace::Global);
        pData->m_fSplineDistance = PatrolSplineKeyToDistance(pSpline->GetDistanceToKeyRemapping(), fKey);

        // measured in global space: fLocalDistance ignores the route's scale.
        // not on the spline yet -> walk to the closest point first instead of stepping past it
        const plVec3 vClosest = pSpline->GetPositionAtDistance(pData->m_fSplineDistance, plSplineComponentSpace::Global);
        if ((vClosest - vOwnerPosition).GetLengthSquared() > fResumeRadiusSqr)
        {
          fStep = 0.0f;
        }
      }

      float fDistance = pData->m_fSplineDistance + fStep * static_cast<float>(pData->m_iSplineDirection);

      if (pSpline->GetClosed())
      {
        // closed loop: wrap around
        fDistance = plMath::Mod(fDistance, fTotalLength);
        if (fDistance < 0.0f)
        {
          fDistance += fTotalLength;
        }
      }
      else
      {
        // open spline: ping-pong between the ends
        if (fDistance > fTotalLength)
        {
          fDistance = fTotalLength;
          pData->m_iSplineDirection = -1;
        }
        else if (fDistance < 0.0f)
        {
          fDistance = 0.0f;
          pData->m_iSplineDirection = 1;
        }
      }

      pData->m_fSplineDistance = fDistance;

      vPickedPosition = pSpline->GetPositionAtDistance(fDistance, plSplineComponentSpace::Global);
      bPicked = true;
      break;
    }

    case plAiPatrolPointMode::RandomAroundHome:
    {
      plAiNavigationComponent* pNavigation = nullptr;
      if (!pOwner->TryGetComponentOfBaseType(pNavigation))
      {
        plLog::Warning("AiPickPatrolPoint: owner '{}' has no plAiNavigationComponent.", pOwner->GetName());
        break;
      }

      if (m_sHomeEntry.IsEmpty())
      {
        if (!pData->m_bHomeCaptured)
        {
          pData->m_bHomeCaptured = true;
          pData->m_vHomePosition = vOwnerPosition;
        }
      }
      else
      {
        // the blackboard outlives the state machine instance, so home is captured once per agent
        const plVariant home = pBlackboard->GetEntryValue(m_sHomeEntry);
        if (home.IsA<plVec3>())
        {
          pData->m_vHomePosition = home.Get<plVec3>();
        }
        else
        {
          pData->m_vHomePosition = vOwnerPosition;
          pBlackboard->SetEntryValue(m_sHomeEntry, vOwnerPosition);
        }
      }

      // may fail while the navmesh sector around home is still generating -> caller should retry later
      bPicked = pNavigation->FindRandomPointAroundCircle(pData->m_vHomePosition, m_fRadius, vPickedPosition);
      break;
    }
  }

  if (bPicked)
  {
    pBlackboard->SetEntryValue(m_sTargetEntry, vPickedPosition);
    SetBlackboardInt(ref_instance, m_sResultEntry, 1);
  }
  else
  {
    SetBlackboardInt(ref_instance, m_sResultEntry, 2);
  }
}

plResult plStateMachineState_AiPickPatrolPoint::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));

  inout_stream << m_Mode;
  inout_stream << m_sTargetEntry;
  inout_stream << m_sResultEntry;
  inout_stream << m_sRouteGlobalKey;
  inout_stream << m_fRadius;
  inout_stream << m_fStepDistance;
  inout_stream << m_fResumeRadius;
  inout_stream << m_sHomeEntry;

  return PL_SUCCESS;
}

plResult plStateMachineState_AiPickPatrolPoint::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));
  const plUInt32 uiVersion = plTypeVersionReadContext::GetContext()->GetTypeVersion(GetStaticRTTI());

  inout_stream >> m_Mode;
  inout_stream >> m_sTargetEntry;
  inout_stream >> m_sResultEntry;
  inout_stream >> m_sRouteGlobalKey;
  inout_stream >> m_fRadius;

  if (uiVersion >= 2)
  {
    inout_stream >> m_fStepDistance;
  }

  if (uiVersion >= 3)
  {
    inout_stream >> m_fResumeRadius;
    inout_stream >> m_sHomeEntry;
  }

  return PL_SUCCESS;
}

bool plStateMachineState_AiPickPatrolPoint::GetInstanceDataDesc(plInstanceDataDesc& out_desc)
{
  out_desc.FillFromType<InstanceData>();
  return true;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiActionDesc, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiActionDesc_Wait, 1, plRTTIDefaultAllocator<plAiActionDesc_Wait>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Duration", m_Duration),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiActionDesc_TurnTowardsTarget, 1, plRTTIDefaultAllocator<plAiActionDesc_TurnTowardsTarget>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("TargetPositionEntry", m_sTargetPositionEntry),
    PL_MEMBER_PROPERTY("TurnAnglesPerSec", m_TurnAnglesPerSec)->AddAttributes(new plDefaultValueAttribute(plAngle::MakeFromDegree(180))),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiActionDesc_Log, 1, plRTTIDefaultAllocator<plAiActionDesc_Log>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Text", m_sText),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiActionDesc_Spawn, 1, plRTTIDefaultAllocator<plAiActionDesc_Spawn>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("ChildObjectName", m_sChildObjectName),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiRunActions, 1, plRTTIDefaultAllocator<plStateMachineState_AiRunActions>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("ResultEntry", m_sResultEntry),
    PL_MEMBER_PROPERTY("EndBehaviorWhenDone", m_bEndBehaviorWhenDone),
    PL_ARRAY_MEMBER_PROPERTY("Actions", m_Actions)->AddFlags(plPropertyFlags::PointerOwner),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiAction* plAiActionDesc_Wait::CreateAction(plStateMachineInstance& ref_instance) const
{
  plAiActionWait* pAction = plAiActionWait::Create();
  pAction->m_Duration = m_Duration;
  return pAction;
}

plAiAction* plAiActionDesc_TurnTowardsTarget::CreateAction(plStateMachineInstance& ref_instance) const
{
  plVec3 vTargetPosition = plVec3::MakeZero();

  if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
  {
    const plVariant value = pBlackboard->GetEntryValue(m_sTargetPositionEntry);
    if (value.IsA<plVec3>())
    {
      vTargetPosition = value.Get<plVec3>();
    }
  }

  plAiActionLerpRotationTowards* pAction = plAiActionLerpRotationTowards::Create();
  pAction->m_vTargetPosition = vTargetPosition;
  pAction->m_TurnAnglesPerSec = m_TurnAnglesPerSec;
  return pAction;
}

plAiAction* plAiActionDesc_Log::CreateAction(plStateMachineInstance& ref_instance) const
{
  plAiActionQuip* pAction = plAiActionQuip::Create();
  pAction->m_sLogMsg = m_sText;
  return pAction;
}

plAiAction* plAiActionDesc_Spawn::CreateAction(plStateMachineInstance& ref_instance) const
{
  plAiActionSpawn* pAction = plAiActionSpawn::Create();
  pAction->m_sChildObjectName = plTempHashedString(m_sChildObjectName);
  return pAction;
}

//////////////////////////////////////////////////////////////////////////

plStateMachineState_AiRunActions::plStateMachineState_AiRunActions(plStringView sName)
  : plStateMachineState(sName)
{
  m_sResultEntry.Assign("Ai_ActionsResult");
}

plStateMachineState_AiRunActions::~plStateMachineState_AiRunActions()
{
  for (auto pAction : m_Actions)
  {
    auto pAllocator = pAction->GetDynamicRTTI()->GetAllocator();
    pAllocator->Deallocate(pAction);
  }
}

void plStateMachineState_AiRunActions::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);
  pData->m_bFinished = false;

  SetBlackboardInt(ref_instance, m_sResultEntry, 0);

  for (const plAiActionDesc* pDesc : m_Actions)
  {
    if (pDesc != nullptr)
    {
      pData->m_Queue.QueueAction(pDesc->CreateAction(ref_instance));
    }
  }
}

void plStateMachineState_AiRunActions::OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  if (plGameObject* pOwner = GetOwnerObject(ref_instance))
  {
    pData->m_Queue.CancelCurrentActions(*pOwner);
  }

  pData->m_Queue.InterruptAndClear();
}

void plStateMachineState_AiRunActions::Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  if (pData->m_bFinished)
    return;

  plGameObject* pOwner = GetOwnerObject(ref_instance);
  if (pOwner == nullptr)
    return;

  pData->m_Queue.Execute(*pOwner, deltaTime, plLog::GetThreadLocalLogSystem());

  if (pData->m_Queue.IsEmpty())
  {
    pData->m_bFinished = true;
    SetBlackboardInt(ref_instance, m_sResultEntry, 1);

    if (m_bEndBehaviorWhenDone)
    {
      SetBlackboardBool(ref_instance, "Ai_BehaviorDone", true);
    }
  }
}

plResult plStateMachineState_AiRunActions::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));

  inout_stream << m_sResultEntry;
  inout_stream << m_bEndBehaviorWhenDone;

  const plUInt32 uiNumActions = m_Actions.GetCount();
  inout_stream << uiNumActions;

  for (const plAiActionDesc* pAction : m_Actions)
  {
    auto pActionType = pAction->GetDynamicRTTI();
    plTypeVersionWriteContext::GetContext()->AddType(pActionType);

    inout_stream << pActionType->GetTypeName();
    plReflectionSerializer::WriteObjectToBinary(inout_stream, pActionType, pAction);
  }

  return PL_SUCCESS;
}

plResult plStateMachineState_AiRunActions::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));

  inout_stream >> m_sResultEntry;
  inout_stream >> m_bEndBehaviorWhenDone;

  plUInt32 uiNumActions = 0;
  inout_stream >> uiNumActions;
  m_Actions.Reserve(uiNumActions);

  plStringBuilder sTypeName;
  for (plUInt32 i = 0; i < uiNumActions; ++i)
  {
    inout_stream >> sTypeName;
    if (const plRTTI* pType = plRTTI::FindTypeByName(sTypeName))
    {
      plUniquePtr<plAiActionDesc> pAction = pType->GetAllocator()->Allocate<plAiActionDesc>();
      plReflectionSerializer::ReadObjectPropertiesFromBinary(inout_stream, *pType, pAction.Borrow());

      m_Actions.PushBack(pAction.Release());
    }
    else
    {
      plLog::Error("Unknown AI action desc type '{}'", sTypeName);
      return PL_FAILURE;
    }
  }

  return PL_SUCCESS;
}

bool plStateMachineState_AiRunActions::GetInstanceDataDesc(plInstanceDataDesc& out_desc)
{
  out_desc.FillFromType<InstanceData>();
  return true;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiStateMachineStates);
