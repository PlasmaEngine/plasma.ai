#include <EnginePluginAi/EnginePluginAiPCH.h>

#include <AiPlugin/Eqs/EqsCapture.h>
#include <AiPlugin/Eqs/EqsWorldModule.h>
#include <AiPlugin/Navigation/Components/NavigationComponent.h>
#include <AiPlugin/Navigation/Components/VoxelNavigationComponent.h>
#include <AiPlugin/Navigation/CrowdWorldModule.h>
#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <AiPlugin/Tactical/SquadWorldModule.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <AiPlugin/Tactical/ThreatWorldModule.h>
#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>
#include <AiPlugin/UtilityAI/Decision/AiDebugProtocol.h>
#include <AiPlugin/UtilityAI/Decision/AiDebugSnapshot.h>
#include <EditorEngineProcessFramework/EngineProcess/EngineProcessDocumentContext.h>
#include <EnginePluginAi/Debug/AiDebugHandler.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/Reflection/ReflectionUtils.h>

plHashTable<plUuid, plAiDebugHandler::Watch> plAiDebugHandler::s_Watches;

namespace
{
  /// Marks the high half of IDs made from object handles; editor GUIDs never have it.
  constexpr plUInt64 s_uiHandleIdMarker = 0xA1DEB06A1DEB06A1ull;

  double ToSeconds(plTime t)
  {
    return t.GetSeconds();
  }

  /// Whether the cvar holds the value, for bool and int cvars; false for unknown cvars.
  bool CVarEquals(const char* szName, plInt32 iValue)
  {
    plCVar* pCVar = plCVar::FindCVarByName(szName);
    if (pCVar == nullptr)
      return false;

    switch (pCVar->GetType())
    {
      case plCVarType::Bool:
        return static_cast<plCVarBool*>(pCVar)->GetValue() == (iValue != 0);
      case plCVarType::Int:
        return static_cast<plCVarInt*>(pCVar)->GetValue() == iValue;
      default:
        return false;
    }
  }

  plInt32 GetIntCVar(const char* szName, plInt32 iDefault)
  {
    if (plCVar* pCVar = plCVar::FindCVarByName(szName))
    {
      if (pCVar->GetType() == plCVarType::Int)
        return static_cast<plCVarInt*>(pCVar)->GetValue();
    }

    return iDefault;
  }

  /// The first agent on the object or below it, depth first.
  const plAiAgentComponent* FindAgentInSubtree(const plGameObject* pObject)
  {
    const plAiAgentComponent* pAgent = nullptr;
    if (pObject->TryGetComponentOfBaseType(pAgent))
      return pAgent;

    for (auto it = pObject->GetChildren(); it.IsValid(); ++it)
    {
      if (const plAiAgentComponent* pChildAgent = FindAgentInSubtree(&(*it)))
        return pChildAgent;
    }

    return nullptr;
  }

  plString GetObjectName(const plWorld* pWorld, const plGameObjectHandle& hObject)
  {
    const plGameObject* pObject = nullptr;
    if (!pWorld->TryGetObject(hObject, pObject))
      return {};

    return pObject->GetName();
  }
} // namespace

void plAiDebugHandler::Startup()
{
  plEngineProcessDocumentContext::s_ToolConfigEvents.AddEventHandler(OnToolConfig);
}

void plAiDebugHandler::Shutdown()
{
  plEngineProcessDocumentContext::s_ToolConfigEvents.RemoveEventHandler(OnToolConfig);
  s_Watches.Clear();
}

void plAiDebugHandler::OnToolConfig(const plEngineProcessToolConfigEvent& e)
{
  if (e.m_pContext == nullptr || e.m_pMsg == nullptr || e.m_pMsg->m_sWhatToDo != plAiDebugRequest::s_szWhatToDo)
    return;

  if (!e.m_pMsg->m_PayloadValue.IsA<plDataBuffer>())
    return;

  plAiDebugRequest request;
  if (request.FromBuffer(e.m_pMsg->m_PayloadValue.Get<plDataBuffer>()).Failed())
    return;

  plAiDebugFrame frame;
  frame.m_uiRequestId = request.m_uiRequestId;

  if (plWorld* pWorld = e.m_pContext->GetWorld())
  {
    PL_LOCK(pWorld->GetWriteMarker());
    BuildFrame(e.m_pContext, pWorld, request, frame);
  }

  plSimpleDocumentConfigMsgToEditor msg;
  msg.m_DocumentGuid = e.m_pMsg->m_DocumentGuid;
  msg.m_sWhatToDo = plAiDebugFrame::s_szWhatToDo;

  plDataBuffer buffer;
  frame.ToBuffer(buffer);
  msg.m_PayloadValue = buffer;

  e.m_pContext->SendProcessMessage(&msg);
}

