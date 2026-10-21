#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiBehaviorAsset.h>
#include <EditorPluginAi/Assets/AiBehaviorAssetWindow.moc.h>
#include <EditorPluginAi/Assets/AiBehaviorViews.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <AiPlugin/UtilityAI/Decision/AiBehaviorLogic.h>
#include <AiPlugin/UtilityAI/Decision/AiInput.h>
#include <Core/Utils/Blackboard.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorUi/Surface.h>
#include <ToolsFoundation/CommandHistory/CommandHistory.h>

namespace
{
  constexpr float s_fCardPadding = 8.0f;
  constexpr float s_fCurveWidth = 64.0f;
  constexpr float s_fCurveHeight = 30.0f;
  constexpr float s_fRangeField = 54.0f;
  constexpr plUInt32 s_uiEditCurveId = 1000;

  const char* const s_szLogicTypes[] = {"<no logic>", "State Machine", "Action Queue"};

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

  plInt32 GetLogicType(const plAiBehaviorAssetObject* pProps)
  {
    if (plDynamicCast<const plAiBehaviorLogic_StateMachine*>(pProps->m_pLogic) != nullptr)
      return 1;
    if (plDynamicCast<const plAiBehaviorLogic_ActionQueue*>(pProps->m_pLogic) != nullptr)
      return 2;
    return 0;
  }

  /// A consideration's output: danger when it vetoes, warning when low, green otherwise.
  plColorGammaUB OutputColor(float fOutput)
  {
    if (fOutput <= 0.0005f)
      return plEditorUi::Danger();
    if (fOutput < 0.5f)
      return plEditorUi::Warning();
    return plAiEngineUi::GoodColor();
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plQtAiBehaviorView

plQtAiBehaviorView::plQtAiBehaviorView(plQtAiBehaviorAssetDocumentWindow* pWindow)
  : plQtAiEngineView("AiBehavior")
  , m_pWindow(pWindow)
{
}

plQtAiBehaviorView::~plQtAiBehaviorView() = default;

bool plQtAiBehaviorView::IsRebuildBlocked() const
{
  return m_pWindow->GetDocument()->GetCommandHistory()->IsInTransaction();
}

void plQtAiBehaviorView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const float fHeader = BuildHeader(pRoot, vSize.x);
  const float fTop = fHeader + BuildLogicRow(pRoot, fHeader, vSize.x);

  plUiScrollView* pBody = AddScrollBody(pRoot, plRectFloat(0, fTop, vSize.x, plMath::Max(vSize.y - fTop, 10.0f)), "Behavior.Body", 10.0f, 8.0f);
  const float fCardWidth = vSize.x - 20.0f;

  const plAiBehaviorAssetObject* pProps = m_pWindow->GetBehaviorDocument()->GetProperties();

  // the section with its add button
  {
    const float fAdd = TextWidth("Add Consideration") + 38.0f;
    plUiPanel* pSection = Section(pBody, "Considerations", AiColor(), FormatNumber(pProps->m_Considerations.GetCount(), 0), "multiplied - any 0 vetoes the behavior", fAdd + 8.0f);

    plUiButton* pAdd = AddTextButton(pSection, plRectFloat(0, 1.0f, fAdd, ControlHeight), "Cons.Add", "Add Consideration", {}, [this]()
      { ShowAddMenu("Cons.Add"); },
      true);
    pAdd->m_vAnchorMin.x = pAdd->m_vAnchorMax.x = 1.0f;
    pAdd->m_vOffsets.x = -fAdd - 4.0f;
  }

  plDynamicArray<plUuid> guids;
  m_pWindow->GetConsiderationGuids(guids);

  const plUInt32 uiCount = plMath::Min(guids.GetCount(), pProps->m_Considerations.GetCount());
  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    BuildConsiderationCard(pBody, fCardWidth, i, uiCount, guids[i], pProps->m_Considerations[i]);
  }

  if (uiCount == 0)
  {
    plUiPanel* pEmpty = Panel(pBody);
    pEmpty->m_vOffsets.w = 150.0f;
    pEmpty->m_Color = plColorGammaUB(0, 0, 0, 0);

    EmptyState(pEmpty, Icon::Curve, "No considerations", "Each consideration scores one input through a response curve.", "Add Consideration", "Cons.EmptyAdd");
    AddAction("Cons.EmptyAdd", {}, [this]()
      { ShowAddMenu("Cons.EmptyAdd"); });
  }
}

