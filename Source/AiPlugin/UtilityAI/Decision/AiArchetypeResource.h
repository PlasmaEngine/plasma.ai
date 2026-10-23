#pragma once

#include <AiPlugin/UtilityAI/Decision/AiBehaviorResource.h>
#include <GameEngine/Utils/BlackboardTemplateResource.h>

using plAiArchetypeResourceHandle = plTypedResourceHandle<class plAiArchetypeResource>;

/// \brief One behavior of an archetype, with an optional archetype-specific weight scale.
struct PL_AIPLUGIN_DLL plAiArchetypeBehaviorEntry
{
  plAiBehaviorResourceHandle m_hBehavior;
  float m_fWeightScale = 1.0f;
};

/// \brief How an agent perceives one kind of stimulus event (hearing, damage, custom).
struct PL_AIPLUGIN_DLL plAiSenseProfile
{
  bool m_bEnabled = true;
  bool m_bUseSightSettings = true;                 ///< decay and memory come from the archetype's sight settings
  float m_fRangeScale = 1.0f;                      ///< scales the radius of the stimuli this sense receives
  float m_fStrengthScale = 1.0f;                   ///< scales the confidence a stimulus gives
  float m_fDecayPerSecond = 0.5f;                  ///< only when not using the sight settings
  plTime m_MemoryDuration = plTime::Seconds(10.0); ///< only when not using the sight settings
};

/// \brief Defines a type of AI agent: its behavior set, blackboard template, team and perception tuning.
struct PL_AIPLUGIN_DLL plAiArchetypeResourceDescriptor
{
  plDynamicArray<plAiArchetypeBehaviorEntry> m_Behaviors;
  plBlackboardTemplateResourceHandle m_hBlackboardTemplate;
  plHashedString m_sTeam;

  /// Name of the child object that carries the plSensorComponents used for sight. Empty = search all children.
  plString m_sSensorObjectName;

  // sight
  float m_fConfidenceGainPerSecond = 4.0f;
  float m_fConfidenceDecayPerSecond = 0.5f;
  plTime m_TargetMemoryDuration = plTime::Seconds(10.0);

  // stimulus senses, indexed by plAiStimulusType (Sight has no profile; it uses the fields above)
  plAiSenseProfile m_Hearing;
  plAiSenseProfile m_Damage;
  plAiSenseProfile m_Custom;
  plUInt8 m_uiMaxTargets = 8; ///< perceived targets kept per agent; the least confident is dropped

  // reactivity: events that trigger a decision before the next scheduled one
  bool m_bWakeOnStimulus = true;                  ///< a stimulus the agent perceives
  bool m_bWakeOnBehaviorEnd = true;               ///< the active behavior reports Done or Failed
  plTime m_MinWakeSpacing = plTime::Seconds(0.1); ///< woken decisions are at least this far apart

  // target switching within the active behavior
  float m_fTargetSwitchMargin = 0.1f;            ///< a new target must score at least this much higher
  plTime m_TargetMinHold = plTime::Seconds(0.5); ///< and the current target must have been held this long

  plTime m_MaxLockDuration; ///< a behavior lock is ignored after this long; zero = no limit

  /// 1: the product of all considerations, compensated once. 2: their geometric mean, so the number of
  /// considerations doesn't change the score. See plAiUtilityEvaluator::ScoreUtility.
  plUInt8 m_uiScoringVersion = 1;

  /// \brief The profile for a stimulus type; nullptr for Sight.
  const plAiSenseProfile* GetSenseProfile(plUInt8 uiStimulusType) const;

  plResult Serialize(plStreamWriter& inout_stream) const;
  plResult Deserialize(plStreamReader& inout_stream);
};

/// \brief Per-agent-type configuration, authored through the 'AI Archetype' asset and referenced by plAiAgentComponent.
class PL_AIPLUGIN_DLL plAiArchetypeResource : public plResource
{
  PL_ADD_DYNAMIC_REFLECTION(plAiArchetypeResource, plResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plAiArchetypeResource);
  PL_RESOURCE_DECLARE_CREATEABLE(plAiArchetypeResource, plAiArchetypeResourceDescriptor);

public:
  plAiArchetypeResource();
  ~plAiArchetypeResource();

  const plAiArchetypeResourceDescriptor& GetDescriptor() const { return m_Descriptor; }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* Stream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage) override;

  plAiArchetypeResourceDescriptor m_Descriptor;
};
