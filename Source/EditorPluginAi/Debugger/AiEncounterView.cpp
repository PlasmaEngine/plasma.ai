#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Debugger/AiEncounterView.h>

#include <AiPlugin/Encounter/EncounterTypes.h>
#include <Core/Utils/Blackboard.h>
#include <EditorEngineProcessFramework/EngineProcess/EngineProcessMessages.h>
#include <EditorFramework/Document/GameObjectDocument.h>
#include <EditorUi/Surface.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

namespace
{
  constexpr float s_fToolbarHeight = 32.0f;
  constexpr float s_fStatusHeight = 22.0f;
  constexpr float s_fChartHeight = 170.0f;
  constexpr float s_fWaveRow = 24.0f;
  constexpr float s_fHoldHeight = 40.0f;
  constexpr float s_fPropRow = 20.0f;
  constexpr double s_fSimulatingPollSeconds = 0.2;
  constexpr double s_fIdlePollSeconds = 1.0;
  constexpr double s_fAnswerTimeoutSeconds = 2.0;
  constexpr plUInt32 s_uiMaxSamples = 6000;

  const char* s_szPartNames[] = {"Agents engaging a player", "Player gunshots and hits", "Damage to the player"};

  plString Name(plStringView sPrefix, plUInt32 uiIndex)
  {
    plStringBuilder sName;
    sName.SetFormat("{}{}", sPrefix, uiIndex);
    return sName;
  }

  void Publish(plBlackboard& ref_board, const plUiDrawListWriter& writer, plStringView sEntry)
  {
    plHashedString sHashed;
    sHashed.Assign(sEntry);
    writer.Publish(ref_board, sHashed);
  }

  plString FormatClock(double fSeconds)
  {
    const plUInt32 uiSeconds = static_cast<plUInt32>(plMath::Max(0.0, fSeconds));

    plStringBuilder sClock;
    sClock.SetFormat("{}:{}", uiSeconds / 60, plArgU(uiSeconds % 60, 2, true));
    return sClock;
  }

  const char* StateName(plUInt8 uiState)
  {
    switch (uiState)
    {
      case plAiEncounterWaveState::Held:
        return "held";
      case plAiEncounterWaveState::Released:
        return "released";
      case plAiEncounterWaveState::Cleared:
        return "cleared";
      default:
        return "waiting";
    }
  }

  plColorGammaUB StateColor(plUInt8 uiState)
  {
    switch (uiState)
    {
      case plAiEncounterWaveState::Held:
        return plEditorUi::Warning();
      case plAiEncounterWaveState::Released:
        return plEditorUi::Accent();
      case plAiEncounterWaveState::Cleared:
        return plAiEngineUi::GoodColor();
      default:
        return plEditorUi::Dim();
    }
  }

  /// The frame's phases as the runtime's type, to evaluate the curve exactly as the director does.
  void GetPhases(const plAiEncounterDebugFrame& frame, plDynamicArray<plAiEncounterPhase>& out_phases)
  {
    out_phases.Clear();
    for (const plAiEncounterDebugFrame::Phase& phase : frame.m_Phases)
    {
      plAiEncounterPhase& entry = out_phases.ExpandAndGetRef();
      entry.m_sName = phase.m_sName;
      entry.m_fDuration = phase.m_fDuration;
      entry.m_fTarget = phase.m_fTarget;
    }
  }

  float GetPacingLength(const plAiEncounterDebugFrame& frame)
  {
    float fLength = 0.0f;
    for (const plAiEncounterDebugFrame::Phase& phase : frame.m_Phases)
      fLength += plMath::Max(0.0f, phase.m_fDuration);

    return fLength;
  }

  void DrawBar(plUiDrawListWriter& ref_writer, const plRectFloat& rect, float fValue, const plColorGammaUB& fill)
  {
    using namespace plEditorUi;

    ref_writer.Rect(rect, Field(), 3.0f);
    ref_writer.RectOutline(rect, Line(), 1.0f, 3.0f);

    const float fFill = plMath::Saturate(fValue);
    if (fFill > 0.0f)
      ref_writer.Rect(plRectFloat(rect.x + 1.0f, rect.y + 1.0f, (rect.width - 2.0f) * fFill, rect.height - 2.0f), fill, 2.0f);
  }
} // namespace

