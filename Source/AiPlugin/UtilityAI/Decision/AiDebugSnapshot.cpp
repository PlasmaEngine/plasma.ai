#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Decision/AiDebugSnapshot.h>

const char* plAiDecisionOutcomeName(plAiDecisionOutcome outcome)
{
  switch (outcome)
  {
    case plAiDecisionOutcome::None:
      return "No decision";
    case plAiDecisionOutcome::Started:
      return "Started";
    case plAiDecisionOutcome::Kept:
      return "Kept";
    case plAiDecisionOutcome::Retargeted:
      return "Changed target";
    case plAiDecisionOutcome::HigherCategory:
      return "Higher category";
    case plAiDecisionOutcome::BeatCommitBonus:
      return "Beat commit bonus";
    case plAiDecisionOutcome::HeldByCommitBonus:
      return "Held by commit bonus";
    case plAiDecisionOutcome::StoppedScoring:
      return "Active stopped scoring";
    case plAiDecisionOutcome::NothingScores:
      return "Nothing scores";
    case plAiDecisionOutcome::Locked:
      return "Locked";
    case plAiDecisionOutcome::Done:
      return "Done";
    case plAiDecisionOutcome::Failed:
      return "Failed";
    case plAiDecisionOutcome::Disabled:
      return "Disabled";
    case plAiDecisionOutcome::Interrupted:
      return "Interrupted";
  }

  return "?";
}

const char* plAiBehaviorVetoName(plAiBehaviorVeto veto)
{
  switch (veto)
  {
    case plAiBehaviorVeto::None:
      return "";
    case plAiBehaviorVeto::NotLoaded:
      return "asset not loaded";
    case plAiBehaviorVeto::Disabled:
      return "disabled";
    case plAiBehaviorVeto::Cooldown:
      return "cooldown";
    case plAiBehaviorVeto::NoTarget:
      return "no target";
    case plAiBehaviorVeto::ZeroUtility:
      return "a consideration scored 0";
  }

  return "?";
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiDebugSnapshot);
