using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class AiPluginModule : ModuleRules
{
    public AiPluginModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/AiPlugin", "AiPluginPCH.h",
            "BUILDSYSTEM_BUILDING_AIPLUGIN_LIB");

        PublicDependencies.Add("RecastModule");
    }
}
