#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Uuid.h>

class plStreamReader;
class plStreamWriter;

/// \brief One item of the viewport's AI overlay menu: a debug cvar and the value that turns it on.
struct plAiDebugOverlay
{
  const char* m_szGroup;
  const char* m_szLabel;
  const char* m_szCVar;
  plInt32 m_iOnValue = 1; ///< bool cvars: 1; int cvars: the value that shows the overlay
  plInt32 m_iOffValue = 0;
  bool m_bIntCVar = false;
};

/// \brief The overlays of the AI menu, in menu order.
PL_AIPLUGIN_DLL plArrayPtr<const plAiDebugOverlay> plAiGetDebugOverlays();

/// \brief The cvar of the menu's "Watched agent only": per-agent overlays only for the agent the AI Debugger watches.
inline constexpr const char* plAiDebugWatchedOnlyCVar = "AI.Agents.DebugWatchedOnly";

/// \brief The editor's AI tools and the engine process exchange debug data with these messages
/// (plSimpleDocumentConfigMsgToEngine "AiDebug.Poll" and plSimpleDocumentConfigMsgToEditor "AiDebug.Frame",
/// payload value = data buffer).
///
/// The editor polls a scene document a few times per second. Each poll names the agent the editor watches; the engine
/// watches that agent (and stops watching the previous one) and answers with the agent list, the watched agent's latest
/// decision and the frame stats. Objects are identified by their editor GUIDs.
struct PL_AIPLUGIN_DLL plAiDebugRequest
{
  static constexpr const char* s_szWhatToDo = "AiDebug.Poll";

  plUInt32 m_uiRequestId = 0;
  plUuid m_WatchedObject;           ///< the game object of the watched agent; invalid = watch nothing
  bool m_bCaptureEqs = false;       ///< record the watched agent's next EQS query
  double m_fHistorySince = -1.0e9; ///< world seconds; every agent's history entries after this are sent
  plUuid m_PickObject;             ///< an object selected in the scene; the engine looks for an agent in its subtree

  void Write(plStreamWriter& inout_stream) const;
  plResult Read(plStreamReader& inout_stream);
  void ToBuffer(plDataBuffer& out_buffer) const;
  plResult FromBuffer(const plDataBuffer& buffer);
};

struct PL_AIPLUGIN_DLL plAiDebugFrame
{
  static constexpr const char* s_szWhatToDo = "AiDebug.Frame";

  /// A behavior switch, or a behavior ending without a successor (m_sTo empty).
  struct HistoryEntry
  {
    double m_fTime = 0.0; ///< world seconds
    plString m_sFrom;
    plString m_sTo;
    plUInt8 m_uiToCategory = 0; ///< plAiBehaviorCategory
    float m_fWinnerScore = 0.0f;
    float m_fRunnerUpScore = 0.0f;
    plUInt8 m_uiOutcome = 0; ///< plAiDecisionOutcome
  };

  /// One row of the agent list.
  struct Agent
  {
    plUuid m_Object;
    plString m_sName;
    plString m_sArchetype; ///< the archetype asset reference as the component stores it
    plVec3 m_vPosition = plVec3::MakeZero();
    plString m_sActiveBehavior; ///< empty when idle
    plUInt8 m_uiActiveCategory = 0;
    plUuid m_Target;
    plString m_sTargetName;
    plUInt8 m_uiLodTier = 0;    ///< plAiBrainWorldModule::LodTier
    plUInt8 m_uiAlertLevel = 0; ///< plAiAlertLevel
    bool m_bLocked = false;
    bool m_bConfigured = false;
    plDynamicArray<HistoryEntry> m_History; ///< entries after the request's m_fHistorySince, oldest first
  };

  struct Consideration
  {
    plString m_sInput; ///< the input type's name, empty for an unconfigured consideration
    float m_fRaw = 0.0f;
    float m_fNormalized = 0.0f;
    float m_fValue = 1.0f;
  };

