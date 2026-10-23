#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Perception/AiPerception.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionRules.h>

float plAiPerceptionRules::Decay(float fConfidence, float fDecayPerSecond, plTime tDiff)
{
  return plMath::Max(0.0f, fConfidence - fDecayPerSecond * static_cast<float>(tDiff.GetSeconds()));
}

bool plAiPerceptionRules::IsForgotten(const plAiPerceivedTarget& target, plTime now, plTime memoryDuration)
{
  return target.m_fConfidence <= 0.0f && (now - target.m_LastStimulusTime) > memoryDuration;
}

void plAiPerceptionRules::GetDecaySettings(const plAiArchetypeResourceDescriptor& archetype, plUInt8 uiSense, float& out_fDecayPerSecond, plTime& out_memoryDuration)
{
  const plAiSenseProfile* pSense = archetype.GetSenseProfile(uiSense);

  if (pSense != nullptr && !pSense->m_bUseSightSettings)
  {
    out_fDecayPerSecond = pSense->m_fDecayPerSecond;
    out_memoryDuration = pSense->m_MemoryDuration;
    return;
  }

  out_fDecayPerSecond = archetype.m_fConfidenceDecayPerSecond;
  out_memoryDuration = archetype.m_TargetMemoryDuration;
}

plAiAlertLevel::Enum plAiPerceptionRules::ComputeAlertLevel(plArrayPtr<const plAiPerceivedTarget> targets, plTime now)
{
  constexpr plUInt8 uiConfirmingSenses = (1 << plAiStimulusType::Sight) | (1 << plAiStimulusType::Damage);

  plAiAlertLevel::Enum level = plAiAlertLevel::Unaware;

  for (const plAiPerceivedTarget& target : targets)
  {
    const bool bSeenNow = (now - target.m_LastSightTime) <= AlertDuration;
    const bool bHurtNow = target.m_uiLastSense == plAiStimulusType::Damage && (now - target.m_LastStimulusTime) <= AlertDuration;

    if (bSeenNow || bHurtNow)
      return plAiAlertLevel::Alert;

    if ((target.m_uiSenses & uiConfirmingSenses) != 0 || target.m_bReported)
    {
      level = plAiAlertLevel::Searching;
    }
    else if (level == plAiAlertLevel::Unaware)
    {
      level = plAiAlertLevel::Suspicious;
    }
  }

  return level;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Perception_AiPerceptionRules);
