#pragma once

#include <AiPlugin/UtilityAI/Decision/AiBehaviorResource.h>

/// \brief Scores utility AI behaviors from their considerations.
class PL_AIPLUGIN_DLL plAiUtilityEvaluator
{
public:
  /// \brief Computes the utility [0,1] of one behavior for one context (one target candidate).
  ///
  /// Considerations are multiplied, then the result is rescaled with the 'compensation factor'
  /// (Infinite Axis Utility System style) so that behaviors with many considerations are not
  /// unfairly punished: s' = s + (1-s) * s * (1 - 1/n).
  ///
  /// If any consideration scores 0, the utility is 0 (veto semantics).
  ///
  /// \param out_pConsiderationValues optional, receives the individual consideration scores (for debug display).
  static float ScoreUtility(const plAiBehaviorResourceDescriptor& behavior, float fWeightScale, const plAiScoringContext& context,
    plHybridArray<float, 8>* out_pConsiderationValues = nullptr);
};
