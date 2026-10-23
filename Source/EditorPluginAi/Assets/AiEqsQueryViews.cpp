#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEditorHelpers.h>
#include <EditorPluginAi/Assets/AiEqsQueryAsset.h>
#include <EditorPluginAi/Assets/AiEqsQueryAssetWindow.h>
#include <EditorPluginAi/Assets/AiEqsQueryViews.h>

#include <AiPlugin/Eqs/EqsContext.h>
#include <AiPlugin/Eqs/EqsGenerator.h>
#include <AiPlugin/Eqs/EqsTest.h>
#include <Core/Utils/Blackboard.h>
#include <EditorUi/Surface.h>
#include <GuiFoundation/UIServices/DynamicStringEnum.h>
#include <ToolsFoundation/CommandHistory/CommandHistory.h>

namespace
{
  constexpr float s_fCardPadding = 8.0f;
  constexpr float s_fSummaryLine = 15.0f;
  constexpr float s_fCurveWidth = 64.0f;
  constexpr float s_fCurveHeight = 30.0f;
  constexpr plUInt32 s_uiEditCurveId = 1000;

  const char* const s_szRunModes[] = {"Single Best", "Random of Top %", "All Matching"};
  const plInt32 s_RunModeValues[] = {plAiEqsRunMode::SingleBest, plAiEqsRunMode::RandomOfTopPercent, plAiEqsRunMode::AllMatching};

  /// Indexed by plAiEqsFilterCondition.
  const char* const s_szConditions[] = {"Legacy score > 0", "True", "Value >= Min", "Value <= Max", "Value between Min / Max", "Reachable", "Direct path"};

  /// Indexed by plAiEqsMissingDataPolicy.
  const char* const s_szMissingData[] = {"Legacy behavior", "Reject candidate", "Skip test", "Fail query"};

  constexpr const char* s_szCurveTooltip = "Input uses authored X coordinates. Values hold flat before the first and after the last point. Empty = identity; one point = constant. Click to edit; right-click for presets.";
  constexpr const char* s_szLegacyCurveTooltip = "Legacy curve domain: input 0..1 spans the first to last point. Disable LegacyCurveDomain in Properties to use authored X coordinates.";

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

  plString PrettyName(const plRTTI* pRtti)
  {
    plStringBuilder sName;
    plAiEqsUi::GetPrettyName(pRtti, sName);
    return sName;
  }

  /// The colour of a candidate dot: green near the best score, warning in the middle, danger at the bottom.
  plColorGammaUB ScoreRamp(float fRelative)
  {
    if (fRelative < 0.5f)
      return plEditorUi::Danger();
    if (fRelative < 0.8f)
      return plEditorUi::Warning();
    return plAiEngineUi::GoodColor();
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plAiEqsQueryView

plAiEqsQueryView::plAiEqsQueryView(plAiEqsQueryAssetDocumentWindow* pWindow)
  : plAiEngineView("AiEqsQuery")
  , m_pWindow(pWindow)
{
}

plAiEqsQueryView::~plAiEqsQueryView() = default;

bool plAiEqsQueryView::IsRebuildBlocked() const
{
  return m_pWindow->GetDocument()->GetCommandHistory()->IsInTransaction();
}

void plAiEqsQueryView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;

  const float fHeader = BuildHeader(pRoot, vSize.x);
  const float fTop = fHeader + BuildContexts(pRoot, fHeader, vSize.x);

  const float fBodyHeight = plMath::Max(vSize.y - fTop - StatusRowHeight - 1.0f, 10.0f);
  plUiScrollView* pBody = AddScrollBody(pRoot, plRectFloat(0, fTop, vSize.x, fBodyHeight), "Eqs.Body", 10.0f, 8.0f);
  const float fCardWidth = vSize.x - 20.0f;

  plAiEngineUi::Section(pBody, "Generator", plAiEngineUi::AiColor(), {}, "produces the candidate set");
  BuildGenerator(pBody, fCardWidth);

  BuildTestsSection(pBody);

  const plAiEqsQueryAssetObject* pProps = m_pWindow->GetEqsDocument()->GetProperties();

  plDynamicArray<plUuid> guids;
  m_pWindow->GetArrayGuids("Tests", guids);

  const plUInt32 uiCount = plMath::Min(guids.GetCount(), pProps->m_Tests.GetCount());
  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    BuildTestCard(pBody, fCardWidth, i, uiCount, guids[i], pProps->m_Tests[i]);
  }

  BuildStatus(pRoot);
}

float plAiEqsQueryView::BuildHeader(plUiElement* pRoot, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiEqsQueryAssetObject* pProps = m_pWindow->GetEqsDocument()->GetProperties();

  plUiPanel* pRow = Panel(pRoot);
  pRow->m_Color = Surface();

  plAiFlowLayout flow(6.0f, 3.0f, fWidth - 6.0f, ControlHeight, 2.0f);

  auto addLabel = [&](plStringView sText)
  {
    plUiText* pLabel = Label(pRow, flow.Take(TextWidth(sText) + 8.0f), sText, Dim());
    pLabel->m_HAlign = plUiTextHAlign::Center;
  };

  // Name
  flow.Group(TextWidth("Name") + 8.0f + 2.0f + 160.0f);
  addLabel("Name");
  AddTextField(pRow, flow.Take(160.0f), "Name", pProps->m_sName, "<asset file name>", "Display name used in debug overlays; falls back to the asset name.", [this](const plString& sText)
    { m_pWindow->SetRootValue("Name", plVariant(sText.GetData()), "Change Query Name"); });

  // Run Mode
  const char* szRunMode = s_szRunModes[0];
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_RunModeValues); ++i)
  {
    if (s_RunModeValues[i] == pProps->m_RunMode.GetValue())
      szRunMode = s_szRunModes[i];
  }

  const float fRunMode = TextWidth(szRunMode) + 34.0f;
  flow.Group(TextWidth("Run Mode") + 8.0f + 2.0f + fRunMode);
  addLabel("Run Mode");
  AddDropdown(pRow, flow.Take(fRunMode), "RunMode", szRunMode, "How the query picks its result from the scored candidates.\n'Random of Top %' spreads agents out (percentage in Raw Properties).", [this]()
    { ShowRunModeMenu(); });

  // Candidates and Max Results
  flow.Group(TextWidth("Candidates") + 8.0f + 2.0f + 64.0f);
  addLabel("Candidates");
  AddNumberField(pRow, flow.Take(64.0f), "Candidates", FormatNumber(pProps->m_uiCandidates, 0), {}, "How many candidates the generator produces (hard cap 64).", [this](const plString& sText)
    {
    double fValue = 0.0;
    if (ParseClamped(sText, 4.0, 64.0, fValue))
      m_pWindow->SetRootValue("Candidates", plVariant(static_cast<plUInt8>(fValue + 0.5)), "Change Candidates");
    else
      Invalidate(); });

  flow.Group(TextWidth("Max Results") + 8.0f + 2.0f + 56.0f);
  addLabel("Max Results");
  AddNumberField(pRow, flow.Take(56.0f), "MaxResults", FormatNumber(pProps->m_uiMaxResults, 0), {}, "Top-N results kept for the consumer (fallbacks for claim races).", [this](const plString& sText)
    {
    double fValue = 0.0;
    if (ParseClamped(sText, 1.0, 16.0, fValue))
      m_pWindow->SetRootValue("MaxResults", plVariant(static_cast<plUInt8>(fValue + 0.5)), "Change Max Results");
    else
      Invalidate(); });

  // Navmesh
  const plStringView sNavmesh = pProps->m_sNavmeshConfig.IsEmpty() ? plStringView("<default>") : pProps->m_sNavmeshConfig.GetView();
  const float fNavmesh = TextWidth(sNavmesh) + 34.0f;
  flow.Group(TextWidth("Navmesh") + 8.0f + 2.0f + fNavmesh);
  addLabel("Navmesh");
  AddDropdown(pRow, flow.Take(fNavmesh), "Navmesh", sNavmesh, "Which navmesh config candidates are projected onto. <default> = the first configured navmesh.", [this]()
    { ShowNavmeshMenu(); });

  const float fHeight = flow.GetBottom() + 3.0f;
  Place(pRow, plRectFloat(0, 0, fWidth, fHeight));

  auto* pLine = Panel(pRoot);
  Place(pLine, plRectFloat(0, fHeight, fWidth, 1.0f));
  pLine->m_Color = Line();

  return fHeight + 1.0f;
}