plUuid plAiDebugHandler::GetObjectId(plEngineProcessDocumentContext* pContext, const plGameObjectHandle& hObject)
{
  if (hObject.IsInvalidated())
    return plUuid::MakeInvalid();

  const plUuid guid = (pContext != nullptr) ? pContext->m_Context.m_GameObjectMap.GetGuid(hObject) : plUuid::MakeInvalid();
  if (guid.IsValid())
    return guid;

  return plUuid(hObject.GetInternalID().m_Data, s_uiHandleIdMarker);
}

plGameObjectHandle plAiDebugHandler::ResolveObjectId(plEngineProcessDocumentContext* pContext, const plUuid& id)
{
  if (!id.IsValid())
    return {};

  plUInt64 uiLow = 0;
  plUInt64 uiHigh = 0;
  id.GetValues(uiLow, uiHigh);

  if (uiHigh == s_uiHandleIdMarker)
    return plGameObjectHandle(plGameObjectId(uiLow));

  return (pContext != nullptr) ? pContext->m_Context.m_GameObjectMap.GetHandle(id) : plGameObjectHandle();
}

void plAiDebugHandler::BuildFrame(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plAiDebugRequest& request, plAiDebugFrame& out_frame)
{
  out_frame.m_fWorldTime = ToSeconds(pWorld->GetClock().GetAccumulatedTime());
  out_frame.m_bSimulating = pWorld->GetWorldSimulationEnabled();

  const plArrayPtr<const plAiDebugOverlay> overlays = plAiGetDebugOverlays();
  for (plUInt32 i = 0; i < overlays.GetCount(); ++i)
  {
    if (CVarEquals(overlays[i].m_szCVar, overlays[i].m_iOnValue))
    {
      out_frame.m_uiOverlays |= PL_BIT(i);
    }
  }

  out_frame.m_bWatchedOnly = CVarEquals(plAiDebugWatchedOnlyCVar, 1);

  plAiBrainWorldModule* pBrain = pWorld->GetModule<plAiBrainWorldModule>();
  out_frame.m_bHasBrain = (pBrain != nullptr);

  // a picked object watches the agent in it: a prefab instance's agent is a child of the object the scene holds
  plAiDebugRequest effective = request;
  if (request.m_PickObject.IsValid())
  {
    plGameObject* pPicked = nullptr;
    if (pWorld->TryGetObject(ResolveObjectId(pContext, request.m_PickObject), pPicked))
    {
      if (const plAiAgentComponent* pAgent = FindAgentInSubtree(pPicked))
      {
        out_frame.m_PickedAgent = GetObjectId(pContext, pAgent->GetOwner()->GetHandle());
        effective.m_WatchedObject = out_frame.m_PickedAgent;
      }
    }
  }

  plComponentHandle hWatched;
  UpdateWatch(pContext, pWorld, effective, hWatched);

  if (pBrain != nullptr)
  {
    plDynamicArray<plAiAgentDebugSummary> summaries;
    pBrain->GetAgentSummaries(summaries);

    out_frame.m_Agents.Reserve(summaries.GetCount());

    plDynamicArray<plAiBrainWorldModule::HistoryEntry> history;

    for (const plAiAgentDebugSummary& summary : summaries)
    {
      const plGameObject* pOwner = nullptr;
      const plAiAgentComponent* pAgent = nullptr;
      if (!pWorld->TryGetObject(summary.m_hOwner, pOwner) || !pWorld->TryGetComponent(summary.m_hComponent, pAgent))
        continue;

      plAiDebugFrame::Agent& agent = out_frame.m_Agents.ExpandAndGetRef();
      agent.m_Object = GetObjectId(pContext, summary.m_hOwner);
      agent.m_sName = pOwner->GetName();
      agent.m_sArchetype = pAgent->GetArchetypeFile();
      agent.m_vPosition = pOwner->GetGlobalPosition();
      agent.m_sActiveBehavior = summary.m_sActiveBehavior.GetView();
      agent.m_uiActiveCategory = summary.m_uiActiveCategory;
      agent.m_Target = GetObjectId(pContext, summary.m_hTarget);
      agent.m_sTargetName = GetObjectName(pWorld, summary.m_hTarget);
      agent.m_uiLodTier = summary.m_uiLodTier;
      agent.m_uiAlertLevel = summary.m_uiAlertLevel;
      agent.m_bLocked = summary.m_bLocked;
      agent.m_bConfigured = summary.m_bConfigured;

      if (pBrain->GetAgentHistory(summary.m_hComponent, history))
      {
        for (const plAiBrainWorldModule::HistoryEntry& entry : history)
        {
          if (ToSeconds(entry.m_Time) <= request.m_fHistorySince)
            continue;

          plAiDebugFrame::HistoryEntry& out = agent.m_History.ExpandAndGetRef();
          out.m_fTime = ToSeconds(entry.m_Time);
          out.m_sFrom = entry.m_sFrom.GetView();
          out.m_sTo = entry.m_sTo.GetView();
          out.m_uiToCategory = entry.m_uiToCategory;
          out.m_fWinnerScore = entry.m_fWinnerScore;
          out.m_fRunnerUpScore = entry.m_fRunnerUpScore;
          out.m_uiOutcome = static_cast<plUInt8>(entry.m_Outcome);
        }
      }
    }

    const plAiBrainFrameStats& stats = pBrain->GetFrameStats();
    out_frame.m_Stats.m_uiAgents = stats.m_uiAgents;
    out_frame.m_Stats.m_uiDecisions = stats.m_uiDecisions;
    out_frame.m_Stats.m_uiWokenDecisions = stats.m_uiWokenDecisions;
    out_frame.m_Stats.m_uiDecisionBudget = stats.m_uiDecisionBudget;
    out_frame.m_Stats.m_fBrainMs = static_cast<float>(stats.m_UpdateTime.GetMilliseconds());
  }

  if (const plAiEqsWorldModule* pEqs = pWorld->GetModuleReadOnly<plAiEqsWorldModule>())
  {
    const plAiEqsFrameStats stats = pEqs->GetFrameStats();
    out_frame.m_Stats.m_uiEqsExecuted = stats.m_uiExecuted;
    out_frame.m_Stats.m_uiEqsSuspended = stats.m_uiSuspended;
    out_frame.m_Stats.m_uiEqsQueued = stats.m_uiQueued;
    out_frame.m_Stats.m_uiEqsTimedOut = stats.m_uiTimedOut;
    out_frame.m_Stats.m_uiRaycasts = stats.m_uiRaycasts;
  }

  if (const plAiCrowdWorldModule* pCrowd = pWorld->GetModuleReadOnly<plAiCrowdWorldModule>())
  {
    out_frame.m_Stats.m_uiCrowdAgents = pCrowd->GetSolvedAgentCount();
  }

  FillNavmesh(pWorld, request, out_frame);
  FillSquads(pContext, pWorld, request, out_frame);

  if (!hWatched.IsInvalidated())
  {
    FillWatched(pContext, pWorld, hWatched, out_frame);
  }
}

