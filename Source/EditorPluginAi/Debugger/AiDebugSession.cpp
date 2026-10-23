#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessMessages.h>
#include <EditorFramework/Document/GameObjectDocument.h>
#include <EditorFramework/IPC/EngineProcessConnection.h>
#include <EditorPluginAi/Debugger/AiDebugSession.h>

namespace
{
  plDynamicArray<plAiDebugSession*> s_Sessions;

  constexpr double s_fSimulatingPollSeconds = 0.2;
  constexpr double s_fIdlePollSeconds = 1.0;
  constexpr double s_fAnswerTimeoutSeconds = 2.0;
} // namespace

plAiDebugSession::plAiDebugSession(plGameObjectDocument* pDocument)
  : m_pDocument(pDocument)
{
  s_Sessions.PushBack(this);

  plEditorUiServices::s_TickEvent.AddEventHandler(plMakeDelegate(&plAiDebugSession::OnTick, this));
  m_pDocument->m_ProcessMessageEvent.AddEventHandler(plMakeDelegate(&plAiDebugSession::OnProcessMessage, this));
}

plAiDebugSession::~plAiDebugSession()
{
  m_pDocument->m_ProcessMessageEvent.RemoveEventHandler(plMakeDelegate(&plAiDebugSession::OnProcessMessage, this));
  plEditorUiServices::s_TickEvent.RemoveEventHandler(plMakeDelegate(&plAiDebugSession::OnTick, this));

  s_Sessions.RemoveAndCopy(this);
}

const plDynamicArray<plAiDebugSession*>& plAiDebugSession::GetSessions()
{
  return s_Sessions;
}

plAiDebugSession* plAiDebugSession::Find(const plDocument* pDocument)
{
  for (plAiDebugSession* pSession : s_Sessions)
  {
    if (pSession->m_pDocument == pDocument)
      return pSession;
  }

  return nullptr;
}

void plAiDebugSession::SetOverlay(plUInt32 uiOverlay, bool bOn)
{
  const plArrayPtr<const plAiDebugOverlay> overlays = plAiGetDebugOverlays();
  if (uiOverlay >= overlays.GetCount())
    return;

  const plAiDebugOverlay& overlay = overlays[uiOverlay];

  plChangeCVarMsgToEngine msg;
  msg.m_sCVarName = overlay.m_szCVar;
  if (overlay.m_bIntCVar)
    msg.m_NewValue = bOn ? overlay.m_iOnValue : overlay.m_iOffValue;
  else
    msg.m_NewValue = bOn;

  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);

  if (bOn)
    m_Frame.m_uiOverlays |= PL_BIT(uiOverlay);
  else
    m_Frame.m_uiOverlays &= ~PL_BIT(uiOverlay);
}

void plAiDebugSession::SetWatchedOnly(bool bOn)
{
  plChangeCVarMsgToEngine msg;
  msg.m_sCVarName = plAiDebugWatchedOnlyCVar;
  msg.m_NewValue = bOn;
  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);

  m_Frame.m_bWatchedOnly = bOn;
}

void plAiDebugSession::SetWatched(const plUuid& object)
{
  if (m_Watched == object)
    return;

  m_Watched = object;
  m_RecordedWatches.Clear();

  // ask right away, so the panel doesn't wait for the next poll
  m_LastPoll = plTime::MakeZero();
}

void plAiDebugSession::Pick(const plUuid& object)
{
  m_PickObject = object;

  // ask right away, so the panel follows the selection without waiting for the next poll
  m_LastPoll = plTime::MakeZero();
}

bool plAiDebugSession::IsConnected() const
{
  return m_bHasFrame && (plTime::Now() - m_LastAnswer).GetSeconds() < 3.0 * s_fAnswerTimeoutSeconds;
}

bool plAiDebugSession::IsWorldStalled() const
{
  return m_bHasFrame && m_Frame.m_bSimulating && (plTime::Now() - m_LastAdvance).GetSeconds() > 1.5;
}

const plAiDebugSession::RecordedAgent* plAiDebugSession::FindRecordedAgent(const plUuid& object) const
{
  for (const RecordedAgent& agent : m_RecordedAgents)
  {
    if (agent.m_Object == object)
      return &agent;
  }

  return nullptr;
}