float plAiEqsQueryView::BuildContexts(plUiElement* pRoot, float fTop, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiEqsQueryAssetObject* pProps = m_pWindow->GetEqsDocument()->GetProperties();

  plUiPanel* pRow = Panel(pRoot);
  pRow->m_Color = Surface();

  plAiFlowLayout flow(6.0f, 3.0f, fWidth - 6.0f, ControlHeight, 4.0f);

  plUiText* pTitle = Label(pRow, flow.Take(TextWidth("CONTEXTS", MinFontSize) + 8.0f), "CONTEXTS", Dim(), MinFontSize);
  pTitle->m_HAlign = plUiTextHAlign::Center;

  // a pill: on chips are the authored slots, the built-in querier stays dim
  auto addChip = [&](plStringView sText, bool bOn, float fExtra) -> plRectFloat
  {
    const float fChipWidth = TextWidth(sText) + 16.0f + fExtra;
    const plRectFloat rect = flow.Take(fChipWidth, 22.0f);

    auto* pChip = Panel(pRow);
    Place(pChip, rect);
    pChip->m_Color = bOn ? Selection() : Mix(Surface(), Field(), 0.6f);
    pChip->m_fBorderWidth = 1.0f;
    pChip->m_BorderColor = bOn ? Mix(Selection(), Accent(), 0.55f) : Mix(Surface(), Line(), 0.6f);
    pChip->m_vCornerRadius = plVec4(11);

    Label(pRow, plRectFloat(rect.x + 8.0f, rect.y, rect.width - 16.0f - fExtra, rect.height), sText, bOn ? Foreground() : Dim());
    return rect;
  };

  const plRectFloat querier = addChip("Querier \xC2\xB7 built-in", false, 0.0f);
  AddClickArea(pRow, querier, "Ctx.Querier", "The querying agent - always available.", {});

  plDynamicArray<plUuid> guids;
  m_pWindow->GetArrayGuids("ContextSlots", guids);

  for (plUInt32 i = 0; i < guids.GetCount(); ++i)
  {
    plStringBuilder sText("<slot> = ?");

    if (i < pProps->m_ContextSlots.GetCount() && pProps->m_ContextSlots[i] != nullptr)
    {
      const plAiEqsContextSlotObject* pSlot = pProps->m_ContextSlots[i];
      sText.SetFormat("{} = {}", pSlot->m_sName, PrettyName(pSlot->m_pContext != nullptr ? pSlot->m_pContext->GetDynamicRTTI() : nullptr));
    }

    const plUuid guid = guids[i];
    const plRectFloat chip = addChip(sText, true, 16.0f);

    AddClickArea(pRow, plRectFloat(chip.x, chip.y, chip.width - 20.0f, chip.height), Name("Ctx.", i, ""), "Click to edit this context slot in Raw Properties.", [this, guid]()
      { m_pWindow->SelectContextSlot(guid); });

    AddIconButton(pRow, plRectFloat(chip.x + chip.width - 20.0f, chip.y + 3.0f, 16.0f, 16.0f), Name("Ctx.", i, ".Remove"), Icon::Clear, "Remove this context slot", [this, guid]()
      { m_pWindow->RemoveContextSlot(guid); });
  }

  const float fAdd = TextWidth("Add Context") + 38.0f;
  AddTextButton(pRow, flow.Take(fAdd), "Ctx.Add", "Add Context", "Add a named context slot ('relative to what?') that generators and tests can reference.", [this]()
    {
    ShowTypeMenu("Ctx.Add", plAiEqsContext::GetStaticRTTI(), "Search contexts", [](const plRTTI*) -> plString
      { return "Contexts"; },
      [this](const plRTTI* pType)
      { m_pWindow->AddContextSlotOfType(pType); }); },
    true);

  const float fHeight = flow.GetBottom() + 3.0f;
  Place(pRow, plRectFloat(0, fTop, fWidth, fHeight));

  auto* pLine = Panel(pRoot);
  Place(pLine, plRectFloat(0, fTop + fHeight, fWidth, 1.0f));
  pLine->m_Color = Line();

  return fHeight + 1.0f;
}

void plAiEqsQueryView::BuildGenerator(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiEqsQueryAssetObject* pProps = m_pWindow->GetEqsDocument()->GetProperties();
  const plAiEqsGenerator* pGenerator = pProps->m_pGenerator;

  plUiPanel* pCard = Card(pBody, plRectFloat(0, 0, fWidth, 10.0f));
  pCard->m_Color = Plate();

  auto* pEdge = Panel(pCard);
  pEdge->m_vAnchorMax.x = 0;
  pEdge->m_vOffsets = plVec4(0, 0, 3, 0);
  pEdge->m_Color = AiColor();
  pEdge->m_vCornerRadius = plVec4(CardRadius, 0, 0, CardRadius);

  // clicking the card selects the generator for Raw Properties
  plUiButton* pHit = AddClickArea(pCard, plRectFloat(0, 0, fWidth, 10.0f), "Gen.Card", {}, [this]()
    { m_pWindow->SelectGenerator(); });
  Fill(pHit);

  plAiFlowLayout flow(10.0f, 6.0f, fWidth - 10.0f, ControlHeight, 8.0f);

  const plString sName = pGenerator != nullptr ? PrettyName(pGenerator->GetDynamicRTTI()) : plString("<no generator>");
  Label(pCard, flow.Take(TextWidth(sName, FontSize), 20.0f), sName, Foreground(), FontSize);

  const plStringView sPayload = plAiEqsUi::GetPayloadLabel(pGenerator);
  if (!sPayload.IsEmpty())
  {
    const plRectFloat badge = flow.Take(BadgeWidth(sPayload), BadgeHeight);
    Badge(pCard, badge.x, badge.y, sPayload, AiColor());
  }

  // Around, Radius and Change... sit at the right end
  const plString sAround = pGenerator != nullptr ? plString(pGenerator->GetCenterContext()) : plString("Querier");
  const float fAround = TextWidth(sAround) + 34.0f;
  const float fChange = TextWidth("Change...") + 20.0f;
  const float fArrow = TextWidth("\xE2\x86\x92");
  flow.AlignRight(flow.GetWidth({TextWidth("Around"), fAround, TextWidth("Radius"), 64.0f, fArrow, 74.0f, fChange}));

  Label(pCard, flow.Take(TextWidth("Around")), "Around", Dim());
  AddDropdown(pCard, flow.Take(fAround), "Gen.Around", sAround, "Which context the generator centers on.", [this]()
    { ShowAroundMenu(); },
    pGenerator != nullptr);

  Label(pCard, flow.Take(TextWidth("Radius")), "Radius", Dim());

  auto radiusField = [&](plStringView sField, float fValue, plStringView sUnit, float fFieldWidth, plStringView sProperty, plStringView sDescription, double fMin, double fMax)
  {
    const plRectFloat rect = flow.Take(fFieldWidth);

    if (pGenerator == nullptr)
    {
      // no generator: a read-only well
      auto* pWell = Panel(pCard);
      Place(pWell, rect);
      pWell->m_Color = Field();
      pWell->m_fBorderWidth = 1.0f;
      pWell->m_BorderColor = Line();
      pWell->m_vCornerRadius = plVec4(4);

      plUiText* pText = Label(pWell, plRectFloat(6.0f, 0, rect.width - 12.0f, rect.height), FormatNumber(fValue, 1), Dim(), FontSize - 2, true);
      pText->m_HAlign = plUiTextHAlign::Right;
      return;
    }

    const plString sPropertyName = sProperty;
    const plString sUndo = sDescription;
    AddNumberField(pCard, rect, sField, FormatNumber(fValue, 1), sUnit, {}, [this, sPropertyName, sUndo, fMin, fMax](const plString& sText)
      {
      double fNew = 0.0;
      if (ParseClamped(sText, fMin, fMax, fNew))
        m_pWindow->SetGeneratorValue(sPropertyName.GetData(), plVariant(static_cast<float>(fNew)), sUndo.GetData());
      else
        Invalidate(); });
  };

  radiusField("Gen.RadiusMin", pGenerator != nullptr ? pGenerator->m_fRadiusMin : 0.0f, {}, 64.0f, "RadiusMin", "Change Radius Min", 0.0, 200.0);
  Label(pCard, flow.Take(fArrow), "\xE2\x86\x92", Dim());
  radiusField("Gen.RadiusMax", pGenerator != nullptr ? pGenerator->m_fRadiusMax : 0.5f, "m", 74.0f, "RadiusMax", "Change Radius Max", 0.5, 500.0);

  AddTextButton(pCard, flow.Take(fChange), "Gen.Change", "Change...", "Replace the generator with a different type.", [this]()
    {
    ShowTypeMenu("Gen.Change", plAiEqsGenerator::GetStaticRTTI(), "Search generators", [](const plRTTI*) -> plString
      { return "Generators"; },
      [this](const plRTTI* pType)
      { m_pWindow->ChangeGeneratorTypeTo(pType); }); });

  // the summary under the controls
  const float y = flow.GetBottom() + 6.0f;

  plStringBuilder sSummary;
  plAiEqsUi::GetGeneratorSummary(pGenerator, sSummary);
  Label(pCard, plRectFloat(10.0f, y, fWidth - 20.0f, s_fSummaryLine), sSummary, Dim(), FontSize - 2, true);

  pCard->m_vOffsets.w = y + s_fSummaryLine + 8.0f;
}

