#pragma once

#include <Core/World/Declarations.h>
#include <EnginePluginAi/EnginePluginAiDLL.h>

struct plAiEncounterDebugFrame;
struct plAiEncounterDebugRequest;
struct plEngineProcessToolConfigEvent;
class plEngineProcessDocumentContext;

/// \brief Engine-process side of the editor's Encounter panel: answers "AiEncounter.Poll" with an "AiEncounter.Frame"
/// (see plAiEncounterDebugRequest).
class PL_ENGINEPLUGINAI_DLL plAiEncounterDebugHandler
{
public:
  static void Startup();
  static void Shutdown();

  /// \brief Fills a frame for the given world. Exposed for tests.
  static void BuildFrame(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plAiEncounterDebugRequest& request, plAiEncounterDebugFrame& out_frame);

private:
  static void OnToolConfig(const plEngineProcessToolConfigEvent& e);
};