plAiEncounterView::plAiEncounterView(plDocument* pDocument)
  : plAiEngineView("AiEncounter")
  , m_pDocument(plDynamicCast<plGameObjectDocument*>(pDocument))
{
  plEditorUiServices::s_TickEvent.AddEventHandler(plMakeDelegate(&plAiEncounterView::OnTick, this));
  m_pDocument->m_ProcessMessageEvent.AddEventHandler(plMakeDelegate(&plAiEncounterView::OnProcessMessage, this));
  m_pDocument->GetSelectionManager()->m_Events.AddEventHandler(plMakeDelegate(&plAiEncounterView::OnSelection, this));
}

plAiEncounterView::~plAiEncounterView()
{
  m_pDocument->GetSelectionManager()->m_Events.RemoveEventHandler(plMakeDelegate(&plAiEncounterView::OnSelection, this));
  m_pDocument->m_ProcessMessageEvent.RemoveEventHandler(plMakeDelegate(&plAiEncounterView::OnProcessMessage, this));
  plEditorUiServices::s_TickEvent.RemoveEventHandler(plMakeDelegate(&plAiEncounterView::OnTick, this));
}

void plAiEncounterView::OnTick(const plEditorUiServices::TickEvent& e)
{
  if (e.m_Type != plEditorUiServices::TickEvent::Type::BeforeFrame)
    return;

  const plTime now = plTime::Now();

  // an answer that never comes (the engine process restarted) must not stop the polling
  if (m_bWaiting && (now - m_LastPoll).GetSeconds() > s_fAnswerTimeoutSeconds)
    m_bWaiting = false;

  const double fInterval = (m_bHasFrame && m_Frame.m_bSimulating) ? s_fSimulatingPollSeconds : s_fIdlePollSeconds;

  if (!m_bWaiting && (now - m_LastPoll).GetSeconds() >= fInterval)
    SendPoll();
}

void plAiEncounterView::SendPoll()
{
  if (++m_uiRequestId == 0)
    ++m_uiRequestId;

  plAiEncounterDebugRequest request;
  request.m_uiRequestId = m_uiRequestId;
  request.m_Director = m_Director;
  request.m_bRestart = m_bRestart;

  plSimpleDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = plAiEncounterDebugRequest::s_szWhatToDo;

  plDataBuffer buffer;
  request.ToBuffer(buffer);
  msg.m_PayloadValue = buffer;

  m_LastPoll = plTime::Now();

  if (m_pDocument->SendMessageToEngine(&msg))
  {
    m_bWaiting = true;
    m_bRestart = false;
  }
}

void plAiEncounterView::OnProcessMessage(const plEditorEngineDocumentMsg* pMsg)
{
  const auto* pConfig = plDynamicCast<const plSimpleDocumentConfigMsgToEditor*>(pMsg);
  if (pConfig == nullptr || pConfig->m_sWhatToDo != plAiEncounterDebugFrame::s_szWhatToDo || !pConfig->m_PayloadValue.IsA<plDataBuffer>())
    return;

  plAiEncounterDebugFrame frame;
  if (frame.FromBuffer(pConfig->m_PayloadValue.Get<plDataBuffer>()).Failed() || frame.m_uiRequestId != m_uiRequestId)
    return;

  m_bWaiting = false;

  Record(frame);

  m_Frame = std::move(frame);
  m_bHasFrame = true;
  Invalidate();
}

void plAiEncounterView::Record(const plAiEncounterDebugFrame& frame)
{
  const bool bRunning = frame.m_bSimulating && frame.m_bHasDirector && frame.m_uiState != 0;

  // another director, a restart, or no encounter running: the measured curve starts over
  const bool bOtherDirector = frame.m_Director != m_Frame.m_Director;
  if (!bRunning || bOtherDirector || frame.m_fEncounterTime + 0.05 < m_fLastEncounterTime)
    m_Samples.Clear();

  m_fLastEncounterTime = bRunning ? frame.m_fEncounterTime : -1.0;

  if (!bRunning || m_Samples.GetCount() >= s_uiMaxSamples)
    return;

  Sample& sample = m_Samples.ExpandAndGetRef();
  sample.m_fPosition = frame.m_fPacingPosition;
  sample.m_fIntensity = frame.m_fIntensity;
}

