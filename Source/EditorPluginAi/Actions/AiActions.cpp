#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Actions/AiActions.h>
#include <EditorPluginAi/Dialogs/AiProjectSettingsEngineDlg.h>
#include <GuiFoundation/Action/ActionMapManager.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEditorAction, 0, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

plActionDescriptorHandle plAiActions::s_hCategoryAi;
plActionDescriptorHandle plAiActions::s_hProjectSettings;

void plAiActions::RegisterActions()
{
  s_hCategoryAi = PL_REGISTER_CATEGORY("Ai");
  s_hProjectSettings = PL_REGISTER_ACTION_1("Ai.Settings.Project", plActionScope::Document, "Ai", "", plAiEditorAction, plAiEditorAction::ActionType::ProjectSettings);
}

void plAiActions::UnregisterActions()
{
  plActionManager::UnregisterAction(s_hCategoryAi);
  plActionManager::UnregisterAction(s_hProjectSettings);
}

void plAiActions::MapMenuActions()
{
  /// \todo Is there a way to integrate into ALL document types in a specific menu (ie. project settings)
  // the scene windows and the package's own AI asset windows
  const char* szMenuBars[] = {"EditorPluginScene_Scene2MenuBar", "AiBehaviorAssetMenuBar", "AiArchetypeAssetMenuBar", "AiEqsQueryAssetMenuBar"};

  for (const char* szMenuBar : szMenuBars)
  {
    plActionMap* pMap = plActionMapManager::GetActionMap(szMenuBar);
    PL_ASSERT_DEV(pMap != nullptr, "Mapping the actions failed!");

    pMap->MapAction(s_hCategoryAi, "G.Plugins.Settings", 5.0f);
    pMap->MapAction(s_hProjectSettings, "G.Plugins.Settings", "Ai", 1.0f);
  }
}

plAiEditorAction::plAiEditorAction(const plActionContext& context, const char* szName, ActionType type)
  : plButtonAction(context, szName, false, "")
{
  m_Type = type;

  switch (m_Type)
  {
    case ActionType::ProjectSettings:
      SetIconPath(":/AiPlugin/plAiPlugin.svg");
      break;
  }
}

plAiEditorAction::~plAiEditorAction() = default;

void plAiEditorAction::Execute(const plVariant& value)
{
  if (m_Type == ActionType::ProjectSettings)
  {
    if (plQtAiProjectSettingsEngineDlg::Open(nullptr))
    {
      plToolsProject::BroadcastConfigChanged();
    }
  }
}
