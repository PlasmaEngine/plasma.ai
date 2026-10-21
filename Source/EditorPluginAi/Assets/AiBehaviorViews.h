#pragma once

#include <EditorPluginAi/Assets/AiEngineView.h>

#include <Foundation/Types/Uuid.h>

class plAiConsiderationObject;
class plQtAiBehaviorAssetDocumentWindow;

/// \brief The engine-drawn "Behavior" dock: the header (name, category, weight, commit, cooldown, logic and the
/// state machine) and the consideration cards.
class plQtAiBehaviorView : public plQtAiEngineView
{
public:
  plQtAiBehaviorView(plQtAiBehaviorAssetDocumentWindow* pWindow);
  ~plQtAiBehaviorView();

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;
  virtual bool IsRebuildBlocked() const override;

private:
  float BuildHeader(plUiElement* pRoot, float fWidth);
  float BuildLogicRow(plUiElement* pRoot, float fTop, float fWidth);
  void BuildConsiderationCard(plUiElement* pBody, float fWidth, plUInt32 uiIndex, plUInt32 uiCount, const plUuid& guid, const plAiConsiderationObject* pNative);

  void ShowCategoryMenu();
  void ShowLogicMenu();
  void ShowCurveMenu(plStringView sAnchor, const plUuid& guid);
  void ShowAddMenu(plStringView sAnchor);

  plQtAiBehaviorAssetDocumentWindow* m_pWindow = nullptr;
  plHybridArray<const plRTTI*, 32> m_MenuTypes;
};

/// \brief The engine-drawn "Score Preview" dock: a simulated raw value per consideration and the utility the
/// runtime would compute from them (plAiUtilityEvaluator::ScoreUtility).
class plQtAiScorePreviewView : public plQtAiEngineView
{
public:
  plQtAiScorePreviewView(plQtAiBehaviorAssetDocumentWindow* pWindow);
  ~plQtAiScorePreviewView();

  /// Recomputes the result from the current ranges, curves and weights (properties changed).
  void Recompute();

  /// Puts every slider back to 70 % of its range (the considerations changed).
  void ResetSliders();

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;

private:
  void BuildSituation(plUiElement* pBody, float fWidth, plUInt32 uiRows);
  void BuildOptions(plUiElement* pPanel, float fWidth, float fTop);
  void BuildResult(plUiElement* pBody, float fWidth);
  void DrawBar(float fUtility, float fCommitThreshold, bool bVetoed);

  plQtAiBehaviorAssetDocumentWindow* m_pWindow = nullptr;
  plHybridArray<float, 8> m_SliderValues; ///< 0..1 of each consideration's input range
  float m_fWeightScale = 1.0f;
  bool m_bRunning = false;
  bool m_bCooldown = false;
  float m_fBarWidth = 100.0f;
};
