#pragma once

#include <Foundation/Tracks/Curve1D.h>

/// Shared AI curve sampling. The caller prepares the curve's linear approximation.
/// New curves use authored X coordinates and hold endpoint values; legacy assets can retain
/// the old mapping of [0,1] onto their first-to-last control point interval.
inline float plAiEvaluateResponseCurve(const plCurve1D& curve, float fInput, bool bLegacyDomain = false)
{
  fInput = plMath::Clamp(fInput, 0.0f, 1.0f);
  if (curve.IsEmpty())
    return fInput;

  const double x = bLegacyDomain ? curve.ConvertNormalizedPos(fInput) : fInput;
  return plMath::Clamp(static_cast<float>(curve.Evaluate(x)), 0.0f, 1.0f);
}
