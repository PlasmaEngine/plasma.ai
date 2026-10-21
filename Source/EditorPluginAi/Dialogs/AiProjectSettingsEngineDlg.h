#pragma once

#include <AiPlugin/Navigation/NavigationConfig.h>
#include <EditorPluginAi/EditorPluginAiDLL.h>
#include <EditorUi/Tabs.h>
#include <EditorUi/Tree.h>
#include <Foundation/Containers/Map.h>
#include <GuiFoundation/EngineUi/EngineDialog.h>

#include <QString>

struct plEditorUiMenu;

namespace plAiProjectSettings
{
  /// The message for a new config name, or an empty string when it can be used: the trimmed name must not be empty and
  /// must differ from every config's name. szKind is "Navmesh" or "Path Search".
  template <typename CONFIG>
  QString ValidateConfigName(const QString& sName, const plDynamicArray<CONFIG>& configs, const char* szKind)
  {
    const QString sTrimmed = sName.trimmed();
    if (sTrimmed.isEmpty())
      return QStringLiteral("The name can't be empty.");

    const plString sUtf8 = sTrimmed.toUtf8().constData();
    for (const CONFIG& config : configs)
    {
      if (config.m_sName == sUtf8)
        return QString("A %1 Config with this name already exists.").arg(szKind);
    }

    return QString();
  }
} // namespace plAiProjectSettings

/// \brief Ai Project Settings drawn by the engine: the tabs Ground Types, Navmesh, Path Search, Tactical and Squads of
/// RuntimeConfigs/AiPluginConfig.cfg.
///
/// Ground types are a list of the 32 slots: check a slot to use it, rename it with F2 or a double-click; <None> and
/// <Default> are fixed. Navmesh and path search configs are picked in a dropdown with Add (a validated name prompt) and
/// Remove (asks first) beside it, and an empty state with Add when there are none. The Path Search page lists <Default>
/// and the used ground types as they are now, each with a Traversal Allowed check box and a Traversal Cost field.
/// Numbers are checked as you type: text that is not exactly one number, or a value out of range, keeps the stored
/// value, the validation line names the field and OK stays disabled. Switching tab or config shows the stored values
/// again. Enter presses OK, also in the ground type list, unless a rename is open. Reset reloads the saved file; OK
/// saves it and refreshes the AiGroundType, AiPathSearchConfig and AiNavmeshConfig enums.
class plQtAiProjectSettingsEngineDlg : public plQtEngineDialog
{
public:
  enum class Tab : plUInt8
  {
    GroundTypes,
    Navmesh,
    PathSearch,
    Tactical,
    Squads,
  };

  plQtAiProjectSettingsEngineDlg(QWidget* pParent);
  ~plQtAiProjectSettingsEngineDlg();

  /// Opens the engine dialog, or the Qt one when engine dialogs are off. True when OK saved the settings.
  static bool Open(QWidget* pParent);

protected:
  void BuildBody(plUiElement* pBody, const plVec2& vSize) override;
  void OnButton(plStringView sId) override;
  void OnUiEvent(const plMsgUiEvent& event) override;
  void OnUiUpdate() override;
  plVec2 GetPreferredSize() const override;

private:
  class GroundTypeAdapter;
  friend class GroundTypeAdapter;

  enum class NumberType : plUInt8
  {
    Float,
    Degree,
    UInt8,
    UInt16,
  };

  /// A number field of the current page and the value it edits.
  struct NumberSetting
  {
    plString m_sKey;
    const char* m_szLabel = nullptr;
    const char* m_szUnit = "";
    const char* m_szToolTip = "";
    NumberType m_Type = NumberType::Float;
    void* m_pValue = nullptr;
    double m_fMin = 0.0;
    double m_fMax = 0.0;
    float m_fWidth = 110.0f;

    plString m_sShownText; ///< the text last read from (or written to) the field
    bool m_bInvalid = false;
  };

  // Data
  void LoadConfig();
  void SaveConfig();
  void SelectNavmesh(plInt32 iIndex);
  void SelectPathSearch(plInt32 iIndex);
  plUInt32 CountUsedGroundTypes() const;
  void ToggleGroundType(plUInt32 uiIndex);
  void ToggleTraversalAllowed(plUInt32 uiGroundType);