void plAiEqsQueryView::BuildTestsSection(plUiElement* pBody)
{
  using namespace plAiEngineUi;

  const plAiEqsQueryAssetObject* pProps = m_pWindow->GetEqsDocument()->GetProperties();
  const float fAdd = TextWidth("Add Test") + 38.0f;

  plUiPanel* pSection = Section(pBody, "Tests", AiColor(), FormatNumber(pProps->m_Tests.GetCount(), 0), "run in order - cheap before expensive, hard filters discard", fAdd + 8.0f);

  plUiButton* pAdd = AddTextButton(pSection, plRectFloat(0, 1.0f, fAdd, plEditorUi::ControlHeight), "Tests.Add", "Add Test", {}, [this]()
    {
    ShowTypeMenu("Tests.Add", plAiEqsTest::GetStaticRTTI(), "Search tests", [](const plRTTI* pType) -> plString
      { return plAiEqsUi::GetTestGroup(pType); },
      [this](const plRTTI* pType)
      { m_pWindow->AddTestOfType(pType); }); },
    true);

  pAdd->m_vAnchorMin.x = pAdd->m_vAnchorMax.x = 1.0f;
  pAdd->m_vOffsets.x = -fAdd - 4.0f;
}

namespace
{
  /// A test card's summary: the configuration, the filter rule, the validation issue and the legacy note.
  void GetTestLines(const plAiEqsQueryAssetObject& props, plUInt32 uiIndex, const plAiEqsTestObject* pNative, plHybridArray<plString, 4>& out_lines)
  {
    const plAiEqsTest* pTest = pNative != nullptr ? pNative->m_pTest : nullptr;

    plStringBuilder sLine;
    plAiEqsUi::GetTestSummary(pTest, sLine);
    out_lines.PushBack(sLine);

    if (pTest != nullptr && pTest->m_Purpose != plAiEqsTestPurpose::ScoreOnly)
    {
      plStringBuilder sRule;
      plAiEqsUi::GetFilterRule(pTest, sRule);
      sLine.SetFormat(pTest->m_bInvertFilter ? "Require NOT ({})" : "Require {}", sRule);
      out_lines.PushBack(sLine);
    }

    if (pTest != nullptr && pTest->m_Purpose != plAiEqsTestPurpose::FilterOnly && pTest->m_bInvertScore)
    {
      sLine.Set(out_lines.PeekBack(), "; score = 1 - curve(raw)");
      out_lines.PeekBack() = sLine;
    }

    const plString sIssue = props.ValidateTest(uiIndex);
    if (!sIssue.IsEmpty())
      out_lines.PushBack(sIssue);

    if (pNative != nullptr && pNative->m_bLegacyCurveDomain)
      out_lines.PushBack("Legacy curve domain (see Properties to migrate)");
  }
} // namespace

void plAiEqsQueryView::BuildTestCard(plUiElement* pBody, float fWidth, plUInt32 uiIndex, plUInt32 uiCount, const plUuid& guid, const plAiEqsTestObject* pNative)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiEqsTest* pTest = pNative != nullptr ? pNative->m_pTest : nullptr;
  const bool bSelected = guid == m_pWindow->GetSelectedObject();

  plHybridArray<plString, 4> lines;
  GetTestLines(*m_pWindow->GetEqsDocument()->GetProperties(), uiIndex, pNative, lines);

  plStringBuilder sTooltip;
  for (const plString& sLine : lines)
    sTooltip.AppendWithSeparator("\n", sLine);

  // the card body selects the test; its tooltip is the whole summary
  plUiPanel* pCard = Card(pBody, plRectFloat(0, 0, fWidth, 10.0f), bSelected ? CardState::Selected : CardState::Normal);

  plUiButton* pHit = AddClickArea(pCard, plRectFloat(0, 0, fWidth, 10.0f), Name("T", uiIndex, ".Card"), sTooltip, [this, guid]()
    { m_pWindow->SelectObject(guid); });
  Fill(pHit);

  // line 1: identity and chips, then weight, curve and the order buttons at the right end
  plAiFlowLayout flow(s_fCardPadding, 6.0f, fWidth - s_fCardPadding, s_fCurveHeight, 8.0f);
  BuildTestIdentity(pCard, flow, pTest);
  BuildTestTools(pCard, flow, uiIndex, uiCount, guid, pNative, bSelected);

  // the summary lines
  float y = flow.GetBottom() + 6.0f;
  for (const plString& sLine : lines)
  {
    Label(pCard, plRectFloat(s_fCardPadding, y, fWidth - 2.0f * s_fCardPadding, s_fSummaryLine), sLine, Muted(), FontSize - 2, true);
    y += s_fSummaryLine;
  }

  // line 2: the filter condition and its bounds, the inversions and the missing-data policy
  if (pTest != nullptr)
    y = BuildTestFilters(pCard, fWidth, y + 6.0f, uiIndex, guid, pTest);

  pCard->m_vOffsets.w = y + 6.0f + 1.0f;
}

void plAiEqsQueryView::BuildTestIdentity(plUiElement* pCard, plAiFlowLayout& ref_flow, const plAiEqsTest* pTest)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plString sName = PrettyName(pTest != nullptr ? pTest->GetDynamicRTTI() : nullptr);
  Label(pCard, ref_flow.Take(plMath::Max(96.0f, TextWidth(sName, FontSize)), 20.0f), sName, Foreground(), FontSize);

  auto addBadge = [&](plStringView sText, const plColorGammaUB& color)
  {
    if (sText.IsEmpty())
      return;

    const plRectFloat badge = ref_flow.Take(BadgeWidth(sText), BadgeHeight);
    Badge(pCard, badge.x, badge.y, sText, color);
  };

  // the context it measures against, what it does and what it costs
  if (const char* szContext = plAiEqsUi::GetTestContext(pTest))
  {
    plStringBuilder sUpper(szContext);
    sUpper.ToUpper();
    addBadge(sUpper, plColorScheme::GetCategoryColor("Utilities", plColorScheme::CategoryColorUsage::BorderColor));
  }

  if (pTest != nullptr)
  {
    addBadge(plAiEqsUi::GetPurposeLabel(pTest), AiColor());

    const plStringView sCost = plAiEqsUi::GetCostLabel(pTest);
    addBadge(sCost, plColorScheme::GetCategoryColor(sCost == "CHEAP" ? "Utilities" : "Lighting", plColorScheme::CategoryColorUsage::BorderColor));
  }
}