  struct Behavior
  {
    plString m_sName;
    plUInt8 m_uiCategory = 0;
    plUInt8 m_uiVeto = 0; ///< plAiBehaviorVeto
    bool m_bActive = false;
    float m_fUtility = 0.0f;
    float m_fScore = 0.0f;
    plInt32 m_iTarget = -1; ///< index into m_Targets
    plHybridArray<Consideration, 8> m_Considerations;
  };

  struct Target
  {
    plUuid m_Object;
    plString m_sName; ///< empty for events without a source
    plVec3 m_vLastKnownPosition = plVec3::MakeZero();
    float m_fConfidence = 0.0f;
    double m_fLastStimulusTime = 0.0; ///< world seconds
    double m_fLastSightTime = 0.0;    ///< world seconds, very negative when never seen
    plUInt8 m_uiLastSense = 0;        ///< plAiStimulusType
    plUInt8 m_uiSenses = 0;           ///< bit (1 << plAiStimulusType) per contributing sense
    bool m_bReported = false;
  };

  struct BlackboardEntry
  {
    plString m_sName;
    plString m_sType;
    plString m_sValue;
  };

  /// The watched agent's latest decision and its live state.
  struct Watched
  {
    plUuid m_Object;
    bool m_bHasDecision = false; ///< false until the first decision after watching began

    double m_fDecisionTime = 0.0;
    double m_fNextDecision = 0.0;
    plUInt32 m_uiDecisionCount = 0;
    plUInt8 m_uiOutcome = 0; ///< plAiDecisionOutcome
    plInt32 m_iActiveBehavior = -1;
    plInt32 m_iActiveTarget = -1;
    float m_fActiveScore = 0.0f;
    float m_fCommitBonus = 0.0f;
    bool m_bLocked = false;
    plInt32 m_iWinner = -1;
    float m_fWinnerScore = 0.0f;
    float m_fRunnerUpScore = 0.0f;

    plDynamicArray<Behavior> m_Behaviors;
    plDynamicArray<Target> m_Targets;
    plDynamicArray<BlackboardEntry> m_Blackboard;

    plString m_sNavigation;      ///< the navigation component type, empty without one
    plString m_sNavigationState; ///< its state name
    plVec3 m_vSteeringTarget = plVec3::MakeZero();

    bool m_bEqsCaptureArmed = false;
    plString m_sEqsQuery; ///< the last captured query, empty before the first capture
    double m_fEqsCaptureTime = 0.0;
    plUInt8 m_uiEqsStatus = 0; ///< plAiEqsQueryResult::Status
    plUInt32 m_uiEqsCandidates = 0;
    plUInt32 m_uiEqsSurvivors = 0;
    float m_fEqsBestScore = 0.0f;
  };

  struct Stats
  {
    plUInt32 m_uiAgents = 0;
    plUInt32 m_uiDecisions = 0;
    plUInt32 m_uiWokenDecisions = 0;
    plUInt32 m_uiDecisionBudget = 0;
    float m_fBrainMs = 0.0f;
    plUInt32 m_uiEqsExecuted = 0;
    plUInt32 m_uiEqsSuspended = 0;
    plUInt32 m_uiEqsQueued = 0;
    plUInt32 m_uiEqsTimedOut = 0;
    plUInt32 m_uiRaycasts = 0;
    plUInt32 m_uiCrowdAgents = 0;
  };

  plUInt32 m_uiRequestId = 0;
  double m_fWorldTime = 0.0;
  bool m_bSimulating = false;
  bool m_bHasBrain = false;      ///< false when the world has no AI agents module yet
  plUInt32 m_uiOverlays = 0;     ///< bit i: overlay i of plAiGetDebugOverlays() is on
  bool m_bWatchedOnly = false;   ///< plAiDebugWatchedOnlyCVar
  plUuid m_PickedAgent;          ///< the agent found in the request's m_PickObject (itself or a descendant); invalid if none

  plDynamicArray<Agent> m_Agents;
  bool m_bHasWatched = false;
  Watched m_Watched;
  Stats m_Stats;

  void Write(plStreamWriter& inout_stream) const;
  plResult Read(plStreamReader& inout_stream);
  void ToBuffer(plDataBuffer& out_buffer) const;
  plResult FromBuffer(const plDataBuffer& buffer);
};
