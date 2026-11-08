#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEditorHelpers.h>
#include <EditorPluginAi/Debugger/AiDebuggerView.h>

#include <AiPlugin/Eqs/EqsTypes.h>
#include <AiPlugin/Navigation/NavMeshStatus.h>
#include <AiPlugin/UtilityAI/Decision/AiDebugSnapshot.h>
#include <AiPlugin/UtilityAI/Decision/AiScoring.h>
#include <Core/Utils/Blackboard.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/Document/GameObjectDocument.h>
#include <EditorUi/Surface.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

namespace
{
  constexpr float s_fToolbarHeight = 32.0f;
  constexpr float s_fStatusHeight = 22.0f;
  constexpr float s_fTabHeight = 30.0f;
  constexpr float s_fAgentRow = 38.0f;
  constexpr float s_fBehaviorRow = 26.0f;
  constexpr float s_fConsiderationRow = 20.0f;
  constexpr float s_fTableRow = 24.0f;
  constexpr float s_fTimelineBar = 26.0f;
  constexpr float s_fTimelineRow = 16.0f;
  constexpr float s_fTimelineLabel = 110.0f;
  constexpr double s_fTimelineSpan = 30.0;
  constexpr plUInt32 s_uiChangedFrames = 2; ///< how long a changed blackboard value stays highlighted (polls)

  plString Name(plStringView sPrefix, plUInt32 uiIndex, plStringView sSuffix = {})
  {
    plStringBuilder sName;
    sName.SetFormat("{}{}{}", sPrefix, uiIndex, sSuffix);
    return sName;
  }

  void Publish(plBlackboard& ref_board, const plUiDrawListWriter& writer, plStringView sEntry)
  {
    plHashedString sHashed;
    sHashed.Assign(sEntry);
    writer.Publish(ref_board, sHashed);
  }

  /// Ready sectors are quiet, building ones carry the AI colour, failed ones the danger colour.
  plColorGammaUB SectorStateColor(plAiNavMeshSectorState state)
  {
    switch (state)
    {
      case plAiNavMeshSectorState::Ready:
        return plEditorUi::Dim();
      case plAiNavMeshSectorState::Failed:
        return plEditorUi::Danger();
      default:
        return plAiEngineUi::AiColor();
    }
  }

  plColorGammaUB CategoryColorOrDim(plUInt8 uiCategory)
  {
    if (uiCategory == plAiDebugSession::s_uiUnknownCategory)
      return plEditorUi::Dim();

    return plAiEngineUi::CategoryColor(uiCategory);
  }

  plUInt32 LadderIndex(plInt32 iCategory)
  {
    const plArrayPtr<const plInt32> ladder = plAiEngineUi::GetCategoryLadder();
    for (plUInt32 i = 0; i < ladder.GetCount(); ++i)
    {
      if (ladder[i] == iCategory)
        return i;
    }

    return ladder.GetCount();
  }

  const char* LodName(plUInt8 uiLod)
  {
    switch (uiLod)
    {
      case 0:
        return "HOT";
      case 1:
        return "WARM";
      default:
        return "COLD";
    }
  }

  plColorGammaUB LodColor(plUInt8 uiLod)
  {
    switch (uiLod)
    {
      case 0:
        return plAiEngineUi::AiColor();
      case 1:
        return plEditorUi::Warning();
      default:
        return plEditorUi::Dim();
    }
  }

  const char* AlertName(plUInt32 uiLevel)
  {
    switch (uiLevel)
    {
      case plAiAlertLevel::Unaware:
        return "Unaware";
      case plAiAlertLevel::Suspicious:
        return "Suspicious";
      case plAiAlertLevel::Searching:
        return "Searching";
      default:
        return "Alert";
    }
  }

  plColorGammaUB AlertColor(plUInt32 uiLevel)
  {
    switch (uiLevel)
    {
      case plAiAlertLevel::Unaware:
        return plColorGammaUB(145, 150, 160);
      case plAiAlertLevel::Suspicious:
        return plColorGammaUB(220, 195, 110);
      case plAiAlertLevel::Searching:
        return plColorGammaUB(225, 175, 130);
      default:
        return plEditorUi::Danger();
    }
  }

  const char* EqsStatusName(plUInt8 uiStatus)
  {
    switch (static_cast<plAiEqsQueryResult::Status>(uiStatus))
    {
      case plAiEqsQueryResult::Status::Pending:
        return "Pending";
      case plAiEqsQueryResult::Status::Ready:
        return "Ready";
      case plAiEqsQueryResult::Status::NoResult:
        return "No result";
      case plAiEqsQueryResult::Status::AreaNotReady:
        return "Area not ready";
      case plAiEqsQueryResult::Status::MissingData:
        return "Missing data";
      case plAiEqsQueryResult::Status::TimedOut:
        return "Timed out";
      default:
        return "Invalid";
    }
  }

  plString PrettyInputName(plStringView sTypeName)
  {
    if (sTypeName.IsEmpty())
      return "(no input)";

    plStringBuilder sName;
    plStringBuilder sType(sTypeName);
    plAiAssetUi::GetPrettyInputName(plRTTI::FindTypeByName(sType), sName);
    return sName.IsEmpty() ? plString(sTypeName) : plString(sName);
  }

  plString ArchetypeName(plStringView sReference)
  {
    if (sReference.IsEmpty())
      return "no archetype";

    auto asset = plAssetCurator::GetSingleton()->FindSubAsset(sReference);
    if (asset.isValid())
      return asset->GetName();

    plStringBuilder sName(sReference);
    return sName.GetFileName();
  }

  /// A behavior score is category + utility; the utility is what the bars show.
  float UtilityOf(float fScore)
  {
    return fScore - plMath::Floor(fScore);
  }

  void DrawBar(plUiDrawListWriter& ref_writer, const plRectFloat& rect, float fValue, const plColorGammaUB& fill, float fThreshold = -1.0f)
  {
    using namespace plEditorUi;

    ref_writer.Rect(rect, Field(), 3.0f);
    ref_writer.RectOutline(rect, Line(), 1.0f, 3.0f);

    const float fFill = plMath::Saturate(fValue);
    if (fFill > 0.0f)
      ref_writer.Rect(plRectFloat(rect.x + 1.0f, rect.y + 1.0f, (rect.width - 2.0f) * fFill, rect.height - 2.0f), fill, 2.0f);

    if (fThreshold >= 0.0f)
    {
      const float x = rect.x + rect.width * plMath::Saturate(fThreshold);
      ref_writer.Line(plVec2(x, rect.y - 1.0f), plVec2(x, rect.y + rect.height + 1.0f), Warning(), 1.5f);
    }
  }
} // namespace

plAiDebuggerView::plAiDebuggerView(plDocument* pDocument)
  : plAiEngineView("AiDebugger")
  , m_pDocument(plDynamicCast<plGameObjectDocument*>(pDocument))
{
  m_pSession = PL_DEFAULT_NEW(plAiDebugSession, m_pDocument);
  m_pSession->m_FrameEvents.AddEventHandler(plMakeDelegate(&plAiDebuggerView::OnFrame, this));
  m_pDocument->GetSelectionManager()->m_Events.AddEventHandler(plMakeDelegate(&plAiDebuggerView::OnSelection, this));
}

plAiDebuggerView::~plAiDebuggerView()
{
  m_pDocument->GetSelectionManager()->m_Events.RemoveEventHandler(plMakeDelegate(&plAiDebuggerView::OnSelection, this));
  m_pSession->m_FrameEvents.RemoveEventHandler(plMakeDelegate(&plAiDebuggerView::OnFrame, this));
}

void plAiDebuggerView::WatchAgent(const plUuid& object, bool bSelectInScene)
{
  if (m_pSession->GetWatched() != object)
  {
    m_sExpanded.Clear();
    m_PreviousBlackboard.Clear();
    m_ChangedAt.Clear();
  }

  m_pSession->SetWatched(object);

  if (bSelectInScene)
  {
    if (const plDocumentObject* pObject = m_pDocument->GetObjectManager()->GetObject(object))
    {
      m_pDocument->GetSelectionManager()->SetSelection(pObject);
    }
  }

  Invalidate();
}

void plAiDebuggerView::OnFrame(const plAiDebugSession& session)
{
  const plAiDebugFrame& frame = session.GetFrame();

  // the simulation restarted: the frozen time belongs to the old run, whose recording is gone
  if (m_bFrozen && frame.m_fWorldTime + 0.5 < m_fViewEnd)
  {
    SetFrozen(false);
  }

  // a pick in the scene changed the watched agent: its rows and values start fresh
  if (m_LastWatched != session.GetWatched())
  {
    m_LastWatched = session.GetWatched();
    m_sExpanded.Clear();
    m_PreviousBlackboard.Clear();
    m_ChangedAt.Clear();
  }

  // remember which blackboard values changed
  if (frame.m_bHasWatched)
  {
    for (const plAiDebugFrame::BlackboardEntry& entry : frame.m_Watched.m_Blackboard)
    {
      const plString* pPrevious = m_PreviousBlackboard.GetValue(entry.m_sName);
      if (pPrevious != nullptr && *pPrevious != entry.m_sValue)
      {
        m_ChangedAt[entry.m_sName] = session.GetFrameCounter();
      }

      m_PreviousBlackboard[entry.m_sName] = entry.m_sValue;
    }
  }

  if (m_bFrozen)
    UpdateStatus();
  else
    Invalidate();
}

void plAiDebuggerView::OnSelection(const plSelectionManagerEvent& e)
{
  if (!m_bFollowSelection)
    return;

  const plDocumentObject* pSelected = m_pDocument->GetSelectionManager()->GetCurrentObject();
  if (pSelected == nullptr)
    return;

  // the engine looks for the agent in the selection; selecting a wall keeps the watched agent
  m_pSession->Pick(pSelected->GetGuid());
}

double plAiDebuggerView::GetHeadTime() const
{
  return m_bFrozen ? m_fHead : m_pSession->GetFrame().m_fWorldTime;
}

const plAiDebugFrame::Watched* plAiDebuggerView::GetShownWatched() const
{
  if (!m_pSession->GetWatched().IsValid())
    return nullptr;

  if (m_bFrozen)
  {
    const plAiDebugSession::RecordedWatch* pRecorded = m_pSession->FindRecordedWatch(m_fHead);
    return pRecorded != nullptr ? &pRecorded->m_Watched : nullptr;
  }

  const plAiDebugFrame& frame = m_pSession->GetFrame();
  if (frame.m_bHasWatched && frame.m_Watched.m_Object == m_pSession->GetWatched())
    return &frame.m_Watched;

  return nullptr;
}

void plAiDebuggerView::GetShownAgents(plDynamicArray<const plAiDebugFrame::Agent*>& out_agents) const
{
  out_agents.Clear();

  for (const plAiDebugFrame::Agent& agent : m_pSession->GetFrame().m_Agents)
  {
    if (!m_sFilter.IsEmpty() && agent.m_sName.FindSubString_NoCase(m_sFilter) == nullptr)
      continue;

    if (m_bAlertedOnly && agent.m_uiAlertLevel == plAiAlertLevel::Unaware)
      continue;

    out_agents.PushBack(&agent);
  }
}

void plAiDebuggerView::GetSegments(const plAiDebugSession::RecordedAgent& agent, double fFrom, double fTo, plDynamicArray<Segment>& out_segments) const
{
  out_segments.Clear();

  Segment current;
  current.m_fStart = agent.m_fFirstSeen;
  current.m_sBehavior = agent.m_sInitialBehavior;
  current.m_uiCategory = agent.m_uiInitialCategory;

  auto close = [&](double fEnd)
  {
    current.m_fEnd = fEnd;
    if (!current.m_sBehavior.IsEmpty() && current.m_fEnd > fFrom && current.m_fStart < fTo && current.m_fEnd > current.m_fStart)
    {
      Segment& segment = out_segments.ExpandAndGetRef();
      segment = current;
      segment.m_fStart = plMath::Max(segment.m_fStart, fFrom);
      segment.m_fEnd = plMath::Min(segment.m_fEnd, fTo);
    }
  };

  for (plUInt32 i = 0; i < agent.m_History.GetCount(); ++i)
  {
    const plAiDebugFrame::HistoryEntry& entry = agent.m_History[i];
    close(entry.m_fTime);

    current = Segment();
    current.m_fStart = entry.m_fTime;
    current.m_sBehavior = entry.m_sTo;
    current.m_uiCategory = entry.m_uiToCategory;
    current.m_iEntry = static_cast<plInt32>(i);
  }

  // an agent that left the world ends where it was last seen
  const double fLiveTime = m_pSession->GetFrame().m_fWorldTime;
  close(agent.m_fLastSeen + 0.5 < fLiveTime ? agent.m_fLastSeen : fTo);
}

bool plAiDebuggerView::GetBehaviorAt(const plUuid& object, double fTime, plString& out_sBehavior, plUInt8& out_uiCategory) const
{
  const plAiDebugSession::RecordedAgent* pAgent = m_pSession->FindRecordedAgent(object);
  if (pAgent == nullptr)
    return false;

  out_sBehavior = pAgent->m_sInitialBehavior;
  out_uiCategory = pAgent->m_uiInitialCategory;

  for (const plAiDebugFrame::HistoryEntry& entry : pAgent->m_History)
  {
    if (entry.m_fTime > fTime)
      break;

    out_sBehavior = entry.m_sTo;
    out_uiCategory = entry.m_uiToCategory;
  }

  return true;
}

void plAiDebuggerView::SetFrozen(bool bFrozen)
{
  if (m_bFrozen == bFrozen)
    return;

  m_bFrozen = bFrozen;

  if (bFrozen)
  {
    m_fViewEnd = m_pSession->GetFrame().m_fWorldTime;
    m_fHead = m_fViewEnd;

    // the shown window must outlive the recording's usual 120 s while frozen
    m_pSession->KeepRecordingFrom(m_fViewEnd - s_fTimelineSpan);
  }
  else
  {
    m_fWhyTime = -1.0;
    m_pSession->KeepRecordingFrom(-1.0);
  }

  Invalidate();
}

void plAiDebuggerView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const float fWidth = vSize.x;
  const float fHeight = vSize.y;

  BuildToolbar(pRoot, fWidth);

  const plRectFloat status(0.0f, fHeight - s_fStatusHeight, fWidth, s_fStatusHeight);
  BuildStatus(pRoot, status);

  if (!m_pSession->HasFrame())
  {
    auto* pEmpty = EmptyState(pRoot, Icon::Info, "Waiting for the scene's engine process", "The AI Debugger reads the running world. Simulate or play the scene to see its agents.");
    Place(pEmpty, plRectFloat(0.0f, s_fToolbarHeight, fWidth, fHeight - s_fToolbarHeight - s_fStatusHeight));
    return;
  }

  plHybridArray<const plAiDebugFrame::Agent*, 32> agents;
  GetShownAgents(agents);

  // the timeline gets a row per agent, up to what leaves the tabs room for a decision's behaviors
  const float fRoom = fHeight - s_fToolbarHeight - s_fStatusHeight - 320.0f;
  const plUInt32 uiMaxRows = static_cast<plUInt32>(plMath::Clamp(fRoom / s_fTimelineRow, 2.0f, 6.0f));
  const plUInt32 uiRows = plMath::Clamp<plUInt32>(agents.GetCount(), 1u, uiMaxRows);
  const float fTimeline = s_fTimelineBar + uiRows * s_fTimelineRow + 10.0f;

  const float fMainTop = s_fToolbarHeight;
  const float fMainHeight = fHeight - s_fToolbarHeight - s_fStatusHeight - fTimeline;
  const float fListWidth = plMath::Clamp(fWidth * 0.3f, 180.0f, 250.0f);

  BuildAgentList(pRoot, plRectFloat(0.0f, fMainTop, fListWidth, fMainHeight));
  BuildTabs(pRoot, plRectFloat(fListWidth + 1.0f, fMainTop, fWidth - fListWidth - 1.0f, fMainHeight));

  const plRectFloat timeline(0.0f, fMainTop + fMainHeight, fWidth, fTimeline);
  BuildTimeline(pRoot, timeline);
  BuildWhy(pRoot, timeline);

  UpdateStatus();
}

