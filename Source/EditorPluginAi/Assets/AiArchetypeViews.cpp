#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiArchetypeAsset.h>
#include <EditorPluginAi/Assets/AiArchetypeAssetWindow.h>
#include <EditorPluginAi/Assets/AiArchetypeViews.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <AiPlugin/UtilityAI/Decision/AiScoring.h>
#include <Core/Utils/Blackboard.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorUi/Surface.h>
#include <ToolsFoundation/CommandHistory/CommandHistory.h>

namespace
{
  constexpr float s_fRowHeight = 30.0f;
  constexpr float s_fBarWidth = 80.0f;
  constexpr float s_fTimelineHeight = 140.0f;
  constexpr float s_fPeekLine = 18.0f;
  constexpr float s_fPeekCurveRow = 28.0f;

  plString Name(plStringView sPrefix, plUInt32 uiIndex, plStringView sSuffix)
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

  /// "4 considerations, target, cooldown 4.0s" (dot-separated), or the unresolved note.
  void GetTags(const plAiBehaviorPeek& peek, plStringBuilder& out_sTags)
  {
    out_sTags.Clear();

    if (!peek.m_bValid)
    {
      out_sTags = "unresolved - asset not found";
      return;
    }

    if (!peek.m_Considerations.IsEmpty())
    {
      out_sTags.SetFormat("{} considerations", peek.m_Considerations.GetCount());

      for (const auto& cons : peek.m_Considerations)
      {
        if (cons.m_bNeedsTarget)
        {
          out_sTags.Append(" \xC2\xB7 target");
          break;
        }
      }
    }

    if (peek.m_fCooldownSeconds > 0.0)
      out_sTags.AppendFormat(" \xC2\xB7 cooldown {}s", plArgF(peek.m_fCooldownSeconds, 1));
  }