void plAiEqsQueryView::BuildTestTools(plUiElement* pCard, plAiFlowLayout& ref_flow, plUInt32 uiIndex, plUInt32 uiCount, const plUuid& guid, const plAiEqsTestObject* pNative, bool bSelected)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiEqsTest* pTest = pNative != nullptr ? pNative->m_pTest : nullptr;
  const bool bScores = pTest == nullptr || pTest->m_Purpose != plAiEqsTestPurpose::FilterOnly;
  const bool bLegacy = pNative != nullptr && pNative->m_bLegacyCurveDomain;

  const float fWeightLabel = TextWidth("wt");
  const float fWeightGroup = fWeightLabel + 5.0f + 56.0f;
  const float fButtons = ref_flow.GetWidth({SmallButtonSize, SmallButtonSize, SmallButtonSize});
  ref_flow.AlignRight(bScores ? ref_flow.GetWidth({fWeightGroup, s_fCurveWidth, fButtons}) : ref_flow.GetWidth({s_fCurveWidth, fButtons}));

  // the weight of scoring tests
  if (bScores && pTest != nullptr)
  {
    const plRectFloat weight = ref_flow.Take(fWeightGroup);
    Label(pCard, plRectFloat(weight.x, weight.y, fWeightLabel, weight.height), "wt", Dim());
    AddNumberField(pCard, plRectFloat(weight.x + fWeightLabel + 5.0f, weight.y, 56.0f, weight.height), Name("T", uiIndex, ".Weight"), FormatTrimmed(pTest->m_fWeight, 2), {}, "Weight of this test in the weight-normalized score sum.", [this, guid](const plString& sText)
      {
      double fValue = 0.0;
      if (ParseClamped(sText, 0.0, 10.0, fValue))
        m_pWindow->SetTestWeight(guid, fValue);
      else
        Invalidate(); });
  }
  else if (bScores)
  {
    ref_flow.Take(fWeightGroup);
  }

  // the response curve of scoring tests; hard filters judge the raw score
  {
    const plRectFloat curve = ref_flow.Take(s_fCurveWidth, s_fCurveHeight);
    const plString sCurve = Name("T", uiIndex, ".Curve");
    AddDrawList(pCard, curve, sCurve);

    plUiDrawListWriter writer;
    const plRectFloat local(0, 0, curve.width, curve.height);

    if (bScores)
    {
      plCurve1D preview;
      if (pNative != nullptr)
        plAiAssetUi::BuildRuntimeCurve(pNative->m_ScoreCurve, preview);

      DrawCurve(writer, local, &preview, bLegacy, bSelected);

      AddClickArea(
        pCard, curve, Name("T", uiIndex, ".CurveHit"), bLegacy ? s_szLegacyCurveTooltip : s_szCurveTooltip, [this, guid]()
        {
        m_pWindow->SelectObject(guid);
        Defer([this, guid]()
          { m_pWindow->OpenCurveEditor(guid); }); },
        [this, guid, uiIndex]()
        { ShowCurveMenu(Name("T", uiIndex, ".CurveHit"), guid); });
    }
    else
    {
      DrawNoCurve(writer, local);
      AddClickArea(pCard, curve, Name("T", uiIndex, ".CurveHit"), "Hard filters judge the raw score - no response curve.", {});
    }

    Publish(GetBoard(), writer, sCurve);
  }

  // order and remove
  AddIconButton(pCard, ref_flow.Take(SmallButtonSize, SmallButtonSize), Name("T", uiIndex, ".Up"), Icon::MoveUp, "Move up (cheap tests always run before expensive ones)", [this, guid]()
    { m_pWindow->MoveTest(guid, -1); },
    uiIndex > 0);

  AddIconButton(pCard, ref_flow.Take(SmallButtonSize, SmallButtonSize), Name("T", uiIndex, ".Down"), Icon::MoveDown, "Move down", [this, guid]()
    { m_pWindow->MoveTest(guid, +1); },
    uiIndex + 1 < uiCount);

  AddIconButton(pCard, ref_flow.Take(SmallButtonSize, SmallButtonSize), Name("T", uiIndex, ".Remove"), Icon::Clear, "Remove this test", [this, guid]()
    { m_pWindow->RemoveTest(guid); });
}

float plAiEqsQueryView::BuildTestFilters(plUiElement* pCard, float fWidth, float fTop, plUInt32 uiIndex, const plUuid& guid, const plAiEqsTest* pTest)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plAiFlowLayout line(s_fCardPadding, fTop, fWidth - s_fCardPadding, ChoiceHeight, 8.0f);

  // filtering tests: the condition, its bounds and the inversion
  if (pTest->m_Purpose != plAiEqsTestPurpose::ScoreOnly)
  {
    const plInt32 iCondition = pTest->m_FilterCondition.GetValue();
    AddDropdown(pCard, line.Take(200.0f, ChoiceHeight), Name("T", uiIndex, ".Condition"), s_szConditions[plMath::Clamp<plInt32>(iCondition, 0, PL_ARRAY_SIZE(s_szConditions) - 1)],
      "Numeric filters use native units: distance/path in meters, direction in degrees, facing dot product, or boolean 0/1. Reachable requires Path Length; Direct path requires Reachable Approx.",
      [this, guid, uiIndex]()
      { ShowConditionMenu(Name("T", uiIndex, ".Condition"), guid); },
      true, true);

    auto boundField = [&](plStringView sLabel, plStringView sSuffix, float fValue, const char* szProperty)
    {
      const float fLabel = TextWidth(sLabel);
      const plRectFloat bound = line.Take(fLabel + 5.0f + 64.0f, ChoiceHeight);
      Label(pCard, plRectFloat(bound.x, bound.y, fLabel, bound.height), sLabel, Dim());
      AddNumberField(pCard, plRectFloat(bound.x + fLabel + 5.0f, bound.y, 64.0f, bound.height), Name("T", uiIndex, sSuffix), FormatTrimmed(fValue, 3), {}, {}, [this, guid, szProperty](const plString& sText)
        {
        double fNew = 0.0;
        if (ParseClamped(sText, -1000000.0, 1000000.0, fNew))
          m_pWindow->SetTestProperty(guid, szProperty, plVariant(static_cast<float>(fNew)));
        else
          Invalidate(); });
    };

    if (iCondition == plAiEqsFilterCondition::AtLeast || iCondition == plAiEqsFilterCondition::Between)
      boundField("Min", ".Min", pTest->m_fFilterMin, "FilterMin");

    if (iCondition == plAiEqsFilterCondition::AtMost || iCondition == plAiEqsFilterCondition::Between)
      boundField("Max", ".Max", pTest->m_fFilterMax, "FilterMax");

    const bool bInvertFilter = pTest->m_bInvertFilter;
    AddCheckBox(pCard, line.Take(CheckBoxSize + 6.0f + TextWidth("Invert filter"), ChoiceHeight), Name("T", uiIndex, ".InvertFilter"), "Invert filter", bInvertFilter, "Negate a valid filter decision. Missing-data handling is independent.", [this, guid, bInvertFilter]()
      { m_pWindow->SetTestProperty(guid, "InvertFilter", plVariant(!bInvertFilter)); },
      true, Dim());
  }

  // scoring tests: the score inversion
  if (pTest->m_Purpose != plAiEqsTestPurpose::FilterOnly)
  {
    const bool bInvertScore = pTest->m_bInvertScore;
    AddCheckBox(pCard, line.Take(CheckBoxSize + 6.0f + TextWidth("Invert score"), ChoiceHeight), Name("T", uiIndex, ".InvertScore"), "Invert score", bInvertScore, "Use 1 - curve(raw). LOS PreferVisible sets the base result before inversion.", [this, guid, bInvertScore]()
      { m_pWindow->SetTestProperty(guid, "InvertScore", plVariant(!bInvertScore)); },
      true, Dim());
  }

  // the missing-data policy at the right end
  const float fMissingLabel = TextWidth("Missing data");
  line.AlignRight(line.GetWidth({fMissingLabel, 150.0f}));
  Label(pCard, line.Take(fMissingLabel, ChoiceHeight), "Missing data", Dim());

  const plInt32 iPolicy = plMath::Clamp<plInt32>(pTest->m_MissingDataPolicy.GetValue(), 0, PL_ARRAY_SIZE(s_szMissingData) - 1);
  AddDropdown(pCard, line.Take(150.0f, ChoiceHeight), Name("T", uiIndex, ".Missing"), s_szMissingData[iPolicy], {}, [this, guid, uiIndex]()
    { ShowMissingDataMenu(Name("T", uiIndex, ".Missing"), guid); },
    true, true);

  return line.GetBottom();
}