void plAiDebuggerView::BuildToolbar(plUiElement* pRoot, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  auto* pBar = Panel(pRoot);
  Place(pBar, plRectFloat(0.0f, 0.0f, fWidth, s_fToolbarHeight));
  pBar->m_Color = Plate();

  auto* pEdge = Panel(pBar);
  Place(pEdge, plRectFloat(0.0f, s_fToolbarHeight - 1.0f, fWidth, 1.0f));
  pEdge->m_Color = Line();

  const plAiDebugFrame& frame = m_pSession->GetFrame();
  const bool bSimulating = m_pSession->HasFrame() && frame.m_bSimulating;

  plStringBuilder sScene(m_pDocument->GetDocumentPath());
  plStringBuilder sState;
  sState.SetFormat("{} \xC2\xB7 {}", sScene.GetFileNameAndExtension(), !m_pSession->IsConnected() ? "no engine" : (bSimulating ? (m_pDocument->GetPauseSimulation() ? "Paused" : "Simulating") : "Not simulating"));

  // the chips at the right end, right to left
  const bool bPaused = m_pDocument->GetPauseSimulation();
  const char* szPause = bPaused ? "Resume simulation" : "Pause simulation";
  const char* szLive = m_bFrozen ? "Frozen" : "Follow live";
  const char* szFollow = "Follow selection";

  const float fChip = 22.0f;
  const float fY = (s_fToolbarHeight - fChip) * 0.5f;
  float x = fWidth - 8.0f;

  x -= ChipWidth(szPause, false);
  AddChip(pBar, plRectFloat(x, fY, ChipWidth(szPause, false), fChip), "Dbg.Pause", szPause, bPaused, plColorGammaUB(0, 0, 0, 0), "Pauses the world simulation; the debugger keeps showing the paused frame.", [this]()
    {
    m_pDocument->SetPauseSimulation(!m_pDocument->GetPauseSimulation());
    Invalidate(); });

  x -= 6.0f + ChipWidth(szLive, true);
  AddChip(pBar, plRectFloat(x, fY, ChipWidth(szLive, true), fChip), "Dbg.Live", szLive, m_bFrozen, m_bFrozen ? Dim() : Danger(), "Follow the running simulation, or stay at a time of the recording. Scrubbing the timeline freezes the view, not the game.", [this]()
    { SetFrozen(!m_bFrozen); });

  x -= 6.0f + ChipWidth(szFollow, false);
  AddChip(pBar, plRectFloat(x, fY, ChipWidth(szFollow, false), fChip), "Dbg.Follow", szFollow, m_bFollowSelection, plColorGammaUB(0, 0, 0, 0), "Watch the agent selected in the scene.", [this]()
    {
    m_bFollowSelection = !m_bFollowSelection;
    if (m_bFollowSelection)
      OnSelection(plSelectionManagerEvent());
    Invalidate(); });

  Label(pBar, plRectFloat(12.0f, 0.0f, plMath::Max(x - 20.0f, 40.0f), s_fToolbarHeight), sState, Muted(), FontSize - 1);
}

void plAiDebuggerView::BuildAgentList(plUiElement* pRoot, const plRectFloat& rect)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  auto* pPane = Panel(pRoot);
  Place(pPane, rect);
  pPane->m_Color = Surface();

  auto* pDivider = Panel(pRoot);
  Place(pDivider, plRectFloat(rect.x + rect.width, rect.y, 1.0f, rect.height));
  pDivider->m_Color = Line();

  // the filter row
  const float fChip = ChipWidth("Alerted", false);
  AddTextField(pPane, plRectFloat(8.0f, 6.0f, rect.width - fChip - 22.0f, ControlHeight), "Dbg.Filter", m_sFilter, "Filter agents", "Only agents whose name contains this text.", [this](const plString& sText)
    {
    m_sFilter = sText;
    Invalidate(); });

  AddChip(pPane, plRectFloat(rect.width - fChip - 8.0f, 7.0f, fChip, 22.0f), "Dbg.Alerted", "Alerted", m_bAlertedOnly, plColorGammaUB(0, 0, 0, 0), "Only agents that perceive a threat (alert level above Unaware).", [this]()
    {
    m_bAlertedOnly = !m_bAlertedOnly;
    Invalidate(); });

  const float fTop = 6.0f + ControlHeight + 6.0f;
  plUiScrollView* pBody = AddScrollBody(pPane, plRectFloat(0.0f, fTop, rect.width, rect.height - fTop), "Dbg.Agents", 0.0f, 0.0f);

  plHybridArray<const plAiDebugFrame::Agent*, 32> agents;
  GetShownAgents(agents);

  if (agents.IsEmpty())
  {
    auto* pNone = Panel(pBody);
    pNone->m_vOffsets.w = 60.0f;
    pNone->m_Color = plColorGammaUB(0, 0, 0, 0);

    const char* szText = !m_pSession->GetFrame().m_bHasBrain || m_pSession->GetFrame().m_Agents.IsEmpty() ? "No AI agents in this world." : "No agent matches the filter.";
    Label(pNone, plRectFloat(12.0f, 8.0f, rect.width - 24.0f, 20.0f), szText, Dim(), FontSize - 2);
    return;
  }

  const double fHead = GetHeadTime();
  const float fRowWidth = rect.width - ScrollbarGutter;

  for (plUInt32 i = 0; i < agents.GetCount(); ++i)
  {
    const plAiDebugFrame::Agent& agent = *agents[i];
    const bool bWatched = agent.m_Object == m_pSession->GetWatched();

    plString sBehavior = agent.m_sActiveBehavior;
    plUInt8 uiCategory = agent.m_uiActiveCategory;
    if (m_bFrozen)
    {
      GetBehaviorAt(agent.m_Object, fHead, sBehavior, uiCategory);
    }

    auto* pRow = Panel(pBody);
    pRow->m_vOffsets.w = s_fAgentRow;
    pRow->m_Color = bWatched ? Selection() : plColorGammaUB(0, 0, 0, 0);

    auto* pStripe = Panel(pRow);
    Place(pStripe, plRectFloat(0.0f, 6.0f, 3.0f, s_fAgentRow - 12.0f));
    pStripe->m_Color = sBehavior.IsEmpty() ? Line() : CategoryColorOrDim(uiCategory);

    const float fBadge = BadgeWidth(LodName(agent.m_uiLodTier));
    Label(pRow, plRectFloat(12.0f, 4.0f, fRowWidth - fBadge - 24.0f, 16.0f), agent.m_sName.IsEmpty() ? "(unnamed)" : agent.m_sName.GetView(), Foreground(), FontSize - 1);

    plStringBuilder sLine;
    sLine.SetFormat("{} \xC2\xB7 {}", sBehavior.IsEmpty() ? "idle" : sBehavior.GetView(), ArchetypeName(agent.m_sArchetype));
    Label(pRow, plRectFloat(12.0f, 20.0f, fRowWidth - 20.0f, 14.0f), sLine, Dim(), FontSize - 3);

    Badge(pRow, fRowWidth - fBadge - 8.0f, 6.0f, LodName(agent.m_uiLodTier), LodColor(agent.m_uiLodTier));

    const plUuid object = agent.m_Object;
    AddClickArea(pRow, plRectFloat(0.0f, 0.0f, fRowWidth, s_fAgentRow), Name("Dbg.Agent.", i), "Watch this agent and select it in the scene.", [this, object]()
      { WatchAgent(object, true); });
  }
}

void plAiDebuggerView::BuildTabs(plUiElement* pRoot, const plRectFloat& rect)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  auto* pPane = Panel(pRoot);
  Place(pPane, rect);
  pPane->m_Color = Surface();
  pPane->m_bClipChildren = true;

  const plAiDebugFrame::Watched* pWatched = GetShownWatched();

  // the tab row
  auto* pTabs = Panel(pPane);
  Place(pTabs, plRectFloat(0.0f, 0.0f, rect.width, s_fTabHeight));
  pTabs->m_Color = Plate();

  auto* pTabLine = Panel(pTabs);
  Place(pTabLine, plRectFloat(0.0f, s_fTabHeight - 1.0f, rect.width, 1.0f));
  pTabLine->m_Color = Line();

  plStringBuilder sPerception("Perception");
  if (pWatched != nullptr && !pWatched->m_Targets.IsEmpty())
    sPerception.AppendFormat("  {}", pWatched->m_Targets.GetCount());

  const char* szTabs[] = {"Decision", sPerception.GetData(), "Blackboard", "Navigation", "Squad", "EQS"};

  float x = 4.0f;
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(szTabs); ++i)
  {
    const bool bCurrent = static_cast<plUInt32>(m_Tab) == i;
    const float fTab = TextWidth(szTabs[i], FontSize - 1) + 22.0f;

    Label(pTabs, plRectFloat(x + 11.0f, 0.0f, fTab - 11.0f, s_fTabHeight - 2.0f), szTabs[i], bCurrent ? Foreground() : Muted(), FontSize - 1)->m_Overflow = plUiTextOverflow::Overflow;

    if (bCurrent)
    {
      auto* pUnderline = Panel(pTabs);
      Place(pUnderline, plRectFloat(x + 6.0f, s_fTabHeight - 2.0f, fTab - 12.0f, 2.0f));
      pUnderline->m_Color = Accent();
    }

    const Tab tab = static_cast<Tab>(i);
    AddClickArea(pTabs, plRectFloat(x, 0.0f, fTab, s_fTabHeight), Name("Dbg.Tab.", i), {}, [this, tab]()
      {
      m_Tab = tab;
      Invalidate(); });

    x += fTab;
  }

  // the watched agent at the right end: its category, name and the shown time
  const plAiDebugFrame::Agent* pAgent = nullptr;
  for (const plAiDebugFrame::Agent& agent : m_pSession->GetFrame().m_Agents)
  {
    if (agent.m_Object == m_pSession->GetWatched())
      pAgent = &agent;
  }

  if (pAgent != nullptr)
  {
    plString sBehavior = pAgent->m_sActiveBehavior;
    plUInt8 uiCategory = pAgent->m_uiActiveCategory;
    if (m_bFrozen)
      GetBehaviorAt(pAgent->m_Object, m_fHead, sBehavior, uiCategory);

    plStringBuilder sTime;
    sTime.SetFormat("t = {} s", plArgF(GetHeadTime(), 1));

    const char* szCategory = sBehavior.IsEmpty() ? "Idle" : plAiAssetUi::CategoryName(uiCategory);
    const float fTime = TextWidth(sTime, FontSize - 2);
    const float fName = TextWidth(pAgent->m_sName, FontSize - 2);
    const float fBadge = BadgeWidth(szCategory);

    float xr = rect.width - 10.0f - fTime;
    if (xr - fName - fBadge - 20.0f > x)
    {
      Label(pTabs, plRectFloat(xr, 0.0f, fTime, s_fTabHeight - 2.0f), sTime, Dim(), FontSize - 2);
      xr -= 8.0f + fName;
      Label(pTabs, plRectFloat(xr, 0.0f, fName, s_fTabHeight - 2.0f), pAgent->m_sName, Foreground(), FontSize - 2);
      xr -= 8.0f + fBadge;
      Badge(pTabs, xr, (s_fTabHeight - BadgeHeight) * 0.5f - 1.0f, szCategory, sBehavior.IsEmpty() ? Dim() : CategoryColorOrDim(uiCategory));
    }
  }

  if (m_Tab == Tab::Squad)
  {
    // squads belong to the world, not to the agent: the tab has its own layout, with a map that takes pointer input
    BuildSquad(pPane, plRectFloat(rect.x, rect.y + s_fTabHeight, rect.width, rect.height - s_fTabHeight));
    return;
  }

  plUiScrollView* pBody = AddScrollBody(pPane, plRectFloat(0.0f, s_fTabHeight, rect.width, rect.height - s_fTabHeight), "Dbg.Body", 0.0f, 0.0f);
  const float fWidth = rect.width - ScrollbarGutter;

  if (m_Tab == Tab::Navigation)
  {
    // the navmesh belongs to the world, not to the agent: its sectors show without a watched agent
    BuildNavigation(pBody, fWidth, pWatched);
    return;
  }

  if (!m_pSession->GetWatched().IsValid())
  {
    BuildEmpty(pBody, fWidth, "Pick an agent", "Click an agent in the list, or select one in the scene while Follow selection is on.");
    return;
  }

  if (pWatched == nullptr && !m_bFrozen)
  {
    bool bInWorld = false;
    for (const plAiDebugFrame::Agent& agent : m_pSession->GetFrame().m_Agents)
      bInWorld |= agent.m_Object == m_pSession->GetWatched();

    if (!bInWorld && m_pSession->HasFrame())
      BuildEmpty(pBody, fWidth, "The watched agent is gone", "It was removed from the world, or the simulation restarted (agents created while it runs get new IDs every run). Pick another agent in the list.");
    else
      BuildEmpty(pBody, fWidth, "Waiting for the agent", "The engine watches the agent from now on; its data shows with the next poll.");

    return;
  }

  if (pWatched == nullptr)
  {
    // only the watched agent's details are recorded: since it was watched, and for the recording's window
    const double fStart = m_pSession->GetRecordedWatchStart();

    plStringBuilder sLine;
    if (fStart >= 0.0)
      sLine.SetFormat("The recording has this agent's details from {} s on. An agent's details are recorded while it is watched.", plArgF(fStart, 1));
    else
      sLine = "The recording has no details of this agent yet. An agent's details are recorded while it is watched.";

    BuildEmpty(pBody, fWidth, "Not recorded at this time", sLine);

    auto* pActions = Panel(pBody);
    pActions->m_vOffsets.w = 44.0f;
    pActions->m_Color = plColorGammaUB(0, 0, 0, 0);

    const char* szLive = "Follow live";
    AddTextButton(pActions, plRectFloat(12.0f, 6.0f, TextWidth(szLive) + 24.0f, ControlHeight), "Dbg.FollowLive", szLive, "Stops scrubbing and shows the simulation as it runs.", [this]()
      { SetFrozen(false); });
    return;
  }

  switch (m_Tab)
  {
    case Tab::Decision:
      BuildDecision(pBody, fWidth, *pWatched);
      break;
    case Tab::Perception:
      BuildPerception(pBody, fWidth, *pWatched);
      break;
    case Tab::Blackboard:
      BuildBlackboard(pBody, fWidth, *pWatched);
      break;
    case Tab::Navigation:
    case Tab::Squad:
      break; // built above
    case Tab::Eqs:
      BuildEqs(pBody, fWidth, *pWatched);
      break;
  }
}

void plAiDebuggerView::BuildEmpty(plUiElement* pBody, float fWidth, plStringView sTitle, plStringView sLine)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  auto* pPanel = Panel(pBody);
  pPanel->m_vOffsets.w = 64.0f;
  pPanel->m_Color = plColorGammaUB(0, 0, 0, 0);

  Label(pPanel, plRectFloat(14.0f, 12.0f, fWidth - 28.0f, 18.0f), sTitle, Muted(), FontSize - 1);
  auto* pLine = Label(pPanel, plRectFloat(14.0f, 32.0f, fWidth - 28.0f, 30.0f), sLine, Dim(), FontSize - 2);
  pLine->m_bWrap = true;
  pLine->m_VAlign = plUiTextVAlign::Top;
}

