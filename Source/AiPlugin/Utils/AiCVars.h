#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Configuration/CVar.h>

// Debugging / tuning cvars for the utility AI framework.
// Declared here so that the brain world module, perception module and components can share them.

extern PL_AIPLUGIN_DLL plCVarBool cvar_AiAgentsShowScores;      ///< Render per-agent behavior scores with consideration breakdown.
extern PL_AIPLUGIN_DLL plCVarBool cvar_AiAgentsShowHistory;     ///< Render the per-agent behavior switch history.
extern PL_AIPLUGIN_DLL plCVarBool cvar_AiAgentsShowPerception;  ///< Render perceived target records.
extern PL_AIPLUGIN_DLL plCVarString cvar_AiAgentsDebugFilter;   ///< Only agents whose object name contains this string show debug info.
extern PL_AIPLUGIN_DLL plCVarInt cvar_AiAgentsMaxDecisionsPerFrame; ///< Upper bound of full decision (scoring) passes per frame.
extern PL_AIPLUGIN_DLL plCVarFloat cvar_AiAgentsDecisionHz;     ///< How often each agent re-evaluates its behavior choice (Hot LOD tier).
extern PL_AIPLUGIN_DLL plCVarBool cvar_AiAgentsAsyncScoring;    ///< Score utility behaviors in parallel in the async world update phase.
extern PL_AIPLUGIN_DLL plCVarFloat cvar_AiAgentsLodHotDistance; ///< Agents closer than this to a LOD center are 'Hot'.
extern PL_AIPLUGIN_DLL plCVarFloat cvar_AiAgentsLodWarmDistance;///< Agents closer than this to a LOD center are 'Warm', beyond they are 'Cold'.
extern PL_AIPLUGIN_DLL plCVarFloat cvar_AiAgentsLodWarmHz;      ///< Decision rate of Warm agents.
extern PL_AIPLUGIN_DLL plCVarFloat cvar_AiAgentsLodColdHz;      ///< Decision rate of Cold agents.
extern PL_AIPLUGIN_DLL plCVarBool cvar_AiAgentsLodFreeze;       ///< Freeze LOD tier assignment (for debugging).
