#pragma once

#include <EditorPluginAi/Assets/AiEngineView.h>
#include <EditorPluginAi/Assets/AiEqsPreviewSimulation.h>

#include <Foundation/Types/Uuid.h>

class plAiEqsTest;
class plAiEqsTestObject;
class plAiEqsQueryAssetDocumentWindow;

/// \brief The engine-drawn "EQS Query" dock: header, context strip, generator card, test cards and a status row.
///
/// Every edit goes through the window's document operations, which keep the undo steps and the live link to the
/// engine process.
class plAiEqsQueryView : public plAiEngineView
{
public:
  plAiEqsQueryView(plAiEqsQueryAssetDocumentWindow* pWindow);
  ~plAiEqsQueryView();

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;
  virtual bool IsRebuildBlocked() const override;

private:
  float BuildHeader(plUiElement* pRoot, float fWidth);
  float BuildContexts(plUiElement* pRoot, float fTop, float fWidth);
  void BuildGenerator(plUiElement* pBody, float fWidth);
  void BuildTestsSection(plUiElement* pBody);
  void BuildTestCard(plUiElement* pBody, float fWidth, plUInt32 uiIndex, plUInt32 uiCount, const plUuid& guid, const plAiEqsTestObject* pNative);
  void BuildTestIdentity(plUiElement* pCard, plAiFlowLayout& ref_flow, const plAiEqsTest* pTest);
  void BuildTestTools(plUiElement* pCard, plAiFlowLayout& ref_flow, plUInt32 uiIndex, plUInt32 uiCount, const plUuid& guid, const plAiEqsTestObject* pNative, bool bSelected);
  float BuildTestFilters(plUiElement* pCard, float fWidth, float fTop, plUInt32 uiIndex, const plUuid& guid, const plAiEqsTest* pTest);
  void BuildStatus(plUiElement* pRoot);

  void ShowRunModeMenu();
  void ShowNavmeshMenu();
  void ShowAroundMenu();
  void ShowConditionMenu(plStringView sAnchor, const plUuid& guid);
  void ShowMissingDataMenu(plStringView sAnchor, const plUuid& guid);
  void ShowCurveMenu(plStringView sAnchor, const plUuid& guid);
  /// A searchable menu of the concrete types derived from pBaseType, grouped by getCategory.
  void ShowTypeMenu(plStringView sAnchor, const plRTTI* pBaseType, plStringView sPlaceholder, std::function<plString(const plRTTI*)> getCategory, std::function<void(const plRTTI*)> onPick);

  plAiEqsQueryAssetDocumentWindow* m_pWindow = nullptr;
  plHybridArray<const plRTTI*, 32> m_MenuTypes;
  plHybridArray<plString, 8> m_MenuChoices;
};

/// \brief The engine-drawn "Query Preview" dock: the simulated situation, the top-down candidates and the score
/// breakdown of the selected candidate.
///
/// The querier, the threat and both occluder ends can be dragged; clicking a candidate shows its breakdown. The
/// query runs locally (plAiEqsPreviewSimulation) on every change.
class plAiEqsPreviewView : public plAiEngineView
{
public:
  plAiEqsPreviewView(plAiEqsQueryAssetDocumentWindow* pWindow);
  ~plAiEqsPreviewView();

  /// Runs the query again and redraws (properties or the situation changed).
  void Recompute();

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;
  virtual void OnViewPointer(const plEditorUiPointerEvent& e) override;

private:
  plUInt32 GetTestRowCount() const;
  void DrawScatter();
  void DrawLegend();
  void DrawBreakdown();
  void UpdateThreatDistance();
  void SetThreatDistance(float fDistance);

  plVec2 ToScreen(const plVec2& vMeters) const;
  plVec2 ToMeters(const plVec2& vLocal) const;

  plAiEqsQueryAssetDocumentWindow* m_pWindow = nullptr;
  plAiEqsPreviewSimulation m_Simulation;
  plInt32 m_iSelected = -1;
  bool m_bAllowDrag = true;

  plRectFloat m_ScatterRect = plRectFloat(0, 0, 1, 1); ///< the scatter's size, at its local origin
  plRectFloat m_PressRect = plRectFloat(0, 0, 0, 0);   ///< where the scatter was on the surface when pressed
  plInt32 m_iDragTarget = -1;                         ///< 0 querier, 1 threat, 2 occluder A, 3 occluder B
  plUInt32 m_uiBreakdownRows = 0;
};
