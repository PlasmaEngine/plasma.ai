#pragma once

#include <AiPlugin/Encounter/EncounterDebugProtocol.h>
#include <EditorPluginAi/Assets/AiEngineView.h>
#include <GuiFoundation/UIServices/UIServices.h>

class plDocument;
class plEditorEngineDocumentMsg;
class plGameObjectDocument;
struct plSelectionManagerEvent;

/// \brief The Encounter panel of a scene (M5): an encounter director's pacing curve against the measured intensity,
/// its waves and why a wave is held.
///
/// Polls the engine process like the AI Debugger, about five times a second while the scene simulates and once a
/// second otherwise. Outside simulation it shows the authored curve and waves. Clicking a wave selects its object, so
/// the property grid edits it; selecting a wave object in the scene selects its row.
class plAiEncounterView : public plAiEngineView
{
public:
  explicit plAiEncounterView(plDocument* pDocument);
  ~plAiEncounterView();

  static constexpr const char* s_szPanelId = "AiEncounter";

  const plAiEncounterDebugFrame& GetFrame() const { return m_Frame; }

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;

private:
  /// One measured point of the chart.
  struct Sample
  {
    float m_fPosition = 0.0f; ///< seconds or cleared waves, as the pacing counts
    float m_fIntensity = 0.0f;
  };

  void OnTick(const plEditorUiServices::TickEvent& e);
  void OnProcessMessage(const plEditorEngineDocumentMsg* pMsg);
  void OnSelection(const plSelectionManagerEvent& e);
  void SendPoll();
  void Record(const plAiEncounterDebugFrame& frame);

  void BuildToolbar(plUiElement* pRoot, float fWidth);
  void BuildPacing(plUiElement* pPane, float fWidth, float& inout_fY);
  void BuildHold(plUiElement* pPane, float fWidth, float& inout_fY);
  void BuildWaves(plUiElement* pPane, const plRectFloat& rect);
  void BuildIntensity(plUiElement* pPane, float fWidth, float& inout_fY);
  void BuildWaveDetails(plUiElement* pPane, float fWidth, float fY);
  void BuildStatus(plUiElement* pRoot, const plRectFloat& rect);
  void DrawChart(const plVec2& vSize);

  void SelectWave(plUInt32 uiWave, bool bSelectInScene);

  /// The right end of the chart: the pacing's length, or further when the encounter ran longer.
  float GetChartSpan() const;

  plGameObjectDocument* m_pDocument = nullptr;

  plAiEncounterDebugFrame m_Frame;
  bool m_bHasFrame = false;
  plUInt32 m_uiRequestId = 0;
  plTime m_LastPoll;
  bool m_bWaiting = false;
  bool m_bRestart = false; ///< sent with the next poll only

  plUuid m_Director;       ///< the director the panel shows; invalid = the first one
  plUuid m_SelectedWave;   ///< the wave's object
  plDynamicArray<Sample> m_Samples;
  double m_fLastEncounterTime = -1.0;
};
