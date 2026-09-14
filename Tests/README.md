# AI semantics regression tests

From the package directory:

```powershell
plasmabuild build AiSemanticsTests --configuration Development
# Point this at the engine binaries used by the package SDK.
$env:PATH = "<engine>/Binaries/Windows_Development;" + $env:PATH
./Bin/Windows_x64_Development/AiSemanticsTests.exe
```

The executable returns nonzero on failure and reports the number of checks.
It exercises the actual plugin implementation, including reflection serialization and graph
compatibility patches. It does not require a running editor, physics world, or GPU.

Optionally pass paths to runtime `.plAiEqsQuery` files freshly transformed from version-1
source assets. These additional checks verify that their curve-domain and missing-data
compatibility settings survived the actual editor migration. Do not use this mode for new
assets or assets deliberately migrated away from legacy settings.

Build `EditorPluginAi` separately to validate the editor integration. Real navigation/physics
queries and visual editor interaction require a scene-level smoke test.