void plAiEncounterView::OnSelection(const plSelectionManagerEvent& e)
{
  const plDocumentObject* pSelected = m_pDocument->GetSelectionManager()->GetCurrentObject();
  if (pSelected == nullptr)
    return;

  for (const plAiEncounterDebugFrame::Director& director : m_Frame.m_Directors)
  {
    if (director.m_Object == pSelected->GetGuid() && director.m_Object != m_Director)
    {
      m_Director = director.m_Object;
      m_LastPoll = plTime::MakeZero();
    }
  }

  for (const plAiEncounterDebugFrame::Wave& wave : m_Frame.m_Waves)
  {
    if (wave.m_Object == pSelected->GetGuid())
    {
      m_SelectedWave = wave.m_Object;
      Invalidate();
    }
  }
}

void plAiEncounterView::SelectWave(plUInt32 uiWave, bool bSelectInScene)
{
  if (uiWave >= m_Frame.m_Waves.GetCount())
    return;

  m_SelectedWave = m_Frame.m_Waves[uiWave].m_Object;

  if (bSelectInScene)
  {
    if (const plDocumentObject* pObject = m_pDocument->GetObjectManager()->GetObject(m_SelectedWave))
      m_pDocument->GetSelectionManager()->SetSelection(pObject);
  }

  Invalidate();
}

float plAiEncounterView::GetChartSpan() const
{
  const float fLength = GetPacingLength(m_Frame);
  const float fRan = m_Samples.IsEmpty() ? 0.0f : m_Samples.PeekBack().m_fPosition;
  const float fMinimum = m_Frame.m_uiPacingBy == plAiEncounterPacingBy::WavesCleared ? plMath::Max(1.0f, static_cast<float>(m_Frame.m_Waves.GetCount())) : 60.0f;

  return plMath::Max(plMath::Max(fMinimum, fLength), fRan * 1.1f);
}

void plAiEncounterView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const float fWidth = vSize.x;
  const float fHeight = vSize.y;

  BuildToolbar(pRoot, fWidth);
  BuildStatus(pRoot, plRectFloat(0.0f, fHeight - s_fStatusHeight, fWidth, s_fStatusHeight));

  const plRectFloat main(0.0f, s_fToolbarHeight, fWidth, fHeight - s_fToolbarHeight - s_fStatusHeight);

  if (!m_bHasFrame || !m_Frame.m_bHasDirector)
  {
    auto* pEmpty = !m_bHasFrame ? EmptyState(pRoot, Icon::Info, "Waiting for the scene's engine process", "The Encounter panel reads the scene's encounter director.")
                                : EmptyState(pRoot, Icon::Info, "No encounter director in this scene", "Add an AI Encounter Director component to an object, and AI Encounter Wave components to objects below it.");
    Place(pEmpty, main);
    return;
  }

  const float fRightWidth = plMath::Clamp(fWidth * 0.32f, 240.0f, 320.0f);
  const float fLeftWidth = fWidth - fRightWidth - 1.0f;

  auto* pLeft = Panel(pRoot);
  Place(pLeft, plRectFloat(0.0f, main.y, fLeftWidth, main.height));
  pLeft->m_Color = Surface();

  auto* pDivider = Panel(pRoot);
  Place(pDivider, plRectFloat(fLeftWidth, main.y, 1.0f, main.height));
  pDivider->m_Color = Line();

  auto* pRight = Panel(pRoot);
  Place(pRight, plRectFloat(fLeftWidth + 1.0f, main.y, fRightWidth, main.height));
  pRight->m_Color = Surface();

  float fY = 0.0f;
  BuildPacing(pLeft, fLeftWidth, fY);
  BuildHold(pLeft, fLeftWidth, fY);
  BuildWaves(pLeft, plRectFloat(0.0f, fY, fLeftWidth, main.height - fY));

  float fRightY = 0.0f;
  BuildIntensity(pRight, fRightWidth, fRightY);
  BuildWaveDetails(pRight, fRightWidth, fRightY);
}

