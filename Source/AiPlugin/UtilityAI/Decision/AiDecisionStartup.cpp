#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Decision/AiArchetypeResource.h>
#include <AiPlugin/UtilityAI/Decision/AiBehaviorResource.h>
#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/Configuration/Startup.h>

// clang-format off
PL_BEGIN_SUBSYSTEM_DECLARATION(AiPlugin, AiDecisionSystem)

BEGIN_SUBSYSTEM_DEPENDENCIES
  "Foundation",
  "Core"
END_SUBSYSTEM_DEPENDENCIES

ON_CORESYSTEMS_STARTUP
{
  plResourceManager::RegisterResourceForAssetType("AiBehavior", plGetStaticRTTI<plAiBehaviorResource>());
  plResourceManager::RegisterResourceForAssetType("AiArchetype", plGetStaticRTTI<plAiArchetypeResource>());
}

ON_CORESYSTEMS_SHUTDOWN
{
}

ON_HIGHLEVELSYSTEMS_STARTUP
{
}

ON_HIGHLEVELSYSTEMS_SHUTDOWN
{
}

PL_END_SUBSYSTEM_DECLARATION;
// clang-format on

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiDecisionStartup);
