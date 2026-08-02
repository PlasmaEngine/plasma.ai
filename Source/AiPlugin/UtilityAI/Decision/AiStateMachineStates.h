#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/UtilityAI/Framework/AiActionQueue.h>
#include <GameEngine/Gameplay/BlackboardComponent.h>
#include <GameEngine/StateMachine/StateMachine.h>

/// \brief State machine state that drives the owner's plAiNavigationComponent towards a position
/// stored on the blackboard.
///
/// Reads the target position (plVec3) from the blackboard entry named 'TargetPositionEntry'
/// (by default 'Ai_TargetPosition', which the AI brain writes for the selected target).
///
/// Progress is reported to the blackboard entry named 'ResultEntry' as an integer:
/// 0 = navigating, 1 = arrived, 2 = failed. Transitions can react via blackboard conditions.
/// If 'EndBehaviorWhenDone' is set (default), the state also raises 'Ai_BehaviorDone' /
/// 'Ai_BehaviorFailed' so single-state utility behaviors finish without extra wiring.
class PL_AIPLUGIN_DLL plStateMachineState_AiNavigateTo : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiNavigateTo, plStateMachineState);

public:
  plStateMachineState_AiNavigateTo(plStringView sName = plStringView());
  ~plStateMachineState_AiNavigateTo();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;
  virtual void OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const override;
  virtual void Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  virtual bool GetInstanceDataDesc(plInstanceDataDesc& out_desc) override;

  plHashedString m_sTargetPositionEntry;
  plHashedString m_sResultEntry;
  float m_fSpeed = 3.0f;
  float m_fReachedDistance = 1.0f;
  bool m_bAllowPartialPath = false;
  bool m_bEndBehaviorWhenDone = true;

private:
  struct InstanceData
  {
    bool m_bNavigationStarted = false;
    bool m_bFinished = false;
  };
};

/// \brief State machine state that writes a list of blackboard entries when entered (and optionally
/// a second list when exited).
///
/// The main use in AI behaviors is to signal 'Ai_BehaviorDone' / 'Ai_LockBehavior' or to publish
/// animation intents (e.g. 'MoveForwards') without any C++ code.
class PL_AIPLUGIN_DLL plStateMachineState_AiSetBlackboardEntries : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiSetBlackboardEntries, plStateMachineState);

public:
  plStateMachineState_AiSetBlackboardEntries(plStringView sName = plStringView());
  ~plStateMachineState_AiSetBlackboardEntries();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;
  virtual void OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  plDynamicArray<plBlackboardEntry> m_OnEnterEntries;
  plDynamicArray<plBlackboardEntry> m_OnExitEntries;
};

//////////////////////////////////////////////////////////////////////////

/// \brief State machine state that waits for a (optionally randomized) duration.
///
/// Writes 0 into 'ResultEntry' (default 'Ai_WaitResult') on enter and 1 once the time is up,
/// so transitions can leave via a blackboard condition. If 'EndBehaviorWhenDone' is set, the
/// state instead raises 'Ai_BehaviorDone' - which makes a single wait state a complete idle
/// behavior for the utility AI layer.
///
/// The duration is picked uniformly between MinDuration and MaxDuration on enter
/// (set them to the same value for a fixed wait). Randomized waits avoid the robotic look of
/// many agents idling in lockstep, which a fixed plStateMachineTransition_Timeout can't do.
class PL_AIPLUGIN_DLL plStateMachineState_AiWait : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiWait, plStateMachineState);

public:
  plStateMachineState_AiWait(plStringView sName = plStringView());
  ~plStateMachineState_AiWait();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;
  virtual void Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  virtual bool GetInstanceDataDesc(plInstanceDataDesc& out_desc) override;

  plTime m_MinDuration = plTime::MakeFromSeconds(1.0);
  plTime m_MaxDuration = plTime::MakeFromSeconds(1.0);
  plHashedString m_sResultEntry;
  bool m_bEndBehaviorWhenDone = false;

private:
  struct InstanceData
  {
    plTime m_Remaining;
    bool m_bFinished = false;
  };
};

//////////////////////////////////////////////////////////////////////////

/// \brief How plStateMachineState_AiPickPatrolPoint chooses the next patrol position.
struct PL_AIPLUGIN_DLL plAiPatrolPointMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    Waypoints,        ///< cycle the children of a route object, resolved via its global key or name
    RandomAroundHome, ///< random navmesh point around the position the agent had when the state first ran
    Spline,           ///< sample along the plSplineComponent of the route object, advancing StepDistance per pick

    Default = Waypoints
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiPatrolPointMode);

