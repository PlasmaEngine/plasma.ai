#pragma once

#include <Core/World/Declarations.h>
#include <EnginePluginAi/EnginePluginAiDLL.h>
#include <Foundation/Containers/HashTable.h>
#include <Foundation/Types/Uuid.h>

struct plAiDebugFrame;
struct plAiDebugRequest;
struct plEngineProcessToolConfigEvent;
class plEngineProcessDocumentContext;

/// \brief Engine-process side of the editor's AI debugger: answers "AiDebug.Poll" with an "AiDebug.Frame"
/// (see plAiDebugRequest).
///
/// Watches the agent each poll names and stops watching the one the previous poll named. Agents without an editor
/// GUID (spawned at runtime) get an ID made from their object handle, so they can be watched too.
class PL_ENGINEPLUGINAI_DLL plAiDebugHandler
{
public:
  static void Startup();
  static void Shutdown();

  /// \brief Fills a frame for the given world. Exposed for tests.
  static void BuildFrame(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plAiDebugRequest& request, plAiDebugFrame& out_frame);

  /// The editor GUID of an object, or an ID made from its handle for objects created at runtime.
  static plUuid GetObjectId(plEngineProcessDocumentContext* pContext, const plGameObjectHandle& hObject);
  static plGameObjectHandle ResolveObjectId(plEngineProcessDocumentContext* pContext, const plUuid& id);

private:
  static void OnToolConfig(const plEngineProcessToolConfigEvent& e);

  static void UpdateWatch(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plAiDebugRequest& request, plComponentHandle& out_hAgent);
  static void FillWatched(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plComponentHandle& hAgent, plAiDebugFrame& out_frame);

  /// The sector list for the sector views; also applies the request's inspected sector and its one-shot rebuild.
  /// The squad list for the Squad tab; also applies the order the request carries.
  static void FillSquads(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plAiDebugRequest& request, plAiDebugFrame& out_frame);

  static void FillNavmesh(plWorld* pWorld, const plAiDebugRequest& request, plAiDebugFrame& out_frame);

  /// The agent one scene document watches.
  struct Watch
  {
    plUInt32 m_uiWorldIndex = plInvalidIndex;
    plComponentHandle m_hAgent;
  };

  static plHashTable<plUuid, Watch> s_Watches;
};