void plAiDebuggerView::BuildDecision(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  if (!watched.m_bHasDecision)
  {
    BuildEmpty(pBody, fWidth, "Waiting for a decision", "The agent is watched; its next decision fills this tab. Agents decide a few times per second while the world simulates.");
    return;
  }

  // the decision in one line
  {
    auto* pLine = Panel(pBody);
    pLine->m_vOffsets.w = 28.0f;
    pLine->m_Color = plColorGammaUB(0, 0, 0, 0);

    plStringBuilder sLine;
    sLine.SetFormat("Decision #{} at {} s \xC2\xB7 {} \xC2\xB7 next in {} s", watched.m_uiDecisionCount, plArgF(watched.m_fDecisionTime, 1), plAiDecisionOutcomeName(static_cast<plAiDecisionOutcome>(watched.m_uiOutcome)),
      plArgF(plMath::Max(0.0, watched.m_fNextDecision - GetHeadTime()), 1));
    Label(pLine, plRectFloat(12.0f, 4.0f, fWidth - 24.0f, 20.0f), sLine, Muted(), FontSize - 2);
  }

  // category ladder first, then score
  plHybridArray<plUInt32, 16> order;
  for (plUInt32 i = 0; i < watched.m_Behaviors.GetCount(); ++i)
    order.PushBack(i);

  order.Sort([&](plUInt32 a, plUInt32 b)
    {
    const plAiDebugFrame::Behavior& lhs = watched.m_Behaviors[a];
    const plAiDebugFrame::Behavior& rhs = watched.m_Behaviors[b];
    const plUInt32 la = LadderIndex(lhs.m_uiCategory);
    const plUInt32 lb = LadderIndex(rhs.m_uiCategory);
    if (la != lb)
      return la < lb;
    return lhs.m_fUtility > rhs.m_fUtility; });

  plStringBuilder sCount;
  sCount.SetFormat("{}", watched.m_Behaviors.GetCount());
  Section(pBody, "Behaviors", AiColor(), sCount, "category ladder first, then utility \xC2\xB7 amber line = what a challenger of the same category must beat");

  // the commit threshold: the active utility plus the commit bonus
  const plAiDebugFrame::Behavior* pActive = (watched.m_iActiveBehavior >= 0 && watched.m_iActiveBehavior < static_cast<plInt32>(watched.m_Behaviors.GetCount())) ? &watched.m_Behaviors[watched.m_iActiveBehavior] : nullptr;
  const float fThreshold = pActive != nullptr ? plMath::Min(1.0f, pActive->m_fUtility + watched.m_fCommitBonus) : -1.0f;

  const float fName = plMath::Clamp(fWidth * 0.26f, 120.0f, 190.0f);
  const float fScore = 40.0f;
  const float fBadges = plMath::Clamp(fWidth * 0.3f, 120.0f, 220.0f);
  const float fBar = plMath::Max(fWidth - 12.0f - fName - fScore - fBadges - 30.0f, 60.0f);

  plUiDrawListWriter writer;

  for (plUInt32 uiRow = 0; uiRow < order.GetCount(); ++uiRow)
  {
    const plUInt32 uiBehavior = order[uiRow];
    const plAiDebugFrame::Behavior& behavior = watched.m_Behaviors[uiBehavior];
    const bool bVetoed = behavior.m_uiVeto != static_cast<plUInt8>(plAiBehaviorVeto::None);
    const bool bExpanded = m_sExpanded == behavior.m_sName;

    auto* pRow = Panel(pBody);
    pRow->m_vOffsets.w = s_fBehaviorRow;
    pRow->m_Color = behavior.m_bActive ? Mix(Surface(), Accent(), 0.08f) : plColorGammaUB(0, 0, 0, 0);

    auto* pStripe = Panel(pRow);
    Place(pStripe, plRectFloat(0.0f, 4.0f, 3.0f, s_fBehaviorRow - 8.0f));
    pStripe->m_Color = CategoryColorOrDim(behavior.m_uiCategory);

    float x = 12.0f;
    const float fBehaviorName = plMath::Min(TextWidth(behavior.m_sName, FontSize - 1), fName - 10.0f);
    Label(pRow, plRectFloat(x, 0.0f, fBehaviorName, s_fBehaviorRow), behavior.m_sName, bVetoed ? Muted() : Foreground(), FontSize - 1);
    Label(pRow, plRectFloat(x + fBehaviorName + 6.0f, 1.0f, fName - fBehaviorName - 6.0f, s_fBehaviorRow), plAiAssetUi::CategoryName(behavior.m_uiCategory), Dim(), FontSize - 3);
    x += fName;

    // the utility bar; the amber line only matters for the active behavior's category
    const plString sBar = Name("Dbg.Bar.", uiRow);
    AddDrawList(pRow, plRectFloat(x, 7.0f, fBar, s_fBehaviorRow - 14.0f), sBar);
    {
      plUiDrawListWriter bar;
      const bool bThreshold = pActive != nullptr && !behavior.m_bActive && behavior.m_uiCategory == pActive->m_uiCategory;
      DrawBar(bar, plRectFloat(0.0f, 0.0f, fBar, s_fBehaviorRow - 14.0f), bVetoed ? 0.0f : behavior.m_fUtility, behavior.m_bActive ? Accent() : Mix(Field(), AiColor(), 0.75f), bThreshold ? fThreshold : -1.0f);
      Publish(GetBoard(), bar, sBar);
    }
    x += fBar + 10.0f;

    Label(pRow, plRectFloat(x, 0.0f, fScore, s_fBehaviorRow), bVetoed ? plString("-") : FormatNumber(behavior.m_fUtility, 2), bVetoed ? Dim() : Foreground(), FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;
    x += fScore + 10.0f;

    // badges: active and its commit bonus, the winner, or why it didn't compete
    const float fBadgeY = (s_fBehaviorRow - BadgeHeight) * 0.5f;
    if (behavior.m_bActive)
    {
      x += Badge(pRow, x, fBadgeY, "active", Accent()) + 4.0f;

      plStringBuilder sCommit;
      sCommit.SetFormat("commit +{}", plArgF(watched.m_fCommitBonus, 2));
      x += Badge(pRow, x, fBadgeY, sCommit, Muted()) + 4.0f;

      if (watched.m_bLocked)
        x += Badge(pRow, x, fBadgeY, "locked", Warning()) + 4.0f;
    }
    else if (static_cast<plInt32>(uiBehavior) == watched.m_iWinner)
    {
      x += Badge(pRow, x, fBadgeY, "winner", GoodColor()) + 4.0f;
    }

    if (bVetoed)
    {
      Badge(pRow, x, fBadgeY, plAiBehaviorVetoName(static_cast<plAiBehaviorVeto>(behavior.m_uiVeto)), Dim());
    }

    const plString sName = behavior.m_sName;
    AddClickArea(pRow, plRectFloat(0.0f, 0.0f, fWidth, s_fBehaviorRow), Name("Dbg.Behavior.", uiRow), "Show the considerations of this behavior.", [this, sName]()
      {
      m_sExpanded = (m_sExpanded == sName) ? plString() : sName;
      Invalidate(); });

    if (!bExpanded)
      continue;

    // the considerations of the best target
    const plUInt32 uiCons = behavior.m_Considerations.GetCount();
    auto* pCons = Panel(pBody);
    pCons->m_vOffsets.w = 22.0f + plMath::Max(1u, uiCons) * s_fConsiderationRow + 8.0f;
    pCons->m_Color = Field();

    const float fInput = plMath::Clamp(fWidth * 0.3f, 120.0f, 220.0f);
    const float fNumber = 70.0f;
    const float fConsBar = plMath::Max(fWidth - 24.0f - fInput - 3.0f * fNumber - 16.0f, 40.0f);
    const float xRaw = 24.0f + fInput;
    const float xNorm = xRaw + fNumber;
    const float xBar = xNorm + fNumber + 8.0f;
    const float xValue = xBar + fConsBar;

    Label(pCons, plRectFloat(24.0f, 4.0f, fInput, 16.0f), "INPUT", Dim(), MinFontSize);
    Label(pCons, plRectFloat(xRaw, 4.0f, fNumber, 16.0f), "RAW", Dim(), MinFontSize)->m_HAlign = plUiTextHAlign::Right;
    Label(pCons, plRectFloat(xNorm, 4.0f, fNumber, 16.0f), "NORMALIZED", Dim(), MinFontSize)->m_HAlign = plUiTextHAlign::Right;
    Label(pCons, plRectFloat(xValue, 4.0f, fNumber, 16.0f), "SCORE", Dim(), MinFontSize)->m_HAlign = plUiTextHAlign::Right;

    if (uiCons == 0)
    {
      Label(pCons, plRectFloat(24.0f, 22.0f, fWidth - 48.0f, s_fConsiderationRow), bVetoed ? "Not scored: it did not compete in this decision." : "No considerations: the behavior scores its weight.", Dim(), FontSize - 2);
    }

    for (plUInt32 c = 0; c < uiCons; ++c)
    {
      const plAiDebugFrame::Consideration& cons = behavior.m_Considerations[c];
      const float y = 22.0f + c * s_fConsiderationRow;

      Label(pCons, plRectFloat(24.0f, y, fInput, s_fConsiderationRow), PrettyInputName(cons.m_sInput), Muted(), FontSize - 2);
      Label(pCons, plRectFloat(xRaw, y, fNumber, s_fConsiderationRow), FormatNumber(cons.m_fRaw, 2), Muted(), FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;
      Label(pCons, plRectFloat(xNorm, y, fNumber, s_fConsiderationRow), FormatNumber(cons.m_fNormalized, 2), Muted(), FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;

      const plString sConsBar = Name(sBar, c, ".Cons");
      AddDrawList(pCons, plRectFloat(xBar, y + 5.0f, fConsBar - 8.0f, s_fConsiderationRow - 10.0f), sConsBar);
      plUiDrawListWriter consBar;
      DrawBar(consBar, plRectFloat(0.0f, 0.0f, fConsBar - 8.0f, s_fConsiderationRow - 10.0f), cons.m_fValue, AiColor());
      Publish(GetBoard(), consBar, sConsBar);

      const plColorGammaUB valueColor = cons.m_fValue <= 0.0005f ? Danger() : (cons.m_fValue < 0.5f ? Warning() : GoodColor());
      Label(pCons, plRectFloat(xValue, y, fNumber, s_fConsiderationRow), FormatNumber(cons.m_fValue, 2), valueColor, FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;
    }
  }
}

void plAiDebuggerView::BuildPerception(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plUInt8 uiAlert = 0;
  for (const plAiDebugFrame::Agent& agent : m_pSession->GetFrame().m_Agents)
  {
    if (agent.m_Object == watched.m_Object)
      uiAlert = agent.m_uiAlertLevel;
  }

  Section(pBody, "Alert level", AiColor(), {}, "derived from the remembered targets; behaviors can score on it with the Alert Level input");

  // four segments, the current one filled
  {
    auto* pStrip = Panel(pBody);
    pStrip->m_vOffsets.w = 40.0f;
    pStrip->m_Color = plColorGammaUB(0, 0, 0, 0);

    const float fSegment = (fWidth - 24.0f - 3.0f * 4.0f) / 4.0f;
    for (plUInt32 i = 0; i < 4; ++i)
    {
      const bool bOn = i == uiAlert;
      auto* pSegment = Panel(pStrip);
      Place(pSegment, plRectFloat(12.0f + i * (fSegment + 4.0f), 8.0f, fSegment, 24.0f));
      pSegment->m_Color = bOn ? AlertColor(i) : Raised();
      pSegment->m_vCornerRadius = plVec4(3.0f);

      auto* pText = Label(pSegment, plRectFloat(0.0f, 0.0f, fSegment, 24.0f), AlertName(i), bOn ? plColorGammaUB(20, 20, 24) : Dim(), FontSize - 2);
      pText->m_HAlign = plUiTextHAlign::Center;
    }
  }

  plStringBuilder sCount;
  sCount.SetFormat("{}", watched.m_Targets.GetCount());
  Section(pBody, "Targets", AiColor(), sCount, "confidence decays while no sense refreshes it; the position is the last known one");

  if (watched.m_Targets.IsEmpty())
  {
    BuildEmpty(pBody, fWidth, "Nothing perceived", "The agent remembers no targets. Sight, hearing, damage and custom stimuli add them.");
    BuildThreat(pBody, fWidth, watched);
    return;
  }

  const float fTarget = plMath::Clamp(fWidth * 0.22f, 100.0f, 170.0f);
  const float fSenses = 4.0f * (BadgeWidth("damage") + 4.0f);
  const float fConfidence = 150.0f;
  const float fLast = 80.0f;
  const float xSenses = 12.0f + fTarget;
  const float xConfidence = xSenses + fSenses + 8.0f;
  const float xLast = xConfidence + fConfidence + 8.0f;
  const float xPosition = xLast + fLast + 8.0f;

  // the header
  {
    auto* pHeader = Panel(pBody);
    pHeader->m_vOffsets.w = 20.0f;
    pHeader->m_Color = plColorGammaUB(0, 0, 0, 0);

    Label(pHeader, plRectFloat(12.0f, 2.0f, fTarget, 16.0f), "TARGET", Dim(), MinFontSize);
    Label(pHeader, plRectFloat(xSenses, 2.0f, fSenses, 16.0f), "SENSES", Dim(), MinFontSize);
    Label(pHeader, plRectFloat(xConfidence, 2.0f, fConfidence, 16.0f), "CONFIDENCE", Dim(), MinFontSize);
    Label(pHeader, plRectFloat(xLast, 2.0f, fLast, 16.0f), "LAST SENSED", Dim(), MinFontSize);
    Label(pHeader, plRectFloat(xPosition, 2.0f, plMath::Max(fWidth - xPosition - 8.0f, 40.0f), 16.0f), "POSITION", Dim(), MinFontSize);
  }

  const char* szSenses[] = {"sight", "hear", "damage", "custom"};
  const double fHead = GetHeadTime();

  for (plUInt32 i = 0; i < watched.m_Targets.GetCount(); ++i)
  {
    const plAiDebugFrame::Target& target = watched.m_Targets[i];

    auto* pRow = Panel(pBody);
    pRow->m_vOffsets.w = s_fTableRow;
    pRow->m_Color = (static_cast<plInt32>(i) == watched.m_iActiveTarget) ? Mix(Surface(), Accent(), 0.08f) : plColorGammaUB(0, 0, 0, 0);

    plStringBuilder sName = target.m_sName;
    if (!target.m_Object.IsValid())
      sName = "Event";
    else if (sName.IsEmpty())
      sName = "(unnamed)";

    Label(pRow, plRectFloat(12.0f, 0.0f, fTarget - 8.0f, s_fTableRow), sName, Foreground(), FontSize - 2);

    float x = xSenses;
    for (plUInt32 s = 0; s < PL_ARRAY_SIZE(szSenses); ++s)
    {
      const bool bOn = (target.m_uiSenses & PL_BIT(s)) != 0;
      x += Badge(pRow, x, (s_fTableRow - BadgeHeight) * 0.5f, szSenses[s], bOn ? AiColor() : Line()) + 4.0f;
    }

    const plString sBar = Name("Dbg.Confidence.", i);
    AddDrawList(pRow, plRectFloat(xConfidence, 8.0f, fConfidence - 44.0f, 8.0f), sBar);
    plUiDrawListWriter bar;
    DrawBar(bar, plRectFloat(0.0f, 0.0f, fConfidence - 44.0f, 8.0f), target.m_fConfidence, AiColor());
    Publish(GetBoard(), bar, sBar);
    Label(pRow, plRectFloat(xConfidence + fConfidence - 38.0f, 0.0f, 38.0f, s_fTableRow), FormatNumber(target.m_fConfidence, 2), Foreground(), FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;

    plStringBuilder sLast;
    sLast.SetFormat("{} s ago", plArgF(plMath::Max(0.0, fHead - target.m_fLastStimulusTime), 1));
    Label(pRow, plRectFloat(xLast, 0.0f, fLast, s_fTableRow), sLast, Muted(), FontSize - 2, true);

    const bool bSeen = fHead - target.m_fLastSightTime < 0.5;
    plStringBuilder sPosition;
    sPosition.SetFormat("{} ({}, {}, {})", bSeen ? "seen" : (target.m_Object.IsValid() ? "last known" : "event"), plArgF(target.m_vLastKnownPosition.x, 1), plArgF(target.m_vLastKnownPosition.y, 1), plArgF(target.m_vLastKnownPosition.z, 1));
    Label(pRow, plRectFloat(xPosition, 0.0f, plMath::Max(fWidth - xPosition - 8.0f, 40.0f), s_fTableRow), sPosition, target.m_bReported ? Warning() : Muted(), FontSize - 2);
  }

  BuildThreat(pBody, fWidth, watched);
}

void plAiDebuggerView::BuildThreat(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  // what the agent's team knows where the agent stands
  Section(pBody, "Threat map", AiColor(), {}, "per team, 2 m cells: line of sight to the enemy positions the team remembers, and danger areas");

  if (!watched.m_bHasThreatMap)
  {
    BuildEmpty(pBody, fWidth, "No threat map", watched.m_sTeam.IsEmpty() ? "The agent has no team, so the threat map has nothing to say about it." : "The team has no map yet. It exists once the world simulates.");
  }
  else
  {
    plStringBuilder sThreat, sDanger;
    sThreat.SetFormat("{}", plArgF(watched.m_fThreatHere, 2));
    sDanger.SetFormat("{}", plArgF(watched.m_fDangerHere, 2));

    const char* szRows[][2] = {
      {"Team", watched.m_sTeam.GetData()},
      {"Threat here", sThreat.GetData()},
      {"Danger here", sDanger.GetData()},
      {"Seen by a known enemy", watched.m_bSeenHere ? "yes" : "no"},
    };

    for (plUInt32 r = 0; r < PL_ARRAY_SIZE(szRows); ++r)
    {
      auto* pRow = Panel(pBody);
      pRow->m_vOffsets.w = 22.0f;
      pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

      const bool bDanger = r == 2 && watched.m_fDangerHere > 0.0f;

      Label(pRow, plRectFloat(12.0f, 0.0f, 200.0f, 22.0f), szRows[r][0], Muted(), FontSize - 2);
      Label(pRow, plRectFloat(212.0f, 0.0f, fWidth - 224.0f, 22.0f), szRows[r][1], bDanger ? Danger() : Foreground(), FontSize - 2, r != 0);
    }
  }

  // the positions its latest cover query chose between
  plStringBuilder sCount;
  sCount.SetFormat("{}", watched.m_CoverCandidates.GetCount());
  Section(pBody, "Cover candidates", AiColor(), sCount, "best first \xC2\xB7 score = protection, nearness and safety from the threat, times 1 minus danger");

  if (watched.m_CoverCandidates.IsEmpty())
  {
    BuildEmpty(pBody, fWidth, "No cover query yet", "The agent's take-cover or hide behavior fills this when it picks a position.");
    return;
  }

  const float fNumber = 52.0f;
  const float xScore = fWidth - 12.0f - fNumber;
  const float xDanger = xScore - fNumber;
  const float xThreat = xDanger - fNumber;
  const float xProtection = xThreat - fNumber;

  // the header
  {
    auto* pHeader = Panel(pBody);
    pHeader->m_vOffsets.w = 20.0f;
    pHeader->m_Color = plColorGammaUB(0, 0, 0, 0);

    Label(pHeader, plRectFloat(12.0f, 2.0f, xProtection - 20.0f, 16.0f), "POINT", Dim(), MinFontSize);

    const char* szColumns[] = {"PROT", "THR", "DNG", "SCORE"};
    const float xColumns[] = {xProtection, xThreat, xDanger, xScore};
    for (plUInt32 i = 0; i < PL_ARRAY_SIZE(szColumns); ++i)
    {
      Label(pHeader, plRectFloat(xColumns[i], 2.0f, fNumber, 16.0f), szColumns[i], Dim(), MinFontSize)->m_HAlign = plUiTextHAlign::Right;
    }
  }

  for (plUInt32 i = 0; i < watched.m_CoverCandidates.GetCount() && i < 10; ++i)
  {
    const plAiDebugFrame::Watched::CoverCandidate& candidate = watched.m_CoverCandidates[i];

    auto* pRow = Panel(pBody);
    pRow->m_vOffsets.w = s_fTableRow;
    pRow->m_Color = candidate.m_bPicked ? Mix(Surface(), Accent(), 0.08f) : plColorGammaUB(0, 0, 0, 0);

    plStringBuilder sPoint;
    sPoint.SetFormat("({}, {}, {})", plArgF(candidate.m_vPosition.x, 1), plArgF(candidate.m_vPosition.y, 1), plArgF(candidate.m_vPosition.z, 1));
    const float fPoint = TextWidth(sPoint, FontSize - 2);
    Label(pRow, plRectFloat(12.0f, 0.0f, fPoint, s_fTableRow), sPoint, Foreground(), FontSize - 2, true);

    float x = 20.0f + fPoint;
    if (candidate.m_uiQuality != 0)
    {
      x += Badge(pRow, x, (s_fTableRow - BadgeHeight) * 0.5f, candidate.m_uiQuality == plAiCoverQuality::High ? "high" : "low", Dim()) + 4.0f;
    }

    if (candidate.m_bPicked)
    {
      Badge(pRow, x, (s_fTableRow - BadgeHeight) * 0.5f, "picked", AiColor());
    }

    const float fValues[] = {candidate.m_fProtection, candidate.m_fThreat, candidate.m_fDanger, candidate.m_fScore};
    const float xColumns[] = {xProtection, xThreat, xDanger, xScore};
    for (plUInt32 c = 0; c < PL_ARRAY_SIZE(fValues); ++c)
    {
      const bool bDanger = c == 2 && fValues[c] > 0.0f;
      Label(pRow, plRectFloat(xColumns[c], 0.0f, fNumber, s_fTableRow), FormatNumber(fValues[c], 2), bDanger ? Danger() : (c == 3 ? Foreground() : Muted()), FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;
    }
  }
}

void plAiDebuggerView::BuildBlackboard(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plStringBuilder sCount;
  sCount.SetFormat("{}", watched.m_Blackboard.GetCount());
  Section(pBody, "Blackboard", AiColor(), sCount, "values that just changed show in the accent colour");

  if (watched.m_Blackboard.IsEmpty())
  {
    BuildEmpty(pBody, fWidth, "No entries", "The agent has no blackboard, or its blackboard is empty.");
    return;
  }

  const float fKey = plMath::Clamp(fWidth * 0.36f, 140.0f, 260.0f);
  const float fType = 80.0f;
  const plUInt32 uiNow = m_pSession->GetFrameCounter();

  for (const plAiDebugFrame::BlackboardEntry& entry : watched.m_Blackboard)
  {
    const plUInt32* pChanged = m_ChangedAt.GetValue(entry.m_sName);
    const bool bChanged = !m_bFrozen && pChanged != nullptr && uiNow - *pChanged < s_uiChangedFrames;

    auto* pRow = Panel(pBody);
    pRow->m_vOffsets.w = 22.0f;
    pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

    Label(pRow, plRectFloat(12.0f, 0.0f, fKey - 8.0f, 22.0f), entry.m_sName, Muted(), FontSize - 2, true);
    Label(pRow, plRectFloat(12.0f + fKey, 0.0f, fWidth - fKey - fType - 32.0f, 22.0f), entry.m_sValue.IsEmpty() ? plString("-") : entry.m_sValue, bChanged ? Accent() : Foreground(), FontSize - 2, true);
    Label(pRow, plRectFloat(fWidth - fType - 12.0f, 0.0f, fType, 22.0f), entry.m_sType, Dim(), FontSize - 3)->m_HAlign = plUiTextHAlign::Right;
  }
}

void plAiDebuggerView::BuildNavmeshSectors(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiDebugFrame::Navmesh& nav = m_pSession->GetFrame().m_Navmesh;

  if (!nav.m_bPresent)
  {
    Section(pBody, "Navmesh sectors", AiColor(), {}, "what the navmesh builds, and why");
    BuildEmpty(pBody, fWidth, "No navmesh", "The world has no navmesh yet. It exists once a scene with navigation components has simulated.");
    return;
  }

  // the section with its tools: the build slots and a rebuild of the inspected sector
  {
    plStringBuilder sCount, sHint;
    sCount.SetFormat("{}", nav.m_Sectors.GetCount());
    sHint.SetFormat("'{}' \xC2\xB7 {} x {} sectors of {} m", nav.m_sName, nav.m_uiSectorsX, nav.m_uiSectorsY, plArgF(nav.m_fSectorSize, 0));

    const char* szRebuild = "Rebuild sector";
    const float fRebuild = TextWidth(szRebuild) + 42.0f;
    const float fMaxLabel = TextWidth("Max builds", FontSize - 2) + 6.0f;
    const float fChip = 26.0f;
    const float fTools = fRebuild + 12.0f + fMaxLabel + 3.0f * (fChip + 3.0f);

    plUiPanel* pSection = Section(pBody, "Navmesh sectors", AiColor(), sCount, sHint, fTools + 8.0f);

    float x = -fTools - 6.0f;
    auto placeRight = [&](plUiElement* pElement, float fWidthOfElement)
    {
      pElement->m_vAnchorMin.x = pElement->m_vAnchorMax.x = 1.0f;
      pElement->m_vOffsets.x = x;
      x += fWidthOfElement + 3.0f;
    };

    const plVec2I32 inspected = m_pSession->GetInspectedSector();

    plUiButton* pRebuild = AddTextButton(pSection, plRectFloat(0, 1.0f, fRebuild, ControlHeight), "Dbg.NavRebuild", szRebuild, "Queues a rebuild of the inspected sector.", [this]()
      {
      m_pSession->RebuildInspectedSector();
      Invalidate(); }, false, inspected.x >= 0);

    auto* pText = static_cast<plUiText*>(pRebuild->m_Children[0]);
    pText->m_HAlign = plUiTextHAlign::Left;
    pText->m_vOffsets.x = 28.0f;

    plUiImage* pIcon = SvgImage(pRebuild, IconPath(Icon::Refresh));
    pIcon->m_vAnchorMin = pIcon->m_vAnchorMax = plVec2(0, 0.5f);
    pIcon->m_vOffsets = plVec4(9, -7, 14, 14);
    pIcon->m_Color = Muted();

    placeRight(pRebuild, fRebuild + 9.0f);

    plUiText* pMaxLabel = Label(pSection, plRectFloat(0, 0, fMaxLabel, SectionHeight), "Max builds", Muted(), FontSize - 2);
    pMaxLabel->m_vAnchorMin.x = pMaxLabel->m_vAnchorMax.x = 1.0f;
    pMaxLabel->m_vOffsets.x = x;
    x += fMaxLabel;

    const plUInt32 uiSlots[] = {1, 2, 4};
    for (plUInt32 i = 0; i < PL_ARRAY_SIZE(uiSlots); ++i)
    {
      plStringBuilder sLabel;
      sLabel.SetFormat("{}", uiSlots[i]);

      const plUInt32 uiSlot = uiSlots[i];
      // a chip is a plate plus a click area, so it takes an absolute position instead of right anchors
      AddChip(pSection, plRectFloat(fWidth + x, 2.0f, fChip, 22.0f),Name("Dbg.NavSlots.", uiSlot), sLabel, nav.m_uiMaxBuilds == uiSlot, plColorGammaUB(0, 0, 0, 0), "How many sectors build at the same time (AI.Navmesh.MaxConcurrentBuilds).", [this, uiSlot]()
        {
        m_pSession->SetIntCVar("AI.Navmesh.MaxConcurrentBuilds", static_cast<plInt32>(uiSlot));
        Invalidate(); });
      x += fChip + 3.0f;
    }
  }

  // the view modes: the same radio group as the AI Overlays menu
  {
    auto* pRow = Panel(pBody);
    pRow->m_vOffsets.w = 34.0f;
    pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

    const float fLabel = TextWidth("View", FontSize - 2) + 16.0f;
    Label(pRow, plRectFloat(12.0f, 0.0f, fLabel, 34.0f), "View", Muted(), FontSize - 2);

    const plArrayPtr<const plAiDebugOverlay> overlays = plAiGetDebugOverlays();
    float x = 12.0f + fLabel;

    for (plUInt32 i = 0; i < overlays.GetCount(); ++i)
    {
      if (overlays[i].m_szRadioGroup == nullptr || !plStringUtils::IsEqual(overlays[i].m_szRadioGroup, "NavmeshView"))
        continue;

      const bool bOn = (m_pSession->GetFrame().m_uiOverlays & PL_BIT(i)) != 0;
      const float fChipWidth = ChipWidth(overlays[i].m_szLabel, false);

      AddChip(pRow, plRectFloat(x, 6.0f, fChipWidth, 22.0f), Name("Dbg.NavView.", i), overlays[i].m_szLabel, bOn, plColorGammaUB(0, 0, 0, 0), "Draws the navmesh in the viewport this way (AI Overlays menu, Navigation).", [this, i, bOn]()
        {
        m_pSession->SetOverlay(i, !bOn);
        Invalidate(); });

      x += fChipWidth + 4.0f;
    }
  }

  // the inspected sector
  const plVec2I32 inspected = m_pSession->GetInspectedSector();
  const plAiDebugFrame::NavSector* pInspected = nullptr;
  for (const plAiDebugFrame::NavSector& sector : nav.m_Sectors)
  {
    if (sector.m_vCoord == inspected)
      pInspected = &sector;
  }

  if (pInspected == nullptr)
  {
    BuildEmpty(pBody, fWidth, "No sector selected", "Watch an agent to see the sector it stands in, or click a sector in the lists below.");
  }
  else
  {
    const plAiNavMeshSectorState state = static_cast<plAiNavMeshSectorState>(pInspected->m_uiState);
    const bool bNear = plAiIsNearPolyBudget(pInspected->m_uiPolys, nav.m_uiPolyBudget);

    plStringBuilder sTitle;
    sTitle.SetFormat("Navmesh Sector ({}, {})", inspected.x, inspected.y);

    auto* pTitle = Panel(pBody);
    pTitle->m_vOffsets.w = 28.0f;
    pTitle->m_Color = plColorGammaUB(0, 0, 0, 0);

    const float fTitle = TextWidth(sTitle, FontSize - 1);
    Label(pTitle, plRectFloat(12.0f, 0.0f, fTitle, 28.0f), sTitle, Foreground(), FontSize - 1);
    Badge(pTitle, 20.0f + fTitle, 6.0f, plAiNavMeshSectorStateName(state), SectorStateColor(state));

    const char* szSource = m_pSession->IsSectorPicked() ? "picked" : "under the watched agent";
    const float fSource = TextWidth(szSource, FontSize - 2);
    Label(pTitle, plRectFloat(fWidth - fSource - 12.0f - (m_pSession->IsSectorPicked() ? 90.0f : 0.0f), 0.0f, fSource, 28.0f), szSource, Dim(), FontSize - 2);

    if (m_pSession->IsSectorPicked())
    {
      AddChip(pTitle, plRectFloat(fWidth - 84.0f, 3.0f, 78.0f, 22.0f), "Dbg.NavFollow", "Follow", false, plColorGammaUB(0, 0, 0, 0), "Goes back to the sector under the watched agent.", [this]()
        {
        m_pSession->PickSector(plVec2I32(-1, -1));
        Invalidate(); });
    }

    plStringBuilder sPolys, sBuilds, sMs;
    sPolys.SetFormat("{} / {}", pInspected->m_uiPolys, nav.m_uiPolyBudget);
    sBuilds.SetFormat("{}", pInspected->m_uiBuilds);
    sMs.SetFormat("{} ms", plArgF(pInspected->m_fBuildMs, 0));

    const char* szRows[][2] = {
      {"Last reason", plAiNavMeshRebuildReasonName(static_cast<plAiNavMeshRebuildReason>(pInspected->m_uiReason))},
      {"Polygons", sPolys.GetData()},
      {"", ""},
      {"Builds", sBuilds.GetData()},
      {"Last build", sMs.GetData()},
    };

    for (plUInt32 r = 0; r < PL_ARRAY_SIZE(szRows); ++r)
    {
      auto* pRow = Panel(pBody);
      pRow->m_vOffsets.w = 22.0f;
      pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

      if (r == 2)
      {
        // the polygon bar under the count, against the budget
        const plString sBar = Name("Dbg.NavPolys.", 0);
        const float fBar = fWidth - 224.0f;
        AddDrawList(pRow, plRectFloat(212.0f, 7.0f, fBar, 8.0f), sBar);
        plUiDrawListWriter bar;
        DrawBar(bar, plRectFloat(0.0f, 0.0f, fBar, 8.0f), nav.m_uiPolyBudget > 0 ? static_cast<float>(pInspected->m_uiPolys) / nav.m_uiPolyBudget : 0.0f, bNear ? Warning() : AiColor());
        Publish(GetBoard(), bar, sBar);
        continue;
      }

      Label(pRow, plRectFloat(12.0f, 0.0f, 200.0f, 22.0f), szRows[r][0], Muted(), FontSize - 2);
      Label(pRow, plRectFloat(212.0f, 0.0f, fWidth - 224.0f, 22.0f), szRows[r][1], Foreground(), FontSize - 2, true);
    }

    const char* szNote = nullptr;
    plColorGammaUB noteColor = Dim();

    if (state == plAiNavMeshSectorState::Failed)
    {
      szNote = "Last build failed. Agents in this sector report NoPathFound. Rebuild retries it.";
      noteColor = Danger();
    }
    else if (bNear)
    {
      szNote = "Close to the polygon limit. Mark clutter as not walkable or raise the cell size.";
      noteColor = Warning();
    }

    if (szNote != nullptr)
    {
      auto* pNote = Panel(pBody);
      pNote->m_vOffsets.w = 36.0f;
      pNote->m_Color = plColorGammaUB(0, 0, 0, 0);

      auto* pLabel = Label(pNote, plRectFloat(12.0f, 4.0f, fWidth - 24.0f, 30.0f), szNote, noteColor, FontSize - 2);
      pLabel->m_bWrap = true;
      pLabel->m_VAlign = plUiTextVAlign::Top;
    }
  }

  // the build queue: sectors that wait or build, running ones first; then the ones that need a look
  plHybridArray<const plAiDebugFrame::NavSector*, 32> queue;
  plHybridArray<const plAiDebugFrame::NavSector*, 32> attention;

  for (const plAiDebugFrame::NavSector& sector : nav.m_Sectors)
  {
    const plAiNavMeshSectorState state = static_cast<plAiNavMeshSectorState>(sector.m_uiState);

    if (state == plAiNavMeshSectorState::Queued || state == plAiNavMeshSectorState::Building || state == plAiNavMeshSectorState::Rebuilding)
      queue.PushBack(&sector);
    else if (state == plAiNavMeshSectorState::Failed || plAiIsNearPolyBudget(sector.m_uiPolys, nav.m_uiPolyBudget))
      attention.PushBack(&sector);
  }

  queue.Sort([](const plAiDebugFrame::NavSector* a, const plAiDebugFrame::NavSector* b) { return a->m_bRunning && !b->m_bRunning; });

  const auto buildRows = [&](const plHybridArray<const plAiDebugFrame::NavSector*, 32>& sectors, const char* szPrefix, bool bQueue)
  {
    for (plUInt32 i = 0; i < sectors.GetCount() && i < 12; ++i)
    {
      const plAiDebugFrame::NavSector& sector = *sectors[i];
      const plAiNavMeshSectorState state = static_cast<plAiNavMeshSectorState>(sector.m_uiState);

      auto* pRow = Panel(pBody);
      pRow->m_vOffsets.w = s_fTableRow;
      pRow->m_Color = sector.m_vCoord == inspected ? Mix(Surface(), Accent(), 0.08f) : plColorGammaUB(0, 0, 0, 0);

      plStringBuilder sText;
      sText.SetFormat("Sector ({}, {}) \xC2\xB7 {}", sector.m_vCoord.x, sector.m_vCoord.y, plAiNavMeshRebuildReasonName(static_cast<plAiNavMeshRebuildReason>(sector.m_uiReason)));
      Label(pRow, plRectFloat(12.0f, 0.0f, fWidth - 220.0f, s_fTableRow), sText, Foreground(), FontSize - 2);

      if (bQueue)
      {
        const plString sBar = Name(szPrefix, i);
        AddDrawList(pRow, plRectFloat(fWidth - 190.0f, 8.0f, 90.0f, 8.0f), sBar);
        plUiDrawListWriter bar;
        DrawBar(bar, plRectFloat(0.0f, 0.0f, 90.0f, 8.0f), sector.m_bRunning ? sector.m_fProgress : 0.0f, AiColor());
        Publish(GetBoard(), bar, sBar);
      }

      plStringBuilder sState;
      if (bQueue)
        sState = sector.m_bRunning ? "building" : "queued";
      else if (state == plAiNavMeshSectorState::Failed)
        sState = "failed";
      else
        sState.SetFormat("{} polygons", sector.m_uiPolys);

      Label(pRow, plRectFloat(fWidth - 92.0f, 0.0f, 80.0f, s_fTableRow), sState, state == plAiNavMeshSectorState::Failed ? Danger() : (bQueue ? Dim() : Warning()), FontSize - 2);

      const plVec2I32 coord = sector.m_vCoord;
      AddClickArea(pRow, plRectFloat(0.0f, 0.0f, fWidth, s_fTableRow), Name(szPrefix, i, ".Pick"), "Inspect this sector and outline it in the viewport.", [this, coord]()
        {
        m_pSession->PickSector(coord);
        Invalidate(); });
    }
  };

  {
    plStringBuilder sCount;
    sCount.SetFormat("{}", queue.GetCount());
    Section(pBody, "Build queue", AiColor(), queue.IsEmpty() ? plStringView("idle") : plStringView(sCount), "moving a blocker or nav link queues the sectors it touches");

    if (queue.IsEmpty())
      BuildEmpty(pBody, fWidth, "No sectors waiting", "All requested sectors are built.");
    else
      buildRows(queue, "Dbg.NavQueue.", true);
  }

  if (!attention.IsEmpty())
  {
    plStringBuilder sCount;
    sCount.SetFormat("{}", attention.GetCount());
    Section(pBody, "Needs a look", Warning(), sCount, "failed builds and sectors close to the polygon limit");
    buildRows(attention, "Dbg.NavAttention.", false);
  }
}

void plAiDebuggerView::BuildNavigation(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched* pWatched)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  BuildNavmeshSectors(pBody, fWidth);

  Section(pBody, "Agent navigation", AiColor(), {}, "the watched agent's navigation component, as the engine reports it");

  if (pWatched == nullptr)
  {
    BuildEmpty(pBody, fWidth, "No agent watched", "Pick an agent to see its navigation component.");
    return;
  }

  const plAiDebugFrame::Watched& watched = *pWatched;

  if (watched.m_sNavigation.IsEmpty())
  {
    BuildEmpty(pBody, fWidth, "No navigation component", "The agent's object has no navigation or voxel navigation component.");
    return;
  }

  plVec3 vPosition = plVec3::MakeZero();
  for (const plAiDebugFrame::Agent& agent : m_pSession->GetFrame().m_Agents)
  {
    if (agent.m_Object == watched.m_Object)
      vPosition = agent.m_vPosition;
  }

  plStringBuilder sTarget, sPosition, sDistance;
  sTarget.SetFormat("({}, {}, {})", plArgF(watched.m_vSteeringTarget.x, 2), plArgF(watched.m_vSteeringTarget.y, 2), plArgF(watched.m_vSteeringTarget.z, 2));
  sPosition.SetFormat("({}, {}, {})", plArgF(vPosition.x, 2), plArgF(vPosition.y, 2), plArgF(vPosition.z, 2));
  sDistance.SetFormat("{} m", plArgF((watched.m_vSteeringTarget - vPosition).GetLength(), 2));

  const char* szRows[][2] = {
    {"Component", watched.m_sNavigation.GetData()},
    {"State", watched.m_sNavigationState.GetData()},
    {"Steering toward", sTarget.GetData()},
    {"Agent position", sPosition.GetData()},
    {"Distance to steering point", sDistance.GetData()},
  };

  for (const auto& row : szRows)
  {
    auto* pRow = Panel(pBody);
    pRow->m_vOffsets.w = 22.0f;
    pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

    Label(pRow, plRectFloat(12.0f, 0.0f, 200.0f, 22.0f), row[0], Muted(), FontSize - 2);
    Label(pRow, plRectFloat(212.0f, 0.0f, fWidth - 224.0f, 22.0f), row[1], Foreground(), FontSize - 2, true);
  }
}

//////////////////////////////////////////////////////////////////////////
// the Squad tab

namespace
{
  const char* SquadIntentName(plUInt8 uiIntent)
  {
    switch (uiIntent)
    {
      case plAiSquadIntent::Engage:
        return "Engage";
      case plAiSquadIntent::Advance:
        return "Advance";
      case plAiSquadIntent::Hold:
        return "Hold";
      case plAiSquadIntent::Retreat:
        return "Retreat";
      default:
        return "None";
    }
  }

  /// What a member does under an intent: the roles of the mockup.
  const char* SquadRole(plUInt8 uiIntent, bool bMoveToken)
  {
    switch (uiIntent)
    {
      case plAiSquadIntent::Hold:
        return "Hold position";
      case plAiSquadIntent::Advance:
        return bMoveToken ? "Bound" : "Fire from cover";
      case plAiSquadIntent::Engage:
        return "Fire from cover";
      case plAiSquadIntent::Retreat:
        return "Fall back";
      default:
        return "Free";
    }
  }

  plAiSquadOrder MakeSlotOrder(const plAiDebugFrame::Squad& squad)
  {
    plAiSquadOrder order;
    order.m_bHasRegion = squad.m_bOrderRegion;
    order.m_vRegionMin = squad.m_vRegionMin;
    order.m_vRegionMax = squad.m_vRegionMax;
    return order;
  }
} // namespace

const plAiDebugFrame::Squad* plAiDebuggerView::FindSquad() const
{
  const plAiDebugFrame& frame = m_pSession->GetFrame();

  for (const plAiDebugFrame::Squad& squad : frame.m_Squads)
  {
    if (squad.m_sId == m_sSquad)
      return &squad;
  }

  return frame.m_Squads.IsEmpty() ? nullptr : &frame.m_Squads[0];
}

plVec2 plAiDebuggerView::SquadMapToWorld(const plVec2& vSurface) const
{
  const float x = vSurface.x - m_SquadMapRect.x;
  const float y = m_SquadMapRect.height - (vSurface.y - m_SquadMapRect.y);

  return plVec2(m_vSquadMapMin.x + x / plMath::Max(m_fSquadMapScale, 0.0001f), m_vSquadMapMin.y + y / plMath::Max(m_fSquadMapScale, 0.0001f));
}

void plAiDebuggerView::IssueSquadDraft()
{
  const plAiDebugFrame::Squad* pSquad = FindSquad();
  if (pSquad == nullptr)
    return;

  m_pSession->IssueSquadOrder(pSquad->m_sId, m_uiSquadIntent, m_bSquadDraft, m_vSquadDraftMin, m_vSquadDraftMax, m_fSquadSeconds, m_bSquadNoRetreat);
  m_bSquadDraft = false;
  Invalidate();
}

namespace
{
  /// A grid step in meters that is at least fMinMeters wide.
  float NiceGridStep(float fMinMeters)
  {
    const float fSteps[] = {1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f};

    for (const float fStep : fSteps)
    {
      if (fStep >= fMinMeters)
        return fStep;
    }

    return 2000.0f;
  }
} // namespace

void plAiDebuggerView::PublishSquadWorld(plUiDrawListWriter& w, const plRectFloat& local)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const float fHeight = local.height;
  const float fScale = plMath::Max(m_fSquadMapScale, 0.0001f);
  const plVec2 vViewMin = m_vSquadMapMin;
  const plVec2 vViewMax = m_vSquadMapMin + plVec2(local.width, fHeight) / fScale;
  const float fStep = NiceGridStep(56.0f / fScale);

  auto toMap = [&](const plVec2& vWorld) { return plVec2((vWorld.x - vViewMin.x) * fScale, fHeight - (vWorld.y - vViewMin.y) * fScale); };

  const plAiDebugFrame::Navmesh& nav = m_pSession->GetFrame().m_Navmesh;
  const bool bSectors = nav.m_bPresent && nav.m_fSectorSize > 0.0f;

  if (bSectors)
  {
    // the navmesh's sector grid, centered on the world origin: the sectors that are built are lit, the others outlined
    const float fSize = nav.m_fSectorSize;
    const plVec2 vOrigin(-0.5f * nav.m_uiSectorsX * fSize, -0.5f * nav.m_uiSectorsY * fSize);
    const plVec2 vOriginEnd = vOrigin + plVec2(nav.m_uiSectorsX * fSize, nav.m_uiSectorsY * fSize);

    for (const plAiDebugFrame::NavSector& sector : nav.m_Sectors)
    {
      const plVec2 vMin = vOrigin + plVec2(sector.m_vCoord.x * fSize, sector.m_vCoord.y * fSize);
      const plVec2 a = toMap(vMin);
      const plVec2 b = toMap(vMin + plVec2(fSize));
      const plRectFloat cell(a.x, b.y, b.x - a.x, a.y - b.y);

      if (cell.x > local.width || cell.y > fHeight || cell.x + cell.width < 0.0f || cell.y + cell.height < 0.0f)
        continue;

      const plAiNavMeshSectorState state = static_cast<plAiNavMeshSectorState>(sector.m_uiState);

      if (state == plAiNavMeshSectorState::Ready || state == plAiNavMeshSectorState::Rebuilding)
        w.Rect(cell, WithAlpha(Foreground(), 12));
      else
        w.RectOutline(cell, Dim(), 1.0f);

      if (cell.width >= 44.0f)
      {
        plStringBuilder sCoord;
        sCoord.SetFormat("{}, {}", sector.m_vCoord.x, sector.m_vCoord.y);
        w.Text(plRectFloat(cell.x + 5.0f, cell.y + 3.0f, cell.width - 8.0f, 14.0f), sCoord, Dim(), FontSize - 3);
      }
    }

    for (plUInt32 i = 0; i <= nav.m_uiSectorsX; ++i)
    {
      const float x = vOrigin.x + i * fSize;
      if (x >= vViewMin.x && x <= vViewMax.x)
        w.Line(toMap(plVec2(x, vOrigin.y)), toMap(plVec2(x, vOriginEnd.y)), Line(), 1.0f);
    }

    for (plUInt32 i = 0; i <= nav.m_uiSectorsY; ++i)
    {
      const float y = vOrigin.y + i * fSize;
      if (y >= vViewMin.y && y <= vViewMax.y)
        w.Line(toMap(plVec2(vOrigin.x, y)), toMap(plVec2(vOriginEnd.x, y)), Line(), 1.0f);
    }
  }
  else
  {
    // without a navmesh a meter grid still tells where things are
    for (float x = plMath::Ceil(vViewMin.x / fStep) * fStep; x <= vViewMax.x; x += fStep)
      w.Line(toMap(plVec2(x, vViewMin.y)), toMap(plVec2(x, vViewMax.y)), Line(), 1.0f);

    for (float y = plMath::Ceil(vViewMin.y / fStep) * fStep; y <= vViewMax.y; y += fStep)
      w.Line(toMap(plVec2(vViewMin.x, y)), toMap(plVec2(vViewMax.x, y)), Line(), 1.0f);
  }

  // the meters along the bottom and left edges
  plStringBuilder sTick;

  for (float x = plMath::Ceil(vViewMin.x / fStep) * fStep; x <= vViewMax.x; x += fStep)
  {
    const float fX = toMap(plVec2(x, 0.0f)).x;
    sTick.SetFormat("{} m", plArgF(x, 0));
    w.Line(plVec2(fX, fHeight - 5.0f), plVec2(fX, fHeight), Muted(), 1.0f);
    w.Text(plRectFloat(fX + 3.0f, fHeight - 17.0f, 64.0f, 12.0f), sTick, Dim(), FontSize - 3);
  }

  for (float y = plMath::Ceil(vViewMin.y / fStep) * fStep; y <= vViewMax.y; y += fStep)
  {
    const float fY = toMap(plVec2(0.0f, y)).y;
    if (fY > fHeight - 24.0f)
      continue;

    sTick.SetFormat("{} m", plArgF(y, 0));
    w.Line(plVec2(0.0f, fY), plVec2(5.0f, fY), Muted(), 1.0f);
    w.Text(plRectFloat(8.0f, fY - 13.0f, 64.0f, 12.0f), sTick, Dim(), FontSize - 3);
  }

  // what the map is: a plan view, the sector grid it follows, and the markers
  {
    plStringBuilder sInfo;
    sInfo.SetFormat("Plan view \xC2\xB7 +X right, +Y up");

    if (bSectors)
      sInfo.AppendFormat(" \xC2\xB7 sectors {} \xC3\x97 {} of {} m", nav.m_uiSectorsX, nav.m_uiSectorsY, plArgF(nav.m_fSectorSize, 0));

    w.Text(plRectFloat(10.0f, 6.0f, local.width * 0.5f, 14.0f), sInfo, Muted(), FontSize - 3);

    float fRight = local.width - 10.0f;

    auto legend = [&](const char* szLabel, const plColorGammaUB& fill, const plColorGammaUB& ring, float fRadius)
    {
      const float fText = TextWidth(szLabel, FontSize - 3);
      fRight -= fText;
      w.Text(plRectFloat(fRight, 6.0f, fText + 2.0f, 14.0f), szLabel, Muted(), FontSize - 3);
      fRight -= 12.0f;
      w.Circle(plVec2(fRight + 4.0f, 13.0f), fRadius, fill, ring, 1.5f);
      fRight -= 12.0f;
    };

    legend("Other agents", Dim(), plColorGammaUB(0, 0, 0, 0), 3.0f);
    legend("Other squads", Field(), Muted(), 4.5f);
    legend("This squad", Field(), AiColor(), 5.0f);
  }
}

void plAiDebuggerView::PublishSquadOthers(plUiDrawListWriter& w, const plRectFloat& local, const plAiDebugFrame::Squad* pSquad)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiDebugFrame& frame = m_pSession->GetFrame();
  const float fHeight = local.height;

  auto toMap = [&](const plVec2& vWorld) { return plVec2((vWorld.x - m_vSquadMapMin.x) * m_fSquadMapScale, fHeight - (vWorld.y - m_vSquadMapMin.y) * m_fSquadMapScale); };

  plHybridArray<plUuid, 32> inSquads;

  for (const plAiDebugFrame::Squad& squad : frame.m_Squads)
  {
    for (const plAiDebugFrame::SquadMember& member : squad.m_Members)
      inSquads.PushBack(member.m_Object);
  }

  // the other squads: their order's region and their members, named by the squad
  for (const plAiDebugFrame::Squad& squad : frame.m_Squads)
  {
    if (pSquad != nullptr && squad.m_sId == pSquad->m_sId)
      continue;

    if (squad.m_bHasOrder && squad.m_bOrderRegion)
    {
      const plVec2 a = toMap(squad.m_vRegionMin);
      const plVec2 b = toMap(squad.m_vRegionMax);
      w.RectOutline(plRectFloat(plMath::Min(a.x, b.x), plMath::Min(a.y, b.y), plMath::Abs(b.x - a.x), plMath::Abs(b.y - a.y)), Dim(), 1.0f);
    }

    for (plUInt32 i = 0; i < squad.m_Members.GetCount(); ++i)
    {
      const plVec2 vMember = toMap(squad.m_Members[i].m_vPosition.GetAsVec2());
      w.Circle(vMember, 5.0f, Field(), Muted(), 1.5f);

      if (i == 0)
      {
        plStringBuilder sLabel;
        sLabel.SetFormat("{} ({})", squad.m_sId, squad.m_Members.GetCount());
        w.Text(plRectFloat(vMember.x + 9.0f, vMember.y - 16.0f, 140.0f, 14.0f), sLabel, Muted(), FontSize - 2);
      }
    }
  }

  // the agents that are in no squad
  plUInt32 uiLabels = 0;

  for (const plAiDebugFrame::Agent& agent : frame.m_Agents)
  {
    if (inSquads.Contains(agent.m_Object))
      continue;

    const plVec2 vAgent = toMap(agent.m_vPosition.GetAsVec2());
    if (!local.Contains(vAgent))
      continue;

    w.Circle(vAgent, 3.0f, Dim());

    if (uiLabels < 48)
    {
      ++uiLabels;
      w.Text(plRectFloat(vAgent.x + 6.0f, vAgent.y - 7.0f, 110.0f, 14.0f), agent.m_sName.GetView(), Dim(), FontSize - 3);
    }
  }
}

void plAiDebuggerView::PublishSquadMap(const plAiDebugFrame::Squad* pSquad)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  if (m_sSquadMapEntry.IsEmpty())
    return;

  plUiDrawListWriter w;
  const plRectFloat local(0.0f, 0.0f, m_SquadMapRect.width, m_SquadMapRect.height);

  w.Rect(local, Field());
  w.PushClip(local);

  PublishSquadWorld(w, local);
  PublishSquadOthers(w, local, pSquad);

  if (pSquad != nullptr)
  {
    const float fHeight = local.height;
    auto toMap = [&](const plVec2& vWorld) { return plVec2((vWorld.x - m_vSquadMapMin.x) * m_fSquadMapScale, fHeight - (vWorld.y - m_vSquadMapMin.y) * m_fSquadMapScale); };

    auto dashedRect = [&](const plVec2& vMin, const plVec2& vMax, const plColorGammaUB& color)
    {
      const plVec2 a = toMap(vMin);
      const plVec2 b = toMap(vMax);
      const plVec2 corners[4] = {plVec2(a.x, a.y), plVec2(b.x, a.y), plVec2(b.x, b.y), plVec2(a.x, b.y)};

      for (plUInt32 i = 0; i < 4; ++i)
      {
        w.Line(corners[i], corners[(i + 1) % 4], color, 1.5f, 5.0f, 4.0f);
      }
    };

    const plColorGammaUB rose = AiColor();

    // the rally point
    {
      const plVec2 vHome = pSquad->m_vHome.GetAsVec2();
      dashedRect(vHome - plVec2(6.0f, 4.0f), vHome + plVec2(6.0f, 4.0f), Dim());
      const plVec2 vLabel = toMap(vHome + plVec2(-6.0f, 4.0f));
      w.Text(plRectFloat(vLabel.x, vLabel.y - 15.0f, 120.0f, 14.0f), "Rally point", Dim(), FontSize - 2);
    }

    // the order's region while it lasts, else the one being drawn
    if (pSquad->m_bHasOrder && pSquad->m_bOrderRegion)
    {
      const plVec2 a = toMap(pSquad->m_vRegionMin);
      const plVec2 b = toMap(pSquad->m_vRegionMax);
      const plRectFloat rect(plMath::Min(a.x, b.x), plMath::Min(a.y, b.y), plMath::Abs(b.x - a.x), plMath::Abs(b.y - a.y));
      w.Rect(rect, WithAlpha(rose, 30));
      w.RectOutline(rect, rose, 1.5f);

      plStringBuilder sLabel;
      sLabel.SetFormat("{}", SquadIntentName(pSquad->m_uiOrderIntent));
      if (pSquad->m_fOrderRemaining > 0.0)
        sLabel.AppendFormat(" \xC2\xB7 {} s", plArgF(pSquad->m_fOrderRemaining, 0));

      w.Text(plRectFloat(rect.x + 4.0f, rect.y - 16.0f, 160.0f, 14.0f), sLabel, Mix(rose, Foreground(), 0.4f), FontSize - 2);
    }
    else if (m_bSquadDraft)
    {
      const plVec2 a = toMap(m_vSquadDraftMin);
      const plVec2 b = toMap(m_vSquadDraftMax);
      const plRectFloat rect(plMath::Min(a.x, b.x), plMath::Min(a.y, b.y), plMath::Abs(b.x - a.x), plMath::Abs(b.y - a.y));
      w.Rect(rect, WithAlpha(Accent(), 20));
      dashedRect(m_vSquadDraftMin, m_vSquadDraftMax, Accent());

      plStringBuilder sLabel;
      sLabel.SetFormat("Draft region \xC2\xB7 {}", SquadIntentName(m_uiSquadIntent));
      w.Text(plRectFloat(rect.x + 4.0f, rect.y - 16.0f, 200.0f, 14.0f), sLabel, Mix(Accent(), Foreground(), 0.4f), FontSize - 2);
    }

    // the target the squad shares
    if (pSquad->m_bHasTarget)
    {
      const plVec2 vTarget = toMap(pSquad->m_vTarget.GetAsVec2());
      const plVec2 triangle[3] = {plVec2(vTarget.x, vTarget.y - 9.0f), plVec2(vTarget.x + 8.0f, vTarget.y + 6.0f), plVec2(vTarget.x - 8.0f, vTarget.y + 6.0f)};
      w.Triangles(plArrayPtr<const plVec2>(triangle, 3), Foreground());
      w.Text(plRectFloat(vTarget.x + 10.0f, vTarget.y - 7.0f, 100.0f, 14.0f), "Enemy", Foreground(), FontSize - 2);
    }

    // the members, with the spot an order's region gives each
    const plAiSquadOrder slots = MakeSlotOrder(*pSquad);
    const plUInt32 uiMembers = pSquad->m_Members.GetCount();

    for (plUInt32 i = 0; i < uiMembers; ++i)
    {
      const plAiDebugFrame::SquadMember& member = pSquad->m_Members[i];
      const plVec2 vMember = toMap(member.m_vPosition.GetAsVec2());

      if (pSquad->m_bHasOrder && pSquad->m_bOrderRegion)
      {
        const plVec2 vSlot = toMap(slots.GetRegionSlot(i, uiMembers));
        if ((vSlot - vMember).GetLength() > 3.0f)
          w.Line(vMember, vSlot, WithAlpha(rose, 128), 1.0f, 3.0f, 3.0f);
      }

      const bool bToken = member.m_bAttackToken || member.m_bMoveToken;
      w.Circle(vMember, 7.0f, rose, bToken ? Accent() : Field(), 2.0f);

      plStringBuilder sName(member.m_sName);
      if (sName.IsEmpty())
        sName.SetFormat("#{}", i + 1);

      w.Text(plRectFloat(vMember.x + 10.0f, vMember.y - 17.0f, 120.0f, 14.0f), sName, Mix(rose, Foreground(), 0.5f), FontSize - 2);
    }
  }

  w.PopClip();
  w.RectOutline(local, Line(), 1.0f);
  Publish(GetBoard(), w, m_sSquadMapEntry);
}

void plAiDebuggerView::BuildSquad(plUiElement* pPane, const plRectFloat& rect)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiDebugFrame& frame = m_pSession->GetFrame();
  const plAiDebugFrame::Squad* pSquad = FindSquad();

  const float fWidth = rect.width;
  const float fHeight = rect.height;

  // the pane's own area below the tab row; this function gets its place on the surface
  auto* pArea = Panel(pPane);
  Place(pArea, plRectFloat(0.0f, s_fTabHeight, fWidth, fHeight));
  pArea->m_Color = plColorGammaUB(0, 0, 0, 0);

  if (pSquad == nullptr)
  {
    auto* pEmpty = EmptyState(pArea, Icon::Group, "No squads", "Agents that share a Squad Id form a squad. Orders go to a squad: hold a spot, advance, engage or retreat, for a time.");
    Place(pEmpty, plRectFloat(0.0f, 0.0f, fWidth, fHeight));
    m_SquadMapRect = plRectFloat(0, 0, 0, 0);
    return;
  }

  m_sSquad = pSquad->m_sId;

  // ---- the toolbar: squad, order, expiry, flags and the buttons ----
  plAiFlowLayout layout(8.0f, 4.0f, fWidth - 8.0f, 30.0f, 6.0f);

  const auto iconButton = [&](const plRectFloat& r, plStringView sName, plStringView sLabel, plEditorUi::Icon icon, plStringView sTooltip, Action action, bool bEnabled)
  {
    plUiButton* pButton = AddTextButton(pArea, r, sName, sLabel, sTooltip, std::move(action), false, bEnabled);

    auto* pText = static_cast<plUiText*>(pButton->m_Children[0]);
    pText->m_HAlign = plUiTextHAlign::Left;
    pText->m_vOffsets.x = 28.0f;

    plUiImage* pIcon = SvgImage(pButton, IconPath(icon));
    pIcon->m_vAnchorMin = pIcon->m_vAnchorMax = plVec2(0, 0.5f);
    pIcon->m_vOffsets = plVec4(9, -7, 14, 14);
    pIcon->m_Color = bEnabled ? Muted() : Dim();
  };

  // squad
  {
    plStringBuilder sSquad;
    sSquad.SetFormat("{} ({})", pSquad->m_sId, pSquad->m_Members.GetCount());

    layout.Group(layout.GetWidth({TextWidth("Squad", FontSize - 2), 150.0f}));
    const plRectFloat label = layout.Take(TextWidth("Squad", FontSize - 2) + 4.0f, 20.0f);
    Label(pArea, label, "Squad", Muted(), FontSize - 2);

    AddDropdown(pArea, layout.Take(150.0f), "Dbg.Squad.Pick", sSquad, "The squad the tab shows and orders.", [this]()
      {
      plEditorUiMenu menu;
      const plAiDebugFrame& current = m_pSession->GetFrame();

      for (plUInt32 i = 0; i < current.m_Squads.GetCount(); ++i)
      {
        plStringBuilder sItem;
        sItem.SetFormat("{} ({})", current.m_Squads[i].m_sId, current.m_Squads[i].m_Members.GetCount());

        plEditorUiMenuItem& item = menu.AddAction(sItem, i);
        item.m_bCheckable = true;
        item.m_bChecked = current.m_Squads[i].m_sId == m_sSquad;
      }

      ShowMenu("Dbg.Squad.Pick", menu, [this](plUInt32 uiId)
        {
        const plAiDebugFrame& after = m_pSession->GetFrame();
        if (uiId < after.m_Squads.GetCount())
        {
          m_sSquad = after.m_Squads[uiId].m_sId;
          m_bSquadDraft = false;
          Invalidate();
        } }); });
  }

  // order
  {
    const plUInt8 uiOrders[] = {plAiSquadIntent::Hold, plAiSquadIntent::Advance, plAiSquadIntent::Engage, plAiSquadIntent::Retreat};
    float fOrders = 4.0f * 3.0f;
    for (const plUInt8 uiOrder : uiOrders)
      fOrders += ChipWidth(SquadIntentName(uiOrder), false);

    layout.Group(TextWidth("Order", FontSize - 2) + 4.0f + fOrders);
    Label(pArea, layout.Take(TextWidth("Order", FontSize - 2) + 4.0f, 20.0f), "Order", Muted(), FontSize - 2);

    for (const plUInt8 uiOrder : uiOrders)
    {
      const plRectFloat chip = layout.Take(ChipWidth(SquadIntentName(uiOrder), false) + 3.0f, 22.0f);
      AddChip(pArea, chip, Name("Dbg.Squad.Order.", uiOrder), SquadIntentName(uiOrder), m_uiSquadIntent == uiOrder, plColorGammaUB(0, 0, 0, 0), "What the squad does while the order lasts.", [this, uiOrder]()
        {
        m_uiSquadIntent = uiOrder;
        Invalidate(); });
    }
  }

  // expiry
  {
    plStringBuilder sSeconds;
    sSeconds.SetFormat("{} s", plArgF(m_fSquadSeconds, 0));

    layout.Group(TextWidth("Expiry", FontSize - 2) + 4.0f + 90.0f + 40.0f);
    Label(pArea, layout.Take(TextWidth("Expiry", FontSize - 2) + 4.0f, 20.0f), "Expiry", Muted(), FontSize - 2);
    AddSlider(pArea, layout.Take(90.0f, 22.0f), "Dbg.Squad.Expiry", 5.0f, 60.0f, m_fSquadSeconds, "How long the order lasts, 5 to 60 seconds.", [this](float fValue)
      {
      m_fSquadSeconds = plMath::Round(fValue);
      Invalidate(); });
    Label(pArea, layout.Take(40.0f, 20.0f), sSeconds, Foreground(), FontSize - 2, true);
  }

  // no retreat
  {
    const float fNoRetreat = ChipWidth("No retreat", false);
    AddChip(pArea, layout.Take(fNoRetreat, 22.0f), "Dbg.Squad.NoRetreat", "No retreat", m_bSquadNoRetreat, plColorGammaUB(0, 0, 0, 0), "Losses don't break the order: the squad stays at its task instead of retreating.", [this]()
      {
      m_bSquadNoRetreat = !m_bSquadNoRetreat;
      Invalidate(); });
  }

  // what the map shows: the squad, or the whole world
  {
    const float fSquadChip = ChipWidth("Squad", false);
    const float fWorldChip = ChipWidth("World", false);
    const float fViewLabel = TextWidth("View", FontSize - 2) + 4.0f;

    layout.Group(fViewLabel + fSquadChip + fWorldChip + 6.0f);
    Label(pArea, layout.Take(fViewLabel, 20.0f), "View", Muted(), FontSize - 2);

    AddChip(pArea, layout.Take(fSquadChip + 3.0f, 22.0f), "Dbg.Squad.ViewSquad", "Squad", !m_bSquadWorld, plColorGammaUB(0, 0, 0, 0), "Fits the map to the squad, its order and its target.", [this]()
      {
      m_bSquadWorld = false;
      Invalidate(); });

    AddChip(pArea, layout.Take(fWorldChip + 3.0f, 22.0f), "Dbg.Squad.ViewWorld", "World", m_bSquadWorld, plColorGammaUB(0, 0, 0, 0), "Fits the map to the whole navmesh and every agent, to see where the squad is in the level.", [this]()
      {
      m_bSquadWorld = true;
      Invalidate(); });
  }

  // issue and cancel
  {
    const float fIssue = TextWidth("Issue") + 42.0f;
    const float fCancel = TextWidth("Cancel") + 42.0f;
    layout.Group(fIssue + fCancel + 6.0f);

    iconButton(layout.Take(fIssue), "Dbg.Squad.Issue", "Issue", Icon::Check, "Gives the squad this order. Draw a region on the map first to have it hold or take a place.", [this]() { IssueSquadDraft(); }, true);
    iconButton(layout.Take(fCancel), "Dbg.Squad.Cancel", "Cancel", Icon::WindowClose, "Ends the squad's order; it goes back to its own intent.", [this, sSquad = pSquad->m_sId]()
      {
      m_pSession->CancelSquadOrder(sSquad);
      Invalidate(); }, pSquad->m_bHasOrder);
  }

  const float fBar = layout.GetBottom() + 6.0f;

  auto* pBarEdge = Panel(pArea);
  Place(pBarEdge, plRectFloat(0.0f, fBar - 1.0f, fWidth, 1.0f));
  pBarEdge->m_Color = Line();

  // ---- the map, left; what the squad decided, right ----
  const float fMapWidth = plMath::Clamp(fWidth * 0.56f, 220.0f, plMath::Max(fWidth - 260.0f, 220.0f));
  const float fMapHeight = plMath::Max(fHeight - s_fTabHeight - fBar, 40.0f);

  m_SquadMapRect = plRectFloat(rect.x, rect.y + s_fTabHeight + fBar, fMapWidth, fMapHeight);

  // the world rectangle that shows: everything the squad has, with room around it
  {
    plVec2 vMin = pSquad->m_vHome.GetAsVec2();
    plVec2 vMax = vMin;

    auto include = [&](const plVec2& v)
    {
      vMin = plVec2(plMath::Min(vMin.x, v.x), plMath::Min(vMin.y, v.y));
      vMax = plVec2(plMath::Max(vMax.x, v.x), plMath::Max(vMax.y, v.y));
    };

    for (const plAiDebugFrame::SquadMember& member : pSquad->m_Members)
      include(member.m_vPosition.GetAsVec2());

    if (pSquad->m_bHasTarget)
      include(pSquad->m_vTarget.GetAsVec2());

    if (pSquad->m_bHasOrder && pSquad->m_bOrderRegion)
    {
      include(pSquad->m_vRegionMin);
      include(pSquad->m_vRegionMax);
    }

    if (m_bSquadDraft)
    {
      include(m_vSquadDraftMin);
      include(m_vSquadDraftMax);
    }

    // the world view: the navmesh's whole grid and every agent
    if (m_bSquadWorld)
    {
      const plAiDebugFrame::Navmesh& nav = frame.m_Navmesh;

      if (nav.m_bPresent && nav.m_fSectorSize > 0.0f)
      {
        const plVec2 vHalf(0.5f * nav.m_uiSectorsX * nav.m_fSectorSize, 0.5f * nav.m_uiSectorsY * nav.m_fSectorSize);
        include(-vHalf);
        include(vHalf);
      }

      for (const plAiDebugFrame::Agent& agent : frame.m_Agents)
        include(agent.m_vPosition.GetAsVec2());
    }

    // a stable frame: at least 60 m, centered on what there is
    const plVec2 vCenter = (vMin + vMax) * 0.5f;
    const float fExtentX = plMath::Max(vMax.x - vMin.x + 24.0f, 60.0f);
    const float fExtentY = plMath::Max(vMax.y - vMin.y + 24.0f, 60.0f * fMapHeight / plMath::Max(fMapWidth, 1.0f));

    m_fSquadMapScale = plMath::Min(fMapWidth / fExtentX, fMapHeight / fExtentY);
    m_vSquadMapMin = plVec2(vCenter.x - fMapWidth / m_fSquadMapScale * 0.5f, vCenter.y - fMapHeight / m_fSquadMapScale * 0.5f);
  }

  m_sSquadMapEntry = "Dbg.Squad.Map";
  AddDrawList(pArea, plRectFloat(0.0f, fBar, fMapWidth, fMapHeight), m_sSquadMapEntry);
  PublishSquadMap(pSquad);

  auto* pDivider = Panel(pArea);
  Place(pDivider, plRectFloat(fMapWidth, fBar, 1.0f, fMapHeight));
  pDivider->m_Color = Line();

  plUiScrollView* pBody = AddScrollBody(pArea, plRectFloat(fMapWidth + 1.0f, fBar, fWidth - fMapWidth - 1.0f, fMapHeight), "Dbg.SquadBody", 0.0f, 0.0f);
  BuildSquadInfo(pBody, fWidth - fMapWidth - 1.0f - ScrollbarGutter, *pSquad);

  PL_IGNORE_UNUSED(frame);
}

void plAiDebuggerView::BuildSquadInfo(plUiElement* pBody, float fWidth, const plAiDebugFrame::Squad& squad)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  // intent: the order's, or the squad's own
  {
    auto* pHeader = Section(pBody, "Intent", AiColor(), {}, {}, 130.0f);

    plStringBuilder sIntent;
    sIntent.SetFormat("{} \xC2\xB7 {}", squad.m_bHasOrder ? "Order" : "Automatic", SquadIntentName(squad.m_bHasOrder ? squad.m_uiOrderIntent : squad.m_uiIntent));

    const float fBadge = BadgeWidth(sIntent);
    Badge(pHeader, fWidth - fBadge - 12.0f, (SectionHeight - BadgeHeight) * 0.5f, sIntent, squad.m_bHasOrder ? Accent() : Dim());
  }

  plStringBuilder sSource, sExpires, sRetreat;

  if (squad.m_bHasOrder)
    sSource = squad.m_sOrderIssuer.IsEmpty() ? plString("code") : squad.m_sOrderIssuer;
  else
    sSource = "The squad's own reasoning";

  if (squad.m_bHasOrder && squad.m_fOrderDuration > 0.0)
    sExpires.SetFormat("{} s", plArgF(squad.m_fOrderRemaining, 1));
  else
    sExpires = squad.m_bHasOrder ? "until cancelled" : "-";

  if (squad.m_bHasOrder && squad.m_bOrderNoRetreat)
    sRetreat = "Blocked by the order";
  else
    sRetreat.SetFormat("Allowed \xC2\xB7 {} / {} losses", squad.m_uiLosses, squad.m_uiRetreatLosses);

  const char* szRows[][2] = {{"Source", sSource.GetData()}, {"Expires", sExpires.GetData()}, {"Retreat", sRetreat.GetData()}};

  for (const auto& row : szRows)
  {
    auto* pRow = Panel(pBody);
    pRow->m_vOffsets.w = 22.0f;
    pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

    Label(pRow, plRectFloat(12.0f, 0.0f, 90.0f, 22.0f), row[0], Muted(), FontSize - 2);
    Label(pRow, plRectFloat(102.0f, 0.0f, fWidth - 114.0f, 22.0f), row[1], Foreground(), FontSize - 2, true);
  }

  // members
  {
    plStringBuilder sCount;
    sCount.SetFormat("{} alive \xC2\xB7 peak {}", squad.m_Members.GetCount(), squad.m_uiPeak);
    Section(pBody, "Members", AiColor(), sCount);

    const float fRole = 112.0f;
    const float fToken = 66.0f;
    const float xRole = fWidth - fRole - fToken - 12.0f;
    const float xToken = fWidth - fToken - 12.0f;

    auto* pHeader = Panel(pBody);
    pHeader->m_vOffsets.w = 20.0f;
    pHeader->m_Color = plColorGammaUB(0, 0, 0, 0);
    Label(pHeader, plRectFloat(12.0f, 2.0f, xRole - 12.0f, 16.0f), "MEMBER", Dim(), MinFontSize);
    Label(pHeader, plRectFloat(xRole, 2.0f, fRole, 16.0f), "ROLE", Dim(), MinFontSize);
    Label(pHeader, plRectFloat(xToken, 2.0f, fToken, 16.0f), "TOKEN", Dim(), MinFontSize);

    const plAiSquadOrder slots = MakeSlotOrder(squad);

    for (plUInt32 i = 0; i < squad.m_Members.GetCount() && i < 8; ++i)
    {
      const plAiDebugFrame::SquadMember& member = squad.m_Members[i];

      auto* pRow = Panel(pBody);
      pRow->m_vOffsets.w = 22.0f;
      pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

      plStringBuilder sName(member.m_sName);
      if (sName.IsEmpty())
        sName.SetFormat("#{}", i + 1);

      const plUInt8 uiIntent = squad.m_bHasOrder ? squad.m_uiOrderIntent : squad.m_uiIntent;
      Label(pRow, plRectFloat(12.0f, 0.0f, xRole - 16.0f, 22.0f), sName, Foreground(), FontSize - 2);
      Label(pRow, plRectFloat(xRole, 0.0f, fRole, 22.0f), SquadRole(uiIntent, member.m_bMoveToken), Muted(), FontSize - 2);

      if (member.m_bAttackToken)
        Badge(pRow, xToken, 3.0f, "attack", Accent());
      else if (member.m_bMoveToken)
        Badge(pRow, xToken, 3.0f, "move", Accent());
    }
  }

  // history
  {
    Section(pBody, "History", AiColor());

    if (squad.m_History.IsEmpty())
    {
      auto* pEmpty = Panel(pBody);
      pEmpty->m_vOffsets.w = 24.0f;
      pEmpty->m_Color = plColorGammaUB(0, 0, 0, 0);
      Label(pEmpty, plRectFloat(12.0f, 0.0f, fWidth - 24.0f, 24.0f), "No intent changes yet.", Dim(), FontSize - 2);
    }

    for (plUInt32 i = 0; i < squad.m_History.GetCount() && i < 8; ++i)
    {
      const plAiDebugFrame::SquadHistory& entry = squad.m_History[i];

      auto* pRow = Panel(pBody);
      pRow->m_vOffsets.w = 22.0f;
      pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

      plStringBuilder sTime;
      sTime.SetFormat("{} s", plArgF(entry.m_fTime, 1));
      Label(pRow, plRectFloat(12.0f, 0.0f, 56.0f, 22.0f), sTime, Dim(), FontSize - 2, true);
      Label(pRow, plRectFloat(72.0f, 0.0f, fWidth - 84.0f, 22.0f), entry.m_sText, Foreground(), FontSize - 2);
    }
  }
}