void plAiEqsQueryView::BuildStatus(plUiElement* pRoot)
{
  const plAiEqsQueryAssetObject* pProps = m_pWindow->GetEqsDocument()->GetProperties();

  plEditorUi::StatusRow(pRoot, "Eqs.StatusLeft", "Eqs.StatusRight");

  plStringBuilder sLeft;
  sLeft.SetFormat("{} tests \xC2\xB7 {} contexts", pProps->m_Tests.GetCount(), pProps->m_ContextSlots.GetCount() + 1);
  GetBoard().SetEntryValue("Eqs.StatusLeft", plString(sLeft));

  // the selected test's type
  plString sRight;
  plDynamicArray<plUuid> guids;
  m_pWindow->GetArrayGuids("Tests", guids);

  for (plUInt32 i = 0; i < guids.GetCount() && i < pProps->m_Tests.GetCount(); ++i)
  {
    if (guids[i] == m_pWindow->GetSelectedObject() && pProps->m_Tests[i] != nullptr && pProps->m_Tests[i]->m_pTest != nullptr)
      sRight = PrettyName(pProps->m_Tests[i]->m_pTest->GetDynamicRTTI());
  }

  GetBoard().SetEntryValue("Eqs.StatusRight", sRight);
}

void plAiEqsQueryView::ShowRunModeMenu()
{
  const plInt32 iCurrent = m_pWindow->GetEqsDocument()->GetProperties()->m_RunMode.GetValue();

  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_szRunModes); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(s_szRunModes[i], i);
    item.m_bCheckable = true;
    item.m_bChecked = s_RunModeValues[i] == iCurrent;
  }

  ShowMenu("RunMode", menu, [this](plUInt32 uiId)
    { m_pWindow->SetRootValue("RunMode", plVariant(s_RunModeValues[uiId]), "Change Run Mode"); });
}

void plAiEqsQueryView::ShowNavmeshMenu()
{
  const plString& sCurrent = m_pWindow->GetEqsDocument()->GetProperties()->m_sNavmeshConfig;

  // navmesh configs from the project settings (dynamic string enum, filled on project open)
  m_MenuChoices.Clear();
  m_MenuChoices.PushBack(plString());

  for (const plString& sValue : plDynamicStringEnum::CreateDynamicEnum("AiNavmeshConfig").GetAllValidValues())
    m_MenuChoices.PushBack(sValue);

  if (!sCurrent.IsEmpty() && !m_MenuChoices.Contains(sCurrent))
    m_MenuChoices.PushBack(sCurrent);

  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < m_MenuChoices.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(m_MenuChoices[i].IsEmpty() ? plStringView("<default>") : m_MenuChoices[i].GetView(), i);
    item.m_bCheckable = true;
    item.m_bChecked = m_MenuChoices[i] == sCurrent;
  }

  ShowMenu("Navmesh", menu, [this](plUInt32 uiId)
    {
    if (uiId < m_MenuChoices.GetCount())
      m_pWindow->SetRootValue("NavmeshConfig", plVariant(m_MenuChoices[uiId].GetData()), "Change Navmesh Config"); });
}

void plAiEqsQueryView::ShowAroundMenu()
{
  const plAiEqsGenerator* pGenerator = m_pWindow->GetEqsDocument()->GetProperties()->m_pGenerator;
  if (pGenerator == nullptr)
    return;

  // Querier, every slot name, and the current value when no slot carries it
  m_MenuChoices.Clear();
  m_MenuChoices.PushBack("Querier");

  plDynamicArray<plString> names;
  m_pWindow->GetContextSlotNames(names);

  for (const plString& sName : names)
  {
    if (sName != "Querier")
      m_MenuChoices.PushBack(sName);
  }

  const plString sCurrent = pGenerator->GetCenterContext();
  if (!sCurrent.IsEmpty() && !m_MenuChoices.Contains(sCurrent))
    m_MenuChoices.PushBack(sCurrent);

  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < m_MenuChoices.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(m_MenuChoices[i], i);
    item.m_bCheckable = true;
    item.m_bChecked = m_MenuChoices[i] == sCurrent;
  }

  ShowMenu("Gen.Around", menu, [this](plUInt32 uiId)
    {
    if (uiId < m_MenuChoices.GetCount())
      m_pWindow->SetGeneratorValue("CenterContext", plVariant(m_MenuChoices[uiId].GetData()), "Change Generator Center"); });
}

void plAiEqsQueryView::ShowConditionMenu(plStringView sAnchor, const plUuid& guid)
{
  const plAiEqsTestObject* pNative = nullptr;
  {
    const plAiEqsQueryAssetObject* pProps = m_pWindow->GetEqsDocument()->GetProperties();
    plDynamicArray<plUuid> guids;
    m_pWindow->GetArrayGuids("Tests", guids);

    for (plUInt32 i = 0; i < guids.GetCount() && i < pProps->m_Tests.GetCount(); ++i)
    {
      if (guids[i] == guid)
        pNative = pProps->m_Tests[i];
    }
  }

  if (pNative == nullptr || pNative->m_pTest == nullptr)
    return;

  const plAiEqsTest* pTest = pNative->m_pTest;

  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_szConditions); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(s_szConditions[i], i);
    item.m_bCheckable = true;
    item.m_bChecked = static_cast<plInt32>(i) == pTest->m_FilterCondition.GetValue();

    // Reachable needs Path Length, Direct path needs Reachable Approx
    if (i == plAiEqsFilterCondition::Reachable)
      item.m_bEnabled = plDynamicCast<const plAiEqsTest_PathLength*>(pTest) != nullptr;
    else if (i == plAiEqsFilterCondition::DirectPath)
      item.m_bEnabled = plDynamicCast<const plAiEqsTest_ReachableApprox*>(pTest) != nullptr;
  }

  ShowMenu(sAnchor, menu, [this, guid](plUInt32 uiId)
    { m_pWindow->SetTestProperty(guid, "FilterCondition", plVariant(static_cast<plInt64>(uiId))); });
}

void plAiEqsQueryView::ShowMissingDataMenu(plStringView sAnchor, const plUuid& guid)
{
  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_szMissingData); ++i)
    menu.AddAction(s_szMissingData[i], i);

  ShowMenu(sAnchor, menu, [this, guid](plUInt32 uiId)
    { m_pWindow->SetTestProperty(guid, "MissingDataPolicy", plVariant(static_cast<plInt64>(uiId))); });
}

