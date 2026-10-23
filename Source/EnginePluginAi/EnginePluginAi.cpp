#include <EnginePluginAi/EnginePluginAiPCH.h>

#include <EnginePluginAi/Debug/AiDebugHandler.h>
#include <EnginePluginAi/Debug/AiEncounterDebugHandler.h>
#include <Foundation/Configuration/Plugin.h>

PL_PLUGIN_ON_LOADED()
{
  plAiDebugHandler::Startup();
  plAiEncounterDebugHandler::Startup();
}

PL_PLUGIN_ON_UNLOADED()
{
  plAiEncounterDebugHandler::Shutdown();
  plAiDebugHandler::Shutdown();
}