  /// The band of a category in the ladder, or the unresolved band after them.
  plUInt32 BandForCategory(plInt32 iCategory)
  {
    const plArrayPtr<const plInt32> ladder = plAiEngineUi::GetCategoryLadder();

    for (plUInt32 i = 0; i < ladder.GetCount(); ++i)
    {
      if (ladder[i] == iCategory)
        return i;
    }

    return ladder.GetCount();
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plAiArchetypeStackView

plAiArchetypeStackView::plAiArchetypeStackView(plAiArchetypeAssetDocumentWindow* pWindow)
  : plAiEngineView("AiArchetypeStack")
  , m_pWindow(pWindow)
{
}

plAiArchetypeStackView::~plAiArchetypeStackView() = default;

bool plAiArchetypeStackView::IsRebuildBlocked() const
{
  return m_pWindow->GetDocument()->GetCommandHistory()->IsInTransaction();
}

void plAiArchetypeStackView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const float fWidth = vSize.x;
  const float fTop = BuildHeader(pRoot, fWidth);

  plUiScrollView* pBody = AddScrollBody(pRoot, plRectFloat(0, fTop, fWidth, plMath::Max(vSize.y - fTop, 10.0f)), "Stack.Body", 0.0f, 0.0f);

  // the hint above the bands
  {
    plUiPanel* pHint = Panel(pBody);
    pHint->m_vOffsets.w = 28.0f;
    pHint->m_Color = plColorGammaUB(0, 0, 0, 0);
    Label(pHint, plRectFloat(10.0f, 0, fWidth - 20.0f, 28.0f), "grouped by the category, higher band always wins; inside a band the highest utility wins", Dim(), FontSize - 2);
  }

  // sort the rows into the ladder bands; the last band holds the unresolved references
  const auto& behaviors = m_pWindow->GetArchetypeDocument()->GetProperties()->m_Behaviors;
  const plArrayPtr<const plInt32> ladder = GetCategoryLadder();

  plDynamicArray<plUuid> guids;
  m_pWindow->GetBehaviorGuids(guids);

  plHybridArray<plHybridArray<plUInt32, 8>, 8> bands;
  bands.SetCount(ladder.GetCount() + 1);

  for (plUInt32 i = 0; i < guids.GetCount() && i < behaviors.GetCount(); ++i)
  {
    const plAiBehaviorPeek& peek = plAiBehaviorPeekCache::Get(behaviors[i].m_sBehavior);
    bands[peek.m_bValid ? BandForCategory(peek.m_iCategory) : ladder.GetCount()].PushBack(i);
  }

  for (plUInt32 uiBand = 0; uiBand < bands.GetCount(); ++uiBand)
  {
    const bool bUnresolved = uiBand == ladder.GetCount();
    const char* szTitle = bUnresolved ? "Unresolved" : plAiAssetUi::CategoryName(ladder[uiBand]);

    Section(pBody, szTitle, bUnresolved ? Border() : CategoryColor(ladder[uiBand]), FormatNumber(bands[uiBand].GetCount(), 0));

    for (plUInt32 i : bands[uiBand])
    {
      const plAiBehaviorPeek& peek = plAiBehaviorPeekCache::Get(behaviors[i].m_sBehavior);
      BuildRow(pBody, fWidth, i, guids[i], peek, behaviors[i].m_sBehavior, behaviors[i].m_fWeightScale);
    }

    if (bands[uiBand].IsEmpty())
    {
      plUiPanel* pEmpty = Panel(pBody);
      pEmpty->m_vOffsets.w = 26.0f;
      pEmpty->m_Color = plColorGammaUB(0, 0, 0, 0);
      Label(pEmpty, plRectFloat(12.0f, 0, fWidth - 24.0f, 24.0f), bUnresolved ? "all references resolve" : "no behaviors", Dim(), FontSize - 2);
    }
  }
}

float plAiArchetypeStackView::BuildHeader(plUiElement* pRoot, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();

  plUiPanel* pRow = Panel(pRoot);
  pRow->m_Color = Surface();

  plAiFlowLayout flow(6.0f, 3.0f, fWidth - 6.0f, ControlHeight, 2.0f);

  auto addLabel = [&](plStringView sText)
  {
    plUiText* pLabel = Label(pRow, flow.Take(TextWidth(sText) + 8.0f), sText, Dim());
    pLabel->m_HAlign = plUiTextHAlign::Center;
  };

  flow.Group(TextWidth("Team") + 10.0f + 130.0f);
  addLabel("Team");
  AddTextField(pRow, flow.Take(130.0f), "Team", pProps->m_sTeam, {}, "Faction/team this archetype belongs to (perception friend-or-foe).", [this](const plString& sText)
    { m_pWindow->SetRootValue("Team", plVariant(sText.GetData()), "Change Team"); });

  flow.Group(TextWidth("Sensor object") + 10.0f + 130.0f);
  addLabel("Sensor object");
  AddTextField(pRow, flow.Take(130.0f), "Sensor", pProps->m_sSensorObjectName, {}, "Name of the child object carrying the sensor components.", [this](const plString& sText)
    { m_pWindow->SetRootValue("SensorObjectName", plVariant(sText.GetData()), "Change Sensor Object"); });

  // the blackboard template: its asset name, browse
  {
    plStringBuilder sDisplay = pProps->m_sBlackboardTemplate;
    if (!pProps->m_sBlackboardTemplate.IsEmpty())
    {
      auto asset = plAssetCurator::GetSingleton()->FindSubAsset(pProps->m_sBlackboardTemplate);
      if (asset.isValid())
        sDisplay = asset->GetName();
    }

    flow.Group(TextWidth("Blackboard template") + 10.0f + 160.0f + 2.0f + 24.0f);
    addLabel("Blackboard template");

    const plRectFloat field = flow.Take(160.0f);
    auto* pWell = Panel(pRow);
    Place(pWell, field);
    pWell->m_Color = Field();
    pWell->m_fBorderWidth = 1.0f;
    pWell->m_BorderColor = Line();
    pWell->m_vCornerRadius = plVec4(4);

    Label(pWell, plRectFloat(8.0f, 0, field.width - 16.0f, field.height), sDisplay.IsEmpty() ? plStringView("<no blackboard template>") : sDisplay.GetView(), sDisplay.IsEmpty() ? Dim() : Muted());
    AddClickArea(pRow, field, "Blackboard", pProps->m_sBlackboardTemplate, {});

    AddIconButton(pRow, flow.Take(24.0f), "Blackboard.Browse", Icon::More, "Select a blackboard template asset", [this]()
      { Defer([this]()
          { m_pWindow->BrowseBlackboardTemplate(); }); });
  }

  const float fAdd = TextWidth("Add Behavior") + 38.0f;
  flow.AlignRight(fAdd);
  AddTextButton(pRow, flow.Take(fAdd), "AddBehavior", "Add Behavior", {}, [this]()
    { Defer([this]()
        { m_pWindow->AddBehavior(); }); },
    true);

  const float fHeight = flow.GetBottom() + 3.0f;
  Place(pRow, plRectFloat(0, 0, fWidth, fHeight));

  auto* pLine = Panel(pRoot);
  Place(pLine, plRectFloat(0, fHeight, fWidth, 1.0f));
  pLine->m_Color = Line();

  return fHeight + 1.0f;
}

void plAiArchetypeStackView::BuildRow(plUiElement* pBody, float fWidth, plUInt32 uiIndex, const plUuid& guid, const plAiBehaviorPeek& peek, plStringView sReference, float fWeightScale)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const bool bSelected = guid == m_pWindow->GetSelectedRow();

