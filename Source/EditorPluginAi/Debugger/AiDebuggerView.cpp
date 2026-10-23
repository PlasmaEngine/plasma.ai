#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEditorHelpers.h>
#include <EditorPluginAi/Debugger/AiDebuggerView.h>

#include <AiPlugin/Eqs/EqsTypes.h>
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

  const char* szTabs[] = {"Decision", sPerception.GetData(), "Blackboard", "Navigation", "EQS"};

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

  plUiScrollView* pBody = AddScrollBody(pPane, plRectFloat(0.0f, s_fTabHeight, rect.width, rect.height - s_fTabHeight), "Dbg.Body", 0.0f, 0.0f);
  const float fWidth = rect.width - ScrollbarGutter;

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
      BuildNavigation(pBody, fWidth, *pWatched);
      break;
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

void plAiDebuggerView::BuildNavigation(plUiElement* pBody, float fWidth, const plAiDebugFrame::Watched& watched)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  Section(pBody, "Navigation", AiColor(), {}, "the agent's navigation component, as the engine reports it");

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
