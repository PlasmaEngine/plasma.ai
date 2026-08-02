using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginAiModule : ModuleRules
{
    public EditorPluginAiModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EditorPluginAi", "EditorPluginAiPCH.h",
            "BUILDSYSTEM_BUILDING_EDITORPLUGINAI_LIB");

        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);
        PublicDependencies.Add("RecastModule");
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/AiPlugin", "plAiPlugin");

        PlasmaPackageQt.ConfigureQtModule(this, context, "EditorPluginAiModule",
            "Source/EditorPluginAi", "Core", "Gui", "Widgets");
    }
}