  // Number fields
  void ClearFields();
  void CollectFields();
  void AddField(plStringView sKey, const char* szLabel, NumberType type, void* pValue, double fMin, double fMax, const char* szUnit = "", float fWidth = 110.0f, const char* szToolTip = "");
  void WriteFields();
  void ReadFields();
  plString GetFieldText(const NumberSetting& field) const;
  static double GetValue(const NumberSetting& field);
  static void SetValue(const NumberSetting& field, double fValue);
  static plString FormatValue(const NumberSetting& field, double fValue);
  static plString GetRangeMessage(plStringView sLabel, double fMin, double fMax, bool bWhole);
  void UpdateValidation();

  // Document
  void UpdateTabs();
  void ShowTab(Tab tab);
  void BuildTabRow(plUiElement* pBody, float fWidth);
  void BuildGroundTypes(plUiElement* pPage, const plVec2& vSize);
  void BuildNavmesh(plUiElement* pPage, const plVec2& vSize);
  void BuildPathSearch(plUiElement* pPage, const plVec2& vSize);
  void BuildPathSearchHeader(plUiElement* pPage, const plRectFloat& rect);
  void BuildPathSearchRow(plUiElement* pRows, float y, float fWidth, plUInt32 uiGroundType, const NumberSetting& cost);
  void BuildTactical(plUiElement* pPage, const plVec2& vSize);
  void BuildSquads(plUiElement* pPage, const plVec2& vSize);
  float BuildConfigBar(plUiElement* pPage, float y, const char* szLabel, plStringView sCurrent, bool bHasConfigs, const char* szPrefix, const char* szScrollView = nullptr);
  void BuildEmptyState(plUiElement* pPage, const plRectFloat& rect, const char* szLine, const char* szAction, const char* szActionId);
  void BuildNumberRow(plEditorUiForm& ref_form, plUiElement* pParent, const NumberSetting& field);
  void BuildNumberWell(plUiElement* pParent, const plRectFloat& rect, const NumberSetting& field);
  void AddPageToolTips(const plEditorUiForm& form);
  float GetNavmeshContentHeight() const;
  void AddDropdownAnchor(plStringView sElement, const plRectFloat& rect, const char* szScrollView = nullptr);
  float GetScrollOffset(const char* szScrollView) const;

  // Interaction
  void ShowMenu(plStringView sElement, const plEditorUiMenu& menu, plDelegate<void(plUInt32)> onPick);
  void ShowConfigMenu(bool bNavmesh);
  void ShowCollisionLayerMenu();
  void ShowCoverNavmeshMenu();
  void AddConfig(bool bNavmesh);
  void RemoveConfig(bool bNavmesh);
  void Reset();
  void OnPointer(const plEditorUiPointerEvent& e);
  void OnKey(const plEditorUiKeyEvent& e);
  void OnTabChanged(plUInt32 uiTab);

  plAiNavigationConfig m_Config;
  plMap<plInt32, plString> m_CollisionLayers; ///< "PhysicsCollisionLayer": layer index to name
  Tab m_Tab = Tab::GroundTypes;
  plInt32 m_iNavmesh = -1;
  plInt32 m_iPathSearch = -1;

  plDynamicArray<NumberSetting> m_Fields;
  plHybridArray<plUInt32, plAiNumGroundTypes> m_PathRows; ///< the ground type of each Traversal Cost field, in page order
  bool m_bToggledOnPress = false; ///< the last press toggled a ground type, so its double-click does not rename

  plUniquePtr<plEditorUiTabs> m_pTabs;
  plUniquePtr<GroundTypeAdapter> m_pGroundTypeAdapter;
  plUniquePtr<plEditorUiTree> m_pGroundTypes;

  /// Where a dropdown was built, in surface units, for a menu opened from the keyboard.
  struct DropdownAnchor
  {
    plRectFloat m_Rect = plRectFloat(0, 0, 0, 0);
    const char* m_szScrollView = nullptr; ///< the scroll view the dropdown moves with, if any
  };

  plRectFloat m_PageRect = plRectFloat(0, 0, 0, 0);
  plMap<plString, DropdownAnchor> m_DropdownAnchors;
  plString m_sPressedElement; ///< the element of the last press; cleared by keys, so keyboard menus use the anchors
  plRectFloat m_PressedRect = plRectFloat(0, 0, 0, 0);

  plEventSubscriptionID m_PointerSubscription = 0;
  plEventSubscriptionID m_KeySubscription = 0;
  plEventSubscriptionID m_TabSubscription = 0;
};
