using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class AiSemanticsTestsModule : ModuleRules
{
    public AiSemanticsTestsModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Tests", "TestsPCH.h",
            "BUILDSYSTEM_BUILDING_AISEMANTICSTESTS_LIB");
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/AiPlugin", "plAiPlugin");
        PublicDependencies.Add("RecastModule");
    }
}
