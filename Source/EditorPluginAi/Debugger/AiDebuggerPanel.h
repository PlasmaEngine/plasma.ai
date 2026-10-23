#pragma once

#include <EditorPluginAi/EditorPluginAiDLL.h>
#include <GuiFoundation/Action/BaseActions.h>

class plDocument;
class plEditorUiDocumentFrame;

/// \brief Adds the AI Debugger panel to every scene's frame and the AI overlay menu to the viewport toolbar.
class PL_EDITORPLUGINAI_DLL plAiDebuggerPanel
{
public:
  static void Startup();
  static void Shutdown();

  /// Pins, opens and raises the AI Debugger of a scene.
  static void ShowPanel(const plDocument* pDocument);

private:
  static void AddShellPanel(plEditorUiDocumentFrame& ref_frame);

  static plActionDescriptorHandle s_hOverlayMenu;
};

/// \brief The viewport's AI menu (M2): one entry per AI debug overlay, each switching its cvar in the engine process.
///
/// "Watched agent only" limits the per-agent overlays to the agent the AI Debugger watches. The check marks come from the
/// scene's debug session, which reads the cvars back from the engine.
class PL_EDITORPLUGINAI_DLL plAiOverlayMenuAction : public plDynamicMenuAction
{
  PL_ADD_DYNAMIC_REFLECTION(plAiOverlayMenuAction, plDynamicMenuAction);

public:
  plAiOverlayMenuAction(const plActionContext& context, const char* szName, const char* szIconPath);

  virtual void GetEntries(plDynamicArray<Item>& out_entries) override;
  virtual void Execute(const plVariant& value) override;

  static constexpr plInt32 s_iWatchedOnly = 1000;
  static constexpr plInt32 s_iAllOff = 1001;
  static constexpr plInt32 s_iOpenDebugger = 1002;
};
