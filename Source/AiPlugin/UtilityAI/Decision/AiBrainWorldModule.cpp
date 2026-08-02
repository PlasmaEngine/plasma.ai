#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiBehaviorLogic.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>
#include <AiPlugin/UtilityAI/Decision/AiUtilityEvaluator.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionWorldModule.h>
#include <AiPlugin/Utils/AiCVars.h>
#include <Core/Utils/Blackboard.h>
#include <Foundation/Serialization/ReflectionSerializer.h>
#include <GameEngine/AI/SensorComponent.h>
#include <GameEngine/Gameplay/BlackboardComponent.h>
#include <RendererCore/Debug/DebugRenderer.h>

// clang-format off
PL_IMPLEMENT_WORLD_MODULE(plAiBrainWorldModule);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiBrainWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

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

  void DestroyLogicClone(plAiBehaviorLogic*& ref_pLogic)
  {
    if (ref_pLogic != nullptr)
    {
      ref_pLogic->GetDynamicRTTI()->GetAllocator()->Deallocate(ref_pLogic);
      ref_pLogic = nullptr;
    }
  }

  plHashedString GetBehaviorName(const plAiBehaviorResourceHandle& hBehavior)
  {
    plHashedString sName;

    if (hBehavior.IsValid())
    {
      plResourceLock<plAiBehaviorResource> pBehavior(hBehavior, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
      if (pBehavior.GetAcquireResult() == plResourceAcquireResult::Final && !pBehavior->GetDescriptor().m_sName.IsEmpty())
      {
        return pBehavior->GetDescriptor().m_sName;
      }

      sName.Assign(hBehavior.GetResourceID());
    }

    return sName;
  }
} // namespace

plAiBrainWorldModule::plAiBrainWorldModule(plWorld* pWorld)
  : plWorldModule(pWorld)
{
}

plAiBrainWorldModule::~plAiBrainWorldModule() = default;

plAiBrainWorldModule::Agent::~Agent()
{
  ValidateAgent(*this);

  // poison, so a double-destruction trips the validation instead of crashing in member destructors
  m_uiCanaryFront = 0xDDDDDDDD;
  m_uiCanaryQueue = 0xDDDDDDDD;
}

void plAiBrainWorldModule::ValidateAgent(const Agent& agent)
{
  PL_ASSERT_ALWAYS(agent.m_uiCanaryFront == 0xA1C0FFEE,
    "AI agent memory corrupted (front canary = {0}). If it reads 0xDDDDDDDD the agent was destructed twice, otherwise something wrote past the start of the agent.",
    plArgU(agent.m_uiCanaryFront, 8, true, 16));

  PL_ASSERT_ALWAYS(agent.m_uiCanaryQueue == 0xBEEFCAFE,
    "AI agent memory corrupted (queue canary = {0}). Something overwrote the memory in front of the action queue (perceived-targets array overflow?).",
    plArgU(agent.m_uiCanaryQueue, 8, true, 16));
}

void plAiBrainWorldModule::Initialize()
{
  SUPER::Initialize();

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiBrainWorldModule::UpdateGather, this);
    updateDesc.m_Phase = plWorldUpdatePhase::PreAsync;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;
    RegisterUpdateFunction(updateDesc);
  }

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiBrainWorldModule::UpdateScore, this);
    updateDesc.m_Phase = plWorldUpdatePhase::Async;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;
    RegisterUpdateFunction(updateDesc);
  }

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiBrainWorldModule::UpdateApply, this);
    updateDesc.m_Phase = plWorldUpdatePhase::PostAsync;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;
    RegisterUpdateFunction(updateDesc);
  }

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiBrainWorldModule::UpdateDebug, this);
    updateDesc.m_Phase = plWorldUpdatePhase::PostTransform;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;
    RegisterUpdateFunction(updateDesc);
  }
}

void plAiBrainWorldModule::RegisterLodCenter(const plComponentHandle& hComponent)
{
  m_LodCenters.PushBack(hComponent);
}

void plAiBrainWorldModule::UnregisterLodCenter(const plComponentHandle& hComponent)
{
  m_LodCenters.RemoveAndSwap(hComponent);
}

plAiBrainWorldModule::LodTier plAiBrainWorldModule::DetermineLodTier(const plVec3& vAgentPosition) const
{
  if (m_LodCenterPositions.IsEmpty())
    return LodTier::Hot;

  float fMinDistSquared = plMath::HighValue<float>();

  for (const plVec3& vCenter : m_LodCenterPositions)
  {
    fMinDistSquared = plMath::Min(fMinDistSquared, (vCenter - vAgentPosition).GetLengthSquared());
  }

  if (fMinDistSquared < plMath::Square(cvar_AiAgentsLodHotDistance.GetValue()))
    return LodTier::Hot;

  if (fMinDistSquared < plMath::Square(cvar_AiAgentsLodWarmDistance.GetValue()))
    return LodTier::Warm;

  return LodTier::Cold;
}

plTime plAiBrainWorldModule::GetDecisionInterval(LodTier tier) const
{
  float fHz = cvar_AiAgentsDecisionHz.GetValue();

  switch (tier)
  {
    case LodTier::Warm:
      fHz = cvar_AiAgentsLodWarmHz.GetValue();
      break;
    case LodTier::Cold:
      fHz = cvar_AiAgentsLodColdHz.GetValue();
      break;
    default:
      break;
  }

  return plTime::MakeFromSeconds(1.0 / plMath::Max(0.1f, fHz));
}

