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

        // The cards and previews are engine-drawn: EditorUi and the retained UI runtime.
        PlasmaPackageSdk.AddImportLibrary(this, context, "plEditorUi");
        PlasmaPackageSdk.AddImportLibrary(this, context, "plGameUiPlugin");

        // Each editor view keeps its own file-local layout constants and helpers under the same names (Publish, Name,
        // s_fToolbarHeight), which collide when views share a unity batch.
        UseUnityBuild = false;
    }
}