void plAiEncounterView::BuildToolbar(plUiElement* pRoot, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  auto* pBar = Panel(pRoot);
  Place(pBar, plRectFloat(0.0f, 0.0f, fWidth, s_fToolbarHeight));
  pBar->m_Color = Plate();

  auto* pEdge = Panel(pBar);
  Place(pEdge, plRectFloat(0.0f, s_fToolbarHeight - 1.0f, fWidth, 1.0f));
  pEdge->m_Color = Line();

  plStringBuilder sScene(m_pDocument->GetDocumentPath());
  plStringBuilder sState;
  sState.SetFormat("{}{}{} \xC2\xB7 {}", sScene.GetFileNameAndExtension(), m_Frame.m_bHasDirector ? " \xC2\xB7 " : "", m_Frame.m_sDirectorName, m_Frame.m_bSimulating ? "Simulating" : "Not simulating");

  const char* szRestart = "Restart encounter";
  const float fChip = 22.0f;
  const float fY = (s_fToolbarHeight - fChip) * 0.5f;
  float x = fWidth - 8.0f - ChipWidth(szRestart, false);

  const bool bCanRestart = m_Frame.m_bSimulating && m_Frame.m_bHasDirector;
  AddChip(pBar, plRectFloat(x, fY, ChipWidth(szRestart, false), fChip), "Enc.Restart", szRestart, false, plColorGammaUB(0, 0, 0, 0), bCanRestart ? "Starts the encounter again: every wave waits for its trigger. Agents already spawned stay." : "Simulate or play the scene to restart its encounter.", [this, bCanRestart]()
    {
    if (!bCanRestart)
      return;

    m_bRestart = true;
    m_LastPoll = plTime::MakeZero(); });

  // with several directors, a chip that steps through them
  if (m_Frame.m_Directors.GetCount() > 1)
  {
    plStringBuilder sNext;
    sNext.SetFormat("Director {} of {}", 1, m_Frame.m_Directors.GetCount());
    for (plUInt32 i = 0; i < m_Frame.m_Directors.GetCount(); ++i)
    {
      if (m_Frame.m_Directors[i].m_Object == m_Frame.m_Director)
        sNext.SetFormat("Director {} of {}", i + 1, m_Frame.m_Directors.GetCount());
    }

    x -= 6.0f + ChipWidth(sNext, false);
    AddChip(pBar, plRectFloat(x, fY, ChipWidth(sNext, false), fChip), "Enc.Next", sNext, false, plColorGammaUB(0, 0, 0, 0), "Show the scene's next encounter director.", [this]()
      {
      const plUInt32 uiCount = m_Frame.m_Directors.GetCount();
      for (plUInt32 i = 0; i < uiCount; ++i)
      {
        if (m_Frame.m_Directors[i].m_Object == m_Frame.m_Director)
        {
          m_Director = m_Frame.m_Directors[(i + 1) % uiCount].m_Object;
          m_LastPoll = plTime::MakeZero();
          break;
        }
      } });
  }

  Label(pBar, plRectFloat(12.0f, 0.0f, plMath::Max(x - 20.0f, 40.0f), s_fToolbarHeight), sState, Muted(), FontSize - 1);
}

void plAiEncounterView::BuildPacing(plUiElement* pPane, float fWidth, float& inout_fY)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const char* szHint = "target intensity over encounter time; a due wave waits while measured intensity is above it";
  auto* pSection = Section(pPane, "Pacing", Muted(), {}, szHint);
  Place(pSection, plRectFloat(0.0f, inout_fY, fWidth, SectionHeight));
  inout_fY += SectionHeight;

  auto* pWell = Panel(pPane);
  Place(pWell, plRectFloat(0.0f, inout_fY, fWidth, s_fChartHeight + 16.0f));
  pWell->m_Color = Background();

  AddDrawList(pWell, plRectFloat(12.0f, 8.0f, fWidth - 24.0f, s_fChartHeight), "Enc.Chart");
  DrawChart(plVec2(fWidth - 24.0f, s_fChartHeight));

  inout_fY += s_fChartHeight + 16.0f;
}

