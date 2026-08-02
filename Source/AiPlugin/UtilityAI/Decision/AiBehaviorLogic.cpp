#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiBehaviorLogic.h>
#include <AiPlugin/UtilityAI/Framework/AiActionQueue.h>
#include <Core/Utils/Blackboard.h>
#include <GameEngine/StateMachine/StateMachine.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiBehaviorLogic, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiBehaviorLogic_StateMachine, 1, plRTTIDefaultAllocator<plAiBehaviorLogic_StateMachine>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("StateMachine", GetStateMachineFile, SetStateMachineFile)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_StateMachine", plDependencyFlags::Package)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiBehaviorLogic_ActionQueue, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

namespace
{
  static const plTempHashedString s_sAiBehaviorDone("Ai_BehaviorDone");
  static const plTempHashedString s_sAiBehaviorFailed("Ai_BehaviorFailed");
  static const plTempHashedString s_sAiLockBehavior("Ai_LockBehavior");
} // namespace

plAiBehaviorLogic_StateMachine::plAiBehaviorLogic_StateMachine() = default;
plAiBehaviorLogic_StateMachine::~plAiBehaviorLogic_StateMachine() = default;

void plAiBehaviorLogic_StateMachine::SetStateMachineFile(const char* szFile)
{
  plStateMachineResourceHandle hResource;

  if (!plStringUtils::IsNullOrEmpty(szFile))
  {
    hResource = plResourceManager::LoadResource<plStateMachineResource>(szFile);
    plResourceManager::PreloadResource(hResource);
  }

  m_hStateMachine = hResource;
}

const char* plAiBehaviorLogic_StateMachine::GetStateMachineFile() const
{
  if (!m_hStateMachine.IsValid())
    return "";

  return m_hStateMachine.GetResourceID();
}

void plAiBehaviorLogic_StateMachine::Activate(plAiAgentContext& ref_context)
{
  if (ref_context.m_pBlackboard != nullptr)
  {
    ref_context.m_pBlackboard->SetEntryValue("Ai_BehaviorDone", false);
    ref_context.m_pBlackboard->SetEntryValue("Ai_BehaviorFailed", false);
    ref_context.m_pBlackboard->SetEntryValue("Ai_LockBehavior", false);
  }

  if (!m_hStateMachine.IsValid())
  {
    plLog::Error("plAiBehaviorLogic_StateMachine has no state machine asset assigned.");
    return;
  }

  plResourceLock<plStateMachineResource> pStateMachine(m_hStateMachine, plResourceAcquireMode::BlockTillLoaded_NeverFail);
  if (pStateMachine.GetAcquireResult() != plResourceAcquireResult::Final)
  {
    plLog::Error("Failed to load AI behavior state machine '{}'", GetStateMachineFile());
    return;
  }

  m_pInstance = pStateMachine->CreateInstance(*ref_context.m_pAgentComponent);
  m_pInstance->SetBlackboard(ref_context.m_pBlackboard);
  m_pInstance->SetStateOrFallback(plHashedString(), 0).IgnoreResult();
}

void plAiBehaviorLogic_StateMachine::Reactivate(plAiAgentContext& ref_context)
{
  // restart the state machine so the behavior reacts to the new target
  Deactivate(ref_context, true);
  Activate(ref_context);
}

plAiBehaviorLogic::State plAiBehaviorLogic_StateMachine::Update(plAiAgentContext& ref_context)
{
  if (m_pInstance == nullptr)
    return State::Failed;

  m_pInstance->Update(ref_context.m_TimeStep);

  if (ref_context.m_pBlackboard != nullptr)
  {
    if (ref_context.m_pBlackboard->GetBoolValue(s_sAiBehaviorFailed))
      return State::Failed;

    if (ref_context.m_pBlackboard->GetBoolValue(s_sAiBehaviorDone))
      return State::Done;

    if (ref_context.m_pBlackboard->GetBoolValue(s_sAiLockBehavior))
      return State::Locked;
  }

  return State::Running;
}

void plAiBehaviorLogic_StateMachine::Deactivate(plAiAgentContext& ref_context, bool bInterrupted)
{
  m_pInstance.Clear();

  if (ref_context.m_pBlackboard != nullptr)
  {
    ref_context.m_pBlackboard->SetEntryValue("Ai_BehaviorDone", false);
    ref_context.m_pBlackboard->SetEntryValue("Ai_BehaviorFailed", false);
    ref_context.m_pBlackboard->SetEntryValue("Ai_LockBehavior", false);
  }
}

//////////////////////////////////////////////////////////////////////////

void plAiBehaviorLogic_ActionQueue::Activate(plAiAgentContext& ref_context)
{
  QueueActions(ref_context);
}

void plAiBehaviorLogic_ActionQueue::Reactivate(plAiAgentContext& ref_context)
{
  if (ref_context.m_pActionQueue != nullptr)
  {
    ref_context.m_pActionQueue->CancelCurrentActions(*ref_context.m_pOwner);
  }

  QueueActions(ref_context);
}

plAiBehaviorLogic::State plAiBehaviorLogic_ActionQueue::Update(plAiAgentContext& ref_context)
{
  // the agent executes the action queue itself, this logic just monitors completion
  if (ref_context.m_pActionQueue == nullptr || ref_context.m_pActionQueue->IsEmpty())
    return State::Done;

  return State::Running;
}

void plAiBehaviorLogic_ActionQueue::Deactivate(plAiAgentContext& ref_context, bool bInterrupted)
{
  if (bInterrupted && ref_context.m_pActionQueue != nullptr)
  {
    ref_context.m_pActionQueue->CancelCurrentActions(*ref_context.m_pOwner);
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiBehaviorLogic);