const plAiDebugSession::RecordedWatch* plAiDebugSession::FindRecordedWatch(double fTime) const
{
  const RecordedWatch* pBest = nullptr;

  for (const RecordedWatch& watch : m_RecordedWatches)
  {
    if (watch.m_fTime > fTime)
      break;

    pBest = &watch;
  }

  return pBest;
}

const plAiDebugSession::RecordedWatch* plAiDebugSession::FindRecordedWatchFrom(double fTime) const
{
  for (const RecordedWatch& watch : m_RecordedWatches)
  {
    if (watch.m_fTime >= fTime)
      return &watch;
  }

  return nullptr;
}

plUInt64 plAiDebugSession::GetRecordingBytes() const
{
  plUInt64 uiBytes = 0;

  for (const RecordedAgent& agent : m_RecordedAgents)
  {
    uiBytes += sizeof(RecordedAgent) + agent.m_History.GetCount() * (sizeof(plAiDebugFrame::HistoryEntry) + 32);
  }

  for (const RecordedWatch& watch : m_RecordedWatches)
  {
    uiBytes += sizeof(RecordedWatch) + watch.m_Watched.m_Behaviors.GetCount() * (sizeof(plAiDebugFrame::Behavior) + 4 * sizeof(plAiDebugFrame::Consideration));
    uiBytes += watch.m_Watched.m_Targets.GetCount() * sizeof(plAiDebugFrame::Target);
    uiBytes += watch.m_Watched.m_Blackboard.GetCount() * (sizeof(plAiDebugFrame::BlackboardEntry) + 32);
  }

  return uiBytes;
}

void plAiDebugSession::OnTick(const plEditorUiServices::TickEvent& e)
{
  if (e.m_Type != plEditorUiServices::TickEvent::Type::BeforeFrame)
    return;

  const plTime now = plTime::Now();

  // an answer that never comes (the engine process restarted) must not stop the polling
  if (m_bWaiting && (now - m_LastPoll).GetSeconds() > s_fAnswerTimeoutSeconds)
  {
    m_bWaiting = false;
  }

  const double fInterval = (m_bHasFrame && m_Frame.m_bSimulating) ? s_fSimulatingPollSeconds : s_fIdlePollSeconds;

  if (!m_bWaiting && (now - m_LastPoll).GetSeconds() >= fInterval)
  {
    SendPoll();
  }
}

void plAiDebugSession::SendPoll()
{
  if (++m_uiRequestId == 0)
    ++m_uiRequestId;

  plAiDebugRequest request;
  request.m_uiRequestId = m_uiRequestId;
  request.m_WatchedObject = m_Watched;
  request.m_bCaptureEqs = m_bCaptureEqs;
  request.m_fHistorySince = m_fHistorySince;
  request.m_PickObject = m_PickObject;

  plSimpleDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = plAiDebugRequest::s_szWhatToDo;

  plDataBuffer buffer;
  request.ToBuffer(buffer);
  msg.m_PayloadValue = buffer;

  m_LastPoll = plTime::Now();

  if (m_pDocument->SendMessageToEngine(&msg))
  {
    m_bWaiting = true;
    m_bCaptureEqs = false;
    m_PickObject = plUuid::MakeInvalid();
  }
}

void plAiDebugSession::OnProcessMessage(const plEditorEngineDocumentMsg* pMsg)
{
  const auto* pConfig = plDynamicCast<const plSimpleDocumentConfigMsgToEditor*>(pMsg);
  if (pConfig == nullptr || pConfig->m_sWhatToDo != plAiDebugFrame::s_szWhatToDo || !pConfig->m_PayloadValue.IsA<plDataBuffer>())
    return;

  plAiDebugFrame frame;
  if (frame.FromBuffer(pConfig->m_PayloadValue.Get<plDataBuffer>()).Failed() || frame.m_uiRequestId != m_uiRequestId)
    return;

  m_bWaiting = false;
  m_LastAnswer = plTime::Now();

  if (frame.m_fWorldTime != m_fAdvancedWorldTime)
  {
    m_fAdvancedWorldTime = frame.m_fWorldTime;
    m_LastAdvance = m_LastAnswer;
  }

  // the engine already watched the picked agent for this frame
  if (frame.m_PickedAgent.IsValid() && frame.m_PickedAgent != m_Watched)
  {
    m_Watched = frame.m_PickedAgent;
    m_RecordedWatches.Clear();
  }

  Record(frame);

  m_Frame = std::move(frame);
  m_bHasFrame = true;
  ++m_uiFrameCounter;

  m_FrameEvents.Broadcast(*this);
}