void plAiDebugHandler::FillSquads(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plAiDebugRequest& request, plAiDebugFrame& out_frame)
{
  plAiSquadWorldModule* pSquads = pWorld->GetModule<plAiSquadWorldModule>();
  if (pSquads == nullptr)
    return;

  // an order the Squad tab gave
  const plAiDebugRequest::OrderCommand& command = request.m_Order;

  if (command.m_Action != plAiDebugRequest::OrderCommand::Action::None && !command.m_sSquad.IsEmpty())
  {
    plHashedString sSquad;
    sSquad.Assign(command.m_sSquad);

    if (command.m_Action == plAiDebugRequest::OrderCommand::Action::Cancel)
    {
      pSquads->CancelOrder(sSquad);
    }
    else
    {
      plAiSquadOrder order;
      order.m_Intent = static_cast<plAiSquadIntent::Enum>(command.m_uiIntent);
      order.m_bHasRegion = command.m_bRegion;
      order.m_vRegionMin = command.m_vRegionMin;
      order.m_vRegionMax = command.m_vRegionMax;
      order.m_Duration = plTime::MakeFromSeconds(command.m_fSeconds);
      order.m_bNoRetreat = command.m_bNoRetreat;
      order.m_sIssuer = "AI Debugger";
      pSquads->IssueOrder(sSquad, order);
    }
  }

  plDynamicArray<plAiSquadWorldModule::SquadInfo> squads;
  pSquads->GetSquadInfos(squads);

  for (const plAiSquadWorldModule::SquadInfo& info : squads)
  {
    plAiDebugFrame::Squad& squad = out_frame.m_Squads.ExpandAndGetRef();
    squad.m_sId = info.m_sId.GetView();
    squad.m_uiPeak = info.m_uiPeakSize;
    squad.m_uiLosses = info.m_uiLosses;
    squad.m_uiRetreatLosses = info.m_uiRetreatLosses;
    squad.m_uiIntent = static_cast<plUInt8>(info.m_Intent.GetValue());
    squad.m_bHasOrder = info.m_bHasOrder;
    squad.m_uiOrderIntent = static_cast<plUInt8>(info.m_Order.m_Intent.GetValue());
    squad.m_bOrderRegion = info.m_Order.m_bHasRegion;
    squad.m_vRegionMin = info.m_Order.m_vRegionMin;
    squad.m_vRegionMax = info.m_Order.m_vRegionMax;
    squad.m_fOrderRemaining = info.m_fOrderRemaining;
    squad.m_fOrderDuration = info.m_Order.m_Duration.GetSeconds();
    squad.m_bOrderNoRetreat = info.m_Order.m_bNoRetreat;
    squad.m_sOrderIssuer = info.m_Order.m_sIssuer;
    squad.m_vHome = info.m_vHome;
    squad.m_bHasTarget = info.m_bHasTarget;
    squad.m_vTarget = info.m_vTargetPosition;

    for (const plAiSquadWorldModule::MemberInfo& memberInfo : info.m_Members)
    {
      plAiDebugFrame::SquadMember& member = squad.m_Members.ExpandAndGetRef();
      member.m_Object = GetObjectId(pContext, memberInfo.m_hOwner);
      member.m_sName = GetObjectName(pWorld, memberInfo.m_hOwner);
      member.m_vPosition = memberInfo.m_vPosition;
      member.m_bAttackToken = memberInfo.m_bAttackToken;
      member.m_bMoveToken = memberInfo.m_bMoveToken;
    }

    for (const plAiSquadWorldModule::HistoryInfo& historyInfo : info.m_History)
    {
      plAiDebugFrame::SquadHistory& entry = squad.m_History.ExpandAndGetRef();
      entry.m_fTime = historyInfo.m_fTime;
      entry.m_sText = historyInfo.m_sText;
    }
  }
}