void plAiEncounterView::DrawChart(const plVec2& vSize)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plUiDrawListWriter writer;

  const float fPad = 22.0f;
  const float fBottom = vSize.y - 16.0f;
  const float fPlotHeight = vSize.y - 30.0f;
  const float fSpan = GetChartSpan();
  const bool bByTime = m_Frame.m_uiPacingBy == plAiEncounterPacingBy::Time;

  auto toX = [&](float fPosition)
  { return fPad + plMath::Saturate(fPosition / fSpan) * (vSize.x - fPad); };
  auto toY = [&](float fIntensity)
  { return fBottom - plMath::Saturate(fIntensity) * fPlotHeight; };

  // the phases: alternate bands with their names
  float fPhaseStart = 0.0f;
  for (plUInt32 i = 0; i < m_Frame.m_Phases.GetCount(); ++i)
  {
    const float fPhaseEnd = fPhaseStart + plMath::Max(0.0f, m_Frame.m_Phases[i].m_fDuration);
    const float x0 = toX(fPhaseStart);
    const float x1 = toX(fPhaseEnd);

    if (i % 2 == 1)
      writer.Rect(plRectFloat(x0, 0.0f, x1 - x0, fBottom), WithAlpha(Foreground(), 5));

    plStringBuilder sName(m_Frame.m_Phases[i].m_sName);
    sName.ToUpper();
    writer.Text(plRectFloat(x0 + 4.0f, 0.0f, plMath::Max(0.0f, x1 - x0 - 6.0f), 12.0f), sName, Dim(), MinFontSize - 1);

    fPhaseStart = fPhaseEnd;
  }

  for (plUInt32 i = 0; i <= 4; ++i)
  {
    const float y = toY(i * 0.25f);
    writer.Line(plVec2(fPad, y), plVec2(vSize.x, y), GridColor());
  }

  writer.Text(plRectFloat(0.0f, toY(1.0f) - 6.0f, fPad - 4.0f, 12.0f), "1", Dim(), MinFontSize - 1, plUiTextHAlign::Right);
  writer.Text(plRectFloat(0.0f, toY(0.0f) - 6.0f, fPad - 4.0f, 12.0f), "0", Dim(), MinFontSize - 1, plUiTextHAlign::Right);

  // the pacing, dashed, evaluated as the director does
  plHybridArray<plAiEncounterPhase, 8> phases;
  GetPhases(m_Frame, phases);

  constexpr plUInt32 uiSteps = 120;
  plVec2 vPrevious(toX(0.0f), toY(plAiEvaluatePacing(phases, 0.0f)));
  for (plUInt32 i = 1; i <= uiSteps; ++i)
  {
    const float fPosition = fSpan * i / uiSteps;
    const plVec2 vPoint(toX(fPosition), toY(plAiEvaluatePacing(phases, fPosition)));
    writer.Line(vPrevious, vPoint, Muted(), 1.5f, 5.0f, 4.0f);
    vPrevious = vPoint;
  }

  // the measured intensity
  for (plUInt32 i = 1; i < m_Samples.GetCount(); ++i)
  {
    const Sample& a = m_Samples[i - 1];
    const Sample& b = m_Samples[i];
    writer.Line(plVec2(toX(a.m_fPosition), toY(a.m_fIntensity)), plVec2(toX(b.m_fPosition), toY(b.m_fIntensity)), AiColor(), 2.0f);
  }

  // the waves: where they were released, or became due and are held (positions are known in time pacing only)
  for (plUInt32 i = 0; i < m_Frame.m_Waves.GetCount() && bByTime; ++i)
  {
    const plAiEncounterDebugFrame::Wave& wave = m_Frame.m_Waves[i];
    if (wave.m_uiState == plAiEncounterWaveState::Waiting)
      continue;

    const float fAt = static_cast<float>(wave.m_uiState == plAiEncounterWaveState::Held ? wave.m_fDueSince : wave.m_fReleasedAt);
    const float x = toX(fAt);
    writer.Line(plVec2(x, 14.0f), plVec2(x, fBottom), StateColor(wave.m_uiState), 1.0f, 2.0f, 3.0f);
    writer.Text(plRectFloat(x + 3.0f, fBottom + 2.0f, 30.0f, 12.0f), Name("W", i + 1), Dim(), MinFontSize - 1, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
  }

  // now
  if (m_Frame.m_bSimulating && m_Frame.m_uiState != 0)
  {
    const float x = toX(m_Frame.m_fPacingPosition);
    writer.Line(plVec2(x, 0.0f), plVec2(x, fBottom), Accent(), 1.5f);
  }

  Publish(GetBoard(), writer, "Enc.Chart");
}

void plAiEncounterView::BuildHold(plUiElement* pPane, float fWidth, float& inout_fY)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  for (plUInt32 i = 0; i < m_Frame.m_Waves.GetCount(); ++i)
  {
    const plAiEncounterDebugFrame::Wave& wave = m_Frame.m_Waves[i];
    if (wave.m_uiState != plAiEncounterWaveState::Held)
      continue;

    plStringBuilder sText;
    sText.SetFormat("W{} is due but held: intensity {} is above pacing {} + {}. It is released when the fight calms down", i + 1, plArgF(m_Frame.m_fIntensity, 2), plArgF(m_Frame.m_fTarget, 2), plArgF(m_Frame.m_fHoldMargin, 2));

    if (wave.m_fMaxHold > 0.0f)
      sText.AppendFormat(", or after {} s at the latest.", plArgF(wave.m_fMaxHold, 0));
    else
      sText.Append(".");

    auto* pHold = Panel(pPane);
    Place(pHold, plRectFloat(0.0f, inout_fY, fWidth, s_fHoldHeight));
    pHold->m_Color = WithAlpha(Warning(), 24);

    auto* pEdge = Panel(pHold);
    Place(pEdge, plRectFloat(0.0f, 0.0f, 3.0f, s_fHoldHeight));
    pEdge->m_Color = Warning();

    Label(pHold, plRectFloat(14.0f, 0.0f, fWidth - 24.0f, s_fHoldHeight), sText, Foreground(), FontSize - 2);

    inout_fY += s_fHoldHeight;
    return;
  }
}

