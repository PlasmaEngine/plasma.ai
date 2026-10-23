using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EnginePluginAiTarget : TargetRules
{
    public EnginePluginAiTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plEnginePluginAi";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);
        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;
        TargetDependencies.Add("AiPlugin");
        ExtraModules.Add("EnginePluginAiModule");
        ExtraModules.Add("RecastModule");
    }
}