void plAiDebugHandler::FillNavmesh(plWorld* pWorld, const plAiDebugRequest& request, plAiDebugFrame& out_frame)
{
  plAiNavMeshWorldModule* pNavModule = pWorld->GetModule<plAiNavMeshWorldModule>();
  if (pNavModule == nullptr)
    return;

  plAiNavMesh* pNavMesh = pNavModule->GetVisualizedNavMesh();
  if (pNavMesh == nullptr)
    return;

  pNavModule->SetInspectedSector(request.m_vInspectSector);

  const plAiNavmeshConfig& config = pNavMesh->GetConfig();
  const bool bInGrid = request.m_vInspectSector.x >= 0 && request.m_vInspectSector.y >= 0 && request.m_vInspectSector.x < config.m_uiNumSectorsX && request.m_vInspectSector.y < config.m_uiNumSectorsY;

  if (request.m_bRebuildSector && bInGrid)
  {
    const plAiNavMesh::SectorID sectorID = pNavMesh->CalculateSectorID(request.m_vInspectSector);
    const plAiNavMeshSector* pSector = pNavMesh->GetSector(sectorID);

    if (pSector != nullptr && pSector->m_FlagUsable == 1)
    {
      pNavMesh->InvalidateSector(sectorID, true, plAiNavMeshRebuildReason::Requested);
    }
    else
    {
      pNavMesh->RequestSector(sectorID);
    }
  }

  plAiDebugFrame::Navmesh& nav = out_frame.m_Navmesh;
  nav.m_bPresent = true;
  nav.m_sName = config.m_sName;
  nav.m_uiSectorsX = config.m_uiNumSectorsX;
  nav.m_uiSectorsY = config.m_uiNumSectorsY;
  nav.m_fSectorSize = pNavMesh->GetSectorSize();
  nav.m_uiPolyBudget = plAiNavMeshWorldModule::GetSectorPolyBudget();
  nav.m_uiMaxBuilds = static_cast<plUInt32>(plMath::Max(1, GetIntCVar("AI.Navmesh.MaxConcurrentBuilds", 4)));
  nav.m_uiViewMode = static_cast<plUInt8>(plMath::Clamp(GetIntCVar("AI.Navmesh.VisualizeMode", 0), 0, 255));

  plDynamicArray<plAiNavMeshSectorStatus> sectors;
  pNavMesh->GetSectorStatus(sectors);
  nav.m_Sectors.Reserve(sectors.GetCount());

  for (const plAiNavMeshSectorStatus& status : sectors)
  {
    plAiDebugFrame::NavSector& out = nav.m_Sectors.ExpandAndGetRef();
    out.m_vCoord = status.m_vCoord;
    out.m_uiState = static_cast<plUInt8>(status.m_State);
    out.m_uiReason = static_cast<plUInt8>(status.m_LastReason);
    out.m_uiPolys = status.m_uiPolyCount;
    out.m_uiBuilds = status.m_uiBuildCount;
    out.m_fBuildMs = static_cast<float>(status.m_LastBuildDuration.GetMilliseconds());
    out.m_fProgress = status.m_fBuildProgress;
    out.m_bRunning = status.m_bBuildRunning;
  }
}