void plAiEncounterView::BuildWaves(plUiElement* pPane, const plRectFloat& rect)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plStringBuilder sCount;
  sCount.SetFormat("{}", m_Frame.m_Waves.GetCount());

  auto* pSection = Section(pPane, "Waves", Muted(), sCount, "click a wave to edit it in the properties");
  Place(pSection, plRectFloat(rect.x, rect.y, rect.width, SectionHeight));

  const float fTop = rect.y + SectionHeight;
  const float fRowWidth = rect.width - ScrollbarGutter;

  // the columns: #, trigger, spawner, squad, state
  const float xTrigger = 44.0f;
  const float xSpawner = xTrigger + fRowWidth * 0.34f;
  const float xSquad = xSpawner + fRowWidth * 0.28f;
  const float xState = xSquad + fRowWidth * 0.14f;

  auto* pHeader = Panel(pPane);
  Place(pHeader, plRectFloat(rect.x, fTop, rect.width, s_fWaveRow));
  pHeader->m_Color = Plate();

  Label(pHeader, plRectFloat(12.0f, 0.0f, 30.0f, s_fWaveRow), "#", Dim(), MinFontSize);
  Label(pHeader, plRectFloat(xTrigger, 0.0f, xSpawner - xTrigger, s_fWaveRow), "Trigger", Dim(), MinFontSize);
  Label(pHeader, plRectFloat(xSpawner, 0.0f, xSquad - xSpawner, s_fWaveRow), "Spawner", Dim(), MinFontSize);
  Label(pHeader, plRectFloat(xSquad, 0.0f, xState - xSquad, s_fWaveRow), "Squad", Dim(), MinFontSize);
  Label(pHeader, plRectFloat(xState, 0.0f, fRowWidth - xState, s_fWaveRow), "State", Dim(), MinFontSize);

  const float fBodyTop = fTop + s_fWaveRow;
  plUiScrollView* pBody = AddScrollBody(pPane, plRectFloat(rect.x, fBodyTop, rect.width, plMath::Max(0.0f, rect.height - (fBodyTop - rect.y))), "Enc.Waves", 0.0f, 0.0f);

  if (m_Frame.m_Waves.IsEmpty())
  {
    auto* pNone = Panel(pBody);
    pNone->m_vOffsets.w = 48.0f;
    pNone->m_Color = plColorGammaUB(0, 0, 0, 0);
    Label(pNone, plRectFloat(12.0f, 8.0f, rect.width - 24.0f, 20.0f), "No waves: add AI Encounter Wave components to objects below the director.", Dim(), FontSize - 2);
    return;
  }

  for (plUInt32 i = 0; i < m_Frame.m_Waves.GetCount(); ++i)
  {
    const plAiEncounterDebugFrame::Wave& wave = m_Frame.m_Waves[i];
    const bool bSelected = wave.m_Object == m_SelectedWave;

    auto* pRow = Panel(pBody);
    pRow->m_vOffsets.w = s_fWaveRow;
    pRow->m_Color = bSelected ? Selection() : plColorGammaUB(0, 0, 0, 0);

    Label(pRow, plRectFloat(12.0f, 0.0f, 30.0f, s_fWaveRow), Name("W", i + 1), Foreground(), FontSize - 2, true);
    Label(pRow, plRectFloat(xTrigger, 0.0f, xSpawner - xTrigger - 6.0f, s_fWaveRow), wave.m_sTrigger, Foreground(), FontSize - 2);
    Label(pRow, plRectFloat(xSpawner, 0.0f, xSquad - xSpawner - 6.0f, s_fWaveRow), wave.m_sSpawner, Foreground(), FontSize - 3, true);
    Label(pRow, plRectFloat(xSquad, 0.0f, xState - xSquad - 6.0f, s_fWaveRow), wave.m_sSquad.IsEmpty() ? "-" : wave.m_sSquad.GetView(), Muted(), FontSize - 3, true);

    const bool bProblem = !wave.m_sProblem.IsEmpty();
    Label(pRow, plRectFloat(xState, 0.0f, fRowWidth - xState, s_fWaveRow), bProblem && wave.m_uiState == plAiEncounterWaveState::Waiting ? "check setup" : StateName(wave.m_uiState), bProblem ? Danger() : StateColor(wave.m_uiState), FontSize - 2);

    AddClickArea(pRow, plRectFloat(0.0f, 0.0f, fRowWidth, s_fWaveRow), Name("Enc.Wave.", i), "Show this wave and select its object, to edit it in the properties.", [this, i]()
      { SelectWave(i, true); });
  }
}