void plAiDebugSession::Record(const plAiDebugFrame& frame)
{
  const double fNow = frame.m_fWorldTime;

  // the simulation restarted: the history of the old run would read as the future, and this frame only has switches
  // after the old run's time, so the next poll asks for everything again
  if (fNow + 0.5 < m_fLastWorldTime)
  {
    ClearRecording();
    return;
  }

  if (m_fLastWorldTime < 0.0)
  {
    m_fRecordingStart = fNow;
  }

  m_fLastWorldTime = fNow;

  for (const plAiDebugFrame::Agent& agent : frame.m_Agents)
  {
    RecordedAgent* pRecorded = const_cast<RecordedAgent*>(FindRecordedAgent(agent.m_Object));

    if (pRecorded == nullptr)
    {
      pRecorded = &m_RecordedAgents.ExpandAndGetRef();
      pRecorded->m_Object = agent.m_Object;
      pRecorded->m_fFirstSeen = fNow;

      if (agent.m_History.IsEmpty())
      {
        pRecorded->m_sInitialBehavior = agent.m_sActiveBehavior;
        pRecorded->m_uiInitialCategory = agent.m_uiActiveCategory;
      }
      else
      {
        // what ran before the first switch the engine still remembers
        pRecorded->m_sInitialBehavior = agent.m_History[0].m_sFrom;
        pRecorded->m_uiInitialCategory = s_uiUnknownCategory;
        pRecorded->m_fFirstSeen = plMath::Max(fNow - s_fRecordingSeconds, agent.m_History[0].m_fTime - 1.0);
      }
    }

    pRecorded->m_sName = agent.m_sName;
    pRecorded->m_sArchetype = agent.m_sArchetype;
    pRecorded->m_fLastSeen = fNow;
    pRecorded->m_History.PushBackRange(agent.m_History);
  }

  m_fHistorySince = fNow;

  // forget what left the window, keeping what ran at its start; a frozen view keeps what it shows
  double fWindowStart = fNow - s_fRecordingSeconds;
  if (m_fKeepFrom >= 0.0)
    fWindowStart = plMath::Clamp(m_fKeepFrom, fNow - s_fMaxKeptSeconds, fWindowStart);

  m_fRecordingStart = plMath::Max(m_fRecordingStart, fWindowStart);

  for (plUInt32 i = m_RecordedAgents.GetCount(); i-- > 0;)
  {
    RecordedAgent& recorded = m_RecordedAgents[i];

    plUInt32 uiDrop = 0;
    while (uiDrop < recorded.m_History.GetCount() && recorded.m_History[uiDrop].m_fTime < fWindowStart)
    {
      recorded.m_sInitialBehavior = recorded.m_History[uiDrop].m_sTo;
      recorded.m_uiInitialCategory = recorded.m_History[uiDrop].m_uiToCategory;
      ++uiDrop;
    }

    if (uiDrop > 0)
    {
      recorded.m_History.RemoveAtAndCopy(0, uiDrop);
      recorded.m_fFirstSeen = fWindowStart;
    }

    if (recorded.m_fLastSeen < fWindowStart)
    {
      m_RecordedAgents.RemoveAtAndCopy(i);
    }
  }

  if (frame.m_bHasWatched && frame.m_Watched.m_Object == m_Watched)
  {
    RecordedWatch& watch = m_RecordedWatches.ExpandAndGetRef();
    watch.m_fTime = fNow;
    watch.m_Watched = frame.m_Watched;
  }

  while (!m_RecordedWatches.IsEmpty() && m_RecordedWatches.PeekFront().m_fTime < fWindowStart)
  {
    m_RecordedWatches.PopFront();
  }
}

void plAiDebugSession::ClearRecording()
{
  m_RecordedAgents.Clear();
  m_RecordedWatches.Clear();
  m_fHistorySince = -1.0e9;
  m_fLastWorldTime = -1.0;
}