void plAiDebuggerView::BuildEqs(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  Section(pBody, "Last query", AiColor(), {}, "captured from the running agent");

  if (watched.m_sEqsQuery.IsEmpty())
  {
    BuildEmpty(pBody, fWidth, "No query captured", "Capture the agent's next EQS query to see what it found. Captures keep every candidate and test measurement.");
  }
  else
  {
    plStringBuilder sCandidates, sBest, sTime;
    sCandidates.SetFormat("{} generated \xC2\xB7 {} survived", watched.m_uiEqsCandidates, watched.m_uiEqsSurvivors);
    sBest.SetFormat("{}", plArgF(watched.m_fEqsBestScore, 3));
    sTime.SetFormat("{} s ({} s ago)", plArgF(watched.m_fEqsCaptureTime, 1), plArgF(plMath::Max(0.0, GetHeadTime() - watched.m_fEqsCaptureTime), 1));

    const char* szRows[][2] = {
      {"Query", watched.m_sEqsQuery.GetData()},
      {"Status", EqsStatusName(watched.m_uiEqsStatus)},
      {"Candidates", sCandidates.GetData()},
      {"Best score", sBest.GetData()},
      {"Captured at", sTime.GetData()},
    };

    for (const auto& row : szRows)
    {
      auto* pRow = Panel(pBody);
      pRow->m_vOffsets.w = 22.0f;
      pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

      Label(pRow, plRectFloat(12.0f, 0.0f, 200.0f, 22.0f), row[0], Muted(), FontSize - 2);
      Label(pRow, plRectFloat(212.0f, 0.0f, fWidth - 224.0f, 22.0f), row[1], Foreground(), FontSize - 2, true);
    }
  }

  auto* pActions = Panel(pBody);
  pActions->m_vOffsets.w = 44.0f;
  pActions->m_Color = plColorGammaUB(0, 0, 0, 0);

  const char* szCapture = "Capture next query";
  const float fButton = TextWidth(szCapture) + 24.0f;
  AddTextButton(pActions, plRectFloat(12.0f, 10.0f, fButton, ControlHeight), "Dbg.Capture", szCapture, "Records the next EQS query this agent runs.", [this]()
    {
    m_pSession->CaptureNextEqsQuery();
    Invalidate(); }, false, !m_bFrozen);

  if (watched.m_bEqsCaptureArmed)
  {
    Label(pActions, plRectFloat(24.0f + fButton, 10.0f, fWidth - fButton - 36.0f, ControlHeight), "Armed: waiting for the agent's next query.", Warning(), FontSize - 2);
  }
}