void plAiDebugHandler::UpdateWatch(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plAiDebugRequest& request, plComponentHandle& out_hAgent)
{
  plAiBrainWorldModule* pBrain = pWorld->GetModule<plAiBrainWorldModule>();

  const plUuid documentGuid = (pContext != nullptr) ? pContext->GetDocumentGuid() : plUuid::MakeInvalid();
  Watch& watch = s_Watches[documentGuid];

  // a recreated world invalidates the old handle, there is nothing to unwatch
  if (watch.m_uiWorldIndex != pWorld->GetIndex())
  {
    watch = Watch();
    watch.m_uiWorldIndex = pWorld->GetIndex();
  }

  plComponentHandle hAgent;

  const plGameObjectHandle hObject = ResolveObjectId(pContext, request.m_WatchedObject);

  plGameObject* pObject = nullptr;
  if (pWorld->TryGetObject(hObject, pObject))
  {
    plAiAgentComponent* pAgent = nullptr;
    if (pObject->TryGetComponentOfBaseType(pAgent))
    {
      hAgent = pAgent->GetHandle();
    }
  }

  if (pBrain != nullptr && watch.m_hAgent != hAgent)
  {
    pBrain->SetAgentWatched(watch.m_hAgent, false);
  }

  watch.m_hAgent = hAgent;

  if (pBrain != nullptr && !hAgent.IsInvalidated())
  {
    // the agent may only have registered with the brain since the last poll, so this repeats every poll
    pBrain->SetAgentWatched(hAgent, true);
  }

  if (request.m_bCaptureEqs && pObject != nullptr)
  {
    if (plAiEqsWorldModule* pEqs = pWorld->GetModule<plAiEqsWorldModule>())
    {
      pEqs->CaptureNextQuery(pObject->GetHandle());
    }
  }

  out_hAgent = hAgent;
}

