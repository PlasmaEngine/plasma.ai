#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/EqsQueryResource.h>
#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/Configuration/Startup.h>

// clang-format off
PL_BEGIN_SUBSYSTEM_DECLARATION(AiPlugin, AiEqsSystem)

BEGIN_SUBSYSTEM_DEPENDENCIES
  "Foundation",
  "Core"
END_SUBSYSTEM_DEPENDENCIES

ON_CORESYSTEMS_STARTUP
{
  plResourceManager::RegisterResourceForAssetType("AiEqsQuery", plGetStaticRTTI<plAiEqsQueryResource>());
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

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Eqs_Implementation_EqsStartup);