  plUiPanel* pRow = Panel(pBody);
  pRow->m_vOffsets.w = s_fRowHeight;
  pRow->m_Color = bSelected ? Selection() : plColorGammaUB(0, 0, 0, 0);

  // the row selects; its tooltip tells where the asset is
  const plStringBuilder sTooltip = peek.m_bValid ? plStringBuilder(peek.m_sAbsPath) : plStringBuilder("The referenced behavior asset could not be resolved.");
  plUiButton* pHit = AddClickArea(pRow, plRectFloat(0, 0, fWidth, s_fRowHeight), Name("Row.", uiIndex, ""), sTooltip, [this, guid]()
    { m_pWindow->SelectBehaviorRow(guid); });
  Fill(pHit);
  pHit->m_HoverColor = bSelected ? Selection() : Mix(Surface(), Raised(), 0.6f);

  const float fCenter = s_fRowHeight * 0.5f;
  float x = 12.0f;

  plUiImage* pIcon = SvgImage(pRow, ":/AssetIcons/AiBehavior.svg");
  Place(pIcon, plRectFloat(x, fCenter - 7.5f, 15.0f, 15.0f));
  pIcon->m_Color = AiColor();
  x += 15.0f + 10.0f;

  // name, base weight and this archetype's scale
  plStringBuilder sName;
  if (peek.m_bValid)
    sName = peek.m_sName;
  else
    sName = sReference.IsEmpty() ? plStringView("<no behavior set>") : sReference;

  Label(pRow, plRectFloat(x, 0, 140.0f, s_fRowHeight), sName, Foreground(), FontSize);
  x += 140.0f + 10.0f;

  plStringBuilder sWeight;
  if (peek.m_bValid)
    sWeight.SetFormat("weight {}", plArgF(peek.m_fWeight, 2));
  else
    sWeight = "weight ?";

  const float fWeight = CodeWidth(sWeight);
  Label(pRow, plRectFloat(x, 0, fWeight, s_fRowHeight), sWeight, Dim(), FontSize - 2, true);
  x += fWeight + 10.0f;

