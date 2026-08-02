#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorFramework/Actions/AssetActions.h>
#include <EditorFramework/Actions/CommonAssetActions.h>
#include <EditorFramework/Actions/ProjectActions.h>
#include <EditorPluginAi/Actions/AiActions.h>
#include <EditorPluginAi/Dialogs/AiProjectSettingsDlg.moc.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <GuiFoundation/Action/CommandHistoryActions.h>
#include <GuiFoundation/Action/DocumentActions.h>
#include <GuiFoundation/Action/EditActions.h>
#include <GuiFoundation/Action/StandardMenus.h>
#include <GuiFoundation/PropertyGrid/PropertyMetaState.h>
#include <GuiFoundation/UIServices/DynamicEnums.h>
#include <GuiFoundation/UIServices/DynamicStringEnum.h>

static void ToolsProjectEventHandler(const plToolsProjectEvent& e);

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
  plToolsProject::GetSingleton()->s_Events.AddEventHandler(ToolsProjectEventHandler);

  plAiActions::RegisterActions();
  plAiActions::MapMenuActions();

  RegisterAiAssetActionMaps("AiBehaviorAsset");
  RegisterAiAssetActionMaps("AiArchetypeAsset");
  RegisterAiAssetActionMaps("AiEqsQueryAsset");
}

void OnUnloadPlugin()
{
  plAiActions::UnregisterActions();
  plToolsProject::GetSingleton()->s_Events.RemoveEventHandler(ToolsProjectEventHandler);
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