void plAiDebuggerView::BuildTimeline(plUiElement* pRoot, const plRectFloat& rect)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  auto* pPane = Panel(pRoot);
  Place(pPane, rect);
  pPane->m_Color = Plate();

  auto* pEdge = Panel(pPane);
  Place(pEdge, plRectFloat(0.0f, 0.0f, rect.width, 1.0f));
  pEdge->m_Color = Line();

  const double fLive = m_pSession->GetFrame().m_fWorldTime;
  const double fEnd = m_bFrozen ? m_fViewEnd : fLive;
  const double fStart = fEnd - s_fTimelineSpan;

  plHybridArray<const plAiDebugFrame::Agent*, 32> agents;
  GetShownAgents(agents);

  // the watched agent first, then the list order
  for (plUInt32 i = 1; i < agents.GetCount(); ++i)
  {
    if (agents[i]->m_Object == m_pSession->GetWatched())
    {
      const plAiDebugFrame::Agent* pWatched = agents[i];
      agents.RemoveAtAndCopy(i);
      agents.InsertAt(0, pWatched);
      break;
    }
  }

  const plUInt32 uiRows = static_cast<plUInt32>((rect.height - s_fTimelineBar - 10.0f) / s_fTimelineRow + 0.5f);
  if (agents.GetCount() > uiRows)
    agents.SetCount(uiRows);

  // the bar: title, what the window shows, the hint
  plStringBuilder sInfo;
  sInfo.SetFormat("{} \xC2\xB7 window {}-{} s \xC2\xB7 {} agents \xC2\xB7 {} KB", m_bFrozen ? "frozen" : "live", plArgF(plMath::Max(0.0, fStart), 0), plArgF(fEnd, 0), m_pSession->GetRecordedAgents().GetCount(), (m_pSession->GetRecordingBytes() + 1023) / 1024);

  const float fTitle = TextWidth("RECORDING", MinFontSize + 1);
  Label(pPane, plRectFloat(12.0f, 0.0f, fTitle, s_fTimelineBar), "RECORDING", Muted(), MinFontSize + 1);
  Label(pPane, plRectFloat(20.0f + fTitle, 0.0f, rect.width * 0.5f, s_fTimelineBar), sInfo, Dim(), FontSize - 2);

  const char* szHint = "drag to scrub \xC2\xB7 click a block to see why it started";
  const float fHint = TextWidth(szHint, FontSize - 2);
  Label(pPane, plRectFloat(rect.width - fHint - 12.0f, 0.0f, fHint, s_fTimelineBar), szHint, Dim(), FontSize - 2);

  // the tracks
  const plRectFloat tracks(8.0f, s_fTimelineBar, rect.width - 16.0f, plMath::Max(1u, agents.GetCount()) * s_fTimelineRow + 4.0f);
  m_TimelineTracks = plRectFloat(rect.x + tracks.x + s_fTimelineLabel, rect.y + tracks.y, tracks.width - s_fTimelineLabel, tracks.height);
  m_fTimelineStart = fStart;
  m_fTimelineSpan = s_fTimelineSpan;
  m_TimelineRows.Clear();

  AddDrawList(pPane, tracks, "Dbg.Timeline");

  plUiDrawListWriter writer;
  const float fTrackWidth = tracks.width - s_fTimelineLabel;
  auto toX = [&](double fTime)
  { return s_fTimelineLabel + static_cast<float>((fTime - fStart) / s_fTimelineSpan) * fTrackWidth; };

  writer.Rect(plRectFloat(s_fTimelineLabel, 0.0f, fTrackWidth, tracks.height), Field(), 3.0f);

  // a faint line every 5 s
  for (double t = plMath::Ceil(fStart / 5.0) * 5.0; t < fEnd; t += 5.0)
  {
    const float x = toX(t);
    writer.Line(plVec2(x, 0.0f), plVec2(x, tracks.height), GridColor());
  }

  plHybridArray<Segment, 32> segments;

  for (plUInt32 i = 0; i < agents.GetCount(); ++i)
  {
    const plAiDebugFrame::Agent& agent = *agents[i];
    const float y = 2.0f + i * s_fTimelineRow;
    const bool bWatched = agent.m_Object == m_pSession->GetWatched();

    m_TimelineRows.PushBack(agent.m_Object);

    if (bWatched)
      writer.Rect(plRectFloat(0.0f, y, tracks.width, s_fTimelineRow), Selection(), 2.0f);

    writer.Text(plRectFloat(4.0f, y, s_fTimelineLabel - 10.0f, s_fTimelineRow), agent.m_sName, bWatched ? Foreground() : Muted(), MinFontSize);

    const plAiDebugSession::RecordedAgent* pRecorded = m_pSession->FindRecordedAgent(agent.m_Object);
    if (pRecorded == nullptr)
      continue;

    GetSegments(*pRecorded, fStart, fEnd, segments);

    for (const Segment& segment : segments)
    {
      const float x0 = toX(segment.m_fStart);
      const float x1 = plMath::Max(toX(segment.m_fEnd) - 1.0f, x0 + 1.0f);
      const plRectFloat block(x0, y + 2.0f, x1 - x0, s_fTimelineRow - 4.0f);
      writer.Rect(block, CategoryColorOrDim(segment.m_uiCategory), 2.0f);

      if (segment.m_iEntry >= 0 && agent.m_Object == m_WhyAgent && pRecorded->m_History[segment.m_iEntry].m_fTime == m_fWhyTime)
        writer.RectOutline(block, Foreground(), 1.5f, 2.0f);

      if (x1 - x0 > 50.0f)
        writer.Text(plRectFloat(x0 + 4.0f, y + 2.0f, x1 - x0 - 6.0f, s_fTimelineRow - 4.0f), segment.m_sBehavior, plColorGammaUB(20, 20, 24), MinFontSize);
    }
  }

  // the head
  const float xHead = toX(m_bFrozen ? m_fHead : fEnd);
  writer.Line(plVec2(xHead, 0.0f), plVec2(xHead, tracks.height), Accent(), 2.0f);

  Publish(GetBoard(), writer, "Dbg.Timeline");
}

