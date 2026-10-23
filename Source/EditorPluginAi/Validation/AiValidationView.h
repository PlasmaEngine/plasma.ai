#pragma once

#include <EditorPluginAi/Assets/AiEngineView.h>
#include <EditorPluginAi/Validation/AiValidation.h>

#include <atomic>

class plAiAssetDocumentWindow;
struct plAiValidationFix;
struct plAssetCuratorEvent;

/// \brief The engine-drawn "Validation" dock at the bottom of the AI asset windows.
///
/// A header with the error and warning counts and the scope ("This asset" or "With dependencies"), a filter row
/// (errors, warnings, search, the resolved blackboard owner) and one row per issue: severity, asset, element with its
/// key and the expected and found values, the message and the quick fixes. Clicking a row selects its element in
/// the window, which shows it in the properties. The dock validates again when the document or a project asset
/// changes.
class plAiValidationView : public plAiEngineView
{
public:
  plAiValidationView(plAiAssetDocumentWindow* pWindow);
  ~plAiValidationView();

  /// Validates again in the next UI frame.
  void Revalidate();

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;
  virtual bool IsRebuildBlocked() const override;
  virtual void OnViewUpdate() override;

private:
  struct Columns
  {
    float m_fSeverity = 0.0f;
    float m_fAsset = 0.0f;
    float m_fAssetWidth = 0.0f;
    float m_fElement = 0.0f;
    float m_fElementWidth = 0.0f;
    float m_fProblem = 0.0f;
    float m_fProblemWidth = 0.0f;
    float m_fFix = 0.0f;
    float m_fFixWidth = 0.0f;
    float m_fWidth = 0.0f;
  };

  float BuildHeader(plUiElement* pRoot, float fWidth);
  float BuildFilters(plUiElement* pRoot, float fTop, float fWidth);
  float BuildColumnHeader(plUiElement* pRoot, float fTop, const Columns& columns);
  void BuildRow(plUiElement* pBody, const Columns& columns, plUInt32 uiIssue, plUInt32 uiRow);
  void BuildFixes(plUiElement* pRow, const Columns& columns, plUInt32 uiIssue, plUInt32 uiRow);
  void BuildEmpty(plUiElement* pBody, float fWidth);
  void AddChip(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sLabel, bool bOn, plStringView sTooltip, Action onClick);

  Columns ComputeColumns(float fWidth) const;
  bool IsInScope(const plAiValidationIssue& issue) const;
  bool IsListed(const plAiValidationIssue& issue) const;

  void SetDependencies(bool bDependencies);
  void SelectIssue(plUInt32 uiIssue);
  void RunFix(plUInt32 uiIssue, plUInt32 uiFix, plStringView sAnchor);
  void ShowFixMenu(plStringView sAnchor, const plAiValidationIssue& issue, const plAiValidationFix& fix);
  void ShowMoreMenu(plStringView sAnchor, plUInt32 uiIssue);
  void ApplyFix(const plAiValidationIssue& issue, const plAiValidationFix& fix, const plString& sChoice);

  void OnCuratorEvent(const plAssetCuratorEvent& e);

  plAiAssetDocumentWindow* m_pWindow = nullptr;
  plAiValidationReport m_Report;
  bool m_bValidate = true;
  std::atomic<bool> m_bAssetsChanged = false;

  bool m_bDependencies = true;
  bool m_bErrors = true;
  bool m_bWarnings = true;
  plString m_sSearch;
  plString m_sSelected; ///< the GetId() of the selected issue
  plString m_sFixError; ///< why the last fix failed

  plEventSubscriptionID m_CuratorSubscription = 0;
};