void plAiDebugHandler::FillWatched(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plComponentHandle& hAgent, plAiDebugFrame& out_frame)
{
  plAiAgentComponent* pAgent = nullptr;
  if (!pWorld->TryGetComponent(hAgent, pAgent))
    return;

  const plGameObject* pOwner = pAgent->GetOwner();

  out_frame.m_bHasWatched = true;
  plAiDebugFrame::Watched& watched = out_frame.m_Watched;
  watched.m_Object = GetObjectId(pContext, pOwner->GetHandle());

  if (const plAiBrainWorldModule* pBrain = pWorld->GetModuleReadOnly<plAiBrainWorldModule>())
  {
    plAiAgentDebugSnapshot snapshot;
    if (pBrain->GetAgentDebugSnapshot(hAgent, snapshot))
    {
      watched.m_bHasDecision = true;
      watched.m_fDecisionTime = ToSeconds(snapshot.m_DecisionTime);
      watched.m_fNextDecision = ToSeconds(snapshot.m_NextDecision);
      watched.m_uiDecisionCount = snapshot.m_uiDecisionCount;
      watched.m_uiOutcome = static_cast<plUInt8>(snapshot.m_Outcome);
      watched.m_iActiveBehavior = snapshot.m_iActiveBehavior;
      watched.m_iActiveTarget = snapshot.m_iActiveTarget;
      watched.m_fActiveScore = snapshot.m_fActiveScore;
      watched.m_fCommitBonus = snapshot.m_fCommitBonus;
      watched.m_bLocked = snapshot.m_bLocked;
      watched.m_iWinner = snapshot.m_iWinner;
      watched.m_fWinnerScore = snapshot.m_fWinnerScore;
      watched.m_fRunnerUpScore = snapshot.m_fRunnerUpScore;

      watched.m_Behaviors.Reserve(snapshot.m_Behaviors.GetCount());
      for (const plAiBehaviorTrace& trace : snapshot.m_Behaviors)
      {
        plAiDebugFrame::Behavior& behavior = watched.m_Behaviors.ExpandAndGetRef();
        behavior.m_sName = trace.m_sName.GetView();
        behavior.m_uiCategory = trace.m_uiCategory;
        behavior.m_uiVeto = static_cast<plUInt8>(trace.m_Veto);
        behavior.m_bActive = trace.m_bActive;
        behavior.m_fUtility = trace.m_fUtility;
        behavior.m_fScore = trace.m_fScore;
        behavior.m_iTarget = trace.m_iTarget;

        for (const plAiConsiderationTrace& consideration : trace.m_Considerations)
        {
          plAiDebugFrame::Consideration& out = behavior.m_Considerations.ExpandAndGetRef();
          out.m_sInput = (consideration.m_pInputType != nullptr) ? consideration.m_pInputType->GetTypeName() : plStringView();
          out.m_fRaw = consideration.m_fRaw;
          out.m_fNormalized = consideration.m_fNormalized;
          out.m_fValue = consideration.m_fValue;
        }
      }

      watched.m_Targets.Reserve(snapshot.m_Targets.GetCount());
      for (const plAiPerceivedTarget& perceived : snapshot.m_Targets)
      {
        plAiDebugFrame::Target& target = watched.m_Targets.ExpandAndGetRef();
        target.m_Object = GetObjectId(pContext, perceived.m_hTarget);
        target.m_sName = GetObjectName(pWorld, perceived.m_hTarget);
        target.m_vLastKnownPosition = perceived.m_vLastKnownPosition;
        target.m_fConfidence = perceived.m_fConfidence;
        target.m_fLastStimulusTime = ToSeconds(perceived.m_LastStimulusTime);
        target.m_fLastSightTime = ToSeconds(perceived.m_LastSightTime);
        target.m_uiLastSense = perceived.m_uiLastSense;
        target.m_uiSenses = perceived.m_uiSenses;
        target.m_bReported = perceived.m_bReported;
      }

      plStringBuilder sValue;
      for (const plAiAgentDebugSnapshot::BlackboardEntry& entry : snapshot.m_Blackboard)
      {
        plAiDebugFrame::BlackboardEntry& out = watched.m_Blackboard.ExpandAndGetRef();
        out.m_sName = entry.m_sName.GetView();

        if (const plRTTI* pType = plReflectionUtils::GetTypeFromVariant(entry.m_Value))
        {
          out.m_sType = pType->GetTypeName();
        }

        if (entry.m_Value.CanConvertTo<plString>())
        {
          out.m_sValue = entry.m_Value.ConvertTo<plString>();
        }
      }
    }
  }

  plStringBuilder sState;

  const plAiNavigationComponent* pNavigation = nullptr;
  const plAiVoxelNavigationComponent* pVoxelNavigation = nullptr;

  if (pOwner->TryGetComponentOfBaseType(pNavigation))
  {
    watched.m_sNavigation = pNavigation->GetDynamicRTTI()->GetTypeName();
    plReflectionUtils::EnumerationToString(pNavigation->GetState(), sState);
    watched.m_sNavigationState = sState;
    watched.m_vSteeringTarget = pNavigation->GetSteeringPosition();
  }
  else if (pOwner->TryGetComponentOfBaseType(pVoxelNavigation))
  {
    watched.m_sNavigation = pVoxelNavigation->GetDynamicRTTI()->GetTypeName();
    plReflectionUtils::EnumerationToString(pVoxelNavigation->GetState(), sState);
    watched.m_sNavigationState = sState;
    watched.m_vSteeringTarget = pVoxelNavigation->GetSteeringPosition();
  }

  if (const plAiEqsWorldModule* pEqs = pWorld->GetModuleReadOnly<plAiEqsWorldModule>())
  {
    watched.m_bEqsCaptureArmed = pEqs->IsCaptureArmed();

    const plAiEqsCapture* pCapture = pEqs->GetLastCapture();
    if (pCapture != nullptr && pCapture->m_hQuerier == pOwner->GetHandle())
    {
      watched.m_sEqsQuery = pCapture->m_sQueryName;
      watched.m_fEqsCaptureTime = ToSeconds(pCapture->m_Time);
      watched.m_uiEqsStatus = static_cast<plUInt8>(pCapture->m_Status);
      watched.m_uiEqsCandidates = pCapture->m_Candidates.GetCount();

      for (const plAiEqsCapture::Candidate& candidate : pCapture->m_Candidates)
      {
        if (!candidate.m_bDiscarded && candidate.m_uiRejectedBy == 0xFFFF)
        {
          ++watched.m_uiEqsSurvivors;
        }
      }

      if (pCapture->m_iSelected >= 0 && pCapture->m_iSelected < static_cast<plInt32>(pCapture->m_Candidates.GetCount()))
      {
        watched.m_fEqsBestScore = pCapture->m_Candidates[pCapture->m_iSelected].m_fFinal;
      }
    }
  }

  // the team's threat map where the agent stands, and the cover its latest query chose between
  const plHashedString sTeam = pAgent->GetResolvedTeam();
  watched.m_sTeam = sTeam.GetView();

  if (plAiTacticalWorldModule* pTactical = pWorld->GetModule<plAiTacticalWorldModule>())
  {
    pTactical->SetDebugWatched(pOwner->GetHandle());

    plAiCoverPointHandle hClaim;
    plAiCoverPoint claimed;
    if (pTactical->GetClaimedCover(pOwner->GetHandle(), hClaim) && pTactical->ResolveCover(hClaim, claimed))
    {
      watched.m_bHasClaim = true;
      watched.m_vClaimedCover = claimed.m_vPosition;
    }

    plDynamicArray<plAiTacticalQueryResult::Candidate> candidates;
    plTime queryTime;
    if (pTactical->GetDebugCandidates(candidates, queryTime))
    {
      watched.m_fCoverQueryTime = ToSeconds(queryTime);

      for (const plAiTacticalQueryResult::Candidate& candidate : candidates)
      {
        plAiDebugFrame::Watched::CoverCandidate& out = watched.m_CoverCandidates.ExpandAndGetRef();
        out.m_vPosition = candidate.m_vPosition;
        out.m_fProtection = candidate.m_fProtection;
        out.m_fThreat = candidate.m_fThreat;
        out.m_fDanger = candidate.m_fDanger;
        out.m_fScore = candidate.m_fScore;
        out.m_bPicked = watched.m_bHasClaim && (candidate.m_vPosition - watched.m_vClaimedCover).GetLengthSquared() < 0.01f;

        plAiCoverPoint point;
        if (candidate.m_CoverHandle.IsValid() && pTactical->ResolveCover(candidate.m_CoverHandle, point))
        {
          out.m_uiQuality = static_cast<plUInt8>(point.m_Quality.GetValue());
        }
      }
    }
  }

  if (const plAiThreatWorldModule* pThreat = pWorld->GetModuleReadOnly<plAiThreatWorldModule>())
  {
    if (const plSharedPtr<const plAiThreatSnapshot> pSnapshot = pThreat->GetSnapshot(sTeam))
    {
      const plVec3 vPosition = pOwner->GetGlobalPosition();
      watched.m_bHasThreatMap = true;
      watched.m_fThreatHere = pSnapshot->GetThreat(vPosition);
      watched.m_fDangerHere = pSnapshot->GetDanger(vPosition);
      watched.m_bSeenHere = pSnapshot->GetVisibility(vPosition) > 0.5f;
    }
  }

  // the viewport's threat map follows the watched agent's team
  if (!sTeam.IsEmpty() && GetIntCVar("AI.Threat.Visualize", 0) > 0)
  {
    if (plCVar* pCVar = plCVar::FindCVarByName("AI.Threat.VisualizeTeam"); pCVar != nullptr && pCVar->GetType() == plCVarType::String)
    {
      *static_cast<plCVarString*>(pCVar) = sTeam.GetString();
    }
  }
}
