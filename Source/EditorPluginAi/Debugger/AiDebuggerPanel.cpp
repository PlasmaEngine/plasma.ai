#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorFramework/Document/GameObjectDocument.h>
#include <EditorFramework/EngineShell/DocumentContent.h>
#include <EditorFramework/EngineShell/DocumentFrame.h>
#include <EditorFramework/EngineShell/EngineShell.h>
#include <EditorPluginAi/Debugger/AiDebuggerPanel.h>
#include <EditorPluginAi/Debugger/AiDebuggerView.h>
#include <EditorPluginAi/Debugger/AiEncounterView.h>
#include <GuiFoundation/Action/ActionManager.h>
#include <GuiFoundation/Action/ActionMapManager.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiOverlayMenuAction, 0, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

plActionDescriptorHandle plAiDebuggerPanel::s_hOverlayMenu;

namespace
{
  constexpr const char* s_szViewToolBar = "EditorPluginScene_ViewToolBar";

  bool IsSceneDocument(const plDocument* pDocument)
  {
    // walk the type chain by name, as for every scene type
    for (const plRTTI* pRtti = pDocument->GetDynamicRTTI(); pRtti != nullptr; pRtti = pRtti->GetParentType())
    {
      if (pRtti->GetTypeName() == "plSceneDocument")
        return true;
    }

    return false;
  }
} // namespace

void plAiDebuggerPanel::Startup()
{
  plEditorUiDocumentContentProvider::RegisterExtraPanels(plGetStaticRTTI<plGameObjectDocument>(), &plAiDebuggerPanel::AddShellPanel);

  // an icon like the view's other menus; the name shows as its tooltip
  s_hOverlayMenu = PL_REGISTER_DYNAMIC_MENU("AI Overlays", plAiOverlayMenuAction, ":/AiPlugin/AiOverlays.svg");

  if (plActionMap* pMap = plActionMapManager::GetActionMap(s_szViewToolBar))
  {
    // after the view's own menus (perspective, render mode, exposure, remote process)
    pMap->MapAction(s_hOverlayMenu, "", 8.0f);
  }
}

void plAiDebuggerPanel::Shutdown()
{
  if (plActionMap* pMap = plActionMapManager::GetActionMap(s_szViewToolBar))
  {
    pMap->UnmapAction(s_hOverlayMenu, "").IgnoreResult();
  }

  plActionManager::UnregisterAction(s_hOverlayMenu);
  plEditorUiDocumentContentProvider::UnregisterExtraPanels(&plAiDebuggerPanel::AddShellPanel);
}

void plAiDebuggerPanel::AddShellPanel(plEditorUiDocumentFrame& ref_frame)
{
  if (!IsSceneDocument(ref_frame.GetDocument()))
    return;

  if (ref_frame.AddContent({plAiDebuggerView::s_szPanelId, "AI Debugger"}, PL_DEFAULT_NEW(plAiDebuggerView, ref_frame.GetDocument())) == nullptr)
    return;

  // under the view, in the bottom strip: scenes without AI keep their view; the AI menu pins it
  plEditorUiDockManager& panels = ref_frame.GetPanels();
  panels.SetPanelIcon(plAiDebuggerView::s_szPanelId, ":/AiPlugin/plAiPlugin.svg");
  panels.DockPanel(plAiDebuggerView::s_szPanelId, "View", plEditorUiDockSide::Bottom, 0.36f);
  panels.UnpinPanel(plAiDebuggerView::s_szPanelId, plEditorUiDockSide::Bottom);

  // the Encounter panel next to it in the bottom strip
  if (ref_frame.AddContent({plAiEncounterView::s_szPanelId, "Encounter"}, PL_DEFAULT_NEW(plAiEncounterView, ref_frame.GetDocument())) == nullptr)
    return;

  panels.SetPanelIcon(plAiEncounterView::s_szPanelId, ":/AiPlugin/Encounter.svg");
  panels.DockPanel(plAiEncounterView::s_szPanelId, "View", plEditorUiDockSide::Bottom, 0.36f);
  panels.UnpinPanel(plAiEncounterView::s_szPanelId, plEditorUiDockSide::Bottom);
}