float plQtAiBehaviorView::BuildHeader(plUiElement* pRoot, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiBehaviorAssetObject* pProps = m_pWindow->GetBehaviorDocument()->GetProperties();

  plUiPanel* pRow = Panel(pRoot);
  pRow->m_Color = Surface();

  plAiFlowLayout flow(6.0f, 3.0f, fWidth - 6.0f, ControlHeight, 2.0f);

  auto addLabel = [&](plStringView sText)
  {
    plUiText* pLabel = Label(pRow, flow.Take(TextWidth(sText) + 8.0f), sText, Dim());
    pLabel->m_HAlign = plUiTextHAlign::Center;
  };

  // Name
  flow.Group(TextWidth("Name") + 10.0f + 140.0f);
  addLabel("Name");
  AddTextField(pRow, flow.Take(140.0f), "Name", pProps->m_sName, "<asset file name>", "Display name used in debug overlays; falls back to the asset name.", [this](const plString& sText)
    { m_pWindow->SetRootValue("Name", plVariant(sText.GetData()), "Change Behavior Name"); });

  // Category, with its ladder colour
  {
    plStringBuilder sTooltip("Priority category - the dominant part of the final score.\nA behavior in a higher category ALWAYS wins against one in a lower category:\n");
    for (plInt32 iCategory : GetCategoryLadder())
      sTooltip.AppendFormat("\n  {}", plAiAssetUi::CategoryName(iCategory));

    const plInt32 iCategory = pProps->m_Category.GetValue();
    const char* szCategory = plAiAssetUi::CategoryName(iCategory);
    const float fCategory = TextWidth(szCategory) + 48.0f;

    flow.Group(TextWidth("Category") + 10.0f + fCategory);
    addLabel("Category");
    AddDropdown(pRow, flow.Take(fCategory), "Category", szCategory, sTooltip, [this]()
      { ShowCategoryMenu(); },
      true, false, CategoryColor(iCategory));
  }

  // Weight, Commit and Cooldown commit once per edit
  auto numberField = [&](plStringView sLabel, plStringView sName, float fValue, plUInt32 uiDecimals, plStringView sUnit, float fFieldWidth, plStringView sTooltip, std::function<void(double)> onValue, double fMin, double fMax)
  {
    flow.Group(TextWidth(sLabel) + 10.0f + fFieldWidth);
    addLabel(sLabel);
    AddNumberField(pRow, flow.Take(fFieldWidth), sName, FormatNumber(fValue, uiDecimals), sUnit, sTooltip, [this, onValue, fMin, fMax](const plString& sText)
      {
      double fNew = 0.0;
      if (ParseClamped(sText, fMin, fMax, fNew))
        onValue(fNew);
      else
        Invalidate(); });
  };

  numberField("Weight", "Weight", pProps->m_fWeight, 2, {}, 64.0f, "Base utility multiplier. Archetypes additionally apply their WeightScale.", [this](double fValue)
    { m_pWindow->SetRootValue("Weight", plVariant(static_cast<float>(fValue)), "Change Weight"); },
    0.0, 10.0);

  numberField("Commit", "Commit", pProps->m_fCommitBonus, 2, {}, 64.0f, "Stickiness: while this behavior runs, a challenger must beat its score by this much.", [this](double fValue)
    { m_pWindow->SetRootValue("CommitBonus", plVariant(static_cast<float>(fValue)), "Change Commit Bonus"); },
    0.0, 1.0);

  numberField("Cooldown", "Cooldown", static_cast<float>(pProps->m_CooldownDuration.GetSeconds()), 1, "s", 70.0f, "After deactivation the behavior scores 0 for this long.", [this](double fValue)
    { m_pWindow->SetRootValue("Cooldown", plVariant(plTime::Seconds(fValue)), "Change Cooldown"); },
    0.0, 3600.0);

  const float fHeight = flow.GetBottom() + 3.0f;
  Place(pRow, plRectFloat(0, 0, fWidth, fHeight));

  auto* pLine = Panel(pRoot);
  Place(pLine, plRectFloat(0, fHeight, fWidth, 1.0f));
  pLine->m_Color = Line();

  return fHeight + 1.0f;
}

