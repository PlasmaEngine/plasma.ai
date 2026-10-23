#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionWorldModule.h>
#include <Core/Utils/Blackboard.h>

// The brain's early decisions (wakes) and the script API that plAiAgentComponent forwards here.

void plAiBrainWorldModule::RequestDecision(plUInt32 uiSlot, plBitflags<plAiWakeReason> reason)
{
  if (uiSlot >= m_Agents.GetCount() || !m_Agents[uiSlot].m_bInUse)
    return;

  Agent& agent = m_Agents[uiSlot];

  if (agent.m_uiWakeReasons == 0)
  {
    m_WokenAgents.PushBack(uiSlot);
  }

  agent.m_uiWakeReasons |= reason.GetValue();

  // never sooner than the spacing allows, never later than already scheduled
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();
  const plTime earliest = plMath::Max(now, agent.m_LastDecision + agent.m_Reactivity.m_MinWakeSpacing);
  agent.m_NextDecision = plMath::Min(agent.m_NextDecision, earliest);
}

void plAiBrainWorldModule::WakeAgentsForExpiredOrders(plTime now)
{
  for (plUInt32 uiSlot : m_ActiveAgents)
  {
    const Agent& agent = m_Agents[uiSlot];

    if (agent.m_uiWakeReasons != 0)
      continue; // already waking

    bool bExpired = agent.m_Command.m_bActive && agent.m_Command.m_Expiry.IsPositive() && now >= agent.m_Command.m_Expiry;

    for (const BehaviorState& behavior : agent.m_Behaviors)
    {
      bExpired |= behavior.m_WeightOverrideEnd.IsPositive() && now >= behavior.m_WeightOverrideEnd;
    }

    if (bExpired)
    {
      RequestDecision(uiSlot, agent.m_Command.m_bActive ? plAiWakeReason::Command : plAiWakeReason::BehaviorSettings);
    }
  }
}

void plAiBrainWorldModule::WakeAgentsForNewStimuli()
{
  if (m_pPerceptionModule == nullptr || m_pPerceptionModule->GetLatestStimulusSequence() == m_uiLastWakeStimulus)
    return;

  const plUInt64 uiLastSeen = m_uiLastWakeStimulus;
  m_uiLastWakeStimulus = m_pPerceptionModule->GetLatestStimulusSequence();

  for (const auto& timedStimulus : m_pPerceptionModule->GetRecentStimuli())
  {
    if (timedStimulus.m_uiSequence <= uiLastSeen)
      continue;

    const plAiStimulus& stimulus = timedStimulus.m_Stimulus;

    for (plUInt32 uiSlot : m_ActiveAgents)
    {
      Agent& agent = m_Agents[uiSlot];

      if (!agent.m_bConfigured || !agent.m_Reactivity.m_bWakeOnStimulus)
        continue;

      const plAiAgentComponent* pComponent = ResolveComponent(agent);
      if (pComponent == nullptr || pComponent->GetOwner()->GetHandle() == stimulus.m_hSource)
        continue;

      // a coarse pre-check; UpdatePerception applies the agent's senses
      if ((stimulus.m_vGlobalPosition - pComponent->GetOwner()->GetGlobalPosition()).GetLengthSquared() > plMath::Square(stimulus.m_fRadius * agent.m_fMaxSenseRangeScale))
        continue;

      if (m_pPerceptionModule->GetAttitude(agent.m_sTeam, stimulus.m_sSourceTeam) == plAiAttitude::Friendly)
        continue;

      RequestDecision(uiSlot, plAiWakeReason::Stimulus);
    }
  }
}

void plAiBrainWorldModule::UpdateCommandAndOverrides(Agent& ref_agent, plTime now)
{
  for (BehaviorState& behavior : ref_agent.m_Behaviors)
  {
    if (behavior.m_WeightOverrideEnd.IsPositive() && now >= behavior.m_WeightOverrideEnd)
    {
      behavior.m_fWeightOverride = 1.0f;
      behavior.m_WeightOverrideEnd = plTime::MakeZero();
    }
  }

  Command& command = ref_agent.m_Command;

  if (command.m_bActive && command.m_Expiry.IsPositive() && now >= command.m_Expiry)
  {
    command.m_bActive = false;
    command.m_bDirty = true;
  }

  if (!command.m_bDirty || ref_agent.m_pBlackboard == nullptr)
    return;

  command.m_bDirty = false;
  ref_agent.m_pBlackboard->SetEntryValue("Ai_CommandActive", command.m_bActive);
  ref_agent.m_pBlackboard->SetEntryValue("Ai_Command", command.m_bActive ? command.m_sName : plHashedString());
  ref_agent.m_pBlackboard->SetEntryValue("Ai_CommandPosition", command.m_vPosition);
  ref_agent.m_pBlackboard->SetEntryValue("Ai_CommandTarget", command.m_bActive ? command.m_hTarget : plGameObjectHandle());
}