void plAiDebuggerView::BuildWhy(plUiElement* pRoot, const plRectFloat& timeline)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  if (m_fWhyTime < 0.0)
    return;

  const plAiDebugSession::RecordedAgent* pAgent = m_pSession->FindRecordedAgent(m_WhyAgent);
  if (pAgent == nullptr)
    return;

  // by time: the recording drops old switches, which moves the indices
  const plAiDebugFrame::HistoryEntry* pEntry = nullptr;
  for (const plAiDebugFrame::HistoryEntry& candidate : pAgent->m_History)
  {
    if (candidate.m_fTime == m_fWhyTime)
    {
      pEntry = &candidate;
      break;
    }
  }

  if (pEntry == nullptr)
    return;

  const plAiDebugFrame::HistoryEntry& entry = *pEntry;
  const plAiDecisionOutcome outcome = static_cast<plAiDecisionOutcome>(entry.m_uiOutcome);

  plStringBuilder sHeader, sSwitch, sReason;
  sHeader.SetFormat("{} at {} s", pAgent->m_sName, plArgF(entry.m_fTime, 1));
  sSwitch.SetFormat("{} \xE2\x86\x92 {} ({})", entry.m_sFrom.IsEmpty() ? "idle" : entry.m_sFrom.GetView(), entry.m_sTo.IsEmpty() ? "idle" : entry.m_sTo.GetView(), entry.m_sTo.IsEmpty() ? "no successor" : plAiAssetUi::CategoryName(entry.m_uiToCategory));

  switch (outcome)
  {
    case plAiDecisionOutcome::Started:
      sReason = "Nothing was running; it scored highest.";
      break;
    case plAiDecisionOutcome::HigherCategory:
      sReason.SetFormat("Won on category: {} outranks the running behavior's category.", plAiAssetUi::CategoryName(entry.m_uiToCategory));
      break;
    case plAiDecisionOutcome::BeatCommitBonus:
      sReason.SetFormat("Won on utility: {} beat {} plus the commit bonus.", plArgF(UtilityOf(entry.m_fWinnerScore), 2), plArgF(UtilityOf(entry.m_fRunnerUpScore), 2));
      break;
    case plAiDecisionOutcome::StoppedScoring:
      sReason = "The running behavior stopped scoring, so the best challenger took over.";
      break;
    case plAiDecisionOutcome::NothingScores:
      sReason = "No behavior scored; the agent stopped.";
      break;
    case plAiDecisionOutcome::Retargeted:
      sReason = "Same behavior, new target.";
      break;
    case plAiDecisionOutcome::Done:
      sReason = "The behavior finished.";
      break;
    case plAiDecisionOutcome::Failed:
      sReason = "The behavior failed.";
      break;
    case plAiDecisionOutcome::Disabled:
      sReason = "The behavior was disabled for this agent.";
      break;
    default:
      sReason = plAiDecisionOutcomeName(outcome);
      break;
  }

  const float fWidth = 340.0f;
  const float fHeight = 92.0f;
  const float x = plMath::Clamp(m_fWhyX - fWidth * 0.5f, timeline.x + 8.0f, timeline.x + timeline.width - fWidth - 8.0f);
  const float y = timeline.y - fHeight - 6.0f;

  auto* pPopover = Panel(pRoot);
  Place(pPopover, plRectFloat(x, y, fWidth, fHeight));
  pPopover->m_Color = Raised();
  pPopover->m_vCornerRadius = plVec4(5.0f);
  pPopover->m_fBorderWidth = 1.0f;
  pPopover->m_BorderColor = Border();

  Label(pPopover, plRectFloat(12.0f, 8.0f, fWidth - 44.0f, 18.0f), sHeader, Foreground(), FontSize - 1);
  Label(pPopover, plRectFloat(12.0f, 28.0f, fWidth - 24.0f, 18.0f), sSwitch, CategoryColorOrDim(entry.m_uiToCategory), FontSize - 2);

  auto* pReason = Label(pPopover, plRectFloat(12.0f, 48.0f, fWidth - 24.0f, 36.0f), sReason, Muted(), FontSize - 2);
  pReason->m_bWrap = true;
  pReason->m_VAlign = plUiTextVAlign::Top;

  AddIconButton(pPopover, plRectFloat(fWidth - SmallButtonSize - 6.0f, 6.0f, SmallButtonSize, SmallButtonSize), "Dbg.WhyClose", Icon::WindowClose, "Close", [this]()
    {
    m_fWhyTime = -1.0;
    Invalidate(); });
}