float plQtAiBehaviorView::BuildLogicRow(plUiElement* pRoot, float fTop, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiBehaviorAssetObject* pProps = m_pWindow->GetBehaviorDocument()->GetProperties();
  const plInt32 iLogic = GetLogicType(pProps);

  plUiPanel* pRow = Panel(pRoot);
  pRow->m_Color = Surface();

  plAiFlowLayout flow(6.0f, 3.0f, fWidth - 6.0f, ControlHeight, 2.0f);

  plUiText* pLabel = Label(pRow, flow.Take(TextWidth("Logic") + 8.0f), "Logic", Dim());
  pLabel->m_HAlign = plUiTextHAlign::Center;

  const float fLogic = TextWidth(s_szLogicTypes[iLogic]) + 34.0f;
  AddDropdown(pRow, flow.Take(fLogic), "Logic", s_szLogicTypes[iLogic], "What runs while this behavior is active.", [this]()
    { ShowLogicMenu(); });

  // the state machine asset: its name, browse and open
  if (const plAiBehaviorLogic_StateMachine* pSm = plDynamicCast<const plAiBehaviorLogic_StateMachine*>(pProps->m_pLogic))
  {
    const plStringView sReference = pSm->GetStateMachineFile();
    plStringBuilder sDisplay = sReference;

    if (!sReference.IsEmpty())
    {
      auto asset = plAssetCurator::GetSingleton()->FindSubAsset(sReference);
      if (asset.isValid())
        sDisplay = asset->GetName();
    }

    const float fField = plMath::Clamp(fWidth - 6.0f - flow.GetWidth({fLogic, TextWidth("Logic") + 8.0f}) - 2.0f * (24.0f + 2.0f) - 12.0f, 120.0f, 340.0f);
    const plRectFloat field = flow.Take(fField);

    auto* pWell = Panel(pRow);
    Place(pWell, field);
    pWell->m_Color = Field();
    pWell->m_fBorderWidth = 1.0f;
    pWell->m_BorderColor = Line();
    pWell->m_vCornerRadius = plVec4(4);

    plUiImage* pIcon = SvgImage(pWell, ":/AssetIcons/StateMachine.svg");
    Place(pIcon, plRectFloat(8.0f, (field.height - 13.0f) * 0.5f, 13.0f, 13.0f));
    pIcon->m_Color = AiColor();

    Label(pWell, plRectFloat(27.0f, 0, field.width - 33.0f, field.height), sDisplay.IsEmpty() ? plStringView("<no state machine>") : sDisplay.GetView(), sDisplay.IsEmpty() ? Dim() : Muted());
    AddClickArea(pRow, field, "Logic.StateMachine", sReference, {});

    AddIconButton(pRow, flow.Take(24.0f), "Logic.Browse", Icon::More, "Select a state machine asset", [this]()
      { Defer([this]()
          { m_pWindow->BrowseStateMachine(); }); });

    AddIconButton(pRow, flow.Take(24.0f), "Logic.Open", Icon::External, "Open the state machine asset", [this]()
      { m_pWindow->OpenStateMachine(); });
  }

  const float fHeight = flow.GetBottom() + 3.0f;
  Place(pRow, plRectFloat(0, fTop, fWidth, fHeight));

  auto* pLine = Panel(pRoot);
  Place(pLine, plRectFloat(0, fTop + fHeight, fWidth, 1.0f));
  pLine->m_Color = Line();

  return fHeight + 1.0f;
}