void plAiEqsQueryView::ShowCurveMenu(plStringView sAnchor, const plUuid& guid)
{
  plEditorUiMenu menu;

  plEditorUiMenuItem& presets = menu.AddSubmenu("Presets");
  for (plUInt32 i = 0; i < plAiEqsUi::GetCurvePresetCount(); ++i)
  {
    plEditorUiMenuItem& item = presets.m_Children.ExpandAndGetRef();
    item.m_sLabel = plAiEqsUi::GetCurvePresetName(i);
    item.m_uiId = i;
  }

  plEditorUiMenuItem& edit = menu.AddAction("Edit Curve...", s_uiEditCurveId);
  edit.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Curve);

  ShowMenu(sAnchor, menu, [this, guid](plUInt32 uiId)
    {
    if (uiId == s_uiEditCurveId)
      m_pWindow->OpenCurveEditor(guid);
    else
      m_pWindow->ApplyCurvePreset(guid, uiId); });
}

void plAiEqsQueryView::ShowTypeMenu(plStringView sAnchor, const plRTTI* pBaseType, plStringView sPlaceholder, std::function<plString(const plRTTI*)> getCategory, std::function<void(const plRTTI*)> onPick)
{
  m_MenuTypes.Clear();
  plDynamicArray<plEditorUiSearchMenuItem> items;

  plRTTI::ForEachDerivedType(
    pBaseType,
    [&](const plRTTI* pRtti)
    {
      if (pRtti == pBaseType)
        return;

      plEditorUiSearchMenuItem& item = items.ExpandAndGetRef();
      item.m_sLabel = PrettyName(pRtti);
      item.m_sCategory = getCategory(pRtti);
      item.m_uiId = m_MenuTypes.GetCount();
      m_MenuTypes.PushBack(pRtti);
    },
    plRTTI::ForEachOptions::ExcludeNotConcrete);

  ShowSearchMenu(sAnchor, items, sPlaceholder, [this, onPick](plUInt32 uiId)
    {
    if (uiId < m_MenuTypes.GetCount())
      onPick(m_MenuTypes[uiId]); });
}

//////////////////////////////////////////////////////////////////////////
// plAiEqsPreviewView

namespace
{
  constexpr float s_fSituationHeight = 88.0f;
  constexpr float s_fLegendHeight = 25.0f;
  constexpr float s_fBreakdownRow = 20.0f;
  constexpr float s_fStatsLine = 15.0f;
  constexpr plUInt32 s_uiStatsLines = 3;
  constexpr float s_fHandleSlack = 10.0f;
  constexpr float s_fCandidateSlack = 7.0f;
} // namespace

plAiEqsPreviewView::plAiEqsPreviewView(plAiEqsQueryAssetDocumentWindow* pWindow)
  : plAiEngineView("AiEqsQueryPreview")
  , m_pWindow(pWindow)
{
}

plAiEqsPreviewView::~plAiEqsPreviewView() = default;

plUInt32 plAiEqsPreviewView::GetTestRowCount() const
{
  plUInt32 uiRows = 0;
  for (const plAiEqsTestObject* pTestObj : m_pWindow->GetEqsDocument()->GetProperties()->m_Tests)
  {
    if (pTestObj != nullptr && pTestObj->m_pTest != nullptr)
      ++uiRows;
  }

  return uiRows;
}

void plAiEqsPreviewView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const float fWidth = vSize.x;
  m_uiBreakdownRows = GetTestRowCount();

  const float fBreakdown = 6.0f + plMath::Max<plUInt32>(m_uiBreakdownRows, 1) * s_fBreakdownRow + 24.0f + s_uiStatsLines * s_fStatsLine + 10.0f;
  const float fFixed = 3.0f * SectionHeight + s_fSituationHeight + s_fLegendHeight + fBreakdown;
  const float fScatter = plMath::Clamp(vSize.y - fFixed, 240.0f, plMath::Max(240.0f, fWidth));

  plUiScrollView* pBody = AddScrollBody(pRoot, plRectFloat(0, 0, fWidth, vSize.y), "Prev.Body", 0.0f, 0.0f);

  // the simulated situation
  Section(pBody, "Simulated situation", AiColor());
  {
    plUiPanel* pSituation = Panel(pBody);
    pSituation->m_vOffsets.w = s_fSituationHeight;
    pSituation->m_Color = plColorGammaUB(0, 0, 0, 0);

    const float fSlider = plMath::Max(fWidth - 10.0f - 100.0f - 8.0f - 54.0f - 8.0f - 10.0f, 40.0f);
    Label(pSituation, plRectFloat(10.0f, 6.0f, 100.0f, ControlHeight), "Threat distance", Muted());
    AddSlider(pSituation, plRectFloat(118.0f, 6.0f, fSlider, ControlHeight), "Prev.Distance", 1.0f, 25.0f, 9.2f, {}, [this](float fValue)
      { SetThreatDistance(fValue); });

    plUiText* pValue = Label(pSituation, plRectFloat(fWidth - 10.0f - 54.0f, 6.0f, 54.0f, ControlHeight), {}, Muted(), FontSize - 2, true, "Prev.DistanceText");
    pValue->m_HAlign = plUiTextHAlign::Right;

    AddCheckBox(pSituation, plRectFloat(10.0f, 36.0f, 220.0f, 20.0f), "Prev.Drag", "drag querier / threat", m_bAllowDrag, "Drag the markers and the occluder endpoints directly in the canvas.", [this]()
      {
      m_bAllowDrag = !m_bAllowDrag;
      SetChecked("Prev.Drag", m_bAllowDrag); });

    AddCheckBox(pSituation, plRectFloat(10.0f, 62.0f, 220.0f, 20.0f), "Prev.SceneSim", "use scene simulation", false, "Scene simulation is not available yet: enable AI.EQS.VisualizeQueries and use a plAiEqsQueryTestComponent for now", {}, false);
  }

  // the candidates, top-down
  Section(pBody, "Candidates - top-down", AiColor());
  {
    plUiPanel* pScatter = Panel(pBody);
    pScatter->m_vOffsets.w = fScatter;
    pScatter->m_Color = Background();

    m_ScatterRect = plRectFloat(0, 0, fWidth, fScatter);
    plUiDrawList* pDrawing = AddDrawList(pScatter, m_ScatterRect, "Prev.Scatter");
    Fill(pDrawing);

    // takes the pointer, so presses carry the scatter's place on the surface
    plUiButton* pHit = AddClickArea(pScatter, m_ScatterRect, "Prev.ScatterHit", {}, {});
    Fill(pHit);
  }

  {
    plUiPanel* pLegend = Panel(pBody);
    pLegend->m_vOffsets.w = s_fLegendHeight;
    pLegend->m_Color = plColorGammaUB(0, 0, 0, 0);

    plUiDrawList* pDrawing = AddDrawList(pLegend, plRectFloat(0, 0, fWidth, s_fLegendHeight), "Prev.Legend");
    Fill(pDrawing);
  }

  // the selected candidate's breakdown; each row carries its trace as a tooltip
  Section(pBody, "Selected candidate - score breakdown", AiColor());
  {
    plUiPanel* pBreakdown = Panel(pBody);
    pBreakdown->m_vOffsets.w = fBreakdown;
    pBreakdown->m_Color = plColorGammaUB(0, 0, 0, 0);

    plUiDrawList* pDrawing = AddDrawList(pBreakdown, plRectFloat(0, 0, fWidth, fBreakdown), "Prev.Breakdown");
    Fill(pDrawing);

    for (plUInt32 i = 0; i < m_uiBreakdownRows; ++i)
      AddClickArea(pBreakdown, plRectFloat(10.0f, 6.0f + i * s_fBreakdownRow, fWidth - 20.0f, s_fBreakdownRow - 4.0f), Name("Prev.Row.", i, ""), {}, {});
  }

  DrawLegend();
  Recompute();
}

