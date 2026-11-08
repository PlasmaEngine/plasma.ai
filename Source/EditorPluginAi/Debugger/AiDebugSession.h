#pragma once

#include <AiPlugin/UtilityAI/Decision/AiDebugProtocol.h>
#include <EditorPluginAi/EditorPluginAiDLL.h>
#include <Foundation/Communication/Event.h>
#include <Foundation/Containers/Deque.h>
#include <Foundation/Time/Time.h>
#include <GuiFoundation/UIServices/UIServices.h>

class plDocument;
class plEditorEngineDocumentMsg;
class plGameObjectDocument;

/// \brief The AI debugger's connection to one scene: polls the engine process for plAiDebugFrame and records them.
///
/// Polls about five times a second while the scene simulates and once a second otherwise. The recording keeps the
/// last 120 s of world time: every agent's behavior switches, and the watched agent's frames, so the timeline can be
/// scrubbed. A world time that goes back (the simulation restarted) starts a new recording.
///
/// The AI Debugger panel owns the session of its scene; other tools (the Behavior window's live score preview) find
/// the sessions with GetSessions.
class PL_EDITORPLUGINAI_DLL plAiDebugSession
{
  PL_DISALLOW_COPY_AND_ASSIGN(plAiDebugSession);

public:
  explicit plAiDebugSession(plGameObjectDocument* pDocument);
  ~plAiDebugSession();

  static constexpr double s_fRecordingSeconds = 120.0;

  /// How far back a frozen view keeps the recording (see KeepRecordingFrom).
  static constexpr double s_fMaxKeptSeconds = 600.0;

  /// The category of a behavior that ran before the recording saw its agent switch.
  static constexpr plUInt8 s_uiUnknownCategory = 0xFF;

  /// The sessions of all open scenes.
  static const plDynamicArray<plAiDebugSession*>& GetSessions();

  plGameObjectDocument* GetDocument() const { return m_pDocument; }

  /// The agent to watch, by its object's GUID; invalid watches nothing.
  void SetWatched(const plUuid& object);
  const plUuid& GetWatched() const { return m_Watched; }

  /// Watches the agent in an object selected in the scene, if it holds one: the object itself, or a descendant (a
  /// prefab instance's agent is a child the prefab creates). Selecting anything without an agent keeps the watch.
  void Pick(const plUuid& object);

  /// Records the watched agent's next EQS query.
  void CaptureNextEqsQuery() { m_bCaptureEqs = true; }

  bool HasFrame() const { return m_bHasFrame; }
  const plAiDebugFrame& GetFrame() const { return m_Frame; }

  /// The session of a scene document, or nullptr.
  static plAiDebugSession* Find(const plDocument* pDocument);

  /// Sets an overlay's cvar in the engine process (uiOverlay indexes plAiGetDebugOverlays()). The latest frame shows
  /// the new state right away; the next frame confirms it.
  void SetOverlay(plUInt32 uiOverlay, bool bOn);

  /// Sets plAiDebugWatchedOnlyCVar in the engine process.
  void SetWatchedOnly(bool bOn);

  /// Sets an int cvar in the engine process.
  void SetIntCVar(const char* szName, plInt32 iValue);

  /// \name Squad orders
  ///@{

  /// Orders a squad with the next poll. A region of zero size means no region; zero seconds last until cancelled.
  void IssueSquadOrder(plStringView sSquad, plUInt8 uiIntent, bool bRegion, const plVec2& vRegionMin, const plVec2& vRegionMax, float fSeconds, bool bNoRetreat);

  /// Ends a squad's order with the next poll.
  void CancelSquadOrder(plStringView sSquad);

  ///@}

  /// \name Navmesh sectors (the sector views)
  ///@{

  /// The navmesh sector the sector views outline and the Navigation tab describes: the one picked, else the one under
  /// the watched agent, else the first failed or busy one. x < 0 when there is none.
  plVec2I32 GetInspectedSector() const;

  /// Picks a sector; (-1, -1) goes back to following the watched agent.
  void PickSector(const plVec2I32& vCoord) { m_vPickedSector = vCoord; }
  bool IsSectorPicked() const { return m_vPickedSector.x >= 0; }

