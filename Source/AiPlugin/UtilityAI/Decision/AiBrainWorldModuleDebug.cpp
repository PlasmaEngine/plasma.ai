#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>
#include <AiPlugin/UtilityAI/Decision/AiUtilityEvaluator.h>
#include <AiPlugin/Utils/AiCVars.h>
#include <Core/Utils/Blackboard.h>
#include <RendererCore/Debug/DebugRenderer.h>

// The brain's debugging data for tools, and its AI.Agents.* debug overlays.

namespace
{
  const char* GetCategoryName(plAiBehaviorCategory::Enum category)
  {
    switch (category)
    {
      case plAiBehaviorCategory::Fallback:
        return "Fallback";
      case plAiBehaviorCategory::Idle:
        return "Idle";
      case plAiBehaviorCategory::ActiveIdle:
        return "ActiveIdle";
      case plAiBehaviorCategory::Investigate:
        return "Investigate";
      case plAiBehaviorCategory::Command:
        return "Command";
      case plAiBehaviorCategory::Combat:
        return "Combat";
      case plAiBehaviorCategory::Interrupt:
        return "Interrupt";
    }

    return "?";
  }
} // namespace

const plAiBrainWorldModule::Agent* plAiBrainWorldModule::FindAgent(const plComponentHandle& hAgent) const
{
  const plAiAgentComponent* pComponent = nullptr;
  if (!GetWorld()->TryGetComponent(hAgent, pComponent) || pComponent->m_uiAgentIndex >= m_Agents.GetCount())
    return nullptr;

  const Agent& agent = m_Agents[pComponent->m_uiAgentIndex];
  return (agent.m_bInUse && agent.m_hComponent == hAgent) ? &agent : nullptr;
}

void plAiBrainWorldModule::SetAgentWatched(const plComponentHandle& hAgent, bool bWatched)
{
  Agent* pAgent = const_cast<Agent*>(FindAgent(hAgent));
  if (pAgent == nullptr || bWatched == (pAgent->m_pDebugSnapshot != nullptr))
    return;

  if (bWatched)
  {
    pAgent->m_pDebugSnapshot = PL_DEFAULT_NEW(plAiAgentDebugSnapshot);
  }
  else
  {
    pAgent->m_pDebugSnapshot.Clear();
  }
}

bool plAiBrainWorldModule::GetAgentDebugSnapshot(const plComponentHandle& hAgent, plAiAgentDebugSnapshot& out_snapshot) const
{
  const Agent* pAgent = FindAgent(hAgent);
  if (pAgent == nullptr || pAgent->m_pDebugSnapshot == nullptr || pAgent->m_pDebugSnapshot->m_uiDecisionCount == 0)
    return false;

  out_snapshot = *pAgent->m_pDebugSnapshot;
  out_snapshot.m_Blackboard.Clear();

  if (pAgent->m_pBlackboard != nullptr)
  {
    for (auto it = pAgent->m_pBlackboard->GetAllEntries().GetIterator(); it.IsValid(); ++it)
    {
      auto& entry = out_snapshot.m_Blackboard.ExpandAndGetRef();
      entry.m_sName = it.Key();
      entry.m_Value = it.Value().m_Value;
    }
  }

  return true;
}

bool plAiBrainWorldModule::GetAgentHistory(const plComponentHandle& hAgent, plDynamicArray<HistoryEntry>& out_history) const
{
  out_history.Clear();

  const Agent* pAgent = FindAgent(hAgent);
  if (pAgent == nullptr)
    return false;

  for (plUInt32 i = 0; i < pAgent->m_History.GetCount(); ++i)
  {
    out_history.PushBack(pAgent->m_History[i]);
  }

  return true;
}

