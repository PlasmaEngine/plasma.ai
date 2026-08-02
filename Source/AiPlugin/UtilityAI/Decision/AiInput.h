#pragma once

#include <AiPlugin/Tactical/SquadTypes.h>
#include <AiPlugin/UtilityAI/Decision/AiScoring.h>
#include <Foundation/Reflection/Reflection.h>

/// \brief Base class for all input value providers used by utility AI considerations.
///
/// An input extracts one raw value from the agent's situation (distance to target, a blackboard
/// value, time since something happened, ...). The consideration then normalizes that value into
/// [0,1] and remaps it through a response curve.
///
/// Inputs are shared, immutable objects owned by the behavior resource. They are evaluated for many
/// agents (and potentially from worker threads), so they must be stateless and must only read from
/// the passed plAiScoringContext.
class PL_AIPLUGIN_DLL plAiInput : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput, plReflectedClass);

public:
  plAiInput() = default;
  virtual ~plAiInput() = default;

  /// \brief Returns the raw (not yet normalized) input value for the given context.
  virtual float Evaluate(const plAiScoringContext& context) const = 0;

  /// \brief Whether this input only makes sense when a target candidate is present.
  ///
  /// If any consideration of a behavior needs a target, the behavior is scored once per
  /// perceived target and skipped when the agent perceives nothing.
  virtual bool NeedsTarget() const { return false; }

  /// \brief Lists the blackboard entries this input reads, so they can be snapshotted for async scoring.
  virtual void CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const {}
};

/// \brief Distance (in meters) between the agent and the scored target's last known position.
class PL_AIPLUGIN_DLL plAiInput_DistanceToTarget : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_DistanceToTarget, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
  virtual bool NeedsTarget() const override { return true; }
};

/// \brief Confidence [0,1] of the scored target record.
class PL_AIPLUGIN_DLL plAiInput_TargetConfidence : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_TargetConfidence, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
  virtual bool NeedsTarget() const override { return true; }
};

/// \brief Reads a numeric blackboard entry of the agent.
class PL_AIPLUGIN_DLL plAiInput_BlackboardValue : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_BlackboardValue, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
  virtual void CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const override;

  void SetEntryName(const char* szName) { m_sEntryName.Assign(szName); } // [ property ]
  const char* GetEntryName() const { return m_sEntryName.GetData(); }    // [ property ]

  plHashedString m_sEntryName;
  float m_fFallback = 0.0f; ///< Used when the entry does not exist or cannot convert to a number.
};

/// \brief How long (in seconds) the scored behavior has been the active behavior. Zero if inactive.
class PL_AIPLUGIN_DLL plAiInput_TimeSinceActivation : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_TimeSinceActivation, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
};

/// \brief How long (in seconds) ago the scored behavior was last deactivated. Large value if never active before.
class PL_AIPLUGIN_DLL plAiInput_TimeSinceDeactivation : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_TimeSinceDeactivation, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
};

/// \brief A constant value. Useful to give a behavior a fixed base utility.
class PL_AIPLUGIN_DLL plAiInput_Constant : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_Constant, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;

  float m_fValue = 1.0f;
};

/// \brief Distance (in meters) between the agent and a position stored in a blackboard entry (plVec3).
class PL_AIPLUGIN_DLL plAiInput_DistanceToPosition : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_DistanceToPosition, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
  virtual void CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const override;

  void SetEntryName(const char* szName) { m_sEntryName.Assign(szName); } // [ property ]
  const char* GetEntryName() const { return m_sEntryName.GetData(); }    // [ property ]

  plHashedString m_sEntryName;
};

/// \brief Angle (in degrees, 0-180) between the agent's forward direction and the direction to the scored target.
///
/// Useful to prefer targets in front of the agent (e.g. only 'see' what is roughly ahead).
class PL_AIPLUGIN_DLL plAiInput_AngleToTarget : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_AngleToTarget, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
  virtual bool NeedsTarget() const override { return true; }
};

/// \brief What plAiInput_CoverAvailability reports.
struct PL_AIPLUGIN_DLL plAiCoverAvailabilityMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    ClaimedQuality,  ///< quality of the claimed cover the agent is STANDING AT: 0 none/not there yet, 0.5 low, 1 high ('Ai_CoverStatus'; radius = AI.Tactical.CoverOccupiedRadius)
    NearbyAvailable, ///< 1 when an unclaimed cover point exists in search range ('Ai_CoverNearby')

    Default = ClaimedQuality
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiCoverAvailabilityMode);

/// \brief The agent's cover situation, published by the AI tactical module.
///
/// The tactical module probes registered agents on a budget and writes 'Ai_CoverStatus' and
/// 'Ai_CoverNearby' into the agent's blackboard - which therefore must come from a
/// plBlackboardComponent on the agent (private brain blackboards are not reachable).
class PL_AIPLUGIN_DLL plAiInput_CoverAvailability : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_CoverAvailability, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
  virtual void CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const override;

  plEnum<plAiCoverAvailabilityMode> m_Mode;
};

/// \brief 1 when the perceived target has clear line of sight to the agent, 0 when blocked or no target.
///
/// Published as 'Ai_TargetExposure' by the AI tactical module (same blackboard requirement as
/// plAiInput_CoverAvailability). High exposure while under threat is the classic 'take cover' driver.
class PL_AIPLUGIN_DLL plAiInput_TargetExposure : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_TargetExposure, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
  virtual void CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const override;
};

/// \brief 1 while this agent holds one of its squad's attack tokens, else 0.
///
/// Published as 'Ai_HasAttackToken' by the AI squad module. Gate fire behaviors on it (rising
/// step) so only N squad members shoot at once; gate suppress/reposition behaviors on the
/// inverted curve. Agents WITHOUT a squad evaluate to 1 - they fight unrestricted.
class PL_AIPLUGIN_DLL plAiInput_SquadHasAttackToken : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_SquadHasAttackToken, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
  virtual void CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const override;
};

/// \brief 1 when the squad's current intent matches DesiredIntent, else 0.
///
/// Published as 'Ai_SquadIntent' by the AI squad module. Use as the driver of squad behaviors
/// (match Advance/Retreat, curve 0->0, 1->1) and as a BIAS on individual behaviors (floored
/// curves, e.g. 1 -> 0.35 on a fight behavior when the squad retreats - never a hard veto).
/// Agents without a squad match 'None'.
class PL_AIPLUGIN_DLL plAiInput_SquadIntent : public plAiInput
{
  PL_ADD_DYNAMIC_REFLECTION(plAiInput_SquadIntent, plAiInput);

public:
  virtual float Evaluate(const plAiScoringContext& context) const override;
  virtual void CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const override;

  plEnum<plAiSquadIntent> m_DesiredIntent;
};