void plAiBrainWorldModule::Deinitialize()
{
  for (plUInt32 uiSlot : m_ActiveAgents)
  {
    ShutdownAgent(m_Agents[uiSlot]);
  }

  // validate all slots before destruction, so corruption is reported with context
  // instead of crashing inside member destructors
  for (plUInt32 uiSlot = 0; uiSlot < m_Agents.GetCount(); ++uiSlot)
  {
    ValidateAgent(m_Agents[uiSlot]);
  }

  m_ActiveAgents.Clear();
  m_FreeAgentSlots.Clear();
  m_DueAgents.Clear();
  m_Agents.Clear();

  SUPER::Deinitialize();
}

plUInt32 plAiBrainWorldModule::RegisterAgent(plAiAgentComponent* pComponent)
{
  if (m_pPerceptionModule == nullptr)
  {
    m_pPerceptionModule = GetWorld()->GetOrCreateModule<plAiPerceptionWorldModule>();
  }

  plUInt32 uiSlot = plInvalidIndex;

  if (!m_FreeAgentSlots.IsEmpty())
  {
    uiSlot = m_FreeAgentSlots.PeekBack();
    m_FreeAgentSlots.PopBack();
  }
  else
  {
    uiSlot = m_Agents.GetCount();
    m_Agents.ExpandAndGetRef();
  }

  Agent& agent = m_Agents[uiSlot];
  ResetAgent(agent);
  agent.m_bInUse = true;
  agent.m_hComponent = pComponent->GetHandle();

  // stagger initial decisions so a scene full of agents doesn't decide in the same frame
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();
  agent.m_NextDecision = now + plTime::MakeFromSeconds(0.01 * (uiSlot % 16));

  m_ActiveAgents.PushBack(uiSlot);
  return uiSlot;
}

void plAiBrainWorldModule::UnregisterAgent(plUInt32 uiAgentIndex)
{
  if (uiAgentIndex >= m_Agents.GetCount() || !m_Agents[uiAgentIndex].m_bInUse)
    return;

  ShutdownAgent(m_Agents[uiAgentIndex]);

  m_ActiveAgents.RemoveAndSwap(uiAgentIndex);
  m_DueAgents.RemoveAndSwap(uiAgentIndex); // slot may be pending a decision this frame
  m_FreeAgentSlots.PushBack(uiAgentIndex);
}

void plAiBrainWorldModule::ShutdownAgent(Agent& ref_agent)
{
  ValidateAgent(ref_agent);

  if (ref_agent.m_pActiveLogic != nullptr)
  {
    // the owner object may already be gone, so don't run Deactivate logic here
    DestroyLogicClone(ref_agent.m_pActiveLogic);
  }

  ref_agent.m_ActionQueue.InterruptAndClear();
  ref_agent.m_pBlackboard = nullptr;
  ref_agent.m_bInUse = false;
  ref_agent.m_bConfigured = false;
  ref_agent.m_iActiveBehavior = -1;
}

void plAiBrainWorldModule::ResetAgent(Agent& ref_agent)
{
  ValidateAgent(ref_agent);

  ref_agent.m_bInUse = false;
  ref_agent.m_bConfigured = false;
  ref_agent.m_bLocked = false;
  ref_agent.m_hComponent.Invalidate();

  ref_agent.m_hArchetype.Invalidate();
  ref_agent.m_uiArchetypeChangeCounter = 0;

  ref_agent.m_pBlackboard = nullptr;
  ref_agent.m_sTeam = plHashedString();
  ref_agent.m_Behaviors.Clear();

  ref_agent.m_iActiveBehavior = -1;
  DestroyLogicClone(ref_agent.m_pActiveLogic);
  ref_agent.m_iActiveTarget = -1;
  ref_agent.m_fActiveScore = 0.0f;
  ref_agent.m_fActiveCommitBonus = 0.0f;

  ref_agent.m_Targets.Clear();
  ref_agent.m_ActionQueue.InterruptAndClear();

  ref_agent.m_LodTier = LodTier::Hot;
  ref_agent.m_NextDecision = plTime::MakeZero();
  ref_agent.m_LastExecution = plTime::MakeZero();
  ref_agent.m_LastStimulusConsumeTime = plTime::MakeZero();
  ref_agent.m_History.Clear();

  ref_agent.m_vSnapshotPosition = plVec3::MakeZero();
  ref_agent.m_qSnapshotRotation = plQuat::MakeIdentity();
  ref_agent.m_BlackboardSnapshot.Clear();
  ref_agent.m_ScoringDescriptors.Clear();
  ref_agent.m_BehaviorTimings.Clear();
  ref_agent.m_Decision = Decision();
}

plAiAgentComponent* plAiBrainWorldModule::ResolveComponent(Agent& ref_agent)
{
  plAiAgentComponent* pComponent = nullptr;
  if (GetWorld()->TryGetComponent(ref_agent.m_hComponent, pComponent) && pComponent->IsActiveAndSimulating())
  {
    return pComponent;
  }

  return nullptr;
}

