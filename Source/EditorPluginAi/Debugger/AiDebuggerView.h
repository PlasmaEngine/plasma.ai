#pragma once

#include <EditorPluginAi/Assets/AiEngineView.h>
#include <EditorPluginAi/Debugger/AiDebugSession.h>
#include <Foundation/Containers/HashTable.h>

class plDocument;
class plGameObjectDocument;
struct plSelectionManagerEvent;

/// \brief The AI Debugger panel of a scene (M1): the agents, the watched agent's decision, perception, blackboard,
/// navigation and EQS, and a timeline of the recorded behavior switches.
///
/// Follows the simulation live, or stays frozen at a time of the recording (scrubbing the timeline freezes the view,
/// not the game). With Follow selection, selecting an agent in the scene watches it.
class plAiDebuggerView : public plAiEngineView
{
public:
  explicit plAiDebuggerView(plDocument* pDocument);
  ~plAiDebuggerView();

  static constexpr const char* s_szPanelId = "AiDebugger";

  plAiDebugSession& GetSession() { return *m_pSession; }

  /// Watches an agent and selects its object in the scene (clicking a row).
  void WatchAgent(const plUuid& object, bool bSelectInScene);

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;
  virtual void OnViewPointer(const plEditorUiPointerEvent& e) override;

private:
  enum class Tab : plUInt8
  {
    Decision,
    Perception,
    Blackboard,
    Navigation,
    Eqs,
  };

  /// A behavior span of one timeline row.
  struct Segment
  {
    double m_fStart = 0.0;
    double m_fEnd = 0.0;
    plString m_sBehavior;
    plUInt8 m_uiCategory = 0;
    plInt32 m_iEntry = -1; ///< the history entry that started it, -1 when it ran before the recording saw a switch
  };

  void OnFrame(const plAiDebugSession& session);
  void OnSelection(const plSelectionManagerEvent& e);

  void BuildToolbar(plUiElement* pRoot, float fWidth);
  void BuildAgentList(plUiElement* pRoot, const plRectFloat& rect);
  void BuildTabs(plUiElement* pRoot, const plRectFloat& rect);
  void BuildDecision(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched);
  void BuildPerception(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched);
  void BuildBlackboard(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched);
  void BuildNavigation(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched);
  void BuildEqs(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched);
  void BuildEmpty(plUiElement* pBody, float fWidth, plStringView sTitle, plStringView sLine);
  void BuildTimeline(plUiElement* pRoot, const plRectFloat& rect);
  void BuildWhy(plUiElement* pRoot, const plRectFloat& timeline);
  void BuildStatus(plUiElement* pRoot, const plRectFloat& rect);
  void UpdateStatus();

  /// The time the panel shows: the live world time, or the frozen head.
  double GetHeadTime() const;

  /// The watched agent's data at the head time, or nullptr.
  const plAiDebugFrame::Watched* GetShownWatched() const;

  /// The agents the list and the timeline show, after the filter.
  void GetShownAgents(plDynamicArray<const plAiDebugFrame::Agent*>& out_agents) const;

  /// The behavior spans of an agent between fFrom and fTo.
  void GetSegments(const plAiDebugSession::RecordedAgent& agent, double fFrom, double fTo, plDynamicArray<Segment>& out_segments) const;

  /// What an agent ran at fTime according to the recording.
  bool GetBehaviorAt(const plUuid& object, double fTime, plString& out_sBehavior, plUInt8& out_uiCategory) const;

  void SetFrozen(bool bFrozen);
  void ScrubTo(float x);
  bool PickTimeline(const plVec2& vPos, plUInt32& out_uiRow, double& out_fTime) const;

  /// Opens the "why" popover of the timeline block at vPos, if it started with a recorded switch.
  void OpenWhy(const plVec2& vPos);

  plGameObjectDocument* m_pDocument = nullptr;
  plUniquePtr<plAiDebugSession> m_pSession;

  Tab m_Tab = Tab::Decision;
  plString m_sFilter;
  bool m_bAlertedOnly = false;
  bool m_bFollowSelection = true;
  bool m_bFrozen = false;
  double m_fHead = 0.0;     ///< frozen: the shown world time
  double m_fViewEnd = 0.0;  ///< the timeline window's end (the live time when frozen began)
  plString m_sExpanded;     ///< the behavior whose considerations are open
  plUuid m_LastWatched;     ///< to notice a watch the session changed (a pick in the scene)

  /// The "why" popover: a recorded switch.
  plUuid m_WhyAgent;
  double m_fWhyTime = -1.0; ///< the time of the explained switch; negative while the popover is closed
  float m_fWhyX = 0.0f; ///< where the block was clicked; the popover opens above it

  /// Blackboard values of the previous frame; changed ones show in the accent colour.
  plHashTable<plString, plString> m_PreviousBlackboard;
  plHashTable<plString, plUInt32> m_ChangedAt;
  plUInt32 m_uiShownFrame = 0;

  // timeline geometry of the last build, for hit-testing
  plRectFloat m_TimelineTracks = plRectFloat(0, 0, 0, 0);
  double m_fTimelineStart = 0.0;
  double m_fTimelineSpan = 30.0;
  plHybridArray<plUuid, 16> m_TimelineRows;
  bool m_bScrubbing = false;
  bool m_bScrubDragged = false; ///< the press moved: a scrub, not a click on a block
  plVec2 m_vScrubPress = plVec2::MakeZero();
};
