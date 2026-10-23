#pragma once

#include <EditorPluginAi/Assets/AiEngineView.h>

#include <Foundation/Types/Uuid.h>

class plAiArchetypeAssetDocumentWindow;
struct plAiBehaviorPeek;

/// \brief The engine-drawn behavior stack: the header (team, blackboard template, Add Behavior), the referenced
/// behaviors grouped into the category bands they arbitrate in, plus the unresolved ones, and a read-only peek into
/// the selected behavior's considerations.
class plAiArchetypeStackView : public plAiEngineView
{
public:
  plAiArchetypeStackView(plAiArchetypeAssetDocumentWindow* pWindow);
  ~plAiArchetypeStackView();

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;
  virtual bool IsRebuildBlocked() const override;

private:
  float BuildHeader(plUiElement* pRoot, float fWidth);
  void BuildRow(plUiElement* pBody, float fWidth, plUInt32 uiIndex, const plUuid& guid, const plAiBehaviorPeek& peek, plStringView sReference, float fWeightScale);
  float BuildPeek(plUiElement* pBody, float fWidth);

  plAiArchetypeAssetDocumentWindow* m_pWindow = nullptr;
};

/// \brief The engine-drawn settings panel: the senses with a memory preview driven by the runtime's perception
/// rules, reactivity, and the scoring version with a preview of what migrating to version 2 changes.
class plAiArchetypeSettingsView : public plAiEngineView
{
public:
  plAiArchetypeSettingsView(plAiArchetypeAssetDocumentWindow* pWindow);
  ~plAiArchetypeSettingsView();

  /// The sense tabs, in plAiStimulusType order.
  enum class Sense : plUInt8
  {
    Sight,
    Hearing,
    Damage,
    Custom,
  };

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;
  virtual bool IsRebuildBlocked() const override;

private:
  using NumberAction = std::function<void(double)>;

  /// One number setting: its label, the field and an optional note after it. Disabled rows show the value dimmed.
  struct NumberRow
  {
    plStringView m_sName;
    plStringView m_sLabel;
    plStringView m_sTooltip;
    plStringView m_sUnit;
    plStringView m_sAfter;
    double m_fValue = 0.0;
    double m_fMin = 0.0;
    double m_fMax = 1.0;
    plUInt32 m_uiDecimals = 2;
    bool m_bEnabled = true;
    NumberAction m_OnValue;
  };

  // controls
  void BuildSectionHeader(plUiElement* pBody, float fWidth, plStringView sName, plStringView sTitle, plStringView sHint, bool* pOpen);
  void AddRowLabel(plUiElement* pBox, const plRectFloat& cell, plStringView sName, plStringView sLabel, plStringView sTooltip, bool bEnabled);
  void AddNumberRow(plUiElement* pBox, const plRectFloat& cell, const NumberRow& row);
  void AddSwitch(plUiElement* pParent, const plRectFloat& rect, plStringView sName, bool bOn, plStringView sTooltip, Action onToggle);
  void AddSegment(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sLabel, bool bOn, plStringView sTooltip, Action onClick);

  // senses
  void BuildSenses(plUiElement* pBody, float fWidth);
  void BuildSenseTabs(plUiElement* pBody, float fWidth);
  void BuildSightFields(plUiElement* pBody, float fWidth);
  void BuildSenseFields(plUiElement* pBody, float fWidth);
  void BuildMemoryPreview(plUiElement* pBody, float fWidth);
  void BuildMaxTargets(plUiElement* pBody, float fWidth);
  void BuildAlertLevels(plUiElement* pBody, float fWidth);

  // reactivity
  void BuildReactivity(plUiElement* pBody, float fWidth);
  void BuildWakeFields(plUiElement* pBody, float fWidth);
  void BuildSwitchingFields(plUiElement* pBody, float fWidth);
  void BuildLockFields(plUiElement* pBody, float fWidth);

  // scoring
  void BuildScoring(plUiElement* pBody, float fWidth);
  void BuildVersionRow(plUiElement* pBody, float fWidth);
  void BuildMigration(plUiElement* pBody, float fWidth);
  void BuildMigrationFooter(plUiElement* pBox, float fTop, float fWidth, plStringView sOrder);

  void UpdateStatus();

  plAiArchetypeAssetDocumentWindow* m_pWindow = nullptr;

  Sense m_Tab = Sense::Sight;
  bool m_bSensesOpen = true;
  bool m_bReactivityOpen = true;
  bool m_bScoringOpen = true;
  bool m_bMigrationOpen = false;
};