void plAiBrainWorldModule::ConfigureAgent(Agent& ref_agent, plAiAgentComponent& ref_component)
{
  ref_agent.m_bConfigured = false;

  if (!ref_component.m_hArchetype.IsValid())
    return;

  plResourceLock<plAiArchetypeResource> pArchetype(ref_component.m_hArchetype, plResourceAcquireMode::BlockTillLoaded_NeverFail);
  if (pArchetype.GetAcquireResult() != plResourceAcquireResult::Final)
    return;

  const plAiArchetypeResourceDescriptor& desc = pArchetype->GetDescriptor();

  ref_agent.m_hArchetype = ref_component.m_hArchetype;
  ref_agent.m_uiArchetypeChangeCounter = pArchetype->GetCurrentResourceChangeCounter();

  // blackboard: prefer one provided by a blackboard component on the owner, otherwise create a private one
  ref_agent.m_pBlackboard = plBlackboardComponent::FindBlackboard(ref_component.GetOwner());

  if (ref_agent.m_pBlackboard == nullptr)
  {
    ref_agent.m_pBlackboard = plBlackboard::Create();
    ref_agent.m_pBlackboard->SetName(ref_component.GetOwner()->GetName());
  }

  if (desc.m_hBlackboardTemplate.IsValid())
  {
    plResourceLock<plBlackboardTemplateResource> pTemplate(desc.m_hBlackboardTemplate, plResourceAcquireMode::BlockTillLoaded_NeverFail);
    if (pTemplate.GetAcquireResult() == plResourceAcquireResult::Final)
    {
      for (const auto& entry : pTemplate->GetDescriptor().m_Entries)
      {
        ref_agent.m_pBlackboard->SetEntryValue(entry.m_sName, entry.m_InitialValue);
        ref_agent.m_pBlackboard->SetEntryFlags(entry.m_sName, entry.m_Flags).IgnoreResult();
      }
    }
  }

  // entries the AI framework itself communicates through
  ref_agent.m_pBlackboard->SetEntryValue("Ai_TargetPosition", plVec3::MakeZero());
  ref_agent.m_pBlackboard->SetEntryValue("Ai_TargetConfidence", 0.0f);
  ref_agent.m_pBlackboard->SetEntryValue("Ai_BehaviorDone", false);
  ref_agent.m_pBlackboard->SetEntryValue("Ai_BehaviorFailed", false);
  ref_agent.m_pBlackboard->SetEntryValue("Ai_LockBehavior", false);

  ref_agent.m_sTeam = ref_component.GetResolvedTeam();

  ref_agent.m_Behaviors.Clear();
  ref_agent.m_Behaviors.Reserve(desc.m_Behaviors.GetCount());

  for (const auto& entry : desc.m_Behaviors)
  {
    if (!entry.m_hBehavior.IsValid())
      continue;

    auto& behaviorState = ref_agent.m_Behaviors.ExpandAndGetRef();
    behaviorState.m_hResource = entry.m_hBehavior;
    behaviorState.m_fWeightScale = entry.m_fWeightScale;
  }

  ref_agent.m_iActiveBehavior = -1;
  ref_agent.m_fActiveScore = 0.0f;
  ref_agent.m_bConfigured = true;
}

