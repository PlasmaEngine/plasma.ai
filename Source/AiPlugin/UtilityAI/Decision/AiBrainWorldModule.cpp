#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiBehaviorLogic.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>
#include <AiPlugin/UtilityAI/Decision/AiUtilityEvaluator.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionRules.h>
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
  void DestroyLogicClone(plAiBehaviorLogic*& ref_pLogic)
  {
    if (ref_pLogic != nullptr)
    {
      ref_pLogic->GetDynamicRTTI()->GetAllocator()->Deallocate(ref_pLogic);
      ref_pLogic = nullptr;
    }
  }
} // namespace

plHashedString plAiBrainWorldModule::GetBehaviorName(const plAiBehaviorResourceHandle& hBehavior)
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

plUInt8 plAiBrainWorldModule::GetBehaviorCategory(const plAiBehaviorResourceHandle& hBehavior)
{
  if (hBehavior.IsValid())
  {
    plResourceLock<plAiBehaviorResource> pBehavior(hBehavior, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
    if (pBehavior.GetAcquireResult() == plResourceAcquireResult::Final)
    {
      return pBehavior->GetDescriptor().m_Category.GetValue();
    }
  }

  return plAiBehaviorCategory::Default;
}

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
  PL_ASSERT_DEV(agent.m_uiCanaryFront == 0xA1C0FFEE,
    "AI agent memory corrupted (front canary = {0}). If it reads 0xDDDDDDDD the agent was destructed twice, otherwise something wrote past the start of the agent.",
    plArgU(agent.m_uiCanaryFront, 8, true, 16));

  PL_ASSERT_DEV(agent.m_uiCanaryQueue == 0xBEEFCAFE,
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
  // other modules may already be gone: the logic destroyed here must not reach into the world
  plAiBehaviorLogic::WorldShutdownScope shutdown;

  // a copy: shutting an agent down can run game code that removes other agents from the list
  plDynamicArray<plUInt32> activeAgents = m_ActiveAgents;

  for (plUInt32 uiSlot : activeAgents)
  {
    if (m_Agents[uiSlot].m_bInUse)
    {
      ShutdownAgent(m_Agents[uiSlot]);
    }
  }

  for (plUInt32 uiSlot = 0; uiSlot < m_Agents.GetCount(); ++uiSlot)
  {
    if (m_Agents[uiSlot].m_bShutdownPending)
    {
      ShutdownAgent(m_Agents[uiSlot]);
    }
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

  Agent& agent = m_Agents[uiAgentIndex];

  m_ActiveAgents.RemoveAndSwap(uiAgentIndex);
  m_DueAgents.RemoveAndSwap(uiAgentIndex); // slot may be pending a decision this frame

  if (agent.m_uiLogicCallDepth > 0)
  {
    // the agent's own logic deleted its owner: the logic clone and the action queue are still on the
    // stack, so only detach the agent here and let the outermost LogicCallScope free it
    agent.m_bInUse = false;
    agent.m_bShutdownPending = true;
    return;
  }

  FreeAgentSlot(uiAgentIndex);
}

void plAiBrainWorldModule::FreeAgentSlot(plUInt32 uiSlot)
{
  ShutdownAgent(m_Agents[uiSlot]);
  m_FreeAgentSlots.PushBack(uiSlot);
}

plAiBrainWorldModule::LogicCallScope::LogicCallScope(plAiBrainWorldModule& ref_module, plUInt32 uiSlot)
  : m_Module(ref_module)
  , m_uiSlot(uiSlot)
{
  ++m_Module.m_Agents[m_uiSlot].m_uiLogicCallDepth;
}

plAiBrainWorldModule::LogicCallScope::~LogicCallScope()
{
  Agent& agent = m_Module.m_Agents[m_uiSlot];

  if (--agent.m_uiLogicCallDepth == 0 && agent.m_bShutdownPending)
  {
    m_Module.FreeAgentSlot(m_uiSlot);
  }
}

void plAiBrainWorldModule::ShutdownAgent(Agent& ref_agent)
{
  ValidateAgent(ref_agent);

  if (ref_agent.m_pActiveLogic != nullptr)
  {
    // no Deactivate: the owner is being removed. Destroying a state machine logic still exits its current state, which
    // releases what the state holds (claims, queries, navigation); see plAiBehaviorLogic::IsWorldShuttingDown.
    DestroyLogicClone(ref_agent.m_pActiveLogic);
  }

  ref_agent.m_ActionQueue.InterruptAndClear();
  ref_agent.m_pBlackboard = nullptr;
  ref_agent.m_bInUse = false;
  ref_agent.m_bShutdownPending = false;
  ref_agent.m_bConfigured = false;
  ref_agent.m_iActiveBehavior = -1;
}

void plAiBrainWorldModule::ResetAgent(Agent& ref_agent)
{
  ValidateAgent(ref_agent);

  ref_agent.m_bInUse = false;
  ref_agent.m_bShutdownPending = false;
  ref_agent.m_uiLogicCallDepth = 0;
  ref_agent.m_bConfigured = false;
  ref_agent.m_bLocked = false;
  ref_agent.m_bLockRequested = false;
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
  ref_agent.m_TargetSince = plTime::MakeZero();
  ref_agent.m_LockedSince = plTime::MakeZero();

  ref_agent.m_Reactivity = Reactivity();
  ref_agent.m_Command = Command();
  ref_agent.m_LastDecision = plTime::MakeFromSeconds(-1000000.0);
  ref_agent.m_LastPerception = plTime::MakeZero();
  ref_agent.m_uiWakeReasons = 0;

  ref_agent.m_Targets.Clear();
  ref_agent.m_uiAlertLevel = plAiAlertLevel::Unaware;
  ref_agent.m_ActionQueue.InterruptAndClear();

  ref_agent.m_LodTier = LodTier::Hot;
  ref_agent.m_NextDecision = plTime::MakeZero();
  ref_agent.m_LastExecution = plTime::MakeZero();
  ref_agent.m_uiLastStimulusSequence = 0;
  ref_agent.m_History.Clear();
  ref_agent.m_uiDecisionCount = 0;
  ref_agent.m_LastOutcome = plAiDecisionOutcome::None;
  ref_agent.m_pDebugSnapshot.Clear();

  ref_agent.m_vSnapshotPosition = plVec3::MakeZero();
  ref_agent.m_qSnapshotRotation = plQuat::MakeIdentity();
  ref_agent.m_BlackboardSnapshot.Clear();
  ref_agent.m_ScoringDescriptors.Clear();
  ref_agent.m_BehaviorSnapshots.Clear();
  ref_agent.m_ActiveTargetHeld = plTime::MakeZero();
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
  ref_agent.m_pBlackboard->SetEntryValue("Ai_AlertLevel", static_cast<plInt32>(ref_agent.m_uiAlertLevel));
  ref_agent.m_pBlackboard->SetEntryValue("Ai_CommandActive", false);
  ref_agent.m_pBlackboard->SetEntryValue("Ai_Command", plHashedString());
  ref_agent.m_pBlackboard->SetEntryValue("Ai_CommandPosition", plVec3::MakeZero());
  ref_agent.m_pBlackboard->SetEntryValue("Ai_CommandTarget", plGameObjectHandle());

  ref_agent.m_sTeam = ref_component.GetResolvedTeam();

  ref_agent.m_Reactivity.m_bWakeOnStimulus = desc.m_bWakeOnStimulus;
  ref_agent.m_Reactivity.m_bWakeOnBehaviorEnd = desc.m_bWakeOnBehaviorEnd;
  ref_agent.m_Reactivity.m_MinWakeSpacing = desc.m_MinWakeSpacing;
  ref_agent.m_Reactivity.m_fTargetSwitchMargin = desc.m_fTargetSwitchMargin;
  ref_agent.m_Reactivity.m_TargetMinHold = desc.m_TargetMinHold;
  ref_agent.m_Reactivity.m_MaxLockDuration = desc.m_MaxLockDuration;
  ref_agent.m_Reactivity.m_uiScoringVersion = desc.m_uiScoringVersion;

  ref_agent.m_fMaxSenseRangeScale = 0.0f;
  for (const plAiSenseProfile* pSense : {&desc.m_Hearing, &desc.m_Damage, &desc.m_Custom})
  {
    if (pSense->m_bEnabled)
    {
      ref_agent.m_fMaxSenseRangeScale = plMath::Max(ref_agent.m_fMaxSenseRangeScale, pSense->m_fRangeScale);
    }
  }

  // a command issued before the blackboard existed is written on the next decision
  ref_agent.m_Command.m_bDirty = true;

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

void plAiBrainWorldModule::UpdatePerception(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now)
{
  // decay and gain over the time that actually passed: decisions can be late (budget) or early (wakes)
  const plTime tDiff = ref_agent.m_LastPerception.IsZero() ? GetDecisionInterval(ref_agent.m_LodTier) : plMath::Clamp(now - ref_agent.m_LastPerception, plTime::MakeZero(), plTime::MakeFromSeconds(10.0));
  ref_agent.m_LastPerception = now;

  plResourceLock<plAiArchetypeResource> pArchetype(ref_agent.m_hArchetype, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
  const plAiArchetypeResourceDescriptor defaultArchetype;
  const plAiArchetypeResourceDescriptor& archetype = (pArchetype.GetAcquireResult() == plResourceAcquireResult::Final) ? pArchetype->GetDescriptor() : defaultArchetype;

  for (plAiPerceivedTarget& target : ref_agent.m_Targets)
  {
    float fDecayPerSecond = 0.0f;
    plTime memoryDuration;
    plAiPerceptionRules::GetDecaySettings(archetype, target.m_uiLastSense, fDecayPerSecond, memoryDuration);

    target.m_fConfidence = plAiPerceptionRules::Decay(target.m_fConfidence, fDecayPerSecond, tDiff);
  }

  GatherSight(ref_agent, ref_component, archetype, now, tDiff);
  GatherStimuli(ref_agent, ref_component, archetype, now);
  ForgetTargets(ref_agent.m_Targets, ref_agent.m_iActiveTarget, archetype, now);

  ref_agent.m_uiAlertLevel = static_cast<plUInt8>(plAiPerceptionRules::ComputeAlertLevel(ref_agent.m_Targets, now));

  if (ref_agent.m_pBlackboard != nullptr)
  {
    ref_agent.m_pBlackboard->SetEntryValue("Ai_AlertLevel", static_cast<plInt32>(ref_agent.m_uiAlertLevel));
  }
}

void plAiBrainWorldModule::GatherSight(Agent& ref_agent, plAiAgentComponent& ref_component, const plAiArchetypeResourceDescriptor& archetype, plTime now, plTime tDiff)
{
  plGameObject* pSensorRoot = ref_component.GetOwner();

  if (!archetype.m_sSensorObjectName.IsEmpty())
  {
    plGameObject* pNamedChild = ref_component.GetOwner()->FindChildByName(plTempHashedString(archetype.m_sSensorObjectName.GetData()), true);
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

  if (archetype.m_sSensorObjectName.IsEmpty())
  {
    for (auto it = pSensorRoot->GetChildren(); it.IsValid(); ++it)
    {
      GatherSensors(&(*it));
    }
  }

  // sight gains while in view; the decay of this update is added back so a visible target doesn't lose confidence
  const float fSeconds = static_cast<float>(tDiff.GetSeconds());
  const float fGain = (archetype.m_fConfidenceGainPerSecond + archetype.m_fConfidenceDecayPerSecond) * fSeconds;

  for (plSensorComponent* pSensor : sensors)
  {
    for (const plGameObjectHandle& hDetected : pSensor->GetLastDetectedObjects())
    {
      const plGameObject* pDetected = nullptr;
      if (!GetWorld()->TryGetObject(hDetected, pDetected))
        continue;

      if (m_pPerceptionModule != nullptr && m_pPerceptionModule->GetAttitude(ref_agent.m_sTeam, GetTeamOf(pDetected)) == plAiAttitude::Friendly)
        continue;

      plAiPerceivedTarget& record = FindOrCreateTarget(ref_agent.m_Targets, hDetected, pDetected->GetGlobalPosition());
      record.m_vLastKnownPosition = pDetected->GetGlobalPosition();
      record.m_vLastKnownVelocity = pDetected->GetLinearVelocity();
      record.m_fConfidence = plMath::Min(1.0f, record.m_fConfidence + fGain);
      record.m_LastStimulusTime = now;
      record.m_LastSightTime = now;
      record.m_uiLastSense = plAiStimulusType::Sight;
      record.m_uiSenses |= 1 << plAiStimulusType::Sight;
      record.m_bReported = false;
    }
  }
}

void plAiBrainWorldModule::GatherStimuli(Agent& ref_agent, plAiAgentComponent& ref_component, const plAiArchetypeResourceDescriptor& archetype, plTime now)
{
  if (m_pPerceptionModule == nullptr)
    return;

  const plVec3 vAgentPosition = ref_component.GetOwner()->GetGlobalPosition();
  const plUInt64 uiLastConsumed = ref_agent.m_uiLastStimulusSequence;
  ref_agent.m_uiLastStimulusSequence = m_pPerceptionModule->GetLatestStimulusSequence();

  for (const auto& timedStimulus : m_pPerceptionModule->GetRecentStimuli())
  {
    if (timedStimulus.m_uiSequence <= uiLastConsumed)
      continue; // already processed in a previous decision

    const plAiStimulus& stimulus = timedStimulus.m_Stimulus;
    const plAiSenseProfile* pSense = archetype.GetSenseProfile(stimulus.m_Type.GetValue());

    if (pSense != nullptr && !pSense->m_bEnabled)
      continue;

    if (stimulus.m_hSource == ref_component.GetOwner()->GetHandle())
      continue; // don't perceive yourself

    const float fRange = stimulus.m_fRadius * ((pSense != nullptr) ? pSense->m_fRangeScale : 1.0f);
    if ((stimulus.m_vGlobalPosition - vAgentPosition).GetLengthSquared() > plMath::Square(fRange))
      continue;

    if (m_pPerceptionModule->GetAttitude(ref_agent.m_sTeam, stimulus.m_sSourceTeam) == plAiAttitude::Friendly)
      continue;

    const float fStrength = stimulus.m_fStrength * ((pSense != nullptr) ? pSense->m_fStrengthScale : 1.0f);

    // remember where it happened: a noise maker that moves on afterwards isn't tracked
    plAiPerceivedTarget& record = FindOrCreateTarget(ref_agent.m_Targets, stimulus.m_hSource, stimulus.GetEventPosition());
    record.m_vLastKnownPosition = stimulus.GetEventPosition();
    record.m_vLastKnownVelocity = plVec3::MakeZero();
    record.m_fConfidence = plMath::Clamp(plMath::Max(record.m_fConfidence, fStrength), 0.0f, 1.0f);
    record.m_LastStimulusTime = now;
    record.m_uiLastSense = stimulus.m_Type.GetValue();
    record.m_uiSenses |= 1 << stimulus.m_Type.GetValue();
    record.m_bReported = stimulus.m_bReport;
  }
}

namespace
{
  /// Removes a perceived target and keeps the active behavior's target index pointing at the same record.
  void RemoveTarget(plHybridArray<plAiPerceivedTarget, 4>& ref_targets, plInt32& ref_iActiveTarget, plUInt32 uiIndex)
  {
    if (ref_iActiveTarget == static_cast<plInt32>(uiIndex))
    {
      ref_iActiveTarget = -1;
    }
    else if (ref_iActiveTarget > static_cast<plInt32>(uiIndex))
    {
      --ref_iActiveTarget;
    }

    ref_targets.RemoveAtAndCopy(uiIndex);
  }
} // namespace

plAiPerceivedTarget& plAiBrainWorldModule::FindOrCreateTarget(plHybridArray<plAiPerceivedTarget, 4>& ref_targets, const plGameObjectHandle& hTarget, const plVec3& vEventPosition)
{
  // events without a source are told apart by where they happened
  constexpr float fSameEventDistance = 2.0f;

  for (plAiPerceivedTarget& target : ref_targets)
  {
    if (target.m_hTarget != hTarget)
      continue;

    if (!hTarget.IsInvalidated() || (target.m_vLastKnownPosition - vEventPosition).GetLengthSquared() <= plMath::Square(fSameEventDistance))
      return target;
  }

  plAiPerceivedTarget& record = ref_targets.ExpandAndGetRef();
  record.m_hTarget = hTarget;
  record.m_vLastKnownPosition = vEventPosition;
  return record;
}

void plAiBrainWorldModule::ForgetTargets(plHybridArray<plAiPerceivedTarget, 4>& ref_targets, plInt32& ref_iActiveTarget, const plAiArchetypeResourceDescriptor& archetype, plTime now)
{
  for (plUInt32 i = ref_targets.GetCount(); i-- > 0;)
  {
    float fDecayPerSecond = 0.0f;
    plTime memoryDuration;
    plAiPerceptionRules::GetDecaySettings(archetype, ref_targets[i].m_uiLastSense, fDecayPerSecond, memoryDuration);

    if (plAiPerceptionRules::IsForgotten(ref_targets[i], now, memoryDuration))
    {
      RemoveTarget(ref_targets, ref_iActiveTarget, i);
    }
  }

  // over the cap: drop the least confident records, never the active target
  const plUInt32 uiMaxTargets = plMath::Max<plUInt32>(1, archetype.m_uiMaxTargets);

  while (ref_targets.GetCount() > uiMaxTargets)
  {
    plInt32 iWeakest = -1;

    for (plUInt32 i = 0; i < ref_targets.GetCount(); ++i)
    {
      if (static_cast<plInt32>(i) != ref_iActiveTarget && (iWeakest < 0 || ref_targets[i].m_fConfidence < ref_targets[iWeakest].m_fConfidence))
      {
        iWeakest = static_cast<plInt32>(i);
      }
    }

    if (iWeakest < 0)
      break;

    RemoveTarget(ref_targets, ref_iActiveTarget, static_cast<plUInt32>(iWeakest));
  }
}

plHashedString plAiBrainWorldModule::GetTeamOf(const plGameObject* pObject)
{
  const plAiAgentComponent* pOtherAgent = nullptr;
  if (pObject->TryGetComponentOfBaseType(pOtherAgent))
  {
    return pOtherAgent->GetResolvedTeam();
  }

  return plHashedString();
}

void plAiBrainWorldModule::BuildScoringSnapshot(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now)
{
  ref_agent.m_vSnapshotPosition = ref_component.GetOwner()->GetGlobalPosition();
  ref_agent.m_qSnapshotRotation = ref_component.GetOwner()->GetGlobalRotation();

  ref_agent.m_ScoringDescriptors.Clear();
  ref_agent.m_BehaviorSnapshots.Clear();
  ref_agent.m_BlackboardSnapshot.Clear();
  ref_agent.m_ActiveTargetHeld = now - ref_agent.m_TargetSince;

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
    auto& snapshot = ref_agent.m_BehaviorSnapshots.ExpandAndGetRef();
    snapshot.m_SinceActivation = bIsActive ? now - behaviorState.m_LastActivation : plTime::MakeZero();
    snapshot.m_SinceDeactivation = now - behaviorState.m_LastDeactivation;
    snapshot.m_fWeight = behaviorState.m_fWeightScale * behaviorState.m_fWeightOverride;
    snapshot.m_bEnabled = behaviorState.m_bEnabled;
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
  context.m_uiAlertLevel = ref_agent.m_uiAlertLevel;

  Decision decision;

  for (plUInt32 uiBehavior = 0; uiBehavior < ref_agent.m_ScoringDescriptors.GetCount(); ++uiBehavior)
  {
    const plAiBehaviorResourceDescriptor* pDescriptor = ref_agent.m_ScoringDescriptors[uiBehavior];

    const BehaviorSnapshot& behavior = ref_agent.m_BehaviorSnapshots[uiBehavior];

    if (pDescriptor == nullptr || !behavior.m_bEnabled)
      continue;

    const bool bIsActive = ref_agent.m_iActiveBehavior == static_cast<plInt32>(uiBehavior);

    context.m_TimeSinceActivation = behavior.m_SinceActivation;
    context.m_TimeSinceDeactivation = behavior.m_SinceDeactivation;

    // the behavior's best (score, target) pair
    float fBestScore = 0.0f;
    plInt32 iBestTarget = -1;
    float fActiveTargetScore = 0.0f;

    auto ScoreTarget = [&](const plAiPerceivedTarget* pTarget, plInt32 iTargetIndex) {
      context.m_pTarget = pTarget;

      const float fUtility = plAiUtilityEvaluator::ScoreUtility(*pDescriptor, behavior.m_fWeight, context, nullptr, ref_agent.m_Reactivity.m_uiScoringVersion);

      if (fUtility <= 0.0f)
        return;

      const float fScore = plAiBehaviorScore::Compose(static_cast<plAiBehaviorCategory::Enum>(pDescriptor->m_Category.GetValue()), fUtility);

      if (bIsActive && iTargetIndex == ref_agent.m_iActiveTarget)
      {
        fActiveTargetScore = fScore;
      }

      if (fScore > fBestScore)
      {
        fBestScore = fScore;
        iBestTarget = iTargetIndex;
      }
    };

    if (pDescriptor->NeedsTarget())
    {
      for (plUInt32 uiTarget = 0; uiTarget < ref_agent.m_Targets.GetCount(); ++uiTarget)
      {
        ScoreTarget(&ref_agent.m_Targets[uiTarget], static_cast<plInt32>(uiTarget));
      }
    }
    else
    {
      ScoreTarget(nullptr, -1);
    }

    if (bIsActive)
    {
      decision.m_fActiveScore = fActiveTargetScore;

      // target hysteresis: keep the current target unless it was held long enough and the new one is clearly better
      const bool bHeldLongEnough = ref_agent.m_ActiveTargetHeld >= ref_agent.m_Reactivity.m_TargetMinHold;
      const bool bClearlyBetter = fBestScore > fActiveTargetScore + ref_agent.m_Reactivity.m_fTargetSwitchMargin;

      if (fActiveTargetScore > 0.0f && iBestTarget != ref_agent.m_iActiveTarget && !(bHeldLongEnough && bClearlyBetter))
      {
        fBestScore = fActiveTargetScore;
        iBestTarget = ref_agent.m_iActiveTarget;
      }
    }

    if (fBestScore <= 0.0f)
      continue;

    if (fBestScore > decision.m_fScore)
    {
      decision.m_fRunnerUpScore = decision.m_fScore;
      decision.m_fScore = fBestScore;
      decision.m_iBehavior = static_cast<plInt32>(uiBehavior);
      decision.m_iTarget = iBestTarget;
    }
    else if (fBestScore > decision.m_fRunnerUpScore)
    {
      decision.m_fRunnerUpScore = fBestScore;
    }
  }

  ref_agent.m_Decision = decision;
  ++ref_agent.m_uiDecisionCount;

  if (ref_agent.m_pDebugSnapshot != nullptr)
  {
    TraceDecision(ref_agent, context);
  }
}

void plAiBrainWorldModule::TraceDecision(Agent& ref_agent, plAiScoringContext context)
{
  plAiAgentDebugSnapshot& snapshot = *ref_agent.m_pDebugSnapshot;
  const Decision& decision = ref_agent.m_Decision;

  snapshot.m_DecisionTime = context.m_Now;
  snapshot.m_uiDecisionCount = ref_agent.m_uiDecisionCount;
  snapshot.m_iWinner = decision.m_iBehavior;
  snapshot.m_fWinnerScore = decision.m_fScore;
  snapshot.m_fRunnerUpScore = decision.m_fRunnerUpScore;
  snapshot.m_Targets = ref_agent.m_Targets;
  snapshot.m_Behaviors.Clear();

  plHybridArray<plAiConsiderationTrace, 8> considerations;

  for (plUInt32 uiBehavior = 0; uiBehavior < ref_agent.m_ScoringDescriptors.GetCount(); ++uiBehavior)
  {
    const plAiBehaviorResourceDescriptor* pDescriptor = ref_agent.m_ScoringDescriptors[uiBehavior];

    // no resource locks here: this may run in the async phase
    plAiBehaviorTrace& trace = snapshot.m_Behaviors.ExpandAndGetRef();
    trace.m_bActive = ref_agent.m_iActiveBehavior == static_cast<plInt32>(uiBehavior);

    if (pDescriptor == nullptr || pDescriptor->m_sName.IsEmpty())
    {
      trace.m_sName.Assign(ref_agent.m_Behaviors[uiBehavior].m_hResource.GetResourceID());
    }
    else
    {
      trace.m_sName = pDescriptor->m_sName;
    }

    if (pDescriptor == nullptr)
    {
      trace.m_Veto = plAiBehaviorVeto::NotLoaded;
      continue;
    }

    const BehaviorSnapshot& behavior = ref_agent.m_BehaviorSnapshots[uiBehavior];
    context.m_TimeSinceActivation = behavior.m_SinceActivation;
    context.m_TimeSinceDeactivation = behavior.m_SinceDeactivation;

    trace.m_uiCategory = pDescriptor->m_Category.GetValue();
    trace.m_Veto = plAiBehaviorVeto::ZeroUtility;

    if (!behavior.m_bEnabled)
    {
      trace.m_Veto = plAiBehaviorVeto::Disabled;
      continue;
    }

    if (pDescriptor->m_CooldownDuration.IsPositive() && behavior.m_SinceDeactivation < pDescriptor->m_CooldownDuration)
    {
      trace.m_Veto = plAiBehaviorVeto::Cooldown;
      continue;
    }

    auto TraceTarget = [&](const plAiPerceivedTarget* pTarget, plInt32 iTargetIndex) {
      context.m_pTarget = pTarget;

      const float fUtility = plAiUtilityEvaluator::ScoreUtility(*pDescriptor, behavior.m_fWeight, context, &considerations, ref_agent.m_Reactivity.m_uiScoringVersion);

      // the first target fills the trace, so a vetoed behavior still shows which consideration vetoed
      if (fUtility > trace.m_fUtility || (trace.m_Considerations.IsEmpty() && iTargetIndex <= 0))
      {
        trace.m_fUtility = fUtility;
        trace.m_iTarget = iTargetIndex;
        trace.m_Considerations = considerations;
      }
    };

    if (pDescriptor->NeedsTarget())
    {
      if (ref_agent.m_Targets.IsEmpty())
      {
        trace.m_Veto = plAiBehaviorVeto::NoTarget;
        continue;
      }

      for (plUInt32 uiTarget = 0; uiTarget < ref_agent.m_Targets.GetCount(); ++uiTarget)
      {
        TraceTarget(&ref_agent.m_Targets[uiTarget], static_cast<plInt32>(uiTarget));
      }
    }
    else
    {
      TraceTarget(nullptr, -1);
    }

    if (trace.m_fUtility > 0.0f)
    {
      trace.m_Veto = plAiBehaviorVeto::None;
      trace.m_fScore = plAiBehaviorScore::Compose(static_cast<plAiBehaviorCategory::Enum>(trace.m_uiCategory), trace.m_fUtility);
    }
  }
}

namespace
{
  plAiDecisionOutcome ClassifyReplacement(float fChallengerScore, float fActiveScore)
  {
    if (fActiveScore <= 0.0f)
      return plAiDecisionOutcome::StoppedScoring;

    if (plAiBehaviorScore::GetCategory(fChallengerScore) > plAiBehaviorScore::GetCategory(fActiveScore))
      return plAiDecisionOutcome::HigherCategory;

    return plAiDecisionOutcome::BeatCommitBonus;
  }
} // namespace

void plAiBrainWorldModule::ApplyDecision(Agent& ref_agent, plAiAgentComponent& ref_component)
{
  ref_agent.m_LastOutcome = ResolveDecision(ref_agent, ref_component);

  if (ref_agent.m_bInUse && ref_agent.m_pDebugSnapshot != nullptr)
  {
    plAiAgentDebugSnapshot& snapshot = *ref_agent.m_pDebugSnapshot;
    snapshot.m_Outcome = ref_agent.m_LastOutcome;
    snapshot.m_iActiveBehavior = ref_agent.m_iActiveBehavior;
    snapshot.m_iActiveTarget = ref_agent.m_iActiveTarget;
    snapshot.m_fActiveScore = ref_agent.m_fActiveScore;
    snapshot.m_fCommitBonus = ref_agent.m_fActiveCommitBonus;
    snapshot.m_bLocked = ref_agent.m_bLocked;
    snapshot.m_uiLodTier = static_cast<plUInt8>(ref_agent.m_LodTier);
    snapshot.m_NextDecision = ref_agent.m_NextDecision;

    for (plUInt32 i = 0; i < snapshot.m_Behaviors.GetCount(); ++i)
    {
      snapshot.m_Behaviors[i].m_bActive = ref_agent.m_iActiveBehavior == static_cast<plInt32>(i);
    }
  }
}

plAiDecisionOutcome plAiBrainWorldModule::ResolveDecision(Agent& ref_agent, plAiAgentComponent& ref_component)
{
  const Decision& decision = ref_agent.m_Decision;

  // disabling a behavior through the script API also ends it while locked
  if (ref_agent.m_iActiveBehavior >= 0 && !ref_agent.m_Behaviors[ref_agent.m_iActiveBehavior].m_bEnabled)
  {
    RecordHistory(ref_agent, GetBehaviorName(ref_agent.m_Behaviors[ref_agent.m_iActiveBehavior].m_hResource), plHashedString(), 0.0f, 0.0f,
      plAiDecisionOutcome::Disabled);
    DeactivateBehavior(ref_agent, ref_component, true);

    if (!ref_agent.m_bInUse)
      return plAiDecisionOutcome::Disabled;

    if (decision.m_iBehavior < 0)
      return plAiDecisionOutcome::Disabled;
  }

  if (ref_agent.m_bLocked)
    return plAiDecisionOutcome::Locked; // the active behavior does not allow switching right now

  if (decision.m_iBehavior < 0)
  {
    if (ref_agent.m_iActiveBehavior >= 0 && decision.m_fActiveScore <= 0.0f)
    {
      // nothing scores anymore, including the active behavior
      RecordHistory(ref_agent, GetBehaviorName(ref_agent.m_Behaviors[ref_agent.m_iActiveBehavior].m_hResource), plHashedString(), 0.0f, 0.0f,
        plAiDecisionOutcome::NothingScores);
      DeactivateBehavior(ref_agent, ref_component, true);
    }

    return plAiDecisionOutcome::NothingScores;
  }

  if (ref_agent.m_iActiveBehavior < 0)
  {
    SwitchBehavior(ref_agent, ref_component, decision.m_iBehavior, decision.m_iTarget, decision.m_fScore, decision.m_fRunnerUpScore,
      plAiDecisionOutcome::Started);
    return plAiDecisionOutcome::Started;
  }

  if (decision.m_iBehavior == ref_agent.m_iActiveBehavior)
  {
    ref_agent.m_fActiveScore = decision.m_fScore;

    if (decision.m_iTarget == ref_agent.m_iActiveTarget)
      return plAiDecisionOutcome::Kept;

    // same behavior, better target -> reactivate
    ref_agent.m_iActiveTarget = decision.m_iTarget;
    ref_agent.m_TargetSince = GetWorld()->GetClock().GetAccumulatedTime();
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

    return plAiDecisionOutcome::Retargeted;
  }

  // the commit bonus of the ACTIVE behavior decides how sticky it is within its category
  if (!plAiBehaviorScore::ShouldReplaceActive(decision.m_fScore, decision.m_fActiveScore, ref_agent.m_fActiveCommitBonus))
  {
    ref_agent.m_fActiveScore = decision.m_fActiveScore;
    return plAiDecisionOutcome::HeldByCommitBonus;
  }

  const plAiDecisionOutcome outcome = ClassifyReplacement(decision.m_fScore, decision.m_fActiveScore);
  SwitchBehavior(ref_agent, ref_component, decision.m_iBehavior, decision.m_iTarget, decision.m_fScore,
    plMath::Max(decision.m_fRunnerUpScore, decision.m_fActiveScore), outcome);

  return outcome;
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
    // a behavior without a target must not act on the previous one's position
    ref_agent.m_pBlackboard->SetEntryValue("Ai_TargetPosition", plVec3::MakeZero());
    ref_agent.m_pBlackboard->SetEntryValue("Ai_TargetConfidence", 0.0f);
    ref_agent.m_pBlackboard->SetEntryValue("Ai_TargetObject", plGameObjectHandle());
  }
}

void plAiBrainWorldModule::SwitchBehavior(Agent& ref_agent, plAiAgentComponent& ref_component, plInt32 iNewBehavior, plInt32 iNewTarget, float fScore, float fRunnerUpScore, plAiDecisionOutcome outcome)
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  plHashedString sFrom;
  if (ref_agent.m_iActiveBehavior >= 0)
  {
    sFrom = GetBehaviorName(ref_agent.m_Behaviors[ref_agent.m_iActiveBehavior].m_hResource);
  }

  DeactivateBehavior(ref_agent, ref_component, true);

  if (!ref_agent.m_bInUse)
    return; // the old behavior's logic deleted the owner

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
  ref_agent.m_TargetSince = now;
  ref_agent.m_fActiveScore = fScore;
  ref_agent.m_fActiveCommitBonus = desc.m_fCommitBonus;
  ref_agent.m_bLocked = false;
  ref_agent.m_bLockRequested = false;
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

  RecordHistory(ref_agent, sFrom, GetBehaviorName(behaviorState.m_hResource), fScore, fRunnerUpScore, outcome, GetBehaviorCategory(behaviorState.m_hResource));
}

void plAiBrainWorldModule::RecordHistory(Agent& ref_agent, const plHashedString& sFrom, const plHashedString& sTo, float fWinnerScore, float fRunnerUpScore, plAiDecisionOutcome outcome, plUInt8 uiToCategory)
{
  HistoryEntry entry;
  entry.m_Time = GetWorld()->GetClock().GetAccumulatedTime();
  entry.m_sFrom = sFrom;
  entry.m_sTo = sTo;
  entry.m_uiToCategory = uiToCategory;
  entry.m_fWinnerScore = fWinnerScore;
  entry.m_fRunnerUpScore = fRunnerUpScore;
  entry.m_Outcome = outcome;

  if (!ref_agent.m_History.CanAppend())
  {
    ref_agent.m_History.PopFront();
  }

  ref_agent.m_History.PushBack(entry);
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
  ref_agent.m_bLockRequested = false;
}

void plAiBrainWorldModule::UpdateLock(Agent& ref_agent, bool bLockRequested)
{
  if (!bLockRequested)
  {
    ref_agent.m_bLockRequested = false;
    ref_agent.m_bLocked = false;
    return;
  }

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  // the timeout counts from the start of an uninterrupted run of Locked reports
  if (!ref_agent.m_bLockRequested)
  {
    ref_agent.m_bLockRequested = true;
    ref_agent.m_LockedSince = now;
  }

  const plTime maxLock = ref_agent.m_Reactivity.m_MaxLockDuration;
  ref_agent.m_bLocked = maxLock.IsZeroOrNegative() || (now - ref_agent.m_LockedSince) <= maxLock;
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

    if (!ref_agent.m_bInUse)
      return; // the logic deleted its owner

    UpdateLock(ref_agent, state == plAiBehaviorLogic::State::Locked);

    if (state == plAiBehaviorLogic::State::Done || state == plAiBehaviorLogic::State::Failed)
    {
      ref_agent.m_LastOutcome = (state == plAiBehaviorLogic::State::Done) ? plAiDecisionOutcome::Done : plAiDecisionOutcome::Failed;
      RecordHistory(ref_agent, GetBehaviorName(ref_agent.m_Behaviors[ref_agent.m_iActiveBehavior].m_hResource), plHashedString(), ref_agent.m_fActiveScore, 0.0f, ref_agent.m_LastOutcome);

      DeactivateBehavior(ref_agent, ref_component, false);

      if (!ref_agent.m_bInUse)
        return;

      // decide on a successor now instead of idling until the next scheduled decision
      if (ref_agent.m_Reactivity.m_bWakeOnBehaviorEnd)
      {
        RequestDecision(ref_component.m_uiAgentIndex, plAiWakeReason::BehaviorEnded);
      }
    }
  }

  ref_agent.m_ActionQueue.Execute(*ref_component.GetOwner(), tDiff, plLog::GetThreadLocalLogSystem());
}

bool plAiBrainWorldModule::GatherAgent(plUInt32 uiSlot, plTime now)
{
  Agent& agent = m_Agents[uiSlot];
  ValidateAgent(agent);

  plAiAgentComponent* pComponent = ResolveComponent(agent);
  if (pComponent == nullptr)
    return false;

  // hot reloads below deactivate behaviors, whose logic may delete the owner
  LogicCallScope logicScope(*this, uiSlot);

  // hot reload: reconfigure when the archetype asset changed. An agent without an archetype has nothing to reload.
  if (pComponent->m_hArchetype.IsValid() || agent.m_hArchetype.IsValid())
  {
    plUInt32 uiChangeCounter = agent.m_uiArchetypeChangeCounter;

    if (pComponent->m_hArchetype.IsValid())
    {
      plResourceLock<plAiArchetypeResource> pArchetype(pComponent->m_hArchetype, plResourceAcquireMode::PointerOnly);
      if (pArchetype.IsValid())
        uiChangeCounter = pArchetype->GetCurrentResourceChangeCounter();
    }

    if (agent.m_hArchetype != pComponent->m_hArchetype || uiChangeCounter != agent.m_uiArchetypeChangeCounter)
    {
      DeactivateBehavior(agent, *pComponent, true);

      if (!agent.m_bInUse)
        return false;

      ConfigureAgent(agent, *pComponent);
    }
  }

  if (!agent.m_bConfigured)
  {
    ConfigureAgent(agent, *pComponent);

    if (!agent.m_bConfigured)
      return false;
  }

  if (!cvar_AiAgentsLodFreeze)
  {
    agent.m_LodTier = DetermineLodTier(pComponent->GetOwner()->GetGlobalPosition());
  }

  agent.m_NextDecision = now + GetDecisionInterval(agent.m_LodTier);
  agent.m_LastDecision = now;
  agent.m_uiWakeReasons = 0;

  UpdateCommandAndOverrides(agent, now);
  UpdatePerception(agent, *pComponent, now);
  BuildScoringSnapshot(agent, *pComponent, now);

  if (agent.m_bInUse)
  {
    m_DueAgents.PushBack(uiSlot);
  }

  return true;
}

void plAiBrainWorldModule::UpdateGather(const UpdateContext& context)
{
  const plTime tStart = plTime::Now();
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

  const plInt32 iFrameBudget = plMath::Max(1, cvar_AiAgentsMaxDecisionsPerFrame.GetValue());
  plInt32 iDecisionBudget = iFrameBudget;

  WakeAgentsForNewStimuli();
  WakeAgentsForExpiredOrders(now);

  // woken agents first: their wake reason is more urgent than the round robin
  {
    plHybridArray<plUInt32, 32> wokenAgents(m_WokenAgents);
    m_WokenAgents.Clear();

    for (plUInt32 uiSlot : wokenAgents)
    {
      Agent& agent = m_Agents[uiSlot];

      if (!agent.m_bInUse || agent.m_uiWakeReasons == 0)
        continue; // unregistered, or already decided this frame

      if (iDecisionBudget <= 0 || agent.m_NextDecision > now)
      {
        m_WokenAgents.PushBack(uiSlot); // spacing or budget: try again next frame
        continue;
      }

      if (GatherAgent(uiSlot, now))
      {
        --iDecisionBudget;
      }
    }
  }

  const plUInt32 uiWokenDecisions = static_cast<plUInt32>(iFrameBudget - iDecisionBudget);

  // iterate a snapshot: the hot-reload path in GatherAgent can deactivate behaviors, whose OnExit logic
  // may delete objects and thereby mutate m_ActiveAgents through UnregisterAgent
  plHybridArray<plUInt32, 128> activeAgents(m_ActiveAgents);
  const plUInt32 uiNumAgents = activeAgents.GetCount();

  // round-robin snapshot pass, bounded by the per-frame budget
  for (plUInt32 i = 0; i < uiNumAgents && iDecisionBudget > 0; ++i)
  {
    const plUInt32 uiSlot = activeAgents[(m_uiDecisionCursor + i) % uiNumAgents];

    if (!m_Agents[uiSlot].m_bInUse || m_Agents[uiSlot].m_NextDecision > now)
      continue;

    if (GatherAgent(uiSlot, now))
    {
      --iDecisionBudget;
    }
  }

  if (uiNumAgents > 0)
  {
    m_uiDecisionCursor = (m_uiDecisionCursor + 1) % uiNumAgents;
  }

  // perception keeps stimuli until the slowest agent has processed them (an agent waiting for its archetype doesn't count)
  if (m_pPerceptionModule != nullptr)
  {
    plUInt64 uiProcessed = m_pPerceptionModule->GetLatestStimulusSequence();

    for (plUInt32 uiSlot : m_ActiveAgents)
    {
      if (m_Agents[uiSlot].m_bConfigured)
      {
        uiProcessed = plMath::Min(uiProcessed, m_Agents[uiSlot].m_uiLastStimulusSequence);
      }
    }

    m_pPerceptionModule->SetProcessedSequence(uiProcessed);
  }

  m_FrameStats.m_uiAgents = uiNumAgents;
  m_FrameStats.m_uiDecisions = static_cast<plUInt32>(iFrameBudget - iDecisionBudget);
  m_FrameStats.m_uiWokenDecisions = uiWokenDecisions;
  m_FrameStats.m_uiDecisionBudget = static_cast<plUInt32>(iFrameBudget);
  m_UpdateTimeThisFrame = plTime::Now() - tStart;
}

void plAiBrainWorldModule::UpdateScore(const UpdateContext& context)
{
  if (!cvar_AiAgentsAsyncScoring || m_DueAgents.IsEmpty())
    return; // synchronous fallback scores in UpdateApply instead

  const plTime tStart = plTime::Now();
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  plTaskSystem::ParallelForIndexed(0u, m_DueAgents.GetCount(), [this, now](plUInt32 uiFirst, plUInt32 uiEnd) {
    for (plUInt32 i = uiFirst; i < uiEnd; ++i)
    {
      ScoreAgent(m_Agents[m_DueAgents[i]], now);
    }
  });

  m_UpdateTimeThisFrame += plTime::Now() - tStart;
}

void plAiBrainWorldModule::UpdateApply(const UpdateContext& context)
{
  const plTime tStart = plTime::Now();
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

    LogicCallScope logicScope(*this, uiSlot);
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

    LogicCallScope logicScope(*this, uiSlot);
    ExecuteActiveBehavior(agent, *pComponent, tDiff);
  }

  m_FrameStats.m_UpdateTime = m_UpdateTimeThisFrame + (plTime::Now() - tStart);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiBrainWorldModule);
