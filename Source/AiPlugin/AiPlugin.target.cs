using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class AiPluginTarget : TargetRules
{
    public AiPluginTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plAiPlugin";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);
        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;
        ExtraModules.Add("AiPluginModule");
        ExtraModules.Add("RecastModule");
    }
}