void plAiBrainWorldModule::UpdatePerception(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now, plTime tDiff)
{
  float fGainPerSecond = 4.0f;
  float fDecayPerSecond = 0.5f;
  plTime memoryDuration = plTime::MakeFromSeconds(10.0);
  plString sSensorObjectName;

  {
    plResourceLock<plAiArchetypeResource> pArchetype(ref_agent.m_hArchetype, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
    if (pArchetype.GetAcquireResult() == plResourceAcquireResult::Final)
    {
      const auto& desc = pArchetype->GetDescriptor();
      fGainPerSecond = desc.m_fConfidenceGainPerSecond;
      fDecayPerSecond = desc.m_fConfidenceDecayPerSecond;
      memoryDuration = desc.m_TargetMemoryDuration;
      sSensorObjectName = desc.m_sSensorObjectName;
    }
  }

  const float fDecay = fDecayPerSecond * static_cast<float>(tDiff.GetSeconds());
  const float fGain = fGainPerSecond * static_cast<float>(tDiff.GetSeconds());

  // decay all current records
  for (auto& target : ref_agent.m_Targets)
  {
    target.m_fConfidence = plMath::Max(0.0f, target.m_fConfidence - fDecay);
  }

  // gather sensor detections (interim Phase A1 sight path; replaced by the perception world module in Phase A2)
  plGameObject* pSensorRoot = ref_component.GetOwner();

  if (!sSensorObjectName.IsEmpty())
  {
    plGameObject* pNamedChild = ref_component.GetOwner()->FindChildByName(plTempHashedString(sSensorObjectName.GetData()), true);
    if (pNamedChild != nullptr)
    {
      pSensorRoot = pNamedChild;
    }
  }

  plHybridArray<plSensorComponent*, 4> sensors;

  auto GatherSensors = [&sensors](plGameObject* pObject) {
    plSensorComponent* pSensor = nullptr;
    if (pObject->TryGetComponentOfBaseType(pSensor))
    {
      sensors.PushBack(pSensor);
    }
  };

  GatherSensors(pSensorRoot);

  if (sSensorObjectName.IsEmpty())
  {
    for (auto it = pSensorRoot->GetChildren(); it.IsValid(); ++it)
    {
      GatherSensors(&(*it));
    }
  }

  auto GetTargetTeam = [this](const plGameObject* pObject) -> plHashedString {
    const plAiAgentComponent* pOtherAgent = nullptr;
    if (pObject->TryGetComponentOfBaseType(pOtherAgent))
    {
      return pOtherAgent->GetResolvedTeam();
    }

    return plHashedString();
  };

  auto GetOrCreateRecord = [&ref_agent](const plGameObjectHandle& hTarget) -> plAiPerceivedTarget& {
    for (auto& target : ref_agent.m_Targets)
    {
      if (target.m_hTarget == hTarget)
        return target;
    }

    auto& record = ref_agent.m_Targets.ExpandAndGetRef();
    record.m_hTarget = hTarget;
    record.m_fConfidence = 0.0f;
    return record;
  };

  for (plSensorComponent* pSensor : sensors)
  {
    for (const plGameObjectHandle& hDetected : pSensor->GetLastDetectedObjects())
    {
      const plGameObject* pDetected = nullptr;
      if (!GetWorld()->TryGetObject(hDetected, pDetected))
        continue;

      if (m_pPerceptionModule != nullptr &&
          m_pPerceptionModule->GetAttitude(ref_agent.m_sTeam, GetTargetTeam(pDetected)) == plAiAttitude::Friendly)
        continue;

      plAiPerceivedTarget& record = GetOrCreateRecord(hDetected);
      record.m_vLastKnownPosition = pDetected->GetGlobalPosition();
      record.m_vLastKnownVelocity = pDetected->GetLinearVelocity();
      record.m_fConfidence = plMath::Min(1.0f, record.m_fConfidence + fDecay + fGain);
      record.m_LastStimulusTime = now;
    }
  }

  // broadcast stimuli (sounds, damage, custom events) posted since this agent's last decision
  if (m_pPerceptionModule != nullptr)
  {
    const plVec3 vAgentPosition = ref_component.GetOwner()->GetGlobalPosition();
    const plTime lastConsume = ref_agent.m_LastStimulusConsumeTime;
    ref_agent.m_LastStimulusConsumeTime = now;

    for (const auto& timedStimulus : m_pPerceptionModule->GetRecentStimuli())
    {
      if (timedStimulus.m_Timestamp <= lastConsume)
        continue; // already processed in a previous decision

      const plAiStimulus& stimulus = timedStimulus.m_Stimulus;

      if (stimulus.m_hSource == ref_component.GetOwner()->GetHandle())
        continue; // don't perceive yourself

      if ((stimulus.m_vGlobalPosition - vAgentPosition).GetLengthSquared() > plMath::Square(stimulus.m_fRadius))
        continue;

      if (m_pPerceptionModule->GetAttitude(ref_agent.m_sTeam, stimulus.m_sSourceTeam) == plAiAttitude::Friendly)
        continue;

      plVec3 vPerceivedPosition = stimulus.m_vGlobalPosition;
      plVec3 vPerceivedVelocity = plVec3::MakeZero();

      const plGameObject* pSource = nullptr;
      if (!stimulus.m_hSource.IsInvalidated() && GetWorld()->TryGetObject(stimulus.m_hSource, pSource))
      {
        vPerceivedPosition = pSource->GetGlobalPosition();
        vPerceivedVelocity = pSource->GetLinearVelocity();
      }

      plAiPerceivedTarget& record = GetOrCreateRecord(stimulus.m_hSource);
      record.m_vLastKnownPosition = vPerceivedPosition;
      record.m_vLastKnownVelocity = vPerceivedVelocity;
      record.m_fConfidence = plMath::Clamp(plMath::Max(record.m_fConfidence, stimulus.m_fStrength), 0.0f, 1.0f);
      record.m_LastStimulusTime = now;
    }
  }

  // forget stale records
  for (plUInt32 i = ref_agent.m_Targets.GetCount(); i-- > 0;)
  {
    const auto& target = ref_agent.m_Targets[i];

    if (target.m_fConfidence <= 0.0f && (now - target.m_LastStimulusTime) > memoryDuration)
    {
      // keep the active behavior's target index stable
      if (ref_agent.m_iActiveTarget == static_cast<plInt32>(i))
      {
        ref_agent.m_iActiveTarget = -1;
      }
      else if (ref_agent.m_iActiveTarget > static_cast<plInt32>(i))
      {
        --ref_agent.m_iActiveTarget;
      }

      ref_agent.m_Targets.RemoveAtAndCopy(i);
    }
  }
}

void plAiBrainWorldModule::BuildScoringSnapshot(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now)
{
  ref_agent.m_vSnapshotPosition = ref_component.GetOwner()->GetGlobalPosition();
  ref_agent.m_qSnapshotRotation = ref_component.GetOwner()->GetGlobalRotation();

  ref_agent.m_ScoringDescriptors.Clear();
  ref_agent.m_BehaviorTimings.Clear();
  ref_agent.m_BlackboardSnapshot.Clear();

  plHybridArray<plHashedString, 16> blackboardEntries;

  for (plUInt32 uiBehavior = 0; uiBehavior < ref_agent.m_Behaviors.GetCount(); ++uiBehavior)
  {
    auto& behaviorState = ref_agent.m_Behaviors[uiBehavior];

    const plAiBehaviorResourceDescriptor* pDescriptor = nullptr;

    plResourceLock<plAiBehaviorResource> pBehavior(behaviorState.m_hResource, plResourceAcquireMode::BlockTillLoaded_NeverFail);
    if (pBehavior.GetAcquireResult() == plResourceAcquireResult::Final)
    {
      // hot reload: if the behavior's asset changed, restart it (also picks up new descriptor data)
      if (pBehavior->GetCurrentResourceChangeCounter() != behaviorState.m_uiResourceChangeCounter)
      {
        behaviorState.m_uiResourceChangeCounter = pBehavior->GetCurrentResourceChangeCounter();

        if (ref_agent.m_iActiveBehavior == static_cast<plInt32>(uiBehavior))
        {
          DeactivateBehavior(ref_agent, ref_component, true);
        }
      }

      // the resource handle in behaviorState keeps the resource (and thus the descriptor) alive;
      // resource reloads don't happen during the world update, so the pointer is stable for this frame
      pDescriptor = &pBehavior->GetDescriptor();

      pDescriptor->CollectBlackboardEntries(blackboardEntries);
    }

    ref_agent.m_ScoringDescriptors.PushBack(pDescriptor);

    const bool bIsActive = ref_agent.m_iActiveBehavior == static_cast<plInt32>(uiBehavior);
    auto& timing = ref_agent.m_BehaviorTimings.ExpandAndGetRef();
    timing.m_SinceActivation = bIsActive ? now - behaviorState.m_LastActivation : plTime::MakeZero();
    timing.m_SinceDeactivation = now - behaviorState.m_LastDeactivation;
  }

  if (ref_agent.m_pBlackboard != nullptr)
  {
    for (const plHashedString& sEntry : blackboardEntries)
    {
      ref_agent.m_BlackboardSnapshot[sEntry] = ref_agent.m_pBlackboard->GetEntryValue(sEntry);
    }
  }
}

void plAiBrainWorldModule::ScoreAgent(Agent& ref_agent, plTime now)
{
  ValidateAgent(ref_agent);

  plAiScoringContext context;
  context.m_vOwnerPosition = ref_agent.m_vSnapshotPosition;
  context.m_qOwnerRotation = ref_agent.m_qSnapshotRotation;
  context.m_Now = now;
  context.m_pBlackboard = nullptr;
  context.m_pBlackboardSnapshot = &ref_agent.m_BlackboardSnapshot;

  Decision decision;

  for (plUInt32 uiBehavior = 0; uiBehavior < ref_agent.m_ScoringDescriptors.GetCount(); ++uiBehavior)
  {
    const plAiBehaviorResourceDescriptor* pDescriptor = ref_agent.m_ScoringDescriptors[uiBehavior];

    if (pDescriptor == nullptr)
      continue;

    const auto& timing = ref_agent.m_BehaviorTimings[uiBehavior];
    const float fWeightScale = ref_agent.m_Behaviors[uiBehavior].m_fWeightScale;
    const bool bIsActive = ref_agent.m_iActiveBehavior == static_cast<plInt32>(uiBehavior);

    context.m_TimeSinceActivation = timing.m_SinceActivation;
    context.m_TimeSinceDeactivation = timing.m_SinceDeactivation;

    auto ScoreAndTrack = [&](const plAiPerceivedTarget* pTarget, plInt32 iTargetIndex) {
      context.m_pTarget = pTarget;

      const float fUtility = plAiUtilityEvaluator::ScoreUtility(*pDescriptor, fWeightScale, context);

      if (fUtility <= 0.0f)
        return;

      const float fFinalScore = plAiBehaviorScore::Compose(static_cast<plAiBehaviorCategory::Enum>(pDescriptor->m_Category.GetValue()), fUtility);

      if (bIsActive && iTargetIndex == ref_agent.m_iActiveTarget)
      {
        decision.m_fActiveScore = fFinalScore;
      }

      if (fFinalScore > decision.m_fScore)
      {
        decision.m_fRunnerUpScore = decision.m_fScore;
        decision.m_fScore = fFinalScore;
        decision.m_iBehavior = static_cast<plInt32>(uiBehavior);
        decision.m_iTarget = iTargetIndex;
      }
      else if (fFinalScore > decision.m_fRunnerUpScore)
      {
        decision.m_fRunnerUpScore = fFinalScore;
      }
    };

    if (pDescriptor->NeedsTarget())
    {
      for (plUInt32 uiTarget = 0; uiTarget < ref_agent.m_Targets.GetCount(); ++uiTarget)
      {
        ScoreAndTrack(&ref_agent.m_Targets[uiTarget], static_cast<plInt32>(uiTarget));
      }
    }
    else
    {
      ScoreAndTrack(nullptr, -1);
    }
  }

  ref_agent.m_Decision = decision;
}

void plAiBrainWorldModule::ApplyDecision(Agent& ref_agent, plAiAgentComponent& ref_component)
{
  const Decision& decision = ref_agent.m_Decision;

  if (ref_agent.m_bLocked)
    return; // the active behavior does not allow switching right now

  if (decision.m_iBehavior < 0)
  {
    if (ref_agent.m_iActiveBehavior >= 0 && decision.m_fActiveScore <= 0.0f)
    {
      // nothing scores anymore, including the active behavior
      DeactivateBehavior(ref_agent, ref_component, true);
    }

    return;
  }

  if (ref_agent.m_iActiveBehavior < 0)
  {
    SwitchBehavior(ref_agent, ref_component, decision.m_iBehavior, decision.m_iTarget, decision.m_fScore, decision.m_fRunnerUpScore);
    return;
  }

  if (decision.m_iBehavior == ref_agent.m_iActiveBehavior)
  {
    ref_agent.m_fActiveScore = decision.m_fScore;

    if (decision.m_iTarget != ref_agent.m_iActiveTarget)
    {
      // same behavior, better target -> reactivate
      ref_agent.m_iActiveTarget = decision.m_iTarget;
      PublishTargetToBlackboard(ref_agent);

      if (ref_agent.m_pActiveLogic != nullptr)
      {
        plAiAgentContext agentContext;
        agentContext.m_pOwner = ref_component.GetOwner();
        agentContext.m_pAgentComponent = &ref_component;
        agentContext.m_pWorld = GetWorld();
        agentContext.m_pBlackboard = ref_agent.m_pBlackboard;
        agentContext.m_pTarget = (decision.m_iTarget >= 0) ? &ref_agent.m_Targets[decision.m_iTarget] : nullptr;
        agentContext.m_pActionQueue = &ref_agent.m_ActionQueue;
        agentContext.m_TimeStep = plTime::MakeZero();

        ref_agent.m_pActiveLogic->Reactivate(agentContext);
      }
    }

    return;
  }

  // the commit bonus of the ACTIVE behavior decides how sticky it is
  if (decision.m_fScore > decision.m_fActiveScore + ref_agent.m_fActiveCommitBonus)
  {
    SwitchBehavior(ref_agent, ref_component, decision.m_iBehavior, decision.m_iTarget, decision.m_fScore,
      plMath::Max(decision.m_fRunnerUpScore, decision.m_fActiveScore));
  }
  else
  {
    ref_agent.m_fActiveScore = decision.m_fActiveScore;
  }
}

void plAiBrainWorldModule::PublishTargetToBlackboard(Agent& ref_agent)
{
  if (ref_agent.m_pBlackboard == nullptr)
    return;

  if (ref_agent.m_iActiveTarget >= 0 && ref_agent.m_iActiveTarget < static_cast<plInt32>(ref_agent.m_Targets.GetCount()))
  {
    const auto& target = ref_agent.m_Targets[ref_agent.m_iActiveTarget];
    ref_agent.m_pBlackboard->SetEntryValue("Ai_TargetPosition", target.m_vLastKnownPosition);
    ref_agent.m_pBlackboard->SetEntryValue("Ai_TargetConfidence", target.m_fConfidence);
    ref_agent.m_pBlackboard->SetEntryValue("Ai_TargetObject", target.m_hTarget);
  }
  else
  {
    ref_agent.m_pBlackboard->SetEntryValue("Ai_TargetConfidence", 0.0f);
    ref_agent.m_pBlackboard->SetEntryValue("Ai_TargetObject", plGameObjectHandle());
  }
}

void plAiBrainWorldModule::SwitchBehavior(Agent& ref_agent, plAiAgentComponent& ref_component, plInt32 iNewBehavior, plInt32 iNewTarget, float fScore, float fRunnerUpScore)
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  plHashedString sFrom;
  if (ref_agent.m_iActiveBehavior >= 0)
  {
    sFrom = GetBehaviorName(ref_agent.m_Behaviors[ref_agent.m_iActiveBehavior].m_hResource);
  }

  DeactivateBehavior(ref_agent, ref_component, true);

  if (iNewBehavior < 0 || iNewBehavior >= static_cast<plInt32>(ref_agent.m_Behaviors.GetCount()))
    return;

  auto& behaviorState = ref_agent.m_Behaviors[iNewBehavior];

  plResourceLock<plAiBehaviorResource> pBehavior(behaviorState.m_hResource, plResourceAcquireMode::BlockTillLoaded_NeverFail);
  if (pBehavior.GetAcquireResult() != plResourceAcquireResult::Final)
    return;

  const plAiBehaviorResourceDescriptor& desc = pBehavior->GetDescriptor();

  if (desc.m_pLogic == nullptr)
  {
    plLog::Error("AI behavior '{}' has no execution logic configured.", desc.m_sName);
    return;
  }

  ref_agent.m_iActiveBehavior = iNewBehavior;
  ref_agent.m_iActiveTarget = iNewTarget;
  ref_agent.m_fActiveScore = fScore;
  ref_agent.m_fActiveCommitBonus = desc.m_fCommitBonus;
  ref_agent.m_bLocked = false;
  behaviorState.m_LastActivation = now;
  behaviorState.m_uiResourceChangeCounter = pBehavior->GetCurrentResourceChangeCounter();

  PublishTargetToBlackboard(ref_agent);

  ref_agent.m_pActiveLogic = plReflectionSerializer::Clone(desc.m_pLogic.Borrow());

  plAiAgentContext context;
  context.m_pOwner = ref_component.GetOwner();
  context.m_pAgentComponent = &ref_component;
  context.m_pWorld = GetWorld();
  context.m_pBlackboard = ref_agent.m_pBlackboard;
  context.m_pTarget = (iNewTarget >= 0) ? &ref_agent.m_Targets[iNewTarget] : nullptr;
  context.m_pActionQueue = &ref_agent.m_ActionQueue;
  context.m_TimeStep = plTime::MakeZero();

  ref_agent.m_pActiveLogic->Activate(context);

  // record history
  {
    HistoryEntry entry;
    entry.m_Time = now;
    entry.m_sFrom = sFrom;
    entry.m_sTo = GetBehaviorName(behaviorState.m_hResource);
    entry.m_fWinnerScore = fScore;
    entry.m_fRunnerUpScore = fRunnerUpScore;

    if (!ref_agent.m_History.CanAppend())
    {
      ref_agent.m_History.PopFront();
    }

    ref_agent.m_History.PushBack(entry);
  }
}