  const float fTimes = CodeWidth("\xC3\x97");
  Label(pRow, plRectFloat(x, 0, fTimes, s_fRowHeight), "\xC3\x97", Dim(), FontSize - 2, true);
  x += fTimes + 10.0f;

  AddNumberField(pRow, plRectFloat(x, fCenter - ControlHeight * 0.5f, 56.0f, ControlHeight), Name("Row.", uiIndex, ".Scale"), FormatNumber(fWeightScale, 2), {}, "This archetype's WeightScale for the behavior.", [this, guid](const plString& sText)
    {
    double fValue = 0.0;
    if (ParseClamped(sText, 0.0, 10.0, fValue))
      m_pWindow->SetWeightScale(guid, fValue);
    else
      Invalidate(); });
  x += 56.0f + 10.0f;

  // the effective weight: base weight times scale, full bar at 2.0
  {
    const plRectFloat bar(x, fCenter - 3.0f, s_fBarWidth, 6.0f);

    auto* pTrack = Panel(pRow);
    Place(pTrack, bar);
    pTrack->m_Color = Field();
    pTrack->m_fBorderWidth = 1.0f;
    pTrack->m_BorderColor = Line();
    pTrack->m_vCornerRadius = plVec4(3);

    const float fFraction = peek.m_bValid ? plMath::Clamp(peek.m_fWeight * fWeightScale / 2.0f, 0.0f, 1.0f) : 0.0f;
    if (fFraction > 0.0f)
    {
      auto* pFill = Panel(pRow);
      Place(pFill, plRectFloat(bar.x + 1.0f, bar.y + 1.0f, (bar.width - 2.0f) * fFraction, bar.height - 2.0f));
      pFill->m_Color = Muted();
      pFill->m_vCornerRadius = plVec4(2);
    }

    AddClickArea(pRow, plRectFloat(bar.x, 0, bar.width, s_fRowHeight), Name("Row.", uiIndex, ".Weight"), "effective weight = base weight \xC3\x97 archetype scale (full bar = 2.0)", [this, guid]()
      { m_pWindow->SelectBehaviorRow(guid); });

    x += s_fBarWidth + 10.0f;
  }

  // open and remove at the right end, the tags between
  const float fButtons = fWidth - 6.0f - 2.0f * SmallButtonSize - 2.0f;

  plStringBuilder sTags;
  GetTags(peek, sTags);
  Label(pRow, plRectFloat(x, 0, plMath::Max(fButtons - 8.0f - x, 10.0f), s_fRowHeight), sTags, Dim(), FontSize - 2, true);

  AddIconButton(pRow, plRectFloat(fButtons, fCenter - SmallButtonSize * 0.5f, SmallButtonSize, SmallButtonSize), Name("Row.", uiIndex, ".Open"), Icon::External, "Open the behavior asset", [this, guid]()
    { m_pWindow->OpenBehavior(guid); });

  AddIconButton(pRow, plRectFloat(fButtons + SmallButtonSize + 2.0f, fCenter - SmallButtonSize * 0.5f, SmallButtonSize, SmallButtonSize), Name("Row.", uiIndex, ".Remove"), Icon::Clear, "Remove this behavior from the archetype", [this, guid]()
    { m_pWindow->RemoveBehavior(guid); });
}

//////////////////////////////////////////////////////////////////////////
// plAiPerceptionView

plAiPerceptionView::plAiPerceptionView(plAiArchetypeAssetDocumentWindow* pWindow)
  : plAiEngineView("AiArchetypePerception")
  , m_pWindow(pWindow)
{
}

plAiPerceptionView::~plAiPerceptionView() = default;

bool plAiPerceptionView::IsRebuildBlocked() const
{
  return m_pWindow->GetDocument()->GetCommandHistory()->IsInTransaction();
}