void plAiBrainWorldModule::GetAgentSummaries(plDynamicArray<plAiAgentDebugSummary>& out_agents) const
{
  out_agents.Clear();

  for (plUInt32 uiSlot : m_ActiveAgents)
  {
    const Agent& agent = m_Agents[uiSlot];

    const plAiAgentComponent* pComponent = nullptr;
    if (!GetWorld()->TryGetComponent(agent.m_hComponent, pComponent))
      continue;

    plAiAgentDebugSummary& summary = out_agents.ExpandAndGetRef();
    summary.m_hComponent = agent.m_hComponent;
    summary.m_hOwner = pComponent->GetOwner()->GetHandle();
    summary.m_uiLodTier = static_cast<plUInt8>(agent.m_LodTier);
    summary.m_uiAlertLevel = agent.m_uiAlertLevel;
    summary.m_bLocked = agent.m_bLocked;
    summary.m_bConfigured = agent.m_bConfigured;
    summary.m_bWatched = agent.m_pDebugSnapshot != nullptr;

    if (agent.m_iActiveBehavior >= 0)
    {
      summary.m_sActiveBehavior = GetBehaviorName(agent.m_Behaviors[agent.m_iActiveBehavior].m_hResource);
      summary.m_uiActiveCategory = GetBehaviorCategory(agent.m_Behaviors[agent.m_iActiveBehavior].m_hResource);
    }

    if (agent.m_iActiveTarget >= 0 && agent.m_iActiveTarget < static_cast<plInt32>(agent.m_Targets.GetCount()))
    {
      summary.m_hTarget = agent.m_Targets[agent.m_iActiveTarget].m_hTarget;
    }
  }
}

void plAiBrainWorldModule::DrawAgentDebugInfo(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now)
{
  const plVec3 vOwnerPosition = ref_component.GetOwner()->GetGlobalPosition();
  plStringBuilder sText;

  // active behavior + score above the agent's head
  {
    plStringView sActive = "<none>";
    plHashedString sActiveName;

    if (ref_agent.m_iActiveBehavior >= 0)
    {
      sActiveName = GetBehaviorName(ref_agent.m_Behaviors[ref_agent.m_iActiveBehavior].m_hResource);
      sActive = sActiveName.GetView();
    }

    sText.SetFormat("{} ({})", sActive, plArgF(ref_agent.m_fActiveScore, 2));
    plDebugRenderer::Draw3DText(GetWorld(), sText, vOwnerPosition + plVec3(0, 0, 2.2f), plColor::Cyan);
  }

  if (cvar_AiAgentsShowScores)
  {
    // full per-behavior breakdown with consideration values
    plAiScoringContext context;
    context.m_vOwnerPosition = vOwnerPosition;
    context.m_qOwnerRotation = ref_component.GetOwner()->GetGlobalRotation();
    context.m_Now = now;
    context.m_pBlackboard = ref_agent.m_pBlackboard.Borrow();
    context.m_uiAlertLevel = ref_agent.m_uiAlertLevel;

    float fTextOffset = 2.0f;

    for (plUInt32 uiBehavior = 0; uiBehavior < ref_agent.m_Behaviors.GetCount(); ++uiBehavior)
    {
      auto& behaviorState = ref_agent.m_Behaviors[uiBehavior];
      fTextOffset -= 0.15f;

      plResourceLock<plAiBehaviorResource> pBehavior(behaviorState.m_hResource, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
      if (pBehavior.GetAcquireResult() != plResourceAcquireResult::Final)
      {
        // don't silently skip - a broken/untransformed asset is the #1 reason a behavior 'never runs'
        sText.SetFormat("  {}: (asset not loaded!)", behaviorState.m_hResource.GetResourceID());
        plDebugRenderer::Draw3DText(GetWorld(), sText, vOwnerPosition + plVec3(0, 0, fTextOffset), plColor::Red);
        continue;
      }

      const auto& desc = pBehavior->GetDescriptor();
      const bool bIsActive = ref_agent.m_iActiveBehavior == static_cast<plInt32>(uiBehavior);

      context.m_TimeSinceActivation = bIsActive ? now - behaviorState.m_LastActivation : plTime::MakeZero();
      context.m_TimeSinceDeactivation = now - behaviorState.m_LastDeactivation;
      context.m_pTarget = nullptr;

      if (desc.NeedsTarget())
      {
        if (ref_agent.m_iActiveTarget >= 0 && bIsActive)
        {
          context.m_pTarget = &ref_agent.m_Targets[ref_agent.m_iActiveTarget];
        }
        else if (!ref_agent.m_Targets.IsEmpty())
        {
          context.m_pTarget = &ref_agent.m_Targets[0];
        }
      }

      plHybridArray<plAiConsiderationTrace, 8> considerations;
      const float fUtility = plAiUtilityEvaluator::ScoreUtility(desc, behaviorState.m_fWeightScale * behaviorState.m_fWeightOverride, context, &considerations, ref_agent.m_Reactivity.m_uiScoringVersion);

      const plAiBehaviorCategory::Enum category = static_cast<plAiBehaviorCategory::Enum>(desc.m_Category.GetValue());
      const float fFinalScore = (fUtility > 0.0f) ? plAiBehaviorScore::Compose(category, fUtility) : 0.0f;

      // show the COMPOSED score (category + utility) - that is what the arbitration actually compares
      sText.SetFormat("{}{}: {} ({} + {})", bIsActive ? "> " : "  ", GetBehaviorName(behaviorState.m_hResource),
        plArgF(fFinalScore, 2), GetCategoryName(category), plArgF(fUtility, 2));

      for (const plAiConsiderationTrace& consideration : considerations)
      {
        sText.AppendFormat(" [{}]", plArgF(consideration.m_fValue, 2));
      }

      if (desc.NeedsTarget() && context.m_pTarget == nullptr)
      {
        sText.Append(" (no target)");
      }

      plDebugRenderer::Draw3DText(GetWorld(), sText, vOwnerPosition + plVec3(0, 0, fTextOffset), bIsActive ? plColor::LawnGreen : plColor::LightGray);
    }
  }

  if (cvar_AiAgentsShowPerception)
  {
    for (const auto& target : ref_agent.m_Targets)
    {
      sText.SetFormat("[{}]", plArgF(target.m_fConfidence, 2));
      const plColor color = plMath::Lerp(plColor::DarkRed, plColor::Yellow, target.m_fConfidence);
      plDebugRenderer::Draw3DText(GetWorld(), sText, target.m_vLastKnownPosition + plVec3(0, 0, 0.5f), color);

      plDebugRenderer::Line line(vOwnerPosition, target.m_vLastKnownPosition);
      plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(&line, 1), color);
    }
  }

  if (cvar_AiAgentsShowHistory)
  {
    plStringBuilder sHistory;

    for (plUInt32 i = ref_agent.m_History.GetCount(); i-- > 0;)
    {
      const auto& entry = ref_agent.m_History[i];
      sHistory.AppendFormat("{}: {} -> {} ({} vs {})\n", plArgF((now - entry.m_Time).GetSeconds(), 1), entry.m_sFrom, entry.m_sTo,
        plArgF(entry.m_fWinnerScore, 2), plArgF(entry.m_fRunnerUpScore, 2));
    }

    plDebugRenderer::DrawInfoText(GetWorld(), plDebugTextPlacement::TopRight, "AiHistory", sHistory, plColor::White);
  }
}

