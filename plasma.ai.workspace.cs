// #pl-version 1

using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class PlasmaAiWorkspace : WorkspaceRules
{
    public PlasmaAiWorkspace(BuildContext context) : base(context)
    {
        TargetNames.Add("AiPlugin");
        TargetNames.Add("EditorPluginAi");
    }
}