void plAiPerceptionView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();
  const float fWidth = vSize.x;

  plUiScrollView* pBody = AddScrollBody(pRoot, plRectFloat(0, 0, fWidth, vSize.y), "Perception.Body", 0.0f, 0.0f);

  // the confidence timeline and its settings
  Section(pBody, "Target confidence over time", AiColor());
  {
    plUiPanel* pPanel = Panel(pBody);
    pPanel->m_Color = plColorGammaUB(0, 0, 0, 0);

    const float fTimelineWidth = plMath::Max(fWidth - 20.0f, 60.0f);
    AddDrawList(pPanel, plRectFloat(10.0f, 6.0f, fTimelineWidth, s_fTimelineHeight), "Perception.Timeline");
    DrawTimeline(fTimelineWidth, s_fTimelineHeight);

    float y = 6.0f + s_fTimelineHeight + 6.0f;

    auto settingRow = [&](plStringView sLabel, plStringView sName, double fValue, plUInt32 uiDecimals, plStringView sUnit, double fMax, plStringView sProperty, plStringView sUndo, bool bTime)
    {
      Label(pPanel, plRectFloat(10.0f, y, fWidth - 20.0f - 108.0f, ControlHeight), sLabel, Muted());

      const plString sPropertyName = sProperty;
      const plString sDescription = sUndo;
      AddNumberField(pPanel, plRectFloat(fWidth - 10.0f - 100.0f, y, 100.0f, ControlHeight), sName, FormatNumber(fValue, uiDecimals), sUnit, {}, [this, sPropertyName, sDescription, fMax, bTime](const plString& sText)
        {
        double fNew = 0.0;
        if (!ParseClamped(sText, 0.0, fMax, fNew))
        {
          Invalidate();
          return;
        }

        if (bTime)
          m_pWindow->SetRootValue(sPropertyName.GetData(), plVariant(plTime::Seconds(fNew)), sDescription.GetData());
        else
          m_pWindow->SetRootValue(sPropertyName.GetData(), plVariant(static_cast<float>(fNew)), sDescription.GetData()); });

      y += ControlHeight + 6.0f;
    };

    settingRow("Confidence gain /s", "Gain", pProps->m_fConfidenceGainPerSecond, 2, {}, 100.0, "ConfidenceGainPerSec", "Change Confidence Gain", false);
    settingRow("Confidence decay /s", "Decay", pProps->m_fConfidenceDecayPerSecond, 2, {}, 100.0, "ConfidenceDecayPerSec", "Change Confidence Decay", false);
    settingRow("Target memory", "Memory", pProps->m_TargetMemoryDuration.GetSeconds(), 1, "s", 600.0, "TargetMemoryDuration", "Change Target Memory", true);

    Label(pPanel, plRectFloat(10.0f, y, fWidth - 20.0f, 16.0f), "reference lines: 0.60 squad share \xC2\xB7 0.15 tutorial gates", Dim(), FontSize - 2);
    y += 16.0f + 8.0f;

    pPanel->m_vOffsets.w = y;
  }

  // the selected behavior, read-only
  Section(pBody, "Peek", AiColor());
  BuildPeek(pBody, fWidth);
}