void plAiBrainWorldModule::DeactivateBehavior(Agent& ref_agent, plAiAgentComponent& ref_component, bool bInterrupted)
{
  if (ref_agent.m_iActiveBehavior < 0)
    return;

  auto& behaviorState = ref_agent.m_Behaviors[ref_agent.m_iActiveBehavior];
  behaviorState.m_LastDeactivation = GetWorld()->GetClock().GetAccumulatedTime();

  if (ref_agent.m_pActiveLogic != nullptr)
  {
    plAiAgentContext context;
    context.m_pOwner = ref_component.GetOwner();
    context.m_pAgentComponent = &ref_component;
    context.m_pWorld = GetWorld();
    context.m_pBlackboard = ref_agent.m_pBlackboard;
    context.m_pTarget = nullptr;
    context.m_pActionQueue = &ref_agent.m_ActionQueue;
    context.m_TimeStep = plTime::MakeZero();

    ref_agent.m_pActiveLogic->Deactivate(context, bInterrupted);
    DestroyLogicClone(ref_agent.m_pActiveLogic);
  }

  ref_agent.m_ActionQueue.CancelCurrentActions(*ref_component.GetOwner());
  ref_agent.m_iActiveBehavior = -1;
  ref_agent.m_iActiveTarget = -1;
  ref_agent.m_fActiveScore = 0.0f;
  ref_agent.m_bLocked = false;
}