void plAiEqsPreviewView::Recompute()
{
  if (!IsReady())
    return;

  m_Simulation.Run(*m_pWindow->GetEqsDocument()->GetProperties());

  const plInt32 iCount = static_cast<plInt32>(m_Simulation.m_Candidates.GetCount());
  if (m_iSelected < 0 || m_iSelected >= iCount || m_Simulation.m_Candidates[m_iSelected].m_bDiscarded)
    m_iSelected = m_Simulation.m_iWinner;

  UpdateThreatDistance();
  DrawScatter();
  DrawBreakdown();
}

void plAiEqsPreviewView::UpdateThreatDistance()
{
  const float fDistance = (m_Simulation.m_vThreat - m_Simulation.m_vQuerier).GetLength();
  SetSliderValue("Prev.Distance", plMath::Clamp(fDistance, 1.0f, 25.0f));

  plStringBuilder sText;
  sText.SetFormat("{} m", plArgF(fDistance, 1));
  GetBoard().SetEntryValue("Prev.DistanceText", plString(sText));
}

void plAiEqsPreviewView::SetThreatDistance(float fDistance)
{
  // the threat moves along its current direction from the querier
  plVec2 vDir = m_Simulation.m_vThreat - m_Simulation.m_vQuerier;
  if (vDir.NormalizeIfNotZero(plVec2(1, 0)).Failed())
    vDir = plVec2(1, 0);

  m_Simulation.m_vThreat = m_Simulation.m_vQuerier + vDir * fDistance;
  Recompute();
}

plVec2 plAiEqsPreviewView::ToScreen(const plVec2& vMeters) const
{
  // a 15 m half extent, Y up
  const float fScale = plMath::Min(m_ScatterRect.width, m_ScatterRect.height) / 30.0f;
  return plVec2(m_ScatterRect.width * 0.5f + vMeters.x * fScale, m_ScatterRect.height * 0.5f - vMeters.y * fScale);
}

plVec2 plAiEqsPreviewView::ToMeters(const plVec2& vLocal) const
{
  const float fScale = plMath::Max(plMath::Min(m_ScatterRect.width, m_ScatterRect.height) / 30.0f, 0.001f);
  return plVec2((vLocal.x - m_ScatterRect.width * 0.5f) / fScale, (m_ScatterRect.height * 0.5f - vLocal.y) / fScale);
}

void plAiEqsPreviewView::DrawScatter()
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plUiDrawListWriter writer;
  const float w = m_ScatterRect.width;
  const float h = m_ScatterRect.height;
  const float fScale = plMath::Min(w, h) / 30.0f;

  writer.Rect(plRectFloat(0, 0, w, h), Background());

  // 2 m grid around the centre
  const plColorGammaUB grid = Mix(Background(), Plate(), 0.6f);
  for (float fMeters = -30.0f; fMeters <= 30.0f; fMeters += 2.0f)
  {
    const float x = w * 0.5f + fMeters * fScale;
    const float y = h * 0.5f + fMeters * fScale;

    if (x >= 0.0f && x <= w)
      writer.Line(plVec2(x, 0), plVec2(x, h), grid);
    if (y >= 0.0f && y <= h)
      writer.Line(plVec2(0, y), plVec2(w, y), grid);
  }

  // the occluder: the local stand-in for level geometry blocking line of sight
  {
    const plVec2 a = ToScreen(m_Simulation.m_vOccluderA);
    const plVec2 b = ToScreen(m_Simulation.m_vOccluderB);
    writer.Line(a, b, Muted(), 4.0f);
    writer.Circle(a, 3.0f, Muted());
    writer.Circle(b, 3.0f, Muted());
    writer.Text(plRectFloat(b.x + 6.0f, b.y - 7.0f, 80.0f, 14.0f), "occluder", Dim(), FontSize - 1);
  }

  // candidates: filtered ones as crosses, scored ones on the ramp relative to the best
  const plAiEqsPreviewCandidate* pWinner = m_Simulation.m_iWinner >= 0 ? &m_Simulation.m_Candidates[m_Simulation.m_iWinner] : nullptr;
  const float fBest = pWinner != nullptr ? plMath::Max(pWinner->m_fFinal, 0.0001f) : 1.0f;

  for (const plAiEqsPreviewCandidate& cand : m_Simulation.m_Candidates)
  {
    const plVec2 pos = ToScreen(cand.m_vPosition);

    if (cand.m_bDiscarded)
    {
      writer.Line(pos + plVec2(-3, -3), pos + plVec2(3, 3), Dim(), 1.3f);
      writer.Line(pos + plVec2(3, -3), pos + plVec2(-3, 3), Dim(), 1.3f);
      continue;
    }

    writer.Circle(pos, 4.5f, ScoreRamp(cand.m_fFinal / fBest));
  }

  if (pWinner != nullptr)
  {
    const plVec2 pos = ToScreen(pWinner->m_vPosition);
    writer.Circle(pos, 8.0f, plColorGammaUB(0, 0, 0, 0), GoodColor(), 2.0f);

    const plString sScore = FormatNumber(pWinner->m_fFinal, 2);
    const float fText = CodeWidth(sScore, FontSize - 1);
    writer.Text(plRectFloat(pos.x + 11.0f, pos.y - 14.0f, fText + 2.0f, 14.0f), sScore, Foreground(), FontSize - 1, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
    DrawStar(writer, plVec2(pos.x + 11.0f + fText + 8.0f, pos.y - 7.0f), 5.0f, Foreground());
  }

  if (m_iSelected >= 0 && m_iSelected < static_cast<plInt32>(m_Simulation.m_Candidates.GetCount()))
    writer.Circle(ToScreen(m_Simulation.m_Candidates[m_iSelected].m_vPosition), 6.5f, plColorGammaUB(0, 0, 0, 0), Accent(), 1.5f);

  // the querier and the threat
  {
    const plColorGammaUB blue = Axis(2);
    const plVec2 q = ToScreen(m_Simulation.m_vQuerier);
    writer.Circle(q, 7.0f, plColorGammaUB(0, 0, 0, 0), blue, 2.0f);
    writer.Circle(q, 2.5f, blue);
    writer.Text(plRectFloat(q.x + 10.0f, q.y + 7.0f, 80.0f, 14.0f), "Querier", blue, FontSize - 1);

    const plVec2 t = ToScreen(m_Simulation.m_vThreat);
    const plVec2 triangle[] = {t + plVec2(0, -7), t + plVec2(6, 5), t + plVec2(-6, 5)};
    writer.Triangles(plMakeArrayPtr(triangle), Danger());
    writer.Text(plRectFloat(t.x + 9.0f, t.y - 3.0f, 80.0f, 14.0f), "Threat", Danger(), FontSize - 1);
  }

  Publish(GetBoard(), writer, "Prev.Scatter");
}

void plAiEqsPreviewView::DrawLegend()
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plUiDrawListWriter writer;
  const float y = s_fLegendHeight * 0.5f + 0.5f;
  float x = 10.0f;

  writer.Rect(plRectFloat(0, 0, m_ScatterRect.width, 1.0f), Line());

  auto text = [&](plStringView sText)
  {
    const float fText = TextWidth(sText);
    writer.Text(plRectFloat(x, y - 8.0f, fText, 16.0f), sText, Dim(), FontSize - 1);
    x += fText + 12.0f;
  };

  // winner: a green dot with a ring
  writer.Circle(plVec2(x + 4.0f, y), 4.0f, GoodColor(), Background(), 2.0f);
  writer.Circle(plVec2(x + 4.0f, y), 6.5f, plColorGammaUB(0, 0, 0, 0), GoodColor(), 1.0f);
  x += 14.0f;
  text("winner");

  // score high to low
  writer.Circle(plVec2(x + 4.0f, y), 4.0f, GoodColor());
  writer.Circle(plVec2(x + 9.0f, y), 4.0f, Warning());
  writer.Circle(plVec2(x + 14.0f, y), 4.0f, Danger());
  x += 23.0f;
  text("score high \xE2\x86\x92 low");

  // filtered out
  writer.Line(plVec2(x, y - 4.0f), plVec2(x + 8.0f, y + 4.0f), Dim(), 1.3f);
  writer.Line(plVec2(x + 8.0f, y - 4.0f), plVec2(x, y + 4.0f), Dim(), 1.3f);
  x += 14.0f;
  text("filtered out");

  Publish(GetBoard(), writer, "Prev.Legend");
}

