#pragma once

#include <AiPlugin/UtilityAI/Decision/AiBehaviorResource.h>

/// \brief Scores utility AI behaviors from their considerations.
class PL_AIPLUGIN_DLL plAiUtilityEvaluator
{
public:
  /// \brief Computes the utility [0,1] of one behavior for one context (one target candidate).
  ///
  /// The consideration values s_i are combined according to the archetype's scoring version, then scaled by the weights:
  /// - Version 1: the product s, rescaled once with the compensation factor s' = s + (1-s) * s * (1 - 1/n).
  ///   More considerations still lower the score: four at 0.8 give 0.59.
  /// - Version 2: the geometric mean (s_1 * ... * s_n)^(1/n). Equal values give that value for any n.
  ///
  /// If any consideration scores 0, the utility is 0 (veto semantics).
  ///
  /// \param out_pTraces optional, receives every consideration's evaluation (for debugging tools). When set, all
  /// considerations are evaluated, even after one vetoes.
  static float ScoreUtility(const plAiBehaviorResourceDescriptor& behavior, float fWeightScale, const plAiScoringContext& context,
    plHybridArray<plAiConsiderationTrace, 8>* out_pTraces = nullptr, plUInt8 uiScoringVersion = 1);

  /// \brief Combines consideration values the way ScoreUtility does, before the weights.
  static float CombineConsiderations(float fProduct, plUInt32 uiNumConsiderations, plUInt8 uiScoringVersion);
};