void plAiBrainWorldModule::ExecuteActiveBehavior(Agent& ref_agent, plAiAgentComponent& ref_component, plTime tDiff)
{
  if (ref_agent.m_pActiveLogic != nullptr)
  {
    // refresh the published target position for behaviors that track moving targets
    PublishTargetToBlackboard(ref_agent);

    plAiAgentContext context;
    context.m_pOwner = ref_component.GetOwner();
    context.m_pAgentComponent = &ref_component;
    context.m_pWorld = GetWorld();
    context.m_pBlackboard = ref_agent.m_pBlackboard;
    context.m_pTarget = (ref_agent.m_iActiveTarget >= 0) ? &ref_agent.m_Targets[ref_agent.m_iActiveTarget] : nullptr;
    context.m_pActionQueue = &ref_agent.m_ActionQueue;
    context.m_TimeStep = tDiff;

    const plAiBehaviorLogic::State state = ref_agent.m_pActiveLogic->Update(context);

    // if this trips, the behavior logic that just ran (state machine states, actions) stomped the agent
    ValidateAgent(ref_agent);

    ref_agent.m_bLocked = (state == plAiBehaviorLogic::State::Locked);

    if (state == plAiBehaviorLogic::State::Done || state == plAiBehaviorLogic::State::Failed)
    {
      DeactivateBehavior(ref_agent, ref_component, false);
    }
  }

  ref_agent.m_ActionQueue.Execute(*ref_component.GetOwner(), tDiff, plLog::GetThreadLocalLogSystem());
}

