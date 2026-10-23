using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EnginePluginAiModule : ModuleRules
{
    public EnginePluginAiModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EnginePluginAi", "EnginePluginAiPCH.h",
            "BUILDSYSTEM_BUILDING_ENGINEPLUGINAI_LIB");

        PlasmaPackageSdk.AddImportLibrary(this, context, "EditorEngineProcessFramework");
        PublicDependencies.Add("RecastModule");
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/AiPlugin", "plAiPlugin");
    }
}