void plAiDebuggerView::BuildStatus(plUiElement* pRoot, const plRectFloat& rect)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  auto* pBar = Panel(pRoot);
  Place(pBar, rect);
  pBar->m_Color = Plate();

  auto* pEdge = Panel(pBar);
  Place(pEdge, plRectFloat(0.0f, 0.0f, rect.width, 1.0f));
  pEdge->m_Color = Line();

  const char* szRight = "Recording keeps the last 120 s";
  const float fRight = TextWidth(szRight, FontSize - 2);
  TintedLabel(pBar, plRectFloat(12.0f, 0.0f, rect.width - fRight - 36.0f, rect.height), "Dbg.Status", "Dbg.StatusTint", FontSize - 2);
  Label(pBar, plRectFloat(rect.width - fRight - 12.0f, 0.0f, fRight, rect.height), szRight, Dim(), FontSize - 2);

  UpdateStatus();
}

void plAiDebuggerView::UpdateStatus()
{
  if (!IsReady())
    return;

  using namespace plEditorUi;

  const plAiDebugFrame::Stats& stats = m_pSession->GetFrame().m_Stats;

  plStringBuilder sStatus;
  if (!m_pSession->HasFrame())
  {
    sStatus = "No data from the engine yet";
  }
  else
  {
    sStatus.SetFormat("Agents {}   \xC2\xB7   Decisions/frame {} / {} ({} woken)   \xC2\xB7   EQS {} run, {} queued, {} raycasts   \xC2\xB7   Crowd {}   \xC2\xB7   Brain {} ms", stats.m_uiAgents, stats.m_uiDecisions, stats.m_uiDecisionBudget, stats.m_uiWokenDecisions, stats.m_uiEqsExecuted,
      stats.m_uiEqsQueued, stats.m_uiRaycasts, stats.m_uiCrowdAgents, plArgF(stats.m_fBrainMs, 2));
  }

  GetBoard().SetEntryValue("Dbg.Status", plString(sStatus));
  GetBoard().SetEntryValue("Dbg.StatusTint", plColor(Muted()));
  RequestRedraw();
}