void plAiBrainWorldModule::UpdateGather(const UpdateContext& context)
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  ++m_uiFrameCounter;
  m_DueAgents.Clear();

  // gather LOD center positions once per frame
  if (!cvar_AiAgentsLodFreeze)
  {
    m_LodCenterPositions.Clear();

    for (const plComponentHandle& hCenter : m_LodCenters)
    {
      plComponent* pComponent = nullptr;
      if (GetWorld()->TryGetComponent(hCenter, pComponent) && pComponent->IsActiveAndSimulating())
      {
        m_LodCenterPositions.PushBack(pComponent->GetOwner()->GetGlobalPosition());
      }
    }
  }

  plInt32 iDecisionBudget = plMath::Max(1, cvar_AiAgentsMaxDecisionsPerFrame.GetValue());

  // iterate a snapshot: the hot-reload path below can deactivate behaviors, whose OnExit logic
  // may delete objects and thereby mutate m_ActiveAgents through UnregisterAgent
  plHybridArray<plUInt32, 128> activeAgents(m_ActiveAgents);
  const plUInt32 uiNumAgents = activeAgents.GetCount();

  // round-robin snapshot pass, bounded by the per-frame budget
  for (plUInt32 i = 0; i < uiNumAgents && iDecisionBudget > 0; ++i)
  {
    const plUInt32 uiSlot = activeAgents[(m_uiDecisionCursor + i) % uiNumAgents];
    Agent& agent = m_Agents[uiSlot];

    ValidateAgent(agent);

    if (!agent.m_bInUse)
      continue;

    if (agent.m_NextDecision > now)
      continue;

    plAiAgentComponent* pComponent = ResolveComponent(agent);
    if (pComponent == nullptr)
      continue;

    // hot reload: reconfigure when the archetype asset changed
    {
      plResourceLock<plAiArchetypeResource> pArchetype(pComponent->m_hArchetype, plResourceAcquireMode::PointerOnly);
      if (agent.m_hArchetype != pComponent->m_hArchetype ||
          (pArchetype.IsValid() && pArchetype->GetCurrentResourceChangeCounter() != agent.m_uiArchetypeChangeCounter))
      {
        DeactivateBehavior(agent, *pComponent, true);
        ConfigureAgent(agent, *pComponent);
      }
    }

    if (!agent.m_bConfigured)
    {
      ConfigureAgent(agent, *pComponent);

      if (!agent.m_bConfigured)
        continue;
    }

    if (!cvar_AiAgentsLodFreeze)
    {
      agent.m_LodTier = DetermineLodTier(pComponent->GetOwner()->GetGlobalPosition());
    }

    const plTime decisionInterval = GetDecisionInterval(agent.m_LodTier);
    agent.m_NextDecision = now + decisionInterval;
    --iDecisionBudget;

    UpdatePerception(agent, *pComponent, now, decisionInterval);
    BuildScoringSnapshot(agent, *pComponent, now);

    m_DueAgents.PushBack(uiSlot);
  }

  if (uiNumAgents > 0)
  {
    m_uiDecisionCursor = (m_uiDecisionCursor + 1) % uiNumAgents;
  }
}