void plAiEncounterView::BuildIntensity(plUiElement* pPane, float fWidth, float& inout_fY)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  auto* pSection = Section(pPane, "Intensity", Muted(), FormatNumber(m_Frame.m_fIntensity, 2));
  Place(pSection, plRectFloat(0.0f, inout_fY, fWidth, SectionHeight));
  inout_fY += SectionHeight + 6.0f;

  const float fLabel = 150.0f;
  const float fValue = 36.0f;
  const float fBar = plMath::Max(20.0f, fWidth - fLabel - fValue - 36.0f);

  AddDrawList(pPane, plRectFloat(0.0f, inout_fY, fWidth, 3 * s_fPropRow), "Enc.Parts");
  plUiDrawListWriter writer;

  for (plUInt32 i = 0; i < 3; ++i)
  {
    const float y = i * s_fPropRow;
    writer.Text(plRectFloat(12.0f, y, fLabel, s_fPropRow), s_szPartNames[i], Muted(), FontSize - 2);
    DrawBar(writer, plRectFloat(12.0f + fLabel + 6.0f, y + 6.0f, fBar, s_fPropRow - 12.0f), m_Frame.m_fParts[i], AiColor());
    writer.Text(plRectFloat(fWidth - fValue - 12.0f, y, fValue, s_fPropRow), FormatNumber(m_Frame.m_fParts[i], 2), Foreground(), FontSize - 2, plUiTextHAlign::Right, plUiTextVAlign::Center, true);
  }

  Publish(GetBoard(), writer, "Enc.Parts");
  inout_fY += 3 * s_fPropRow + 4.0f;

  plStringBuilder sNote;
  sNote.SetFormat("Weights {} / {} / {}, smoothed over {} s. {} agents engage a player. Measured from AI perception and the stimuli the game already posts.", FormatTrimmed(m_Frame.m_fWeights[0], 2), FormatTrimmed(m_Frame.m_fWeights[1], 2), FormatTrimmed(m_Frame.m_fWeights[2], 2), FormatTrimmed(m_Frame.m_fSmoothing, 1), m_Frame.m_uiEngaged);

  auto* pNote = Label(pPane, plRectFloat(12.0f, inout_fY, fWidth - 24.0f, 44.0f), sNote, Dim(), MinFontSize);
  pNote->m_bWrap = true;
  inout_fY += 50.0f;
}

