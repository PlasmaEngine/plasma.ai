#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/Declarations.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Strings/HashedString.h>
#include <Foundation/Time/Time.h>
#include <Foundation/Types/Variant.h>

class plBlackboard;

/// \brief Priority category for utility AI behaviors.
///
/// The category is the dominant part of a behavior's final score: a behavior in a higher category
/// always wins against one in a lower category, no matter their utility values.
/// This mirrors the priority ladder of the legacy plAiScoreCategory.
struct PL_AIPLUGIN_DLL plAiBehaviorCategory
{
  using StorageType = plUInt8;

  enum Enum
  {
    Fallback,    ///< if really nothing else is available
    Idle,        ///< idle actions (simple animation playback and such)
    ActiveIdle,  ///< the main actions to do when an NPC is idle, e.g. wander around, follow a patrol path
    Investigate, ///< in case something interesting is detected
    Command,     ///< things the player (or level designer) instructs the NPC to do that should have high priority
    Combat,      ///< combat related behavior
    Interrupt,   ///< things that even override combat scenarios (hit reactions, falling down, etc)

    Default = ActiveIdle
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiBehaviorCategory);

/// \brief A target that an AI agent knows about, with memory semantics.
///
/// In Phase A1 these records are fed from plSensorComponent detections.
/// The perception world module (Phase A2) extends them with stimulus-driven
/// confidence gain/decay and last-known-position behavior.
struct PL_AIPLUGIN_DLL plAiPerceivedTarget
{
  plGameObjectHandle m_hTarget;
  plVec3 m_vLastKnownPosition = plVec3::MakeZero();
  plVec3 m_vLastKnownVelocity = plVec3::MakeZero();
  float m_fConfidence = 0.0f;  ///< 0 = about to be forgotten, 1 = fully perceived right now
  plTime m_LastStimulusTime;   ///< world time at which the last stimulus for this target arrived
};

/// \brief All data an plAiInput may read while scoring a behavior.
///
/// IMPORTANT (threading contract, see plAiBrainWorldModule): when scoring runs in the async world
/// update phase, inputs may ONLY read from this context. The blackboard pointer is then null and
/// blackboard reads must go through the snapshot table instead. Inputs must never follow
/// plGameObject pointers or other world memory.
struct PL_AIPLUGIN_DLL plAiScoringContext
{
  plVec3 m_vOwnerPosition = plVec3::MakeZero();
  plQuat m_qOwnerRotation = plQuat::MakeIdentity();
  plTime m_Now;

  /// Target candidate this scoring pass runs against. Null for target-less behaviors.
  const plAiPerceivedTarget* m_pTarget = nullptr;

  /// Live blackboard. Only set when scoring on the main thread.
  const plBlackboard* m_pBlackboard = nullptr;

  /// Snapshot of blackboard entries taken in the PreAsync phase. Set when scoring in the async phase.
  const plHashTable<plHashedString, plVariant>* m_pBlackboardSnapshot = nullptr;

  /// How long the scored behavior has been active on this agent. Zero if it is not the active behavior.
  plTime m_TimeSinceActivation;

  /// How long ago the scored behavior was last deactivated on this agent. Very large if never.
  plTime m_TimeSinceDeactivation;

  /// \brief Reads a blackboard entry, transparently using the snapshot when scoring asynchronously.
  plVariant GetBlackboardValue(const plHashedString& sEntryName) const;
};

/// \brief Final score of one (behavior, target) pair.
///
/// The float encoding keeps the legacy convention: integral part = category, fractional part = utility.
struct PL_AIPLUGIN_DLL plAiBehaviorScore
{
  float m_fScore = 0.0f;         ///< category + [0,1) utility value
  plUInt32 m_uiBehaviorIndex = plInvalidIndex;
  plInt32 m_iTargetIndex = -1;   ///< index into the agent's perceived target list, -1 if target-less

  static float Compose(plAiBehaviorCategory::Enum category, float fUtility)
  {
    return static_cast<float>(category) + plMath::Clamp(fUtility, 0.0f, 0.999f);
  }
};
