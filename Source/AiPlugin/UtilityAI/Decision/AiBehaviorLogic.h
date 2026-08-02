#pragma once

#include <AiPlugin/UtilityAI/Decision/AiScoring.h>
#include <Core/Utils/Blackboard.h>
#include <Foundation/Types/SharedPtr.h>
#include <Foundation/Types/UniquePtr.h>
#include <GameEngine/StateMachine/StateMachineResource.h>

class plGameObject;
class plWorld;
class plAiActionQueue;
class plAiAgentComponent;
class plStateMachineInstance;

/// \brief Everything a behavior logic may access while executing on an agent.
///
/// Behavior logic always executes on the main thread (PostAsync world update phase),
/// so it is safe to modify the owner object, components and the blackboard from here.
struct PL_AIPLUGIN_DLL plAiAgentContext
{
  plGameObject* m_pOwner = nullptr;
  plAiAgentComponent* m_pAgentComponent = nullptr;
  plWorld* m_pWorld = nullptr;
  plSharedPtr<plBlackboard> m_pBlackboard;
  const plAiPerceivedTarget* m_pTarget = nullptr; ///< the target the behavior was selected for, if any
  plAiActionQueue* m_pActionQueue = nullptr;
  plTime m_TimeStep;
};

/// \brief Executes a selected behavior on an agent.
///
/// The utility layer decides WHICH behavior runs; a logic instance decides WHAT the behavior does.
/// Logic classes are reflected so that behavior assets can select a logic type and configure its
/// properties. The instance stored in the behavior resource acts as an immutable prototype;
/// each agent activating the behavior receives its own clone, so logic implementations may keep
/// per-activation state in member variables.
class PL_AIPLUGIN_DLL plAiBehaviorLogic : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiBehaviorLogic, plReflectedClass);

public:
  enum class State
  {
    Running, ///< keep executing
    Locked,  ///< keep executing AND do not allow switching to another behavior right now
    Done,    ///< behavior finished successfully, the agent should pick something new
    Failed,  ///< behavior cannot continue, the agent should pick something new
  };

  plAiBehaviorLogic() = default;
  virtual ~plAiBehaviorLogic() = default;

  /// \brief Called once when the behavior becomes the agent's active behavior.
  virtual void Activate(plAiAgentContext& ref_context) = 0;

  /// \brief Called when the behavior stays active but its target changed.
  virtual void Reactivate(plAiAgentContext& ref_context) {}

  /// \brief Called every execution tick while active.
  virtual State Update(plAiAgentContext& ref_context) = 0;

  /// \brief Called when the behavior stops being the active behavior.
  /// \param bInterrupted true when another behavior took over, false when this logic reported Done/Failed.
  virtual void Deactivate(plAiAgentContext& ref_context, bool bInterrupted) {}
};

/// \brief Behavior logic that runs a state machine asset.
///
/// This is the default execution model for data-driven behaviors: the utility layer picks the
/// behavior, the state machine asset (edited in the existing state machine editor) describes what
/// the behavior does.
///
/// Communication happens through the agent's blackboard:
/// - before activation the brain module writes 'Ai_TargetPosition' / 'Ai_TargetObject' entries
/// - the state machine signals completion by setting 'Ai_BehaviorDone' or 'Ai_BehaviorFailed' to true
/// - while 'Ai_LockBehavior' is true, the utility layer will not switch to another behavior
class PL_AIPLUGIN_DLL plAiBehaviorLogic_StateMachine : public plAiBehaviorLogic
{
  PL_ADD_DYNAMIC_REFLECTION(plAiBehaviorLogic_StateMachine, plAiBehaviorLogic);

public:
  plAiBehaviorLogic_StateMachine();
  ~plAiBehaviorLogic_StateMachine();

  virtual void Activate(plAiAgentContext& ref_context) override;
  virtual void Reactivate(plAiAgentContext& ref_context) override;
  virtual State Update(plAiAgentContext& ref_context) override;
  virtual void Deactivate(plAiAgentContext& ref_context, bool bInterrupted) override;

  void SetStateMachineFile(const char* szFile); // [ property ]
  const char* GetStateMachineFile() const;      // [ property ]

  plStateMachineResourceHandle m_hStateMachine;

private:
  plUniquePtr<plStateMachineInstance> m_pInstance;
};

/// \brief Base class for C++-implemented behaviors that queue plAiActions.
///
/// Derive from this in game code, override QueueActions() and fill the queue. The queue is
/// executed by the agent; when it runs empty the behavior reports Done.
class PL_AIPLUGIN_DLL plAiBehaviorLogic_ActionQueue : public plAiBehaviorLogic
{
  PL_ADD_DYNAMIC_REFLECTION(plAiBehaviorLogic_ActionQueue, plAiBehaviorLogic);

public:
  virtual void Activate(plAiAgentContext& ref_context) override;
  virtual void Reactivate(plAiAgentContext& ref_context) override;
  virtual State Update(plAiAgentContext& ref_context) override;
  virtual void Deactivate(plAiAgentContext& ref_context, bool bInterrupted) override;

protected:
  /// \brief Override to fill the agent's action queue. Called on activation and reactivation.
  virtual void QueueActions(plAiAgentContext& ref_context) = 0;
};
