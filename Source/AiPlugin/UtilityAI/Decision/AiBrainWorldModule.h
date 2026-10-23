#pragma once

#include <AiPlugin/UtilityAI/Decision/AiArchetypeResource.h>
#include <AiPlugin/UtilityAI/Decision/AiDebugSnapshot.h>
#include <AiPlugin/UtilityAI/Framework/AiActionQueue.h>
#include <Core/World/WorldModule.h>
#include <Foundation/Containers/Deque.h>
#include <Foundation/Containers/StaticRingBuffer.h>

class plAiAgentComponent;
class plAiBehaviorLogic;
class plAiPerceptionWorldModule;

/// \brief Why an agent decides before its next scheduled decision.
struct PL_AIPLUGIN_DLL plAiWakeReason
{
  using StorageType = plUInt8;

  enum Enum
  {
    Script = PL_BIT(0),           ///< RequestDecision
    Command = PL_BIT(1),          ///< a command was issued or cancelled
    Stimulus = PL_BIT(2),         ///< the agent perceived a stimulus
    BehaviorEnded = PL_BIT(3),    ///< the active behavior reported Done or Failed
    BehaviorSettings = PL_BIT(4), ///< a behavior was enabled, disabled or reweighted

    Default = Script
  };

  struct Bits
  {
    StorageType Script : 1;
    StorageType Command : 1;
    StorageType Stimulus : 1;
    StorageType BehaviorEnded : 1;
    StorageType BehaviorSettings : 1;
  };
};

PL_DECLARE_FLAGS_OPERATORS(plAiWakeReason);

/// \brief Central driver of all utility AI agents in a world.
///
/// Owns the per-agent decision state (behavior sets, perceived targets, action queue, history) and
/// updates all agents from one place instead of per-component updates. Decisions (utility scoring)
/// are bucketed round-robin and bounded by AI.Agents.MaxDecisionsPerFrame; behavior execution runs
/// every frame.
///
/// Threading contract (prepared for the async scoring phase):
/// - UpdateApply (PostAsync) is the only place that touches game objects, components, blackboards
///   and behavior logic.
/// - Utility scoring itself only reads plAiScoringContext. Once scoring moves to the Async phase,
///   contexts are filled from snapshots in PreAsync and the parallel pass never accesses the world.
class PL_AIPLUGIN_DLL plAiBrainWorldModule final : public plWorldModule
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiBrainWorldModule, plWorldModule);

  // exported classes get all implicit members generated - the copy members would require
  // copying Agents, which is forbidden (plAiActionQueue owns raw pointers)
  PL_DISALLOW_COPY_AND_ASSIGN(plAiBrainWorldModule);

public:
  plAiBrainWorldModule(plWorld* pWorld);
  ~plAiBrainWorldModule();

  virtual void Initialize() override;
  virtual void Deinitialize() override;

  /// \brief Called by plAiAgentComponent when it starts simulating. Returns the agent slot index.
  plUInt32 RegisterAgent(plAiAgentComponent* pComponent);

  /// \brief Called by plAiAgentComponent on deactivation.
  void UnregisterAgent(plUInt32 uiAgentIndex);

  /// \brief Called by plAiLodCenterComponent to mark a relevance center (player, camera).
  void RegisterLodCenter(const plComponentHandle& hComponent);
  void UnregisterLodCenter(const plComponentHandle& hComponent);

  /// \brief AI update LOD tiers, assigned by distance to the nearest LOD center.
  enum class LodTier : plUInt8
  {
    Hot = 0,  ///< full decision rate, execution every frame
    Warm = 1, ///< reduced decision rate, execution every 2nd frame
    Cold = 2, ///< low decision rate, execution every 4th frame
  };

  /// \brief One entry of the per-agent behavior history (for debugging): a switch, or a behavior ending.
  struct HistoryEntry
  {
    plTime m_Time;
    plHashedString m_sFrom;
    plHashedString m_sTo;        ///< empty when the behavior ended without a successor
    plUInt8 m_uiToCategory = 0; ///< plAiBehaviorCategory of m_sTo
    float m_fWinnerScore = 0.0f;
    float m_fRunnerUpScore = 0.0f;
    plAiDecisionOutcome m_Outcome = plAiDecisionOutcome::None;
  };

  /// \name Debugging tools
  ///
  /// Read-only views for the AI debugger and overlays. Watching an agent records detailed traces of its
  /// decisions but never changes them. Main thread only.
  ///@{

  /// \brief Starts or stops recording detailed decision snapshots for an agent.
  void SetAgentWatched(const plComponentHandle& hAgent, bool bWatched);

  /// \brief The latest decision snapshot of a watched agent. False when the agent isn't watched or hasn't decided yet.
  bool GetAgentDebugSnapshot(const plComponentHandle& hAgent, plAiAgentDebugSnapshot& out_snapshot) const;

  /// \brief The agent's behavior history, oldest first. False for unknown agents.
  bool GetAgentHistory(const plComponentHandle& hAgent, plDynamicArray<HistoryEntry>& out_history) const;

  /// \brief One summary per registered agent.
  void GetAgentSummaries(plDynamicArray<plAiAgentDebugSummary>& out_agents) const;

  /// \brief What the last update did.
  const plAiBrainFrameStats& GetFrameStats() const { return m_FrameStats; }

  ///@}

  /// \brief Schedules a decision for the agent in the given slot as soon as its wake spacing allows.
  void RequestDecision(plUInt32 uiSlot, plBitflags<plAiWakeReason> reason);