void plAiEncounterView::BuildWaveDetails(plUiElement* pPane, float fWidth, float fY)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plInt32 iWave = -1;
  for (plUInt32 i = 0; i < m_Frame.m_Waves.GetCount(); ++i)
  {
    if (m_Frame.m_Waves[i].m_Object == m_SelectedWave)
      iWave = static_cast<plInt32>(i);
  }

  auto* pSection = Section(pPane, "Wave", Accent(), iWave >= 0 ? Name("W", iWave + 1).GetView() : plStringView());
  Place(pSection, plRectFloat(0.0f, fY, fWidth, SectionHeight));
  fY += SectionHeight + 6.0f;

  if (iWave < 0)
  {
    Label(pPane, plRectFloat(12.0f, fY, fWidth - 24.0f, s_fPropRow), "Select a wave to see how it is set up.", Dim(), FontSize - 2);
    return;
  }

  const plAiEncounterDebugFrame::Wave& wave = m_Frame.m_Waves[iWave];

  plStringBuilder sSetup;
  if (wave.m_sSpawnerType.IsEmpty())
    sSetup = "nothing to release";
  else
    sSetup.SetFormat("{} \xC2\xB7 {}", wave.m_sSpawnerType, wave.m_sReleasePath);

  plStringBuilder sHold;
  if (wave.m_bHoldWhenIntense)
    sHold.SetFormat("Intensity > pacing + {}", plArgF(m_Frame.m_fHoldMargin, 2));
  else
    sHold = "Never held";

  plStringBuilder sMaxHold;
  if (!wave.m_bHoldWhenIntense)
    sMaxHold = "-";
  else if (wave.m_fMaxHold > 0.0f)
    sMaxHold.SetFormat("{} s", plArgF(wave.m_fMaxHold, 0));
  else
    sMaxHold = "No limit";

  plStringBuilder sAlive;
  if (wave.m_uiState == plAiEncounterWaveState::Released || wave.m_uiState == plAiEncounterWaveState::Cleared)
    sAlive.SetFormat("{} (most {})", wave.m_iAlive, wave.m_iPeakAlive);
  else
    sAlive = "-";

  const char* szLabels[] = {"Trigger", "Spawner", "Released by", "Squad", "Hold while", "Hold at most", "Alive", "State"};
  const plString values[] = {wave.m_sTrigger, wave.m_sSpawner, plString(sSetup), wave.m_sSquad.IsEmpty() ? plString("unknown") : wave.m_sSquad, plString(sHold), plString(sMaxHold), plString(sAlive), plString(StateName(wave.m_uiState))};

  const float fLabel = 96.0f;
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(szLabels); ++i)
  {
    Label(pPane, plRectFloat(12.0f, fY, fLabel, s_fPropRow), szLabels[i], Muted(), FontSize - 2);
    Label(pPane, plRectFloat(12.0f + fLabel, fY, fWidth - fLabel - 24.0f, s_fPropRow), values[i], i == 7 ? StateColor(wave.m_uiState) : Foreground(), FontSize - 2, i == 1 || i == 3);
    fY += s_fPropRow;
  }

  fY += 6.0f;

  if (!wave.m_sProblem.IsEmpty())
  {
    auto* pProblem = Label(pPane, plRectFloat(12.0f, fY, fWidth - 24.0f, 44.0f), wave.m_sProblem, Danger(), FontSize - 2);
    pProblem->m_bWrap = true;
    fY += 50.0f;
  }

  auto* pNote = Label(pPane, plRectFloat(12.0f, fY, fWidth - 24.0f, 60.0f), "The spawner keeps doing the spawning: spawn points, budget, max alive, spawn delay. The director only decides when it may start, and counts the wave's agents through its squad.", Dim(), MinFontSize);
  pNote->m_bWrap = true;
}

void plAiEncounterView::BuildStatus(plUiElement* pRoot, const plRectFloat& rect)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  auto* pBar = Panel(pRoot);
  Place(pBar, rect);
  pBar->m_Color = Plate();

  auto* pEdge = Panel(pBar);
  Place(pEdge, plRectFloat(0.0f, 0.0f, rect.width, 1.0f));
  pEdge->m_Color = Line();

  plInt32 iAlive = 0;
  plUInt32 uiReleased = 0;
  for (const plAiEncounterDebugFrame::Wave& wave : m_Frame.m_Waves)
  {
    if (wave.m_uiState == plAiEncounterWaveState::Released)
      iAlive += wave.m_iAlive;

    if (wave.m_uiState == plAiEncounterWaveState::Released || wave.m_uiState == plAiEncounterWaveState::Cleared)
      ++uiReleased;
  }

  plStringView sPhase = "Not started";
  if (m_Frame.m_uiState == 2)
    sPhase = "Finished";
  else if (m_Frame.m_uiState == 1 && m_Frame.m_uiPhase < m_Frame.m_Phases.GetCount())
    sPhase = m_Frame.m_Phases[m_Frame.m_uiPhase].m_sName;
  else if (m_Frame.m_uiState == 1)
    sPhase = "Running";

  plStringBuilder sStatus;
  sStatus.SetFormat("Alive {}   \xC2\xB7   Released {} / {}   \xC2\xB7   Phase {}", iAlive, uiReleased, m_Frame.m_Waves.GetCount(), sPhase);
  Label(pBar, plRectFloat(12.0f, 0.0f, rect.width - 80.0f, rect.height), sStatus, Muted(), FontSize - 2);

  const plString sClock = FormatClock(m_Frame.m_fEncounterTime);
  Label(pBar, plRectFloat(rect.width - 60.0f, 0.0f, 48.0f, rect.height), sClock, Muted(), FontSize - 2, true);
}
