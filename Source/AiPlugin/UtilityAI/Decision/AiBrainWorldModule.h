#pragma once

#include <AiPlugin/UtilityAI/Decision/AiArchetypeResource.h>
#include <AiPlugin/UtilityAI/Framework/AiActionQueue.h>
#include <Core/World/WorldModule.h>
#include <Foundation/Containers/Deque.h>
#include <Foundation/Containers/StaticRingBuffer.h>

class plAiAgentComponent;
class plAiBehaviorLogic;
class plAiPerceptionWorldModule;

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

  /// \brief One entry of the per-agent behavior switch history (for debugging).
  struct HistoryEntry
  {
    plTime m_Time;
    plHashedString m_sFrom;
    plHashedString m_sTo;
    float m_fWinnerScore = 0.0f;
    float m_fRunnerUpScore = 0.0f;
  };

private:
  friend class plAiAgentComponent;

  /// Per-agent state of one behavior from the archetype's behavior set.
  struct BehaviorState
  {
    plAiBehaviorResourceHandle m_hResource;
    plUInt32 m_uiResourceChangeCounter = 0;
    float m_fWeightScale = 1.0f;
    plTime m_LastActivation = plTime::MakeZero();
    plTime m_LastDeactivation = plTime::MakeFromSeconds(-1000000.0);
  };

  /// Per-behavior timing snapshot for one scoring pass.
  struct BehaviorTiming
  {
    plTime m_SinceActivation;
    plTime m_SinceDeactivation;
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
    bool m_bLocked = false;
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

    plHybridArray<plAiPerceivedTarget, 4> m_Targets;
    plUInt32 m_uiCanaryQueue = 0xBEEFCAFE; ///< directly guards the action queue header
    plAiActionQueue m_ActionQueue;

    LodTier m_LodTier = LodTier::Hot;
    plTime m_NextDecision;
    plTime m_LastExecution;
    plTime m_LastStimulusConsumeTime;
    plStaticRingBuffer<HistoryEntry, 32> m_History;

    // ---- scoring snapshot: filled in UpdateGather (PreAsync), read-only during UpdateScore (Async) ----
    // The parallel scoring pass must not touch the world, live blackboards or resources; everything it
    // needs is captured here. The descriptor pointers stay valid because the behavior resource handles
    // keep the resources alive and resource reloads never run during the world update.
    plVec3 m_vSnapshotPosition = plVec3::MakeZero();
    plQuat m_qSnapshotRotation = plQuat::MakeIdentity();
    plHashTable<plHashedString, plVariant> m_BlackboardSnapshot;
    plHybridArray<const plAiBehaviorResourceDescriptor*, 8> m_ScoringDescriptors; ///< parallel to m_Behaviors, nullptr = not loaded
    plHybridArray<BehaviorTiming, 8> m_BehaviorTimings;                           ///< parallel to m_Behaviors
    Decision m_Decision;
  };

  void UpdateGather(const UpdateContext& context); // PreAsync: perception, LOD, snapshots
  void UpdateScore(const UpdateContext& context);  // Async: parallel utility scoring on snapshots
  void UpdateApply(const UpdateContext& context);  // PostAsync: behavior switches + execution
  void UpdateDebug(const UpdateContext& context);  // PostTransform: overlays

  void ConfigureAgent(Agent& ref_agent, plAiAgentComponent& ref_component);
  void ShutdownAgent(Agent& ref_agent);
  void ResetAgent(Agent& ref_agent); ///< resets all fields; Agents are never copied or moved (plAiActionQueue owns raw pointers)

  static void ValidateAgent(const Agent& agent); ///< asserts on the corruption tripwires

  void UpdatePerception(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now, plTime tDiff);
  void BuildScoringSnapshot(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now);
  static void ScoreAgent(Agent& ref_agent, plTime now); ///< pure function of the snapshot, thread-safe
  void ApplyDecision(Agent& ref_agent, plAiAgentComponent& ref_component);
  void ExecuteActiveBehavior(Agent& ref_agent, plAiAgentComponent& ref_component, plTime tDiff);
  void SwitchBehavior(Agent& ref_agent, plAiAgentComponent& ref_component, plInt32 iNewBehavior, plInt32 iNewTarget, float fScore, float fRunnerUpScore);
  void DeactivateBehavior(Agent& ref_agent, plAiAgentComponent& ref_component, bool bInterrupted);
  void PublishTargetToBlackboard(Agent& ref_agent);
  void DrawAgentDebugInfo(Agent& ref_agent, plAiAgentComponent& ref_component, plTime now);

  LodTier DetermineLodTier(const plVec3& vAgentPosition) const;
  plTime GetDecisionInterval(LodTier tier) const;

  plAiAgentComponent* ResolveComponent(Agent& ref_agent);

  plDeque<Agent> m_Agents; // stable adresses, slots are reused via free list
  plDynamicArray<plUInt32> m_FreeAgentSlots;
  plDynamicArray<plUInt32> m_ActiveAgents;
  plDynamicArray<plUInt32> m_DueAgents; ///< slots snapshotted this frame, to be scored and applied
  plUInt32 m_uiDecisionCursor = 0;
  plUInt32 m_uiFrameCounter = 0;

  plDynamicArray<plComponentHandle> m_LodCenters;
  plDynamicArray<plVec3> m_LodCenterPositions; // gathered once per frame

  plAiPerceptionWorldModule* m_pPerceptionModule = nullptr;
};
