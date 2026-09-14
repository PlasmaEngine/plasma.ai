using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class AiSemanticsTestsTarget : TargetRules
{
    public AiSemanticsTestsTarget(BuildContext context) : base(context)
    {
        Type = TargetType.Executable;
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);
        ExtraModules.Add("AiSemanticsTestsModule");
        TargetDependencies.Add("AiPlugin");
    }
}
