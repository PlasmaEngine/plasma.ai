#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Utils/AiCVars.h>

plCVarBool cvar_AiAgentsShowScores("AI.Agents.ShowScores", false, plCVarFlags::Default, "Render per-agent behavior scores with consideration breakdown.");
plCVarBool cvar_AiAgentsShowHistory("AI.Agents.ShowHistory", false, plCVarFlags::Default, "Render the per-agent behavior switch history.");
plCVarBool cvar_AiAgentsShowPerception("AI.Agents.ShowPerception", false, plCVarFlags::Default, "Render perceived target records.");
plCVarString cvar_AiAgentsDebugFilter("AI.Agents.DebugFilter", "", plCVarFlags::Default, "Only agents whose object name contains this string show debug info. Empty = all agents.");
plCVarInt cvar_AiAgentsMaxDecisionsPerFrame("AI.Agents.MaxDecisionsPerFrame", 32, plCVarFlags::Default, "Upper bound of full decision (scoring) passes per frame across all agents.");
plCVarFloat cvar_AiAgentsDecisionHz("AI.Agents.DecisionHz", 5.0f, plCVarFlags::Default, "How often each agent re-evaluates its behavior choice, in Hz (Hot LOD tier).");
plCVarBool cvar_AiAgentsAsyncScoring("AI.Agents.AsyncScoring", true, plCVarFlags::Default, "Score utility behaviors in parallel in the async world update phase.");
plCVarFloat cvar_AiAgentsLodHotDistance("AI.Agents.Lod.HotDistance", 25.0f, plCVarFlags::Default, "Agents closer than this to a LOD center are 'Hot' (full rates).");
plCVarFloat cvar_AiAgentsLodWarmDistance("AI.Agents.Lod.WarmDistance", 60.0f, plCVarFlags::Default, "Agents closer than this to a LOD center are 'Warm', beyond they are 'Cold'.");
plCVarFloat cvar_AiAgentsLodWarmHz("AI.Agents.Lod.WarmHz", 2.0f, plCVarFlags::Default, "Decision rate of Warm agents, in Hz.");
plCVarFloat cvar_AiAgentsLodColdHz("AI.Agents.Lod.ColdHz", 0.5f, plCVarFlags::Default, "Decision rate of Cold agents, in Hz.");
plCVarBool cvar_AiAgentsLodFreeze("AI.Agents.Lod.Freeze", false, plCVarFlags::Default, "Freeze LOD tier assignment (for debugging).");

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Utils_AiCVars);