void plAiBrainWorldModule::UpdateDebug(const UpdateContext& context)
{
  if (!cvar_AiAgentsShowScores && !cvar_AiAgentsShowPerception && !cvar_AiAgentsShowHistory)
  {
    // still honor per-component debug flags
    bool bAnyDebug = false;

    for (plUInt32 uiSlot : m_ActiveAgents)
    {
      plAiAgentComponent* pComponent = ResolveComponent(m_Agents[uiSlot]);
      if (pComponent != nullptr && pComponent->m_bDebugInfo)
      {
        bAnyDebug = true;
        break;
      }
    }

    if (!bAnyDebug)
      return;
  }

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();
  const plStringView sFilter = cvar_AiAgentsDebugFilter.GetValue().GetView();

  for (plUInt32 uiSlot : m_ActiveAgents)
  {
    Agent& agent = m_Agents[uiSlot];

    plAiAgentComponent* pComponent = ResolveComponent(agent);
    if (pComponent == nullptr)
      continue;

    const bool bCVarDebug = cvar_AiAgentsShowScores || cvar_AiAgentsShowPerception || cvar_AiAgentsShowHistory;

    if (!pComponent->m_bDebugInfo)
    {
      if (!bCVarDebug)
        continue;

      if (!sFilter.IsEmpty() && pComponent->GetOwner()->GetName().FindSubString_NoCase(sFilter) == nullptr)
        continue;

      if (cvar_AiAgentsDebugWatchedOnly && agent.m_pDebugSnapshot == nullptr)
        continue;
    }

    DrawAgentDebugInfo(agent, *pComponent, now);
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiBrainWorldModuleDebug);
