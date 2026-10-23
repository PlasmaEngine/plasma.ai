#pragma once

#include <AiPlugin/UtilityAI/Decision/AiArchetypeResource.h>

/// \brief The perception math the brain applies to perceived targets.
///
/// Editor previews call the same functions, so they show what the runtime does.
namespace plAiPerceptionRules
{
  /// \brief Confidence after decaying for tDiff at the given rate.
  PL_AIPLUGIN_DLL float Decay(float fConfidence, float fDecayPerSecond, plTime tDiff);

  /// \brief A record is forgotten once its confidence reached 0 and no stimulus arrived for the memory duration.
  PL_AIPLUGIN_DLL bool IsForgotten(const plAiPerceivedTarget& target, plTime now, plTime memoryDuration);

  /// \brief Decay rate and memory duration for a record last refreshed by the given sense (plAiStimulusType).
  PL_AIPLUGIN_DLL void GetDecaySettings(const plAiArchetypeResourceDescriptor& archetype, plUInt8 uiSense, float& out_fDecayPerSecond, plTime& out_memoryDuration);

  /// \brief The alert level the given records amount to. See plAiAlertLevel.
  PL_AIPLUGIN_DLL plAiAlertLevel::Enum ComputeAlertLevel(plArrayPtr<const plAiPerceivedTarget> targets, plTime now);

  /// \brief How long after the last sighting or damage an agent stays Alert.
  constexpr plTime AlertDuration = plTime::MakeFromSeconds(1.0);
} // namespace plAiPerceptionRules