void plQtAiBehaviorView::BuildConsiderationCard(plUiElement* pBody, float fWidth, plUInt32 uiIndex, plUInt32 uiCount, const plUuid& guid, const plAiConsiderationObject* pNative)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const bool bSelected = guid == m_pWindow->GetSelectedConsideration();
  const bool bLegacy = pNative != nullptr && pNative->m_bLegacyCurveDomain;
  const plAiInput* pInput = pNative != nullptr ? pNative->m_pInput : nullptr;

  plStringBuilder sName, sSummary;
  plAiAssetUi::GetPrettyInputName(pInput != nullptr ? pInput->GetDynamicRTTI() : nullptr, sName);
  if (pNative != nullptr)
    plAiAssetUi::GetInputParamSummary(pInput, sSummary);

  plUiPanel* pCard = Card(pBody, plRectFloat(0, 0, fWidth, 10.0f), bSelected ? CardState::Selected : CardState::Normal);

  plUiButton* pHit = AddClickArea(pCard, plRectFloat(0, 0, fWidth, 10.0f), Name("C", uiIndex, ".Card"), {}, [this, guid]()
    { m_pWindow->SelectConsideration(guid); });
  Fill(pHit);

  plAiFlowLayout flow(s_fCardPadding, 6.0f, fWidth - s_fCardPadding, s_fCurveHeight, 8.0f);

  Label(pCard, flow.Take(plMath::Max(140.0f, TextWidth(sName, FontSize)), 20.0f), sName, Foreground(), FontSize);

  if (pInput != nullptr && pInput->NeedsTarget())
  {
    const plRectFloat badge = flow.Take(BadgeWidth("TARGET"), BadgeHeight);
    Badge(pCard, badge.x, badge.y, "TARGET", AiColor());
  }

  // the summary takes the room between the identity and the controls, or a line of its own
  const float fIn = TextWidth("in");
  const float fArrow = TextWidth("\xE2\x86\x92");
  const float fRange = fIn + 5.0f + s_fRangeField + 5.0f + fArrow + 5.0f + s_fRangeField;
  const float fRight = flow.GetWidth({fRange, s_fCurveWidth, SmallButtonSize, SmallButtonSize, SmallButtonSize});

  const plRectFloat line = flow.Take(0.0f, 16.0f);
  const float fInline = (fWidth - s_fCardPadding) - fRight - 8.0f - line.x;
  const bool bInlineSummary = fInline >= 120.0f;

  if (bInlineSummary && !sSummary.IsEmpty())
    Label(pCard, plRectFloat(line.x, line.y, fInline, line.height), sSummary, Muted(), FontSize - 2, true);

  flow.AlignRight(fRight);

  // in min -> max
  {
    const plRectFloat range = flow.Take(fRange);
    float x = range.x;

    Label(pCard, plRectFloat(x, range.y, fIn, range.height), "in", Dim());
    x += fIn + 5.0f;

    AddNumberField(pCard, plRectFloat(x, range.y, s_fRangeField, range.height), Name("C", uiIndex, ".Min"), FormatTrimmed(pNative != nullptr ? pNative->m_fInputMin : 0.0f, 2), {}, "InputMin - raw values at or below this normalize to 0", [this, guid](const plString& sText)
      {
      double fValue = 0.0;
      if (ParseClamped(sText, -100000.0, 100000.0, fValue))
        m_pWindow->SetConsiderationRange(guid, true, fValue);
      else
        Invalidate(); });
    x += s_fRangeField + 5.0f;

    Label(pCard, plRectFloat(x, range.y, fArrow, range.height), "\xE2\x86\x92", Dim());
    x += fArrow + 5.0f;

    AddNumberField(pCard, plRectFloat(x, range.y, s_fRangeField, range.height), Name("C", uiIndex, ".Max"), FormatTrimmed(pNative != nullptr ? pNative->m_fInputMax : 1.0f, 2), {}, "InputMax - raw values at or above this normalize to 1", [this, guid](const plString& sText)
      {
      double fValue = 0.0;
      if (ParseClamped(sText, -100000.0, 100000.0, fValue))
        m_pWindow->SetConsiderationRange(guid, false, fValue);
      else
        Invalidate(); });
  }

  // the response curve
  {
    const plRectFloat curve = flow.Take(s_fCurveWidth, s_fCurveHeight);
    const plString sCurve = Name("C", uiIndex, ".Curve");
    AddDrawList(pCard, curve, sCurve);

    plCurve1D preview;
    if (pNative != nullptr)
      plAiAssetUi::BuildRuntimeCurve(pNative->m_ResponseCurve, preview);

    plUiDrawListWriter writer;
    DrawCurve(writer, plRectFloat(0, 0, curve.width, curve.height), &preview, bLegacy, bSelected);
    Publish(GetBoard(), writer, sCurve);

    AddClickArea(
      pCard, curve, Name("C", uiIndex, ".CurveHit"), bLegacy ? s_szLegacyCurveTooltip : s_szCurveTooltip, [this, guid]()
      {
      m_pWindow->SelectConsideration(guid);
      Defer([this, guid]()
        { m_pWindow->OpenCurveEditor(guid); }); },
      [this, guid, uiIndex]()
      { ShowCurveMenu(Name("C", uiIndex, ".CurveHit"), guid); });
  }

  AddIconButton(pCard, flow.Take(SmallButtonSize, SmallButtonSize), Name("C", uiIndex, ".Up"), Icon::MoveUp, "Move up", [this, guid]()
    { m_pWindow->MoveConsideration(guid, -1); },
    uiIndex > 0);

  AddIconButton(pCard, flow.Take(SmallButtonSize, SmallButtonSize), Name("C", uiIndex, ".Down"), Icon::MoveDown, "Move down", [this, guid]()
    { m_pWindow->MoveConsideration(guid, +1); },
    uiIndex + 1 < uiCount);

  AddIconButton(pCard, flow.Take(SmallButtonSize, SmallButtonSize), Name("C", uiIndex, ".Remove"), Icon::Clear, "Remove this consideration", [this, guid]()
    { m_pWindow->RemoveConsideration(guid); });

  float y = flow.GetBottom();

  if (!bInlineSummary && !sSummary.IsEmpty())
  {
    y += 6.0f;
    Label(pCard, plRectFloat(s_fCardPadding, y, fWidth - 2.0f * s_fCardPadding, 15.0f), sSummary, Muted(), FontSize - 2, true);
    y += 15.0f;
  }

  pCard->m_vOffsets.w = y + 6.0f + 1.0f;
}

