#pragma once

#include <EditorPluginAi/Assets/AiEngineView.h>

#include <AiPlugin/UtilityAI/Decision/AiDebugProtocol.h>
#include <Foundation/Types/Uuid.h>
#include <GuiFoundation/UIServices/UIServices.h>

class plAiConsiderationObject;
class plAiBehaviorAssetDocumentWindow;

/// \brief The engine-drawn "Behavior" dock: the header (name, category, weight, commit, cooldown, logic and the
/// state machine) and the consideration cards.
class plAiBehaviorView : public plAiEngineView
{
public:
  plAiBehaviorView(plAiBehaviorAssetDocumentWindow* pWindow);
  ~plAiBehaviorView();

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

  plAiBehaviorAssetDocumentWindow* m_pWindow = nullptr;
  plHybridArray<const plRTTI*, 32> m_MenuTypes;
};

/// \brief The engine-drawn "Score Preview" dock: a raw value per consideration and the utility the runtime would
/// compute from them (plAiUtilityEvaluator::ScoreUtility).
///
/// The values are simulated (sliders) or live: the inputs of this behavior's latest decision in the agent the AI
/// Debugger of a simulating scene watches. Copy to sliders keeps the live values to experiment with.
class plAiScorePreviewView : public plAiEngineView
{
public:
  plAiScorePreviewView(plAiBehaviorAssetDocumentWindow* pWindow);
  ~plAiScorePreviewView();

  /// Recomputes the result from the current ranges, curves and weights (properties changed).
  void Recompute();

  /// Puts every slider back to 70 % of its range (the considerations changed).
  void ResetSliders();

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;

private:
  /// Where the live values come from, and why there are none.
  enum class LiveState : plUInt8
  {
    NoScene,     ///< no scene's AI Debugger has data
    NoAgent,     ///< no agent is watched
    NoBehavior,  ///< the watched agent's archetype doesn't use this behavior
    NoDecision,  ///< the agent hasn't decided since it is watched
    Stalled,     ///< the scene simulates but its world doesn't advance: paused, or its viewport is hidden
    Live,
  };

  void BuildSource(plUiElement* pRoot, float fWidth);
  void BuildSituation(plUiElement* pBody, float fWidth, plUInt32 uiRows);
  void BuildOptions(plUiElement* pPanel, float fWidth, float fTop);
  void BuildResult(plUiElement* pBody, float fWidth);
  void BuildCompetition(plUiElement* pBody, float fWidth);
  void DrawBar(float fUtility, float fCommitThreshold, bool bVetoed);
  void DrawSparklines();

  void SetLive(bool bLive);
  void ShowAgentMenu();
  void CopyToSliders();
  void OnTick(const plEditorUiServices::TickEvent& e);

  /// Reads the live values; true when the state or the values changed.
  bool UpdateLive();
  plString GetBehaviorName() const;

  plAiBehaviorAssetDocumentWindow* m_pWindow = nullptr;
  plHybridArray<float, 8> m_SliderValues; ///< 0..1 of each consideration's input range
  float m_fWeightScale = 1.0f;
  bool m_bRunning = false;
  bool m_bCooldown = false;
  float m_fBarWidth = 100.0f;

  bool m_bLive = false;
  LiveState m_LiveState = LiveState::NoScene;
  plUInt32 m_uiLiveFrames = 0;        ///< the sessions' frame counters added up, to notice new frames
  plString m_sLiveAgent;
  float m_fLiveUtility = 0.0f;        ///< what the runtime computed for this behavior
  bool m_bLiveActive = false;
  plHybridArray<float, 8> m_LiveRaw;  ///< the raw input values of the latest decision
  plHybridArray<plHybridArray<float, 40>, 8> m_LiveHistory; ///< the last 40 outputs per consideration
  plAiDebugFrame::Watched m_LiveWatched; ///< the decision the values come from
  float m_fSparkWidth = 90.0f;
};