plAiBrainWorldModule::BehaviorState* plAiBrainWorldModule::FindBehaviorState(plUInt32 uiSlot, plStringView sBehavior)
{
  if (uiSlot >= m_Agents.GetCount() || !m_Agents[uiSlot].m_bInUse)
    return nullptr;

  for (BehaviorState& behavior : m_Agents[uiSlot].m_Behaviors)
  {
    if (GetBehaviorName(behavior.m_hResource).GetView() == sBehavior)
      return &behavior;
  }

  return nullptr;
}

void plAiBrainWorldModule::SetBehaviorEnabled(plUInt32 uiSlot, plStringView sBehavior, bool bEnabled)
{
  BehaviorState* pBehavior = FindBehaviorState(uiSlot, sBehavior);
  if (pBehavior == nullptr || pBehavior->m_bEnabled == bEnabled)
    return;

  pBehavior->m_bEnabled = bEnabled;
  RequestDecision(uiSlot, plAiWakeReason::BehaviorSettings);
}

void plAiBrainWorldModule::SetBehaviorWeight(plUInt32 uiSlot, plStringView sBehavior, float fScale, plTime duration)
{
  BehaviorState* pBehavior = FindBehaviorState(uiSlot, sBehavior);
  if (pBehavior == nullptr)
    return;

  pBehavior->m_fWeightOverride = plMath::Max(0.0f, fScale);
  pBehavior->m_WeightOverrideEnd = duration.IsPositive() ? GetWorld()->GetClock().GetAccumulatedTime() + duration : plTime::MakeZero();
  RequestDecision(uiSlot, plAiWakeReason::BehaviorSettings);
}

void plAiBrainWorldModule::IssueCommand(plUInt32 uiSlot, plStringView sCommand, const plVec3& vPosition, const plGameObjectHandle& hTarget, plTime expiry)
{
  if (uiSlot >= m_Agents.GetCount() || !m_Agents[uiSlot].m_bInUse)
    return;

  Command& command = m_Agents[uiSlot].m_Command;
  command.m_sName.Assign(sCommand);
  command.m_vPosition = vPosition;
  command.m_hTarget = hTarget;
  command.m_Expiry = expiry.IsPositive() ? GetWorld()->GetClock().GetAccumulatedTime() + expiry : plTime::MakeZero();
  command.m_bActive = true;
  command.m_bDirty = true;

  RequestDecision(uiSlot, plAiWakeReason::Command);
}

void plAiBrainWorldModule::CancelCommand(plUInt32 uiSlot)
{
  if (uiSlot >= m_Agents.GetCount() || !m_Agents[uiSlot].m_bInUse || !m_Agents[uiSlot].m_Command.m_bActive)
    return;

  m_Agents[uiSlot].m_Command.m_bActive = false;
  m_Agents[uiSlot].m_Command.m_bDirty = true;

  RequestDecision(uiSlot, plAiWakeReason::Command);
}

plHashedString plAiBrainWorldModule::GetActiveBehaviorName(plUInt32 uiSlot) const
{
  if (uiSlot >= m_Agents.GetCount() || !m_Agents[uiSlot].m_bInUse || m_Agents[uiSlot].m_iActiveBehavior < 0)
    return plHashedString();

  const Agent& agent = m_Agents[uiSlot];
  return GetBehaviorName(agent.m_Behaviors[agent.m_iActiveBehavior].m_hResource);
}

plGameObjectHandle plAiBrainWorldModule::GetActiveTarget(plUInt32 uiSlot) const
{
  if (uiSlot >= m_Agents.GetCount() || !m_Agents[uiSlot].m_bInUse)
    return plGameObjectHandle();

  const Agent& agent = m_Agents[uiSlot];

  if (agent.m_iActiveTarget < 0 || agent.m_iActiveTarget >= static_cast<plInt32>(agent.m_Targets.GetCount()))
    return plGameObjectHandle();

  return agent.m_Targets[agent.m_iActiveTarget].m_hTarget;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiBrainWorldModuleOrders);
