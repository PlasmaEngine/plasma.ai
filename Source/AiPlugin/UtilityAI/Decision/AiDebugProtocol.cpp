#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Decision/AiDebugProtocol.h>
#include <Foundation/IO/MemoryStream.h>

// The plUuid stream operators return void, so GUIDs are written in statements of their own. The reader is not called
// "s": PL_SUCCEED_OR_RETURN declares a local of that name.

namespace
{
  constexpr plUInt8 s_uiProtocolVersion = 3;

  // clang-format off
  constexpr plAiDebugOverlay s_Overlays[] = {
    {"Navigation", "Navmesh", "AI.Navmesh.Visualize", 0, -1, true},
    {"Navigation", "Nav links", "AI.Navmesh.VisualizeLinks"},
    {"Navigation", "Blockers and obstacles", "AI.Navmesh.VisualizeBlockers"},
    {"Navigation", "Crowd avoidance", "AI.Crowd.Visualize"},
    {"Navigation", "Voxel grid", "AI.VoxelGrid.Visualize"},
    {"Perception", "Perceived targets", "AI.Agents.ShowPerception"},
    {"Decisions", "Behavior scores", "AI.Agents.ShowScores"},
    {"Decisions", "Behavior history", "AI.Agents.ShowHistory"},
    {"Tactical", "Cover points", "AI.Tactical.VisualizeCover"},
    {"Tactical", "Tactical queries", "AI.Tactical.VisualizeQueries"},
    {"Tactical", "Smart objects", "AI.SmartObjects.ShowDebug"},
    {"Tactical", "Squads and intent", "AI.Squad.ShowDebug"},
    {"Tactical", "EQS results", "AI.EQS.VisualizeQueries", 1, 0, true},
    {"Tactical", "EQS score breakdown", "AI.EQS.VisualizeScores"},
    {"Statistics", "EQS statistics", "AI.EQS.Stats"},
    {"Statistics", "Tactical statistics", "AI.Tactical.ShowStats"},
    {"Statistics", "Navmesh build statistics", "AI.Navmesh.ShowBuildStats"},
  };
  // clang-format on

  static_assert(PL_ARRAY_SIZE(s_Overlays) <= 32, "plAiDebugFrame::m_uiOverlays has one bit per overlay");

  template <typename T>
  void WriteToBuffer(const T& msg, plDataBuffer& out_buffer)
  {
    plContiguousMemoryStreamStorage storage;
    plMemoryStreamWriter writer(&storage);
    msg.Write(writer);

    out_buffer.SetCountUninitialized(storage.GetStorageSize32());
    if (storage.GetStorageSize32() > 0)
    {
      plMemoryUtils::Copy(out_buffer.GetData(), storage.GetData(), storage.GetStorageSize32());
    }
  }

  template <typename T>
  plResult ReadFromBuffer(T& msg, const plDataBuffer& buffer)
  {
    if (buffer.IsEmpty())
      return PL_FAILURE;

    plRawMemoryStreamReader reader(buffer.GetData(), buffer.GetCount());
    return msg.Read(reader);
  }

  template <typename Array, typename WriteItem>
  void WriteItems(plStreamWriter& out, const Array& items, WriteItem writeItem)
  {
    out << items.GetCount();
    for (const auto& item : items)
    {
      writeItem(item);
    }
  }

  template <typename Array, typename ReadItem>
  plResult ReadItems(plStreamReader& in, Array& out_items, ReadItem readItem)
  {
    plUInt32 uiCount = 0;
    in >> uiCount;

    if (uiCount > 1024 * 1024)
      return PL_FAILURE; // a corrupt count, don't allocate it

    out_items.SetCount(uiCount);

    plResult res = PL_SUCCESS;
    for (auto& item : out_items)
    {
      if (readItem(item).Failed())
      {
        res = PL_FAILURE;
      }
    }

    return res;
  }

  void WriteHistory(plStreamWriter& out, const plDynamicArray<plAiDebugFrame::HistoryEntry>& history)
  {
    WriteItems(out, history, [&](const plAiDebugFrame::HistoryEntry& h) {
      out << h.m_fTime << h.m_sFrom << h.m_sTo << h.m_uiToCategory << h.m_fWinnerScore << h.m_fRunnerUpScore << h.m_uiOutcome;
    });
  }

  plResult ReadHistory(plStreamReader& in, plDynamicArray<plAiDebugFrame::HistoryEntry>& out_history)
  {
    return ReadItems(in, out_history, [&](plAiDebugFrame::HistoryEntry& h) -> plResult {
      in >> h.m_fTime >> h.m_sFrom >> h.m_sTo >> h.m_uiToCategory >> h.m_fWinnerScore >> h.m_fRunnerUpScore >> h.m_uiOutcome;
      return PL_SUCCESS;
    });
  }

  void WriteWatched(plStreamWriter& out, const plAiDebugFrame::Watched& w)
  {
    out << w.m_Object;
    out << w.m_bHasDecision;
    out << w.m_fDecisionTime << w.m_fNextDecision << w.m_uiDecisionCount << w.m_uiOutcome;
    out << w.m_iActiveBehavior << w.m_iActiveTarget << w.m_fActiveScore << w.m_fCommitBonus << w.m_bLocked;
    out << w.m_iWinner << w.m_fWinnerScore << w.m_fRunnerUpScore;

    WriteItems(out, w.m_Behaviors, [&](const plAiDebugFrame::Behavior& b) {
      out << b.m_sName << b.m_uiCategory << b.m_uiVeto << b.m_bActive << b.m_fUtility << b.m_fScore << b.m_iTarget;
      WriteItems(out, b.m_Considerations, [&](const plAiDebugFrame::Consideration& c) { out << c.m_sInput << c.m_fRaw << c.m_fNormalized << c.m_fValue; });
    });

    WriteItems(out, w.m_Targets, [&](const plAiDebugFrame::Target& t) {
      out << t.m_Object;
      out << t.m_sName << t.m_vLastKnownPosition << t.m_fConfidence << t.m_fLastStimulusTime << t.m_fLastSightTime;
      out << t.m_uiLastSense << t.m_uiSenses << t.m_bReported;
    });

    WriteItems(out, w.m_Blackboard, [&](const plAiDebugFrame::BlackboardEntry& e) { out << e.m_sName << e.m_sType << e.m_sValue; });

    out << w.m_sNavigation << w.m_sNavigationState << w.m_vSteeringTarget;
    out << w.m_bEqsCaptureArmed << w.m_sEqsQuery << w.m_fEqsCaptureTime << w.m_uiEqsStatus << w.m_uiEqsCandidates << w.m_uiEqsSurvivors << w.m_fEqsBestScore;
  }

  plResult ReadWatched(plStreamReader& in, plAiDebugFrame::Watched& w)
  {
    in >> w.m_Object;
    in >> w.m_bHasDecision;
    in >> w.m_fDecisionTime >> w.m_fNextDecision >> w.m_uiDecisionCount >> w.m_uiOutcome;
    in >> w.m_iActiveBehavior >> w.m_iActiveTarget >> w.m_fActiveScore >> w.m_fCommitBonus >> w.m_bLocked;
    in >> w.m_iWinner >> w.m_fWinnerScore >> w.m_fRunnerUpScore;

    PL_SUCCEED_OR_RETURN(ReadItems(in, w.m_Behaviors, [&](plAiDebugFrame::Behavior& b) -> plResult {
      in >> b.m_sName >> b.m_uiCategory >> b.m_uiVeto >> b.m_bActive >> b.m_fUtility >> b.m_fScore >> b.m_iTarget;
      return ReadItems(in, b.m_Considerations, [&](plAiDebugFrame::Consideration& c) -> plResult {
        in >> c.m_sInput >> c.m_fRaw >> c.m_fNormalized >> c.m_fValue;
        return PL_SUCCESS;
      });
    }));

    PL_SUCCEED_OR_RETURN(ReadItems(in, w.m_Targets, [&](plAiDebugFrame::Target& t) -> plResult {
      in >> t.m_Object;
      in >> t.m_sName >> t.m_vLastKnownPosition >> t.m_fConfidence >> t.m_fLastStimulusTime >> t.m_fLastSightTime;
      in >> t.m_uiLastSense >> t.m_uiSenses >> t.m_bReported;
      return PL_SUCCESS;
    }));

    PL_SUCCEED_OR_RETURN(ReadItems(in, w.m_Blackboard, [&](plAiDebugFrame::BlackboardEntry& e) -> plResult {
      in >> e.m_sName >> e.m_sType >> e.m_sValue;
      return PL_SUCCESS;
    }));

    in >> w.m_sNavigation >> w.m_sNavigationState >> w.m_vSteeringTarget;
    in >> w.m_bEqsCaptureArmed >> w.m_sEqsQuery >> w.m_fEqsCaptureTime >> w.m_uiEqsStatus >> w.m_uiEqsCandidates >> w.m_uiEqsSurvivors >> w.m_fEqsBestScore;

    return PL_SUCCESS;
  }
} // namespace

plArrayPtr<const plAiDebugOverlay> plAiGetDebugOverlays()
{
  return plArrayPtr<const plAiDebugOverlay>(s_Overlays);
}

void plAiDebugRequest::Write(plStreamWriter& inout_stream) const
{
  inout_stream << s_uiProtocolVersion;
  inout_stream << m_uiRequestId;
  inout_stream << m_WatchedObject;
  inout_stream << m_bCaptureEqs;
  inout_stream << m_fHistorySince;
  inout_stream << m_PickObject;
}

plResult plAiDebugRequest::Read(plStreamReader& inout_stream)
{
  plUInt8 uiVersion = 0;
  inout_stream >> uiVersion;
  if (uiVersion != s_uiProtocolVersion)
    return PL_FAILURE;

  inout_stream >> m_uiRequestId;
  inout_stream >> m_WatchedObject;
  inout_stream >> m_bCaptureEqs;
  inout_stream >> m_fHistorySince;
  inout_stream >> m_PickObject;
  return PL_SUCCESS;
}

void plAiDebugRequest::ToBuffer(plDataBuffer& out_buffer) const
{
  WriteToBuffer(*this, out_buffer);
}

plResult plAiDebugRequest::FromBuffer(const plDataBuffer& buffer)
{
  return ReadFromBuffer(*this, buffer);
}

void plAiDebugFrame::Write(plStreamWriter& inout_stream) const
{
  plStreamWriter& out = inout_stream;

  out << s_uiProtocolVersion;
  out << m_uiRequestId << m_fWorldTime << m_bSimulating << m_bHasBrain << m_uiOverlays << m_bWatchedOnly;
  out << m_PickedAgent;

  WriteItems(out, m_Agents, [&](const Agent& a) {
    out << a.m_Object;
    out << a.m_sName << a.m_sArchetype << a.m_vPosition << a.m_sActiveBehavior << a.m_uiActiveCategory;
    out << a.m_Target;
    out << a.m_sTargetName << a.m_uiLodTier << a.m_uiAlertLevel << a.m_bLocked << a.m_bConfigured;
    WriteHistory(out, a.m_History);
  });

  out << m_bHasWatched;
  if (m_bHasWatched)
  {
    WriteWatched(out, m_Watched);
  }

  out << m_Stats.m_uiAgents << m_Stats.m_uiDecisions << m_Stats.m_uiWokenDecisions << m_Stats.m_uiDecisionBudget << m_Stats.m_fBrainMs;
  out << m_Stats.m_uiEqsExecuted << m_Stats.m_uiEqsSuspended << m_Stats.m_uiEqsQueued << m_Stats.m_uiEqsTimedOut << m_Stats.m_uiRaycasts;
  out << m_Stats.m_uiCrowdAgents;
}

plResult plAiDebugFrame::Read(plStreamReader& inout_stream)
{
  plStreamReader& in = inout_stream;

  plUInt8 uiVersion = 0;
  in >> uiVersion;
  if (uiVersion != s_uiProtocolVersion)
    return PL_FAILURE;

  in >> m_uiRequestId >> m_fWorldTime >> m_bSimulating >> m_bHasBrain >> m_uiOverlays >> m_bWatchedOnly;
  in >> m_PickedAgent;

  PL_SUCCEED_OR_RETURN(ReadItems(in, m_Agents, [&](Agent& a) -> plResult {
    in >> a.m_Object;
    in >> a.m_sName >> a.m_sArchetype >> a.m_vPosition >> a.m_sActiveBehavior >> a.m_uiActiveCategory;
    in >> a.m_Target;
    in >> a.m_sTargetName >> a.m_uiLodTier >> a.m_uiAlertLevel >> a.m_bLocked >> a.m_bConfigured;
    return ReadHistory(in, a.m_History);
  }));

  in >> m_bHasWatched;
  if (m_bHasWatched)
  {
    PL_SUCCEED_OR_RETURN(ReadWatched(in, m_Watched));
  }

  in >> m_Stats.m_uiAgents >> m_Stats.m_uiDecisions >> m_Stats.m_uiWokenDecisions >> m_Stats.m_uiDecisionBudget >> m_Stats.m_fBrainMs;
  in >> m_Stats.m_uiEqsExecuted >> m_Stats.m_uiEqsSuspended >> m_Stats.m_uiEqsQueued >> m_Stats.m_uiEqsTimedOut >> m_Stats.m_uiRaycasts;
  in >> m_Stats.m_uiCrowdAgents;

  return PL_SUCCESS;
}

void plAiDebugFrame::ToBuffer(plDataBuffer& out_buffer) const
{
  WriteToBuffer(*this, out_buffer);
}

plResult plAiDebugFrame::FromBuffer(const plDataBuffer& buffer)
{
  return ReadFromBuffer(*this, buffer);
}
