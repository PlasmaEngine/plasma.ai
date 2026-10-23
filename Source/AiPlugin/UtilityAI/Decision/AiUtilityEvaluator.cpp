#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Decision/AiUtilityEvaluator.h>

float plAiUtilityEvaluator::ScoreUtility(const plAiBehaviorResourceDescriptor& behavior, float fWeightScale, const plAiScoringContext& context,
  plHybridArray<plAiConsiderationTrace, 8>* out_pTraces /*= nullptr*/, plUInt8 uiScoringVersion /*= 1*/)
{
  if (out_pTraces != nullptr)
  {
    out_pTraces->Clear();
  }

  // cooldown veto
  if (behavior.m_CooldownDuration.IsPositive() && context.m_TimeSinceDeactivation < behavior.m_CooldownDuration)
  {
    return 0.0f;
  }

  float fScore = 1.0f;
  plUInt32 uiNumConsiderations = 0;

  for (const auto& consideration : behavior.m_Considerations)
  {
    if (consideration.m_pInput == nullptr)
      continue;

    const float fValue = consideration.Evaluate(context, (out_pTraces != nullptr) ? &out_pTraces->ExpandAndGetRef() : nullptr);

    fScore *= fValue;
    ++uiNumConsiderations;

    if (fScore <= 0.0f)
    {
      fScore = 0.0f;

      if (out_pTraces == nullptr)
        break; // early out, unless a debugging tool wants all values
    }
  }

  fScore = CombineConsiderations(fScore, uiNumConsiderations, uiScoringVersion);
  fScore *= behavior.m_fWeight * fWeightScale;

  return plMath::Clamp(fScore, 0.0f, 1.0f);
}

float plAiUtilityEvaluator::CombineConsiderations(float fProduct, plUInt32 uiNumConsiderations, plUInt8 uiScoringVersion)
{
  if (uiNumConsiderations <= 1 || fProduct <= 0.0f)
    return fProduct;

  if (uiScoringVersion >= 2)
  {
    return plMath::Pow(fProduct, 1.0f / static_cast<float>(uiNumConsiderations));
  }

  // compensation factor: counteracts the score shrinking with every additional consideration
  const float fModification = 1.0f - (1.0f / static_cast<float>(uiNumConsiderations));
  return fProduct + ((1.0f - fProduct) * fProduct * fModification);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiUtilityEvaluator);