/// \brief State machine state that picks the next patrol position and writes it to the blackboard.
///
/// Waypoints mode: the route is an object with a global key; its children are visited in order
/// (the current index is per state machine instance). RandomAroundHome mode: picks a random
/// navmesh point (via the owner's plAiNavigationComponent) around the agent's home position.
///
/// Writes the position into 'TargetEntry' (default 'Ai_PatrolTarget') and 1 into 'ResultEntry'
/// (default 'Ai_PatrolResult') on success, 2 on failure (missing route, empty route, no navmesh
/// point found - e.g. the navmesh sector is not generated yet, in which case retrying later works).
class PL_AIPLUGIN_DLL plStateMachineState_AiPickPatrolPoint : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiPickPatrolPoint, plStateMachineState);

public:
  plStateMachineState_AiPickPatrolPoint(plStringView sName = plStringView());
  ~plStateMachineState_AiPickPatrolPoint();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  virtual bool GetInstanceDataDesc(plInstanceDataDesc& out_desc) override;

  plEnum<plAiPatrolPointMode> m_Mode;
  plHashedString m_sTargetEntry;
  plHashedString m_sResultEntry;
  plString m_sRouteGlobalKey;    ///< Waypoints/Spline mode: global key OR object name of the route object
  float m_fRadius = 10.0f;       ///< RandomAroundHome mode: how far away from home points may be
  float m_fStepDistance = 5.0f;  ///< Spline mode: how far along the spline each pick advances

private:
  struct InstanceData
  {
    plUInt32 m_uiNextWaypoint = 0;
    bool m_bHomeCaptured = false;
    bool m_bRouteSearched = false;
    plInt8 m_iSplineDirection = 1; ///< open splines ping-pong: +1 forward, -1 backward
    float m_fSplineDistance = 0.0f;
    plVec3 m_vHomePosition = plVec3::MakeZero();
    plGameObjectHandle m_hRoute; ///< resolved once (global key, then object name), cached
  };

  plGameObject* ResolveRouteObject(plWorld* pWorld, InstanceData* pData) const;
};

//////////////////////////////////////////////////////////////////////////

/// \brief Base class for descriptors that create plAiActions for the plStateMachineState_AiRunActions state.
class PL_AIPLUGIN_DLL plAiActionDesc : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiActionDesc, plReflectedClass);

public:
  /// \brief Creates the pooled action instance. Ownership goes to the queue.
  virtual plAiAction* CreateAction(plStateMachineInstance& ref_instance) const = 0;
};

/// \brief Waits for a fixed duration.
class PL_AIPLUGIN_DLL plAiActionDesc_Wait : public plAiActionDesc
{
  PL_ADD_DYNAMIC_REFLECTION(plAiActionDesc_Wait, plAiActionDesc);

public:
  virtual plAiAction* CreateAction(plStateMachineInstance& ref_instance) const override;

  plTime m_Duration;
};

/// \brief Turns the owner towards the position stored in a blackboard entry.
class PL_AIPLUGIN_DLL plAiActionDesc_TurnTowardsTarget : public plAiActionDesc
{
  PL_ADD_DYNAMIC_REFLECTION(plAiActionDesc_TurnTowardsTarget, plAiActionDesc);

public:
  virtual plAiAction* CreateAction(plStateMachineInstance& ref_instance) const override;

  plHashedString m_sTargetPositionEntry;
  plAngle m_TurnAnglesPerSec = plAngle::MakeFromDegree(180);
};

/// \brief Writes a log message (a stand-in for barks/quips during prototyping).
class PL_AIPLUGIN_DLL plAiActionDesc_Log : public plAiActionDesc
{
  PL_ADD_DYNAMIC_REFLECTION(plAiActionDesc_Log, plAiActionDesc);

public:
  virtual plAiAction* CreateAction(plStateMachineInstance& ref_instance) const override;

  plString m_sText;
};

/// \brief Triggers the plSpawnComponent on the named child object (e.g. a muzzle spawning a projectile).
class PL_AIPLUGIN_DLL plAiActionDesc_Spawn : public plAiActionDesc
{
  PL_ADD_DYNAMIC_REFLECTION(plAiActionDesc_Spawn, plAiActionDesc);

public:
  virtual plAiAction* CreateAction(plStateMachineInstance& ref_instance) const override;

  plHashedString m_sChildObjectName;
};

/// \brief State machine state that runs a sequence of plAiActions and finishes when the queue is empty.
///
/// Bridges the C++ action building blocks into state machine assets. Writes 1 into 'ResultEntry'
/// when all actions finished, 2 if an action failed.
class PL_AIPLUGIN_DLL plStateMachineState_AiRunActions : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiRunActions, plStateMachineState);

public:
  plStateMachineState_AiRunActions(plStringView sName = plStringView());
  ~plStateMachineState_AiRunActions();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;
  virtual void OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const override;
  virtual void Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  virtual bool GetInstanceDataDesc(plInstanceDataDesc& out_desc) override;

  plHashedString m_sResultEntry;
  bool m_bEndBehaviorWhenDone = false;

  plSmallArray<plAiActionDesc*, 2> m_Actions; // owned, same pattern as plStateMachineState_Compound::m_SubStates

private:
  struct InstanceData
  {
    plAiActionQueue m_Queue;
    bool m_bFinished = false;
  };
};