void plQtAiBehaviorView::ShowCategoryMenu()
{
  const plInt32 iCurrent = m_pWindow->GetBehaviorDocument()->GetProperties()->m_Category.GetValue();

  plEditorUiMenu menu;
  for (plInt32 iCategory : plAiEngineUi::GetCategoryLadder())
  {
    plEditorUiMenuItem& item = menu.AddAction(plAiAssetUi::CategoryName(iCategory), static_cast<plUInt32>(iCategory));
    item.m_bCheckable = true;
    item.m_bChecked = iCategory == iCurrent;
  }

  ShowMenu("Category", menu, [this](plUInt32 uiId)
    { m_pWindow->SetRootValue("Category", plVariant(static_cast<plInt32>(uiId)), "Change Category"); });
}

void plQtAiBehaviorView::ShowLogicMenu()
{
  const plInt32 iCurrent = GetLogicType(m_pWindow->GetBehaviorDocument()->GetProperties());

  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_szLogicTypes); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(s_szLogicTypes[i], i);
    item.m_bCheckable = true;
    item.m_bChecked = static_cast<plInt32>(i) == iCurrent;
  }

  ShowMenu("Logic", menu, [this](plUInt32 uiId)
    { m_pWindow->ChangeLogicType(static_cast<plInt32>(uiId)); });
}