void plAiDebuggerPanel::ShowPanel(const plDocument* pDocument)
{
  plEditorUiEngineShell* pShell = plEditorUiEngineShell::GetSingleton();
  plEditorUiDocumentFrame* pFrame = pShell != nullptr ? pShell->FindFrame(pDocument) : nullptr;
  if (pFrame == nullptr)
    return;

  plEditorUiDockManager& panels = pFrame->GetPanels();
  if (panels.IsPanelUnpinned(plAiDebuggerView::s_szPanelId))
    panels.PinPanel(plAiDebuggerView::s_szPanelId);

  if (!panels.IsPanelOpen(plAiDebuggerView::s_szPanelId))
    panels.OpenPanel(plAiDebuggerView::s_szPanelId);

  panels.SetCurrentPanel(plAiDebuggerView::s_szPanelId);
}

//////////////////////////////////////////////////////////////////////////

plAiOverlayMenuAction::plAiOverlayMenuAction(const plActionContext& context, const char* szName, const char* szIconPath)
  : plDynamicMenuAction(context, szName, szIconPath)
{
}

void plAiOverlayMenuAction::GetEntries(plDynamicArray<Item>& out_entries)
{
  out_entries.Clear();

  const plAiDebugSession* pSession = plAiDebugSession::Find(m_Context.m_pDocument);
  const plUInt32 uiOn = pSession != nullptr ? pSession->GetFrame().m_uiOverlays : 0;

  const plArrayPtr<const plAiDebugOverlay> overlays = plAiGetDebugOverlays();
  plStringView sGroup;

  for (plUInt32 i = 0; i < overlays.GetCount(); ++i)
  {
    // a separator between the groups, and the group in front of each label
    if (sGroup != overlays[i].m_szGroup && !sGroup.IsEmpty())
    {
      Item& separator = out_entries.ExpandAndGetRef();
      separator.m_ItemFlags.Add(Item::ItemFlags::Separator);
    }

    sGroup = overlays[i].m_szGroup;

    plStringBuilder sDisplay;
    sDisplay.SetFormat("{}: {}", overlays[i].m_szGroup, overlays[i].m_szLabel);

    Item& item = out_entries.ExpandAndGetRef();
    item.m_sDisplay = sDisplay;
    item.m_CheckState = (uiOn & PL_BIT(i)) ? Item::CheckMark::Checked : Item::CheckMark::Unchecked;
    item.m_UserValue = static_cast<plInt32>(i);
  }

  {
    Item& separator = out_entries.ExpandAndGetRef();
    separator.m_ItemFlags.Add(Item::ItemFlags::Separator);
  }

  {
    Item& item = out_entries.ExpandAndGetRef();
    item.m_sDisplay = "Watched agent only";
    item.m_CheckState = (pSession != nullptr && pSession->GetFrame().m_bWatchedOnly) ? Item::CheckMark::Checked : Item::CheckMark::Unchecked;
    item.m_UserValue = s_iWatchedOnly;
  }

  {
    Item& item = out_entries.ExpandAndGetRef();
    item.m_sDisplay = "Turn all off";
    item.m_UserValue = s_iAllOff;
  }

  {
    Item& separator = out_entries.ExpandAndGetRef();
    separator.m_ItemFlags.Add(Item::ItemFlags::Separator);
  }

  {
    Item& item = out_entries.ExpandAndGetRef();
    item.m_sDisplay = "Open AI Debugger";
    item.m_UserValue = s_iOpenDebugger;
  }
}

void plAiOverlayMenuAction::Execute(const plVariant& value)
{
  plAiDebugSession* pSession = plAiDebugSession::Find(m_Context.m_pDocument);
  if (pSession == nullptr || !value.CanConvertTo<plInt32>())
    return;

  const plInt32 iValue = value.ConvertTo<plInt32>();

  if (iValue == s_iOpenDebugger)
  {
    plAiDebuggerPanel::ShowPanel(m_Context.m_pDocument);
    return;
  }

  const plUInt32 uiCount = plAiGetDebugOverlays().GetCount();

  if (iValue == s_iWatchedOnly)
  {
    pSession->SetWatchedOnly(!pSession->GetFrame().m_bWatchedOnly);
  }
  else if (iValue == s_iAllOff)
  {
    for (plUInt32 i = 0; i < uiCount; ++i)
    {
      if (pSession->GetFrame().m_uiOverlays & PL_BIT(i))
        pSession->SetOverlay(i, false);
    }
  }
  else if (iValue >= 0 && static_cast<plUInt32>(iValue) < uiCount)
  {
    pSession->SetOverlay(iValue, (pSession->GetFrame().m_uiOverlays & PL_BIT(iValue)) == 0);
  }
}