void plAiBrainWorldModule::UpdateScore(const UpdateContext& context)
{
  if (!cvar_AiAgentsAsyncScoring || m_DueAgents.IsEmpty())
    return; // synchronous fallback scores in UpdateApply instead

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  plTaskSystem::ParallelForIndexed(0u, m_DueAgents.GetCount(), [this, now](plUInt32 uiFirst, plUInt32 uiEnd) {
    for (plUInt32 i = uiFirst; i < uiEnd; ++i)
    {
      ScoreAgent(m_Agents[m_DueAgents[i]], now);
    }
  });
}

void plAiBrainWorldModule::UpdateApply(const UpdateContext& context)
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  // synchronous scoring fallback
  if (!cvar_AiAgentsAsyncScoring)
  {
    for (plUInt32 uiSlot : m_DueAgents)
    {
      ScoreAgent(m_Agents[uiSlot], now);
    }
  }

  // Behavior logic may delete game objects immediately, which deactivates their agent components
  // and mutates m_DueAgents / m_ActiveAgents through UnregisterAgent - while we are iterating them.
  // Therefore iterate over snapshots and re-validate each slot before touching it.
  plHybridArray<plUInt32, 64> dueAgents(m_DueAgents);
  m_DueAgents.Clear();

  // apply this frame's decisions
  for (plUInt32 uiSlot : dueAgents)
  {
    Agent& agent = m_Agents[uiSlot];

    if (!agent.m_bInUse)
      continue; // unregistered by an earlier agent's logic this frame

    plAiAgentComponent* pComponent = ResolveComponent(agent);
    if (pComponent == nullptr)
      continue;

    ApplyDecision(agent, *pComponent);
  }

  // execution pass, rate-limited by LOD tier
  plHybridArray<plUInt32, 128> activeAgents(m_ActiveAgents);

  for (plUInt32 uiSlot : activeAgents)
  {
    Agent& agent = m_Agents[uiSlot];

    ValidateAgent(agent);

    if (!agent.m_bInUse)
      continue; // unregistered by an earlier agent's logic this frame

    const plUInt32 uiExecDivider = (agent.m_LodTier == LodTier::Hot) ? 1 : (agent.m_LodTier == LodTier::Warm ? 2 : 4);

    if (((m_uiFrameCounter + uiSlot) % uiExecDivider) != 0)
      continue;

    plAiAgentComponent* pComponent = ResolveComponent(agent);
    if (pComponent == nullptr)
      continue;

    // pass the actually elapsed time, so lower execution rates don't slow down behavior logic
    plTime tDiff = now - agent.m_LastExecution;
    tDiff = plMath::Min(tDiff, plTime::MakeFromSeconds(0.5)); // guard against huge steps (first exec, world pauses)
    agent.m_LastExecution = now;

    ExecuteActiveBehavior(agent, *pComponent, tDiff);
  }
}

//////////////////////////////////////////////////////////////////////////

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

      plHybridArray<float, 8> considerationValues;
      const float fUtility = plAiUtilityEvaluator::ScoreUtility(desc, behaviorState.m_fWeightScale, context, &considerationValues);

      const plAiBehaviorCategory::Enum category = static_cast<plAiBehaviorCategory::Enum>(desc.m_Category.GetValue());
      const float fFinalScore = (fUtility > 0.0f) ? plAiBehaviorScore::Compose(category, fUtility) : 0.0f;

      // show the COMPOSED score (category + utility) - that is what the arbitration actually compares
      sText.SetFormat("{}{}: {} ({} + {})", bIsActive ? "> " : "  ", GetBehaviorName(behaviorState.m_hResource),
        plArgF(fFinalScore, 2), GetCategoryName(category), plArgF(fUtility, 2));

      for (float fValue : considerationValues)
      {
        sText.AppendFormat(" [{}]", plArgF(fValue, 2));
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
    }

    DrawAgentDebugInfo(agent, *pComponent, now);
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiBrainWorldModule);
