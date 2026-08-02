#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Decision/AiUtilityEvaluator.h>

float plAiUtilityEvaluator::ScoreUtility(const plAiBehaviorResourceDescriptor& behavior, float fWeightScale, const plAiScoringContext& context,
  plHybridArray<float, 8>* out_pConsiderationValues /*= nullptr*/)
{
  if (out_pConsiderationValues != nullptr)
  {
    out_pConsiderationValues->Clear();
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

    const float fValue = consideration.Evaluate(context);

    if (out_pConsiderationValues != nullptr)
    {
      out_pConsiderationValues->PushBack(fValue);
    }

    fScore *= fValue;
    ++uiNumConsiderations;

    if (fScore <= 0.0f)
    {
      fScore = 0.0f;

      if (out_pConsiderationValues == nullptr)
        break; // early out, unless the debug display wants all values
    }
  }

  if (uiNumConsiderations > 1 && fScore > 0.0f)
  {
    // compensation factor: counteracts the score shrinking with every additional consideration
    const float fModification = 1.0f - (1.0f / static_cast<float>(uiNumConsiderations));
    fScore = fScore + ((1.0f - fScore) * fScore * fModification);
  }

  fScore *= behavior.m_fWeight * fWeightScale;

  return plMath::Clamp(fScore, 0.0f, 1.0f);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiUtilityEvaluator);
