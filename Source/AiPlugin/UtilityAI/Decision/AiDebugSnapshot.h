#pragma once

#include <AiPlugin/UtilityAI/Decision/AiBehaviorResource.h>
#include <Foundation/Types/Variant.h>

/// \brief Why a behavior did not compete in a decision.
enum class plAiBehaviorVeto : plUInt8
{
  None,        ///< it competed
  NotLoaded,   ///< the behavior asset isn't loaded
  Disabled,    ///< switched off for this agent
  Cooldown,    ///< deactivated less than its cooldown ago
  NoTarget,    ///< it needs a target and the agent perceives none
  ZeroUtility, ///< a consideration scored 0
};

/// \brief How a decision treated the active behavior.
enum class plAiDecisionOutcome : plUInt8
{
  None,              ///< no decision yet
  Started,           ///< no behavior was active; the winner started
  Kept,              ///< the active behavior won again
  Retargeted,        ///< the active behavior won with a different target
  HigherCategory,    ///< a challenger from a higher category replaced it
  BeatCommitBonus,   ///< a same-category challenger beat the active score plus its commit bonus
  HeldByCommitBonus, ///< a same-category challenger scored higher, but not by more than the commit bonus
  StoppedScoring,    ///< the active behavior no longer scored and a challenger replaced it
  NothingScores,     ///< no behavior scored; the active one was stopped
  Locked,            ///< the active behavior is locked; nothing was compared
  Done,              ///< the active behavior finished
  Failed,            ///< the active behavior failed
  Disabled,          ///< the active behavior was disabled for this agent
};

PL_AIPLUGIN_DLL const char* plAiDecisionOutcomeName(plAiDecisionOutcome outcome);
PL_AIPLUGIN_DLL const char* plAiBehaviorVetoName(plAiBehaviorVeto veto);

/// \brief One behavior's result in a decision.
struct PL_AIPLUGIN_DLL plAiBehaviorTrace
{
  plHashedString m_sName;
  plUInt8 m_uiCategory = 0;
  plAiBehaviorVeto m_Veto = plAiBehaviorVeto::None;
  bool m_bActive = false;
  float m_fUtility = 0.0f;                                   ///< after weights, for the best target
  float m_fScore = 0.0f;                                     ///< category + utility; 0 when it didn't compete
  plInt32 m_iTarget = -1;                                    ///< index into the snapshot's targets, -1 = target-less
  plHybridArray<plAiConsiderationTrace, 8> m_Considerations; ///< for the best target
};

/// \brief Everything a debugging tool shows about one agent's latest decision.
///
/// Only filled for agents a tool watches (plAiBrainWorldModule::SetAgentWatched). Filling it does not
/// change the agent's decisions.
struct PL_AIPLUGIN_DLL plAiAgentDebugSnapshot
{
  plTime m_DecisionTime;          ///< when the decision was scored
  plUInt32 m_uiDecisionCount = 0; ///< increases with every scored decision of this agent
  plAiDecisionOutcome m_Outcome = plAiDecisionOutcome::None;

  plInt32 m_iActiveBehavior = -1; ///< index into m_Behaviors after the decision
  plInt32 m_iActiveTarget = -1;
  float m_fActiveScore = 0.0f;
  float m_fCommitBonus = 0.0f;
  bool m_bLocked = false;

  plInt32 m_iWinner = -1; ///< best-scoring behavior of the decision
  float m_fWinnerScore = 0.0f;
  float m_fRunnerUpScore = 0.0f;

  plUInt8 m_uiLodTier = 0; ///< plAiBrainWorldModule::LodTier
  plTime m_NextDecision;

  plHybridArray<plAiBehaviorTrace, 8> m_Behaviors; ///< in archetype order
  plHybridArray<plAiPerceivedTarget, 4> m_Targets; ///< as scored

  struct BlackboardEntry
  {
    plHashedString m_sName;
    plVariant m_Value;
  };

  plHybridArray<BlackboardEntry, 16> m_Blackboard; ///< the live blackboard when the snapshot was read
};

/// \brief One row of a debugging tool's agent list.
struct PL_AIPLUGIN_DLL plAiAgentDebugSummary
{
  plComponentHandle m_hComponent;
  plGameObjectHandle m_hOwner;
  plHashedString m_sActiveBehavior; ///< empty when idle
  plUInt8 m_uiActiveCategory = 0;   ///< plAiBehaviorCategory of the active behavior
  plGameObjectHandle m_hTarget;     ///< the active behavior's target, if any
  plUInt8 m_uiLodTier = 0;
  plUInt8 m_uiAlertLevel = 0; ///< plAiAlertLevel
  bool m_bLocked = false;
  bool m_bConfigured = false; ///< false while the archetype is missing or not loaded
  bool m_bWatched = false;
};

/// \brief What the brain did in its last update, for status displays.
struct PL_AIPLUGIN_DLL plAiBrainFrameStats
{
  plUInt32 m_uiAgents = 0;
  plUInt32 m_uiDecisions = 0;      ///< decisions prepared this frame, scheduled and woken
  plUInt32 m_uiWokenDecisions = 0; ///< of those, woken early
  plUInt32 m_uiDecisionBudget = 0; ///< AI.Agents.MaxDecisionsPerFrame
  plTime m_UpdateTime;             ///< time spent in the gather, score and apply updates
};
