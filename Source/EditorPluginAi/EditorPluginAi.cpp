#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorFramework/Actions/AssetActions.h>
#include <EditorFramework/Actions/CommonAssetActions.h>
#include <EditorFramework/Actions/ProjectActions.h>
#include <AiPlugin/Navigation/NavigationConfig.h>
#include <EditorFramework/EngineShell/WindowContent.h>
#include <EditorPluginAi/Actions/AiActions.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Configuration/Plugin.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <GuiFoundation/Action/CommandHistoryActions.h>
#include <GuiFoundation/Action/DocumentActions.h>
#include <GuiFoundation/Action/EditActions.h>
#include <GuiFoundation/Action/StandardMenus.h>
#include <GuiFoundation/PropertyGrid/PropertyMetaState.h>
#include <GuiFoundation/UIServices/DynamicEnums.h>
#include <GuiFoundation/UIServices/DynamicStringEnum.h>

static void ToolsProjectEventHandler(const plToolsProjectEvent& e);

/// The package's icon folder: the icons under their ids (":/AssetIcons/AiBehavior.svg" is
/// "EditorIcons/AssetIcons/AiBehavior.svg"). The plugin binary lives in <package>/Bin/<configuration>.
static plString s_sIconFolder;

static void RegisterIconFolder()
{
  plStringBuilder sPlugin, sCopy;
  plPlugin::GetPluginPaths("plEditorPluginAi", sPlugin, sCopy, 0);

  plStringBuilder sFolder = sPlugin.GetFileDirectory();
  sFolder.AppendPath("..", "..", "EditorIcons");
  sFolder.MakeCleanPath();

  s_sIconFolder = sFolder;
  plEditorUi::AddIconFolder(s_sIconFolder);
}

static void UnregisterIconFolder()
{
  plEditorUi::RemoveIconFolder(s_sIconFolder);
  s_sIconFolder.Clear();
}

// The AI editors in the engine shell: the main view gets the width; the preview and Raw Properties share the right
// column, one above the other, as both follow the selection.

static void ArrangeAiArchetype(plEditorUiDockManager& ref_panels)
{
  ref_panels.DockPanel("AiArchetypeInsightDockWidget", "AiArchetypeAssetDockWidget", plEditorUiDockSide::Right, 0.36f);
  ref_panels.DockPanel("AiArchetypeRawPropertyDockWidget", "AiArchetypeInsightDockWidget", plEditorUiDockSide::Bottom, 0.45f);
}

static void ArrangeAiBehavior(plEditorUiDockManager& ref_panels)
{
  ref_panels.DockPanel("AiBehaviorScorePreviewDockWidget", "AiBehaviorAssetDockWidget", plEditorUiDockSide::Right, 0.36f);
  ref_panels.DockPanel("AiBehaviorRawPropertyDockWidget", "AiBehaviorScorePreviewDockWidget", plEditorUiDockSide::Bottom, 0.4f);
}

static void ArrangeAiEqsQuery(plEditorUiDockManager& ref_panels)
{
  // the preview is a top-down scatter that needs room
  ref_panels.DockPanel("AiEqsQueryPreviewDockWidget", "AiEqsQueryAssetDockWidget", plEditorUiDockSide::Right, 0.45f);
  ref_panels.DockPanel("AiEqsQueryRawPropertyDockWidget", "AiEqsQueryPreviewDockWidget", plEditorUiDockSide::Bottom, 0.35f);
}

static void RegisterAiAssetActionMaps(const char* szAssetName)
{
  plStringBuilder sMapping;

  // Menu Bar
  {
    sMapping.SetFormat("{}MenuBar", szAssetName);
    plActionMapManager::RegisterActionMap(sMapping).AssertSuccess();

    plStandardMenus::MapActions(sMapping, plStandardMenuTypes::Default | plStandardMenuTypes::Edit);
    plProjectActions::MapActions(sMapping);
    plDocumentActions::MapMenuActions(sMapping);
    plAssetActions::MapMenuActions(sMapping);
    plCommandHistoryActions::MapActions(sMapping);
    plEditActions::MapActions(sMapping, false, false);
  }

  // Tool Bar
  {
    sMapping.SetFormat("{}ToolBar", szAssetName);
    plActionMapManager::RegisterActionMap(sMapping).AssertSuccess();

    plDocumentActions::MapToolbarActions(sMapping);
    plCommandHistoryActions::MapActions(sMapping, "");
    plAssetActions::MapToolBarActions(sMapping, true);
  }
}

void OnLoadPlugin()
{
  RegisterIconFolder();

  plToolsProject::GetSingleton()->s_Events.AddEventHandler(ToolsProjectEventHandler);

  plAiActions::RegisterActions();

  RegisterAiAssetActionMaps("AiBehaviorAsset");
  RegisterAiAssetActionMaps("AiArchetypeAsset");
  RegisterAiAssetActionMaps("AiEqsQueryAsset");

  // after the asset menu bars exist, which also get the Ai Project Settings entry
  plAiActions::MapMenuActions();

  plWindowContentProvider::SetDefaultArrangement("AiArchetype", &ArrangeAiArchetype);
  plWindowContentProvider::SetDefaultArrangement("AiBehavior", &ArrangeAiBehavior);
  plWindowContentProvider::SetDefaultArrangement("AiEqsQuery", &ArrangeAiEqsQuery);
}

void OnUnloadPlugin()
{
  plWindowContentProvider::SetDefaultArrangement("AiArchetype", nullptr);
  plWindowContentProvider::SetDefaultArrangement("AiBehavior", nullptr);
  plWindowContentProvider::SetDefaultArrangement("AiEqsQuery", nullptr);

  plAiActions::UnregisterActions();
  plToolsProject::GetSingleton()->s_Events.RemoveEventHandler(ToolsProjectEventHandler);

  UnregisterIconFolder();
}

PL_PLUGIN_ON_LOADED()
{
  OnLoadPlugin();
}

PL_PLUGIN_ON_UNLOADED()
{
  OnUnloadPlugin();
}

void UpdateGroundTypeDynamicEnumValues()
{
  plAiNavigationConfig cfg;
  cfg.Load().IgnoreResult();

  {
    auto& cfe = plDynamicEnum::GetDynamicEnum("AiGroundType");
    cfe.Clear();

    cfe.SetValueAndName(-1, "<Undefined>");

    // add all names and values that are active
    for (plInt32 i = 0; i < plAiNumGroundTypes; ++i)
    {
      if (cfg.m_GroundTypes[i].m_bUsed)
      {
        cfe.SetValueAndName(i, cfg.m_GroundTypes[i].m_sName);
      }
    }
  }

  {
    auto& de = plDynamicStringEnum::CreateDynamicEnum("AiPathSearchConfig");
    de.Clear();

    for (const auto& pc : cfg.m_PathSearchConfigs)
    {
      de.AddValidValue(pc.m_sName);
    }

    de.SortValues();
  }

  {
    auto& de = plDynamicStringEnum::CreateDynamicEnum("AiNavmeshConfig");
    de.Clear();

    for (const auto& pc : cfg.m_NavmeshConfigs)
    {
      de.AddValidValue(pc.m_sName);
    }

    de.SortValues();
  }
}

static void ToolsProjectEventHandler(const plToolsProjectEvent& e)
{
  if (e.m_Type == plToolsProjectEvent::Type::ProjectSaveState)
  {
  }

  if (e.m_Type == plToolsProjectEvent::Type::ProjectOpened)
  {
    UpdateGroundTypeDynamicEnumValues();
  }
}
