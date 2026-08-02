using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class RecastModule : ModuleRules
{
    public RecastModule(BuildContext context) : base(context)
    {
        Type = ModuleType.ThirdParty;
        SourceDirectory = "ThirdParty/Recast";

        // Recast/Detour has no dllexport annotations, so the engine DLL that compiles it
        // does not export its symbols. Consumers that reference Detour types directly
        // (e.g. the Ai editor plugin) must compile it into their own DLL.
        ExportsSymbols = false;

        PublicIncludePaths.Add("..");
        PublicIncludePaths.Add("Recast/Include");
        PublicIncludePaths.Add("Detour/Include");
        PublicIncludePaths.Add("DetourTileCache/Include");
        PublicIncludePaths.Add("DetourCrowd/Include");

        PublicDefinitions.Add("BUILDSYSTEM_ENABLE_RECAST_SUPPORT");
        PublicDefinitions.Add("BUILDSYSTEM_BUILDING_RECAST_LIB");

        CppStandard = CppStandard.Cpp20;
        UseUnityBuild = false;
        TreatWarningsAsErrors = false;
    }
}