void plAiPerceptionView::DrawTimeline(float fWidth, float fHeight)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();

  const double fGain = plMath::Max(0.01f, pProps->m_fConfidenceGainPerSecond);
  const double fDecay = plMath::Max(0.0f, pProps->m_fConfidenceDecayPerSecond);
  const double fMemory = plMath::Max(0.0, pProps->m_TargetMemoryDuration.GetSeconds());

  // sight is lost after 2 s; the record is forgotten when the memory runs out
  const double tLost = 2.0;
  const double tForget = tLost + fMemory;
  const double tEnd = plMath::Clamp(tForget + 2.0, 8.0, 60.0);

  // the reference labels sit left of the plot
  auto X = [&](double t)
  { return static_cast<float>(32.0 + (t / tEnd) * (fWidth - 40.0)); };
  auto Y = [&](double c)
  { return static_cast<float>(fHeight - 16.0 - plMath::Clamp(c, 0.0, 1.0) * (fHeight - 26.0)); };

  plUiDrawListWriter writer;
  writer.Rect(plRectFloat(0, 0, fWidth, fHeight), Background());

  // the sighted region; opaque tints, as translucent fills blend in linear space and come out much stronger
  writer.Rect(plRectFloat(X(0), 4.0f, X(tLost) - X(0), fHeight - 20.0f), Mix(Background(), GoodColor(), 0.08f));

  // reference lines: 0.60 squad share, 0.15 tutorial gates
  auto referenceLine = [&](double fValue, const plColorGammaUB& color, plStringView sLabel)
  {
    writer.Line(plVec2(X(0), Y(fValue)), plVec2(fWidth - 8.0f, Y(fValue)), Mix(Background(), color, 0.7f), 1.0f, 4.0f, 4.0f);
    writer.Text(plRectFloat(2.0f, Y(fValue) - 7.0f, 28.0f, 14.0f), sLabel, color, MinFontSize, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
  };

  referenceLine(0.6, Warning(), "0.60");
  referenceLine(0.15, Axis(2), "0.15");

  // confidence: rises while sighted, decays afterwards, gone at the memory cutoff
  {
    constexpr plUInt32 uiSamples = 120;
    plVec2 points[uiSamples + 1];
    const double fPeak = plMath::Min(1.0, fGain * tLost);

    for (plUInt32 i = 0; i <= uiSamples; ++i)
    {
      const double t = (static_cast<double>(i) / uiSamples) * tEnd;
      double c = 0.0;

      if (t <= tLost)
        c = plMath::Min(1.0, fGain * t);
      else if (t <= tForget)
        c = plMath::Max(0.0, fPeak - fDecay * (t - tLost));

      points[i] = plVec2(X(t), Y(c));
    }

    writer.Polyline(plMakeArrayPtr(points), AiColor(), 2.0f);
  }

  // sight lost and forgotten
  writer.Line(plVec2(X(tLost), 4.0f), plVec2(X(tLost), fHeight - 16.0f), Dim(), 1.0f, 3.0f, 3.0f);
  writer.Line(plVec2(X(tForget), 4.0f), plVec2(X(tForget), fHeight - 16.0f), Danger(), 1.0f, 3.0f, 3.0f);

  writer.Text(plRectFloat(X(tLost) + 4.0f, 4.0f, 90.0f, 14.0f), "sight lost 2 s", Dim(), FontSize - 2);
  writer.Text(plRectFloat(plMath::Min(X(tForget) + 4.0f, fWidth - 70.0f), 16.0f, 66.0f, 14.0f), "forgotten", Danger(), FontSize - 2);

  writer.Text(plRectFloat(X(0), fHeight - 15.0f, 40.0f, 14.0f), "0 s", Dim(), FontSize - 2);

  plStringBuilder sEnd;
  sEnd.SetFormat("{} s", plArgF(tEnd, 0));
  writer.Text(plRectFloat(fWidth - 8.0f - 50.0f, fHeight - 15.0f, 50.0f, 14.0f), sEnd, Dim(), FontSize - 2, plUiTextHAlign::Right);

  Publish(GetBoard(), writer, "Perception.Timeline");
}

