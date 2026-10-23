using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginAiTarget : TargetRules
{
    public EditorPluginAiTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plEditorPluginAi";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);
        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;
        TargetDependencies.Add("AiPlugin");
        TargetDependencies.Add("EnginePluginAi"); // the debugger talks to it; built with the editor plugin
        ExtraModules.Add("EditorPluginAiModule");
        ExtraModules.Add("RecastModule");
    }
}