void plAiEqsPreviewView::DrawBreakdown()
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiEqsQueryAssetObject* pProps = m_pWindow->GetEqsDocument()->GetProperties();
  const float fWidth = m_ScatterRect.width;
  const plAiEqsPreviewCandidate* pSelected = (m_iSelected >= 0 && m_iSelected < static_cast<plInt32>(m_Simulation.m_Candidates.GetCount())) ? &m_Simulation.m_Candidates[m_iSelected] : nullptr;

  plUiDrawListWriter writer;
  float y = 6.0f;

  // one row per test: name, bar, state and curved score
  plUInt32 uiRow = 0;
  for (const plAiEqsTestObject* pTestObj : pProps->m_Tests)
  {
    if (pTestObj == nullptr || pTestObj->m_pTest == nullptr || uiRow >= m_uiBreakdownRows)
      continue;

    const plAiEqsTest* pTest = pTestObj->m_pTest;
    const bool bScores = pTest->m_Purpose != plAiEqsTestPurpose::FilterOnly;
    const plString sRowName = Name("Prev.Row.", uiRow, "");

    writer.Text(plRectFloat(10.0f, y, 110.0f, 16.0f), PrettyName(pTest->GetDynamicRTTI()), Muted(), FontSize - 1);

    const plRectFloat bar(128.0f, y + 4.0f, plMath::Max(fWidth - 128.0f - 78.0f - 10.0f, 10.0f), 8.0f);
    writer.Rect(bar, Field(), 4.0f);
    writer.RectOutline(bar, Line(), 1.0f, 4.0f);

    if (pSelected != nullptr && uiRow < pSelected->m_TestTrace.GetCount())
    {
      const plAiEqsTestTrace& trace = pSelected->m_TestTrace[uiRow];
      const float fFill = bScores ? plMath::Saturate(pSelected->m_TestScores[uiRow]) : (trace.m_bEvaluated && trace.m_bPassed && !trace.m_bSkipped ? 1.0f : 0.0f);

      if (fFill > 0.0f)
        writer.Rect(plRectFloat(bar.x + 1.0f, bar.y + 1.0f, (bar.width - 2.0f) * fFill, bar.height - 2.0f), bScores ? Muted() : Dim(), 3.0f);

      plStringBuilder sValue(plAiEqsUi::GetTraceState(trace));
      if (bScores && trace.m_bEvaluated)
        sValue.AppendFormat(" {}", plArgF(trace.m_fCurved, 2));

      const bool bRejected = trace.m_bEvaluated && !trace.m_bPassed && !trace.m_bSkipped;
      writer.Text(plRectFloat(fWidth - 10.0f - 74.0f, y, 74.0f, 16.0f), sValue, bRejected ? Danger() : Muted(), FontSize - 2, plUiTextHAlign::Right, plUiTextVAlign::Center, true);

      plStringBuilder sDetail;
      plAiEqsUi::GetTraceDetail(trace, sDetail);
      SetToolTip(GetBoard(), sRowName, sDetail);
    }
    else
    {
      writer.Text(plRectFloat(fWidth - 10.0f - 74.0f, y, 74.0f, 16.0f), "-", Muted(), FontSize - 2, plUiTextHAlign::Right, plUiTextVAlign::Center, true);
      SetToolTip(GetBoard(), sRowName, {});
    }

    y += s_fBreakdownRow;
    ++uiRow;
  }

  if (uiRow == 0)
  {
    writer.Text(plRectFloat(10.0f, y, fWidth - 20.0f, 16.0f), "add tests to see the per-candidate breakdown here", Dim(), FontSize - 2);
    y += s_fBreakdownRow;
  }

  // the final score
  if (pSelected != nullptr)
  {
    writer.Text(plRectFloat(10.0f, y + 2.0f, 40.0f, 18.0f), "final", Foreground(), FontSize - 1);

    const plString sFinal = FormatNumber(pSelected->m_fFinal, 2);
    const float fFinal = CodeWidth(sFinal, FontSize);
    writer.Text(plRectFloat(50.0f, y + 2.0f, fFinal + 2.0f, 18.0f), sFinal, Foreground(), FontSize, plUiTextHAlign::Left, plUiTextVAlign::Center, true);

    if (pSelected->m_bWinner)
    {
      DrawStar(writer, plVec2(50.0f + fFinal + 14.0f, y + 11.0f), 5.0f, Foreground());
      writer.Text(plRectFloat(50.0f + fFinal + 24.0f, y + 2.0f, 60.0f, 18.0f), "winner", Foreground(), FontSize - 1);
    }
  }
  else
  {
    writer.Text(plRectFloat(10.0f, y + 2.0f, fWidth - 20.0f, 18.0f), "no surviving candidate", Dim(), FontSize - 1);
  }

  y += 24.0f + 4.0f;

  // the stats lines
  plStringBuilder sStats;
  m_Simulation.GetStats(sStats);

  plHybridArray<plStringView, 8> lines;
  sStats.Split(false, lines, "\n");

  for (plUInt32 i = 0; i < lines.GetCount() && i < s_uiStatsLines; ++i)
  {
    writer.Text(plRectFloat(10.0f, y, fWidth - 20.0f, s_fStatsLine), lines[i], Dim(), FontSize - 2, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
    y += s_fStatsLine;
  }

  Publish(GetBoard(), writer, "Prev.Breakdown");
}

void plAiEqsPreviewView::OnViewPointer(const plEditorUiPointerEvent& e)
{
  using Type = plEditorUiPointerEvent::Type;

  switch (e.m_Type)
  {
    case Type::Press:
    {
      if (e.m_Button != plEditorUiPointerEvent::Button::Left || e.m_sElement != "Prev.ScatterHit")
        return;

      m_PressRect = e.m_ElementRect;
      const plVec2 vLocal = e.m_vPosition - plVec2(m_PressRect.x, m_PressRect.y);

      // the markers and the occluder ends first, while dragging is allowed
      if (m_bAllowDrag)
      {
        const plVec2 targets[] = {m_Simulation.m_vQuerier, m_Simulation.m_vThreat, m_Simulation.m_vOccluderA, m_Simulation.m_vOccluderB};
        for (plUInt32 i = 0; i < PL_ARRAY_SIZE(targets); ++i)
        {
          if ((ToScreen(targets[i]) - vLocal).GetLength() < s_fHandleSlack)
          {
            m_iDragTarget = static_cast<plInt32>(i);
            return;
          }
        }
      }

      // otherwise: candidate selection
      for (plUInt32 i = 0; i < m_Simulation.m_Candidates.GetCount(); ++i)
      {
        if (m_Simulation.m_Candidates[i].m_bDiscarded)
          continue;

        if ((ToScreen(m_Simulation.m_Candidates[i].m_vPosition) - vLocal).GetLength() < s_fCandidateSlack)
        {
          m_iSelected = static_cast<plInt32>(i);
          DrawScatter();
          DrawBreakdown();
          return;
        }
      }

      return;
    }

    case Type::Move:
    case Type::DragStart:
    case Type::DragMove:
    {
      if (m_iDragTarget < 0)
        return;

      const plVec2 vMeters = ToMeters(e.m_vPosition - plVec2(m_PressRect.x, m_PressRect.y));
      plVec2* targets[] = {&m_Simulation.m_vQuerier, &m_Simulation.m_vThreat, &m_Simulation.m_vOccluderA, &m_Simulation.m_vOccluderB};
      *targets[m_iDragTarget] = vMeters;

      Recompute();
      return;
    }

    case Type::Release:
    case Type::Drop:
    case Type::DragCancel:
      m_iDragTarget = -1;
      return;

    default:
      return;
  }
}