float plAiPerceptionView::BuildPeek(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  // the selected row's behavior reference
  plString sReference;
  bool bSelected = false;
  {
    plDynamicArray<plUuid> guids;
    m_pWindow->GetBehaviorGuids(guids);

    for (plUInt32 i = 0; i < guids.GetCount(); ++i)
    {
      if (guids[i] == m_pWindow->GetSelectedRow())
      {
        sReference = m_pWindow->GetBehaviorRef(i);
        bSelected = true;
      }
    }
  }

  const plAiBehaviorPeek* pPeek = bSelected ? &plAiBehaviorPeekCache::Get(sReference) : nullptr;
  const bool bValid = pPeek != nullptr && pPeek->m_bValid;
  const plUInt32 uiCurves = bValid ? pPeek->m_Considerations.GetCount() : 0;

  float fHeight = 6.0f + s_fPeekLine + 10.0f;
  if (bValid)
    fHeight += s_fPeekLine + plMath::Max<plUInt32>(uiCurves, 1) * s_fPeekCurveRow + s_fPeekLine;

  plUiPanel* pPanel = Panel(pBody);
  pPanel->m_vOffsets.w = fHeight;
  pPanel->m_Color = plColorGammaUB(0, 0, 0, 0);
  AddDrawList(pPanel, plRectFloat(0, 0, fWidth, fHeight), "Perception.Peek");

  plUiDrawListWriter writer;
  const float fTextWidth = fWidth - 20.0f;
  float y = 6.0f;

  if (!bSelected)
  {
    writer.Text(plRectFloat(10.0f, y, fTextWidth, s_fPeekLine), "select a behavior row to peek at its considerations", Dim(), FontSize - 2);
  }
  else if (!bValid)
  {
    writer.Text(plRectFloat(10.0f, y, fTextWidth, s_fPeekLine), "unresolved - asset not found", Dim(), FontSize - 2);
  }
  else
  {
    // the name and its tags, then category, weight, commit and logic
    const plStringBuilder sName(pPeek->m_sName);
    const float fName = TextWidth(sName);
    writer.Text(plRectFloat(10.0f, y, fName, s_fPeekLine), sName, Foreground(), FontSize - 1);

    plStringBuilder sTags;
    GetTags(*pPeek, sTags);
    if (!sTags.IsEmpty())
    {
      sTags.Prepend("\xC2\xB7 ");
      writer.Text(plRectFloat(10.0f + fName + 6.0f, y, plMath::Max(fTextWidth - fName - 6.0f, 10.0f), s_fPeekLine), sTags, Dim(), FontSize - 1);
    }

    y += s_fPeekLine;

    plStringBuilder sSummary;
    sSummary.SetFormat("{} \xC2\xB7 weight {} \xC2\xB7 commit {} \xC2\xB7 {}", plAiAssetUi::CategoryName(pPeek->m_iCategory), plArgF(pPeek->m_fWeight, 2), plArgF(pPeek->m_fCommitBonus, 2), pPeek->m_sLogic.IsEmpty() ? plStringView("no logic") : pPeek->m_sLogic.GetView());
    writer.Text(plRectFloat(10.0f, y, fTextWidth, s_fPeekLine), sSummary, CategoryColor(pPeek->m_iCategory), FontSize - 2, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
    y += s_fPeekLine;

    // each consideration with its curve
    for (const auto& cons : pPeek->m_Considerations)
    {
      DrawCurve(writer, plRectFloat(10.0f, y + 2.0f, 48.0f, 24.0f), &cons.m_Curve, cons.m_bLegacyCurveDomain, false);

      plStringBuilder sLabel;
      sLabel.SetFormat("{} [{} \xE2\x86\x92 {}]", cons.m_sLabel, cons.m_fInputMin, cons.m_fInputMax);
      writer.Text(plRectFloat(66.0f, y, fTextWidth - 56.0f, s_fPeekCurveRow), sLabel, Muted(), FontSize - 1);
      y += s_fPeekCurveRow;
    }

    if (pPeek->m_Considerations.IsEmpty())
    {
      writer.Text(plRectFloat(10.0f, y, fTextWidth, s_fPeekCurveRow), "no considerations - scores its raw weight", Dim(), FontSize - 2);
      y += s_fPeekCurveRow;
    }

    writer.Text(plRectFloat(10.0f, y, fTextWidth, s_fPeekLine), "read-only peek \xC2\xB7 use the row's open button to edit", Dim(), MinFontSize);
  }

  Publish(GetBoard(), writer, "Perception.Peek");
  return fHeight;
}