  /// Rebuilds the inspected sector with the next poll.
  void RebuildInspectedSector();

  /// The sector a world position lies in, from the navmesh grid of the latest frame; x < 0 without a navmesh.
  plVec2I32 GetSectorAt(const plVec3& vPosition) const;

  ///@}

  /// Increases with every received frame.
  plUInt32 GetFrameCounter() const { return m_uiFrameCounter; }

  /// Whether the engine answered within the last few seconds.
  bool IsConnected() const;

  /// Whether the scene simulates but its world time hasn't moved for a while: the simulation is paused, or the scene
  /// gets no frames because none of its viewports shows (the engine still answers the polls between frames).
  bool IsWorldStalled() const;

  /// One agent's behavior switches, oldest first.
  struct RecordedAgent
  {
    plUuid m_Object;
    plString m_sName;
    plString m_sArchetype;
    plString m_sInitialBehavior; ///< active when the recording first saw the agent
    plUInt8 m_uiInitialCategory = 0;
    double m_fFirstSeen = 0.0;
    double m_fLastSeen = 0.0; ///< agents that are gone stay until their last switch leaves the recording
    plDynamicArray<plAiDebugFrame::HistoryEntry> m_History;
  };

  /// A frame of the watched agent.
  struct RecordedWatch
  {
    double m_fTime = 0.0;
    plAiDebugFrame::Watched m_Watched;
  };

  const plDeque<RecordedAgent>& GetRecordedAgents() const { return m_RecordedAgents; }
  const RecordedAgent* FindRecordedAgent(const plUuid& object) const;

  /// The watched agent's recorded frame closest before fTime, or nullptr.
  const RecordedWatch* FindRecordedWatch(double fTime) const;

  /// The watched agent's first recorded frame at or after fTime, or nullptr.
  const RecordedWatch* FindRecordedWatchFrom(double fTime) const;

  /// The time of the watched agent's oldest recorded frame, or a negative value when there is none. The watched agent
  /// is only recorded since it was watched.
  double GetRecordedWatchStart() const { return m_RecordedWatches.IsEmpty() ? -1.0 : m_RecordedWatches.PeekFront().m_fTime; }

  double GetRecordingStart() const { return m_fRecordingStart; }

  /// Keeps everything recorded after fTime, even when it is older than s_fRecordingSeconds (up to s_fMaxKeptSeconds),
  /// so a frozen view doesn't lose what it shows while the simulation goes on. A negative time keeps the usual window.
  void KeepRecordingFrom(double fTime) { m_fKeepFrom = fTime; }

  /// The approximate memory the recording uses, for the timeline header.
  plUInt64 GetRecordingBytes() const;

  /// Broadcast when a frame arrived.
  plEvent<const plAiDebugSession&> m_FrameEvents;

private:
  void OnTick(const plEditorUiServices::TickEvent& e);
  void OnProcessMessage(const plEditorEngineDocumentMsg* pMsg);
  void SendPoll();
  void Record(const plAiDebugFrame& frame);
  void ClearRecording();

  plGameObjectDocument* m_pDocument = nullptr;

  plUuid m_Watched;
  plUuid m_PickObject; ///< sent with the next poll only
  bool m_bCaptureEqs = false;
  plVec2I32 m_vPickedSector = plVec2I32(-1, -1);
  bool m_bRebuildSector = false; ///< sent with the next poll only
  plAiDebugRequest::OrderCommand m_PendingOrder; ///< sent with the next poll only

  plUInt32 m_uiRequestId = 0;
  plTime m_LastPoll;
  plTime m_LastAnswer;
  bool m_bWaiting = false;

  double m_fAdvancedWorldTime = -1.0; ///< the world time of the last frame that moved it
  plTime m_LastAdvance;               ///< when that frame arrived

  plAiDebugFrame m_Frame;
  bool m_bHasFrame = false;
  plUInt32 m_uiFrameCounter = 0;

  double m_fHistorySince = -1.0e9;
  double m_fRecordingStart = 0.0;
  double m_fLastWorldTime = -1.0;
  double m_fKeepFrom = -1.0;
  plDeque<RecordedAgent> m_RecordedAgents;
  plDeque<RecordedWatch> m_RecordedWatches;
};
