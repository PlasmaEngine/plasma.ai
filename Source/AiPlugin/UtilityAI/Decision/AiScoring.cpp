#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Decision/AiScoring.h>
#include <Core/Utils/Blackboard.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiBehaviorCategory, 1)
  PL_ENUM_CONSTANTS(plAiBehaviorCategory::Fallback, plAiBehaviorCategory::Idle, plAiBehaviorCategory::ActiveIdle, plAiBehaviorCategory::Investigate)
  PL_ENUM_CONSTANTS(plAiBehaviorCategory::Command, plAiBehaviorCategory::Combat, plAiBehaviorCategory::Interrupt)
PL_END_STATIC_REFLECTED_ENUM;
// clang-format on

plVariant plAiScoringContext::GetBlackboardValue(const plHashedString& sEntryName) const
{
  if (m_pBlackboardSnapshot != nullptr)
  {
    plVariant value;
    m_pBlackboardSnapshot->TryGetValue(sEntryName, value);
    return value;
  }

  if (m_pBlackboard != nullptr)
  {
    return m_pBlackboard->GetEntryValue(sEntryName);
  }

  return plVariant();
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiScoring);