bool plAiDebuggerView::PickTimeline(const plVec2& vPos, plUInt32& out_uiRow, double& out_fTime) const
{
  if (!m_TimelineTracks.Contains(vPos) || m_TimelineTracks.width <= 0.0f)
    return false;

  out_fTime = m_fTimelineStart + (vPos.x - m_TimelineTracks.x) / m_TimelineTracks.width * m_fTimelineSpan;
  out_uiRow = static_cast<plUInt32>((vPos.y - m_TimelineTracks.y - 2.0f) / s_fTimelineRow);
  return true;
}

void plAiDebuggerView::ScrubTo(float x)
{
  const float fClamped = plMath::Clamp(x, m_TimelineTracks.x, m_TimelineTracks.x + m_TimelineTracks.width);
  m_fHead = m_fTimelineStart + (fClamped - m_TimelineTracks.x) / m_TimelineTracks.width * m_fTimelineSpan;
  m_fHead = plMath::Min(m_fHead, m_fViewEnd);
  Invalidate();
}

void plAiDebuggerView::OnViewPointer(const plEditorUiPointerEvent& e)
{
  using Type = plEditorUiPointerEvent::Type;

  // the Squad tab's map: dragging draws the region of the next order
  if (m_Tab == Tab::Squad)
  {
    if (e.m_Type == Type::Press && e.m_Button == plEditorUiPointerEvent::Button::Left && m_SquadMapRect.width > 0.0f && m_SquadMapRect.Contains(e.m_vPosition))
    {
      const plAiDebugFrame::Squad* pSquad = FindSquad();

      if (pSquad != nullptr && !pSquad->m_bHasOrder)
      {
        m_bSquadDragging = true;
        m_vSquadDragStart = SquadMapToWorld(e.m_vPosition);
        m_vSquadDraftMin = m_vSquadDragStart;
        m_vSquadDraftMax = m_vSquadDragStart + plVec2(2.0f);
        m_bSquadDraft = true;
        PublishSquadMap(pSquad);
        return;
      }
    }

    if (m_bSquadDragging)
    {
      if (e.m_Type == Type::DragStart || e.m_Type == Type::DragMove || e.m_Type == Type::Move)
      {
        const plVec2 vNow = SquadMapToWorld(e.m_vPosition);
        m_vSquadDraftMin = plVec2(plMath::Min(vNow.x, m_vSquadDragStart.x), plMath::Min(vNow.y, m_vSquadDragStart.y));
        m_vSquadDraftMax = plVec2(plMath::Max(vNow.x, m_vSquadDragStart.x), plMath::Max(vNow.y, m_vSquadDragStart.y));
        m_vSquadDraftMax = plVec2(plMath::Max(m_vSquadDraftMax.x, m_vSquadDraftMin.x + 2.0f), plMath::Max(m_vSquadDraftMax.y, m_vSquadDraftMin.y + 2.0f));

        // straight to the map: the document rebuilds only after the button is released
        PublishSquadMap(FindSquad());
        return;
      }

      if (e.m_Type == Type::Release || e.m_Type == Type::Drop || e.m_Type == Type::DragCancel)
      {
        m_bSquadDragging = false;
        Invalidate();
        return;
      }
    }
  }

  if (e.m_Type == Type::Press && e.m_Button == plEditorUiPointerEvent::Button::Left)
  {
    plUInt32 uiRow = 0;
    double fTime = 0.0;
    if (!PickTimeline(e.m_vPosition, uiRow, fTime))
      return;

    // a press scrubs; releasing it without dragging explains the block under it
    SetFrozen(true);
    m_bScrubbing = true;
    m_bScrubDragged = false;
    m_vScrubPress = e.m_vPosition;
    m_fWhyTime = -1.0;

    ScrubTo(e.m_vPosition.x);
    return;
  }

  if (!m_bScrubbing)
    return;

  if (e.m_Type == Type::DragStart || e.m_Type == Type::DragMove || e.m_Type == Type::Move)
  {
    if ((e.m_vPosition - m_vScrubPress).GetLength() > 4.0f)
      m_bScrubDragged = true;

    if (m_bScrubDragged)
      ScrubTo(e.m_vPosition.x);

    return;
  }

  if (e.m_Type == Type::Release || e.m_Type == Type::Drop)
  {
    m_bScrubbing = false;

    if (!m_bScrubDragged)
      OpenWhy(m_vScrubPress);

    return;
  }

  if (e.m_Type == Type::DragCancel)
  {
    m_bScrubbing = false;
  }
}

void plAiDebuggerView::OpenWhy(const plVec2& vPos)
{
  plUInt32 uiRow = 0;
  double fTime = 0.0;
  if (!PickTimeline(vPos, uiRow, fTime) || uiRow >= m_TimelineRows.GetCount())
    return;

  const plUuid object = m_TimelineRows[uiRow];
  const plAiDebugSession::RecordedAgent* pAgent = m_pSession->FindRecordedAgent(object);
  if (pAgent == nullptr)
    return;

  plHybridArray<Segment, 32> segments;
  GetSegments(*pAgent, m_fTimelineStart, m_fTimelineStart + m_fTimelineSpan, segments);

  // only a block that started with a recorded switch has a why
  for (const Segment& segment : segments)
  {
    if (fTime < segment.m_fStart || fTime >= segment.m_fEnd || segment.m_iEntry < 0)
      continue;

    m_WhyAgent = object;
    m_fWhyTime = pAgent->m_History[segment.m_iEntry].m_fTime;
    m_fWhyX = vPos.x;

    WatchAgent(object, true);

    // show the decision that made the switch: the first frame after it, while the block still runs
    const plAiDebugSession::RecordedWatch* pAfter = m_pSession->FindRecordedWatchFrom(segment.m_fStart);
    const double fHead = (pAfter != nullptr && pAfter->m_fTime < segment.m_fEnd) ? pAfter->m_fTime : segment.m_fStart + 0.01;
    m_fHead = plMath::Min(fHead, m_fViewEnd);
    Invalidate();
    return;
  }
}