private:
  friend class plAiAgentComponent;

  /// Per-agent state of one behavior from the archetype's behavior set.
  struct BehaviorState
  {
    plAiBehaviorResourceHandle m_hResource;
    plUInt32 m_uiResourceChangeCounter = 0;
    float m_fWeightScale = 1.0f; ///< from the archetype
    plTime m_LastActivation = plTime::MakeZero();
    plTime m_LastDeactivation = plTime::MakeFromSeconds(-1000000.0);

    // set through plAiAgentComponent
    bool m_bEnabled = true;
    float m_fWeightOverride = 1.0f;
    plTime m_WeightOverrideEnd; ///< zero = until changed
  };

  /// Per-behavior inputs of one scoring pass, captured on the main thread.
  struct BehaviorSnapshot
  {
    plTime m_SinceActivation;
    plTime m_SinceDeactivation;
    float m_fWeight = 1.0f; ///< archetype weight scale times the override
    bool m_bEnabled = true;
  };

  /// The archetype settings the brain reads every decision, cached at configuration.
  struct Reactivity
  {
    bool m_bWakeOnStimulus = true;
    bool m_bWakeOnBehaviorEnd = true;
    plTime m_MinWakeSpacing;
    float m_fTargetSwitchMargin = 0.0f;
    plTime m_TargetMinHold;
    plTime m_MaxLockDuration;
    plUInt8 m_uiScoringVersion = 1;
  };

  /// A command issued through plAiAgentComponent::IssueCommand, mirrored to the blackboard.
  struct Command
  {
    plHashedString m_sName;
    plVec3 m_vPosition = plVec3::MakeZero();
    plGameObjectHandle m_hTarget;
    plTime m_Expiry; ///< zero = no expiry
    bool m_bActive = false;
    bool m_bDirty = false; ///< changed since the blackboard was last written
  };

  /// Result of one scoring pass, written by the (potentially parallel) scoring step,
  /// consumed by the main-thread apply step.
  struct Decision
  {
    plInt32 m_iBehavior = -1;
    plInt32 m_iTarget = -1;
    float m_fScore = 0.0f;
    float m_fRunnerUpScore = 0.0f;
    float m_fActiveScore = 0.0f; ///< what the currently active (behavior, target) pair scored this round
  };

  struct Agent
  {
    ~Agent();

    // memory-corruption tripwires: validated on every access, poisoned on destruction.
    // catches stray writes into agent memory and double-destruction at the point of damage
    // instead of much later during world teardown.
    plUInt32 m_uiCanaryFront = 0xA1C0FFEE;

    bool m_bInUse = false;
    bool m_bConfigured = false;
    bool m_bLocked = false;          ///< the active behavior's lock is in effect
    bool m_bLockRequested = false;   ///< the active behavior reports Locked, possibly past the lock timeout
    bool m_bShutdownPending = false; ///< unregistered while its logic was running; freed when the call returns
    plUInt16 m_uiLogicCallDepth = 0; ///< > 0 while behavior logic or actions of this agent are on the stack
    plComponentHandle m_hComponent;

    plAiArchetypeResourceHandle m_hArchetype;
    plUInt32 m_uiArchetypeChangeCounter = 0;

    plSharedPtr<plBlackboard> m_pBlackboard;
    plHashedString m_sTeam;
    plHybridArray<BehaviorState, 8> m_Behaviors;

    plInt32 m_iActiveBehavior = -1;
    plAiBehaviorLogic* m_pActiveLogic = nullptr; ///< clone of the resource's logic prototype, RTTI-allocated
    plInt32 m_iActiveTarget = -1;                ///< index into m_Targets, -1 = target-less
    float m_fActiveScore = 0.0f;
    float m_fActiveCommitBonus = 0.0f;
    plTime m_TargetSince; ///< when the active target was chosen
    plTime m_LockedSince; ///< when the active behavior last started reporting Locked

    Reactivity m_Reactivity;
    Command m_Command;
    plTime m_LastDecision;
    plTime m_LastPerception;
    plUInt8 m_uiWakeReasons = 0;        ///< plAiWakeReason bits of a pending woken decision
    float m_fMaxSenseRangeScale = 1.0f; ///< largest range scale of the enabled stimulus senses

    plHybridArray<plAiPerceivedTarget, 4> m_Targets;
    plUInt8 m_uiAlertLevel = 0;            ///< plAiAlertLevel, updated with perception
    plUInt32 m_uiCanaryQueue = 0xBEEFCAFE; ///< directly guards the action queue header
    plAiActionQueue m_ActionQueue;

    LodTier m_LodTier = LodTier::Hot;
    plTime m_NextDecision;
    plTime m_LastExecution;
    plUInt64 m_uiLastStimulusSequence = 0; ///< newest perception stimulus this agent has processed
    plStaticRingBuffer<HistoryEntry, 64> m_History;

    // ---- debugging tools ----
    plUInt32 m_uiDecisionCount = 0;
    plAiDecisionOutcome m_LastOutcome = plAiDecisionOutcome::None;
    plUniquePtr<plAiAgentDebugSnapshot> m_pDebugSnapshot; ///< only for watched agents; written by ScoreAgent

    // ---- scoring snapshot: filled in UpdateGather (PreAsync), read-only during UpdateScore (Async) ----
    // The parallel scoring pass must not touch the world, live blackboards or resources; everything it
    // needs is captured here. The descriptor pointers stay valid because the behavior resource handles
    // keep the resources alive and resource reloads never run during the world update.
    plVec3 m_vSnapshotPosition = plVec3::MakeZero();
    plQuat m_qSnapshotRotation = plQuat::MakeIdentity();
    plHashTable<plHashedString, plVariant> m_BlackboardSnapshot;
    plHybridArray<const plAiBehaviorResourceDescriptor*, 8> m_ScoringDescriptors; ///< parallel to m_Behaviors, nullptr = not loaded
    plHybridArray<BehaviorSnapshot, 8> m_BehaviorSnapshots;                       ///< parallel to m_Behaviors
    plTime m_ActiveTargetHeld;                                                    ///< how long the active target has been held
    Decision m_Decision;
  };

  void UpdateGather(const UpdateContext& context); // PreAsync: perception, LOD, snapshots
  void UpdateScore(const UpdateContext& context);  // Async: parallel utility scoring on snapshots
  void UpdateApply(const UpdateContext& context);  // PostAsync: behavior switches + execution
  void UpdateDebug(const UpdateContext& context);  // PostTransform: overlays

  /// Brackets calls that may run behavior logic or actions of one agent. Logic may delete the agent's
  /// owner; the resulting UnregisterAgent is then deferred until the outermost scope ends.
  class LogicCallScope
  {
  public:
    LogicCallScope(plAiBrainWorldModule& ref_module, plUInt32 uiSlot);
    ~LogicCallScope();

  private:
    plAiBrainWorldModule& m_Module;
    plUInt32 m_uiSlot;
  };

  void ConfigureAgent(Agent& ref_agent, plAiAgentComponent& ref_component);
  void ShutdownAgent(Agent& ref_agent);
  void FreeAgentSlot(plUInt32 uiSlot);
  void ResetAgent(Agent& ref_agent); ///< resets all fields; Agents are never copied or moved (plAiActionQueue owns raw pointers)

  static void ValidateAgent(const Agent& agent); ///< asserts on the corruption tripwires

  bool GatherAgent(plUInt32 uiSlot, plTime now); ///< prepares a decision; true when it used decision budget
  void UpdateCommandAndOverrides(Agent& ref_agent, plTime now);
  void UpdatePerception(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now);
  void GatherSight(Agent& ref_agent, plAiAgentComponent& ref_component, const plAiArchetypeResourceDescriptor& archetype, plTime now, plTime tDiff);
  void GatherStimuli(Agent& ref_agent, plAiAgentComponent& ref_component, const plAiArchetypeResourceDescriptor& archetype, plTime now);
  static plAiPerceivedTarget& FindOrCreateTarget(plHybridArray<plAiPerceivedTarget, 4>& ref_targets, const plGameObjectHandle& hTarget, const plVec3& vEventPosition);
  static void ForgetTargets(plHybridArray<plAiPerceivedTarget, 4>& ref_targets, plInt32& ref_iActiveTarget, const plAiArchetypeResourceDescriptor& archetype, plTime now);
  static plHashedString GetTeamOf(const plGameObject* pObject);

  // plAiAgentComponent's script API
  void SetBehaviorEnabled(plUInt32 uiSlot, plStringView sBehavior, bool bEnabled);
  void SetBehaviorWeight(plUInt32 uiSlot, plStringView sBehavior, float fScale, plTime duration);
  void IssueCommand(plUInt32 uiSlot, plStringView sCommand, const plVec3& vPosition, const plGameObjectHandle& hTarget, plTime expiry);
  void CancelCommand(plUInt32 uiSlot);
  plHashedString GetActiveBehaviorName(plUInt32 uiSlot) const;
  plGameObjectHandle GetActiveTarget(plUInt32 uiSlot) const;
  BehaviorState* FindBehaviorState(plUInt32 uiSlot, plStringView sBehavior);
  void WakeAgentsForNewStimuli();
  void WakeAgentsForExpiredOrders(plTime now); ///< commands and weight overrides that ran out
  void BuildScoringSnapshot(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now);
  static void ScoreAgent(Agent& ref_agent, plTime now); ///< pure function of the snapshot, thread-safe
  void ApplyDecision(Agent& ref_agent, plAiAgentComponent& ref_component);
  void ExecuteActiveBehavior(Agent& ref_agent, plAiAgentComponent& ref_component, plTime tDiff);
  void UpdateLock(Agent& ref_agent, bool bLockRequested);
  void SwitchBehavior(Agent& ref_agent, plAiAgentComponent& ref_component, plInt32 iNewBehavior, plInt32 iNewTarget, float fScore, float fRunnerUpScore, plAiDecisionOutcome outcome);
  void DeactivateBehavior(Agent& ref_agent, plAiAgentComponent& ref_component, bool bInterrupted);
  void RecordHistory(Agent& ref_agent, const plHashedString& sFrom, const plHashedString& sTo, float fWinnerScore, float fRunnerUpScore, plAiDecisionOutcome outcome, plUInt8 uiToCategory = 0);
  static void TraceDecision(Agent& ref_agent, plAiScoringContext context); ///< fills the debug snapshot; same math as ScoreAgent
  plAiDecisionOutcome ResolveDecision(Agent& ref_agent, plAiAgentComponent& ref_component);
  const Agent* FindAgent(const plComponentHandle& hAgent) const;
  void PublishTargetToBlackboard(Agent& ref_agent);
  void DrawAgentDebugInfo(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now);

  LodTier DetermineLodTier(const plVec3& vAgentPosition) const;
  plTime GetDecisionInterval(LodTier tier) const;

  plAiAgentComponent* ResolveComponent(Agent& ref_agent);

  /// The behavior's display name, or its resource ID when the asset has none. Locks the resource: main thread only.
  static plHashedString GetBehaviorName(const plAiBehaviorResourceHandle& hBehavior);
  static plUInt8 GetBehaviorCategory(const plAiBehaviorResourceHandle& hBehavior); ///< plAiBehaviorCategory, Default while not loaded

  plDeque<Agent> m_Agents; // stable adresses, slots are reused via free list
  plDynamicArray<plUInt32> m_FreeAgentSlots;
  plDynamicArray<plUInt32> m_ActiveAgents;
  plDynamicArray<plUInt32> m_DueAgents; ///< slots snapshotted this frame, to be scored and applied
  plDynamicArray<plUInt32> m_WokenAgents; ///< slots with a pending woken decision, served before the round robin
  plUInt32 m_uiDecisionCursor = 0;
  plUInt32 m_uiFrameCounter = 0;

  plDynamicArray<plComponentHandle> m_LodCenters;
  plDynamicArray<plVec3> m_LodCenterPositions; // gathered once per frame

  plAiPerceptionWorldModule* m_pPerceptionModule = nullptr;
  plUInt64 m_uiLastWakeStimulus = 0; ///< newest stimulus already checked for wakes

  plAiBrainFrameStats m_FrameStats;
  plTime m_UpdateTimeThisFrame; ///< accumulated over the update phases, published at the end of UpdateApply
};