void plQtAiBehaviorView::ShowCurveMenu(plStringView sAnchor, const plUuid& guid)
{
  plEditorUiMenu menu;

  plEditorUiMenuItem& presets = menu.AddSubmenu("Presets");
  for (plUInt32 i = 0; i < plQtAiBehaviorAssetDocumentWindow::GetCurvePresetCount(); ++i)
  {
    plEditorUiMenuItem& item = presets.m_Children.ExpandAndGetRef();
    item.m_sLabel = plQtAiBehaviorAssetDocumentWindow::GetCurvePresetName(i);
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

void plQtAiBehaviorView::ShowAddMenu(plStringView sAnchor)
{
  m_MenuTypes.Clear();
  plDynamicArray<plEditorUiSearchMenuItem> items;

  plRTTI::ForEachDerivedType(
    plAiInput::GetStaticRTTI(),
    [&](const plRTTI* pRtti)
    {
      if (pRtti == plAiInput::GetStaticRTTI())
        return;

      plStringBuilder sLabel;
      plAiAssetUi::GetPrettyInputName(pRtti, sLabel);

      plEditorUiSearchMenuItem& item = items.ExpandAndGetRef();
      item.m_sLabel = sLabel;
      item.m_sCategory = plAiAssetUi::GetInputGroup(pRtti);
      item.m_uiId = m_MenuTypes.GetCount();
      m_MenuTypes.PushBack(pRtti);
    },
    plRTTI::ForEachOptions::ExcludeNotConcrete);

  ShowSearchMenu(sAnchor, items, "Search inputs", [this](plUInt32 uiId)
    {
    if (uiId < m_MenuTypes.GetCount())
      m_pWindow->AddConsiderationOfType(m_MenuTypes[uiId]); });
}

//////////////////////////////////////////////////////////////////////////
// plQtAiScorePreviewView

namespace
{
  constexpr float s_fScoreRow = 20.0f;
  constexpr float s_fScoreRowGap = 6.0f;
  constexpr float s_fSliderWidth = 90.0f;
  constexpr float s_fRawWidth = 44.0f;
  constexpr float s_fOutWidth = 40.0f;
  constexpr float s_fBarHeight = 18.0f;
} // namespace

plQtAiScorePreviewView::plQtAiScorePreviewView(plQtAiBehaviorAssetDocumentWindow* pWindow)
  : plQtAiEngineView("AiScorePreview")
  , m_pWindow(pWindow)
{
}

plQtAiScorePreviewView::~plQtAiScorePreviewView() = default;

void plQtAiScorePreviewView::ResetSliders()
{
  m_SliderValues.Clear();
  Invalidate();
}

void plQtAiScorePreviewView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiBehaviorAssetObject* pProps = m_pWindow->GetBehaviorDocument()->GetProperties();
  const float fWidth = vSize.x;

  // one row per consideration with an input
  plUInt32 uiRows = 0;
  for (const plAiConsiderationObject* pCons : pProps->m_Considerations)
  {
    if (pCons != nullptr && pCons->m_pInput != nullptr)
      ++uiRows;
  }

  while (m_SliderValues.GetCount() < uiRows)
    m_SliderValues.PushBack(0.7f);

  plUiScrollView* pBody = AddScrollBody(pRoot, plRectFloat(0, 0, fWidth, vSize.y), "Score.Body", 0.0f, 0.0f);

  Section(pBody, "Simulated situation", AiColor());
  BuildSituation(pBody, fWidth, uiRows);

  Section(pBody, "Result \xE2\x80\x94 mirrors ScoreUtility", AiColor());
  BuildResult(pBody, fWidth);

  GetBoard().SetEntryValue("Score.DimTint", plColor(Dim()));
  GetBoard().SetEntryValue("Score.MutedTint", plColor(Muted()));

  Recompute();
}

void plQtAiScorePreviewView::BuildSituation(plUiElement* pBody, float fWidth, plUInt32 uiRows)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiBehaviorAssetObject* pProps = m_pWindow->GetBehaviorDocument()->GetProperties();

  const float fRows = uiRows > 0 ? uiRows * (s_fScoreRow + s_fScoreRowGap) : s_fScoreRow + s_fScoreRowGap;
  plUiPanel* pSituation = Panel(pBody);
  pSituation->m_vOffsets.w = 6.0f + fRows + 4.0f + ControlHeight + 8.0f;
  pSituation->m_Color = plColorGammaUB(0, 0, 0, 0);

  // a row per consideration: name, slider, raw value and output
  const float fName = plMath::Max(fWidth - 20.0f - s_fSliderWidth - s_fRawWidth - s_fOutWidth - 3.0f * 8.0f, 60.0f);
  float y = 6.0f;
  plUInt32 uiRow = 0;

  for (const plAiConsiderationObject* pCons : pProps->m_Considerations)
  {
    if (pCons == nullptr || pCons->m_pInput == nullptr)
      continue;

    plStringBuilder sName;
    plAiAssetUi::GetPrettyInputName(pCons->m_pInput->GetDynamicRTTI(), sName);

    float x = 10.0f;
    Label(pSituation, plRectFloat(x, y, fName, s_fScoreRow), sName, Muted());
    x += fName + 8.0f;

    const plUInt32 uiSlider = uiRow;
    AddSlider(pSituation, plRectFloat(x, y, s_fSliderWidth, s_fScoreRow), Name("Score.Slider.", uiRow, ""), 0.0f, 1.0f, m_SliderValues[uiRow], {}, [this, uiSlider](float fValue)
      {
      m_SliderValues[uiSlider] = fValue;
      Recompute(); });
    x += s_fSliderWidth + 8.0f;

    TintedLabel(pSituation, plRectFloat(x, y, s_fRawWidth, s_fScoreRow), Name("Score.Raw.", uiRow, ""), "Score.DimTint", FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;
    x += s_fRawWidth + 8.0f;

    TintedLabel(pSituation, plRectFloat(x, y, s_fOutWidth, s_fScoreRow), Name("Score.Out.", uiRow, ""), Name("Score.OutTint.", uiRow, ""), FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;
    AddClickArea(pSituation, plRectFloat(x, y, s_fOutWidth, s_fScoreRow), Name("Score.OutHit.", uiRow, ""), "Curve output for this consideration (its factor in the product)", {});

    y += s_fScoreRow + s_fScoreRowGap;
    ++uiRow;
  }

  if (uiRows == 0)
  {
    Label(pSituation, plRectFloat(10.0f, y, fWidth - 20.0f, s_fScoreRow), "add considerations to simulate their scoring here", Dim(), FontSize - 2);
    y += s_fScoreRow + s_fScoreRowGap;
  }

  BuildOptions(pSituation, fWidth, y + 4.0f);
}

void plQtAiScorePreviewView::BuildOptions(plUiElement* pPanel, float fWidth, float fTop)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  // the archetype's scale, running and cooldown
  plAiFlowLayout flow(10.0f, fTop, fWidth - 10.0f, ControlHeight, 12.0f);

  const float fScaleLabel = TextWidth("Archetype \xC3\x97scale");
  const plRectFloat scale = flow.Take(fScaleLabel + 8.0f + 64.0f);
  Label(pPanel, plRectFloat(scale.x, scale.y, fScaleLabel, scale.height), "Archetype \xC3\x97scale", Muted());
  AddNumberField(pPanel, plRectFloat(scale.x + fScaleLabel + 8.0f, scale.y, 64.0f, scale.height), "Score.Scale", FormatNumber(m_fWeightScale, 2), {}, "Simulates the WeightScale an archetype applies to this behavior.", [this](const plString& sText)
    {
    double fValue = 0.0;
    if (ParseClamped(sText, 0.0, 10.0, fValue))
      m_fWeightScale = static_cast<float>(fValue);

    Invalidate();
    Recompute(); });

  AddCheckBox(pPanel, flow.Take(CheckBoxSize + 6.0f + TextWidth("running")), "Score.Running", "running", m_bRunning, "When active, a challenger must beat the current score plus the commit bonus.", [this]()
    {
    m_bRunning = !m_bRunning;
    SetChecked("Score.Running", m_bRunning);
    Recompute(); });

  AddCheckBox(pPanel, flow.Take(CheckBoxSize + 6.0f + TextWidth("on cooldown")), "Score.Cooldown", "on cooldown", m_bCooldown, "ScoreUtility returns 0 while TimeSinceDeactivation < Cooldown.", [this]()
    {
    m_bCooldown = !m_bCooldown;
    SetChecked("Score.Cooldown", m_bCooldown);
    Recompute(); });
}

void plQtAiScorePreviewView::BuildResult(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plUiPanel* pResult = Panel(pBody);
  pResult->m_vOffsets.w = 6.0f + 2.0f * 18.0f + 34.0f + s_fBarHeight + 6.0f + 32.0f + 8.0f;
  pResult->m_Color = plColorGammaUB(0, 0, 0, 0);

  // the product and its compensation
  float y = 6.0f;
  auto mathRow = [&](plStringView sLabel, plStringView sBinding)
  {
    Label(pResult, plRectFloat(10.0f, y, fWidth - 100.0f, 18.0f), sLabel, Muted());
    TintedLabel(pResult, plRectFloat(fWidth - 90.0f, y, 80.0f, 18.0f), sBinding, "Score.MutedTint", FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;
    y += 18.0f;
  };

  mathRow("product of factors", "Score.Product");
  mathRow("after n-axis compensation", "Score.Compensated");

  // the utility, its category, the bar and the note
  TintedLabel(pResult, plRectFloat(10.0f, y + 2.0f, 70.0f, 30.0f), "Score.Utility", "Score.UtilityTint", 22, true);
  TintedLabel(pResult, plRectFloat(84.0f, y + 2.0f, fWidth - 94.0f, 30.0f), "Score.Category", "Score.CategoryTint", FontSize - 1);
  y += 34.0f;

  m_fBarWidth = fWidth - 20.0f;
  AddDrawList(pResult, plRectFloat(10.0f, y, m_fBarWidth, s_fBarHeight), "Score.Bar");
  y += s_fBarHeight + 6.0f;

  plUiBoundText* pNote = TintedLabel(pResult, plRectFloat(10.0f, y, fWidth - 20.0f, 32.0f), "Score.Note", "Score.NoteTint", FontSize - 2);
  pNote->m_bWrap = true;
  pNote->m_VAlign = plUiTextVAlign::Top;
}

void plQtAiScorePreviewView::Recompute()
{
  if (!IsReady())
    return;

  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiBehaviorAssetObject* pProps = m_pWindow->GetBehaviorDocument()->GetProperties();
  plBlackboard& board = GetBoard();

  // each consideration at its slider's raw value, through its curve
  plHybridArray<float, 8> factors;
  bool bVetoed = false;
  plUInt32 uiRow = 0;

  for (const plAiConsiderationObject* pCons : pProps->m_Considerations)
  {
    if (pCons == nullptr || pCons->m_pInput == nullptr)
      continue;

    const float t = uiRow < m_SliderValues.GetCount() ? m_SliderValues[uiRow] : 0.7f;
    const float fRaw = plMath::Lerp(pCons->m_fInputMin, pCons->m_fInputMax, t);

    plCurve1D curve;
    plAiAssetUi::BuildRuntimeCurve(pCons->m_ResponseCurve, curve);

    const float fOut = plAiAssetUi::EvaluateConsideration(curve, fRaw, pCons->m_fInputMin, pCons->m_fInputMax, nullptr, pCons->m_bLegacyCurveDomain);
    factors.PushBack(fOut);
    bVetoed |= fOut <= 0.0005f;

    board.SetEntryValue(Name("Score.Raw.", uiRow, ""), FormatNumber(fRaw, 2));
    board.SetEntryValue(Name("Score.Out.", uiRow, ""), FormatNumber(fOut, 2));
    board.SetEntryValue(Name("Score.OutTint.", uiRow, ""), plColor(OutputColor(fOut)));
    ++uiRow;
  }

  // mirrors plAiUtilityEvaluator::ScoreUtility: product, n-axis compensation, weight, clamp
  float fProduct = 1.0f;
  for (float f : factors)
    fProduct *= f;

  float fCompensated = fProduct;
  if (factors.GetCount() > 1 && fProduct > 0.0f)
  {
    const float fModification = 1.0f - (1.0f / static_cast<float>(factors.GetCount()));
    fCompensated = fProduct + ((1.0f - fProduct) * fProduct * fModification);
  }

  float fUtility = plMath::Clamp(fCompensated * pProps->m_fWeight * m_fWeightScale, 0.0f, 1.0f);
  if (m_bCooldown)
    fUtility = 0.0f;

  const plInt32 iCategory = pProps->m_Category.GetValue();
  plStringBuilder sCategory;
  sCategory.SetFormat("{} + utility", plAiAssetUi::CategoryName(iCategory));

  board.SetEntryValue("Score.Product", FormatNumber(fProduct, 3));
  board.SetEntryValue("Score.Compensated", FormatNumber(fCompensated, 3));
  board.SetEntryValue("Score.Utility", FormatNumber(fUtility, 2));
  board.SetEntryValue("Score.UtilityTint", plColor((bVetoed || m_bCooldown) ? Danger() : Foreground()));
  board.SetEntryValue("Score.Category", plString(sCategory));
  board.SetEntryValue("Score.CategoryTint", plColor(CategoryColor(iCategory)));

  // the note under the bar
  plStringBuilder sNote;
  plColorGammaUB noteColor = Dim();
  float fCommitThreshold = -1.0f;

  if (m_bCooldown)
  {
    sNote = "cooldown veto - ScoreUtility returns 0 while TimeSinceDeactivation < Cooldown";
    noteColor = Danger();
  }
  else if (m_bRunning)
  {
    fCommitThreshold = plMath::Min(1.0f, fUtility + pProps->m_fCommitBonus);
    sNote.SetFormat("running: a challenger must beat {} + {} commit bonus to take over", plArgF(fUtility, 2), plArgF(pProps->m_fCommitBonus, 2));
    noteColor = Warning();
  }
  else if (bVetoed)
  {
    sNote = "vetoed - a factor is 0, the behavior scores 0 no matter the others";
    noteColor = Danger();
  }
  else
  {
    sNote = "enable 'running' to see the commit-bonus switch threshold";
  }

  board.SetEntryValue("Score.Note", plString(sNote));
  board.SetEntryValue("Score.NoteTint", plColor(noteColor));

  DrawBar(fUtility, fCommitThreshold, bVetoed || m_bCooldown);
}

void plQtAiScorePreviewView::DrawBar(float fUtility, float fCommitThreshold, bool bVetoed)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plUiDrawListWriter writer;
  const plRectFloat rect(0, 0, m_fBarWidth, s_fBarHeight);

  writer.Rect(rect, Field(), 3.0f);
  writer.RectOutline(rect, Line(), 1.0f, 3.0f);

  // the utility in the AI colour, fading in from the left; a veto shows in the danger colour
  const float fFill = plMath::Saturate(fUtility);
  if (fFill > 0.0f)
  {
    const plRectFloat fill(1.0f, 1.0f, (rect.width - 2.0f) * fFill, rect.height - 2.0f);

    if (bVetoed)
      writer.Rect(fill, Mix(Field(), Danger(), 0.45f), 2.0f);
    else
      writer.HorizontalGradient(fill, Mix(Field(), AiColor(), 0.35f), AiColor());
  }

  // the commit-bonus switch threshold while running
  if (fCommitThreshold >= 0.0f)
  {
    const float x = rect.width * plMath::Saturate(fCommitThreshold);
    writer.Line(plVec2(x, 1.0f), plVec2(x, rect.height - 1.0f), Warning(), 1.5f, 3.0f, 3.0f);
  }

  Publish(GetBoard(), writer, "Score.Bar");
}
