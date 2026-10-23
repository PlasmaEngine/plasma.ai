#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiAssetWindow.h>
#include <EditorPluginAi/Validation/AiValidationFixes.h>
#include <EditorPluginAi/Validation/AiValidationView.h>

#include <Core/Utils/Blackboard.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorUi/Surface.h>
#include <Foundation/Threading/ThreadUtils.h>
#include <ToolsFoundation/CommandHistory/CommandHistory.h>

namespace
{
  constexpr float s_fPadding = 12.0f;
  constexpr float s_fGap = 10.0f;
  constexpr float s_fSeverityWidth = 44.0f;
  constexpr float s_fColumnHeaderHeight = 22.0f;
  constexpr float s_fRowHeight = 40.0f;
  constexpr float s_fChipHeight = 20.0f;
  constexpr float s_fSearchWidth = 180.0f;
  constexpr float s_fFixButtonHeight = 22.0f;

  plString Name(plStringView sPrefix, plUInt32 uiIndex, plStringView sSuffix)
  {
    plStringBuilder sName;
    sName.SetFormat("{}{}{}", sPrefix, uiIndex, sSuffix);
    return sName;
  }

  plStringView SeverityLabel(plAiValidationSeverity::Enum severity)
  {
    switch (severity)
    {
      case plAiValidationSeverity::Error:
        return "ERR";
      case plAiValidationSeverity::Warning:
        return "WARN";
      default:
        return "INFO";
    }
  }

  plColorGammaUB SeverityColor(plAiValidationSeverity::Enum severity)
  {
    switch (severity)
    {
      case plAiValidationSeverity::Error:
        return plEditorUi::Danger();
      case plAiValidationSeverity::Warning:
        return plEditorUi::Warning();
      default:
        return plEditorUi::Dim();
    }
  }

  /// "Ai_CoverStaus · expected number, found missing"
  plString GetKeyLine(const plAiValidationIssue& issue)
  {
    plStringBuilder sLine;
    sLine.SetFormat("{} \xC2\xB7 expected {}, found {}", issue.m_sKey, issue.m_sExpected, issue.m_sActual);
    return sLine;
  }

  bool ContainsNoCase(plStringView sText, plStringView sSearch)
  {
    return sText.FindSubString_NoCase(sSearch) != nullptr;
  }

  /// "Add Ammo (number) to GruntBoard" for the template fix, else the fix's label.
  plString GetActionLabel(const plAiValidationFix& fix)
  {
    if (fix.m_Kind != plAiValidationFix::AddKeyToTemplate)
      return fix.m_sLabel;

    plStringBuilder sLabel;
    sLabel.SetFormat("Add {} ({}) to {}", fix.m_sKey, fix.m_sValueType, fix.m_sTargetName);
    return sLabel;
  }
} // namespace

plAiValidationView::plAiValidationView(plAiAssetDocumentWindow* pWindow)
  : plAiEngineView("AiValidation")
  , m_pWindow(pWindow)
{
  m_CuratorSubscription = plAssetCurator::GetSingleton()->m_Events.AddEventHandler(plMakeDelegate(&plAiValidationView::OnCuratorEvent, this));
}

plAiValidationView::~plAiValidationView()
{
  plAssetCurator::GetSingleton()->m_Events.RemoveEventHandler(m_CuratorSubscription);
}

void plAiValidationView::Revalidate()
{
  m_bValidate = true;
  Invalidate();
}

bool plAiValidationView::IsRebuildBlocked() const
{
  return m_pWindow->GetDocument()->GetCommandHistory()->IsInTransaction();
}

void plAiValidationView::OnCuratorEvent(const plAssetCuratorEvent& e)
{
  switch (e.m_Type)
  {
    case plAssetCuratorEvent::Type::AssetAdded:
    case plAssetCuratorEvent::Type::AssetRemoved:
    case plAssetCuratorEvent::Type::AssetMoved:
    case plAssetCuratorEvent::Type::AssetUpdated:
    case plAssetCuratorEvent::Type::AssetListReset:
      m_bAssetsChanged = true;
      break;

    default:
      break;
  }

  if (m_bAssetsChanged && plThreadUtils::IsMainThread())
    RequestRedraw();
}

void plAiValidationView::OnViewUpdate()
{
  if (m_bAssetsChanged.exchange(false))
    Revalidate();

  // the search filters while typing
  const plString sSearch = GetBoard().GetEntryValue(plTempHashedString("Va.Search.Text"), plString()).ConvertTo<plString>();
  if (sSearch != m_sSearch)
  {
    m_sSearch = sSearch;
    Invalidate();
  }
}

void plAiValidationView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;

  if (m_bValidate)
  {
    m_bValidate = false;
    plAiValidation::Validate(m_pWindow->GetDocument(), m_Report);
  }

  const float fWidth = vSize.x;
  const Columns columns = ComputeColumns(fWidth);

  float fTop = BuildHeader(pRoot, fWidth);
  fTop += BuildFilters(pRoot, fTop, fWidth);
  fTop += BuildColumnHeader(pRoot, fTop, columns);

  auto* pBack = Panel(pRoot);
  plAiEngineUi::Place(pBack, plRectFloat(0, fTop, fWidth, plMath::Max(vSize.y - fTop, 10.0f)));
  pBack->m_Color = Background();

  plUiScrollView* pBody = AddScrollBody(pRoot, plRectFloat(0, fTop, fWidth, plMath::Max(vSize.y - fTop, 10.0f)), "Va.Body", 0.0f, 0.0f);

  plUInt32 uiRows = 0;
  for (plUInt32 i = 0; i < m_Report.m_Issues.GetCount(); ++i)
  {
    if (!IsListed(m_Report.m_Issues[i]))
      continue;

    BuildRow(pBody, columns, i, uiRows);
    ++uiRows;
  }

  if (uiRows == 0)
    BuildEmpty(pBody, fWidth);
}

float plAiValidationView::BuildHeader(plUiElement* pRoot, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plUInt32 uiErrors = 0;
  plUInt32 uiWarnings = 0;

  for (const plAiValidationIssue& issue : m_Report.m_Issues)
  {
    if (!IsInScope(issue))
      continue;

    uiErrors += issue.m_Severity == plAiValidationSeverity::Error ? 1 : 0;
    uiWarnings += issue.m_Severity == plAiValidationSeverity::Warning ? 1 : 0;
  }

  plStringBuilder sCount;
  sCount.SetFormat("{} error{} \xC2\xB7 {} warning{}", uiErrors, uiErrors == 1 ? "" : "s", uiWarnings, uiWarnings == 1 ? "" : "s");

  // the scope chips at the right end
  const float fThis = TextWidth("This asset", FontSize - 2) + 18.0f;
  const float fDeps = TextWidth("With dependencies", FontSize - 2) + 18.0f;
  const float fChips = fThis + 6.0f + fDeps;

  plUiPanel* pHeader = Section(pRoot, "Validation", Warning(), sCount, {}, fChips + 8.0f);
  pHeader->m_vOffsets.z = fWidth;

  const float y = (SectionHeight - s_fChipHeight) * 0.5f;
  float x = fWidth - 8.0f - fChips;

  AddChip(pHeader, plRectFloat(x, y, fThis, s_fChipHeight), "Va.This", "This asset", !m_bDependencies, "List the problems of this asset only", [this]()
    { SetDependencies(false); });
  x += fThis + 6.0f;

  AddChip(pHeader, plRectFloat(x, y, fDeps, s_fChipHeight), "Va.Deps", "With dependencies", m_bDependencies, "Also list the problems of the assets this one references", [this]()
    { SetDependencies(true); });

  return SectionHeight;
}

float plAiValidationView::BuildFilters(plUiElement* pRoot, float fTop, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plUiPanel* pRow = Panel(pRoot);
  pRow->m_Color = Surface();

  plAiFlowLayout flow(8.0f, 3.0f, fWidth - 8.0f, ControlHeight, 10.0f);

  AddCheckBox(pRow, flow.Take(CheckBoxSize + 6.0f + TextWidth("Errors")), "Va.Errors", "Errors", m_bErrors, "Show errors", [this]()
    {
    m_bErrors = !m_bErrors;
    Invalidate(); });

  AddCheckBox(pRow, flow.Take(CheckBoxSize + 6.0f + TextWidth("Warnings")), "Va.Warnings", "Warnings", m_bWarnings, "Show warnings and notes", [this]()
    {
    m_bWarnings = !m_bWarnings;
    Invalidate(); });

  AddTextField(pRow, flow.Take(s_fSearchWidth), "Va.Search", m_sSearch, "Search", "Filter the rows by asset, element, key or message", [this](const plString& sText)
    {
    m_sSearch = sText;
    Invalidate(); });

  // the resolved board owner, or why the last fix failed, in the rest of the line
  const plRectFloat line = flow.Take(0.0f, ControlHeight);
  const float fRest = plMath::Max(fWidth - 8.0f - line.x, 10.0f);
  const plRectFloat rest(line.x, line.y, fRest, line.height);

  const bool bFixError = !m_sFixError.IsEmpty();
  plUiText* pOwner = Label(pRow, rest, bFixError ? m_sFixError.GetView() : m_Report.m_sOwner.GetView(), bFixError ? Danger() : Muted(), FontSize - 2);
  pOwner->m_Overflow = plUiTextOverflow::Ellipsis;

  AddClickArea(pRow, rest, "Va.Owner", bFixError ? m_sFixError.GetView() : m_Report.m_sOwnerTooltip.GetView(), {});

  const float fHeight = flow.GetBottom() + 3.0f;
  Place(pRow, plRectFloat(0, fTop, fWidth, fHeight));

  auto* pLine = Panel(pRoot);
  Place(pLine, plRectFloat(0, fTop + fHeight, fWidth, 1.0f));
  pLine->m_Color = Line();

  return fHeight + 1.0f;
}

plAiValidationView::Columns plAiValidationView::ComputeColumns(float fWidth) const
{
  Columns columns;
  columns.m_fWidth = fWidth;
  columns.m_fFixWidth = plMath::Clamp(fWidth * 0.2f, 90.0f, 170.0f);
  columns.m_fAssetWidth = plMath::Clamp(fWidth * 0.18f, 80.0f, 190.0f);

  // element and problem share the rest 1 : 1.3
  const float fFixed = s_fPadding * 2.0f + s_fSeverityWidth + columns.m_fAssetWidth + columns.m_fFixWidth + s_fGap * 4.0f;
  const float fFlexible = plMath::Max(fWidth - fFixed, 80.0f);

  columns.m_fElementWidth = fFlexible / 2.3f;
  columns.m_fProblemWidth = fFlexible - columns.m_fElementWidth;

  columns.m_fSeverity = s_fPadding;
  columns.m_fAsset = columns.m_fSeverity + s_fSeverityWidth + s_fGap;
  columns.m_fElement = columns.m_fAsset + columns.m_fAssetWidth + s_fGap;
  columns.m_fProblem = columns.m_fElement + columns.m_fElementWidth + s_fGap;
  columns.m_fFix = columns.m_fProblem + columns.m_fProblemWidth + s_fGap;
  return columns;
}

float plAiValidationView::BuildColumnHeader(plUiElement* pRoot, float fTop, const Columns& columns)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  plUiPanel* pRow = Panel(pRoot);
  Place(pRow, plRectFloat(0, fTop, columns.m_fWidth, s_fColumnHeaderHeight));
  pRow->m_Color = Surface();

  auto title = [&](float x, float fColumnWidth, plStringView sTitle)
  {
    Label(pRow, plRectFloat(x, 0, fColumnWidth, s_fColumnHeaderHeight), sTitle, Dim(), MinFontSize);
  };

  title(columns.m_fSeverity, s_fSeverityWidth, "SEV");
  title(columns.m_fAsset, columns.m_fAssetWidth, "ASSET");
  title(columns.m_fElement, columns.m_fElementWidth, "ELEMENT / KEY");
  title(columns.m_fProblem, columns.m_fProblemWidth, "PROBLEM");

  auto* pLine = Panel(pRoot);
  Place(pLine, plRectFloat(0, fTop + s_fColumnHeaderHeight, columns.m_fWidth, 1.0f));
  pLine->m_Color = Line();

  return s_fColumnHeaderHeight + 1.0f;
}

void plAiValidationView::BuildRow(plUiElement* pBody, const Columns& columns, plUInt32 uiIssue, plUInt32 uiRow)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiValidationIssue& issue = m_Report.m_Issues[uiIssue];
  const bool bSelected = issue.GetId() == m_sSelected;
  const plString sKeyLine = GetKeyLine(issue);

  plUiPanel* pRow = Panel(pBody);
  pRow->m_vOffsets.w = s_fRowHeight;
  pRow->m_Color = bSelected ? Selection() : plColorGammaUB(0, 0, 0, 0);

  // the row selects its element; the tooltip has the whole text
  plStringBuilder sTooltip;
  sTooltip.SetFormat("{}\n{}\n{}", issue.m_sElement, sKeyLine, issue.m_sMessage);

  plUiButton* pHit = AddClickArea(pRow, plRectFloat(0, 0, columns.m_fWidth, s_fRowHeight), Name("Va.Row", uiRow, ""), sTooltip, [this, uiIssue]()
    { SelectIssue(uiIssue); });
  Fill(pHit);
  pHit->m_HoverColor = bSelected ? Selection() : Mix(Background(), Raised(), 0.6f);

  if (bSelected)
  {
    auto* pEdge = Panel(pRow);
    Place(pEdge, plRectFloat(0, 0, 2.0f, s_fRowHeight));
    pEdge->m_Color = Accent();
  }

  const float fCenter = s_fRowHeight * 0.5f;
  Badge(pRow, columns.m_fSeverity, fCenter - BadgeHeight * 0.5f, SeverityLabel(issue.m_Severity), SeverityColor(issue.m_Severity));

  Label(pRow, plRectFloat(columns.m_fAsset, 0, columns.m_fAssetWidth, s_fRowHeight), issue.m_sAsset, Muted(), FontSize - 1)->m_Overflow = plUiTextOverflow::Ellipsis;

  // element, then the key with the expected and found values
  Label(pRow, plRectFloat(columns.m_fElement, 4.0f, columns.m_fElementWidth, 17.0f), issue.m_sElement, Foreground(), FontSize - 1)->m_Overflow = plUiTextOverflow::Ellipsis;
  Label(pRow, plRectFloat(columns.m_fElement, 21.0f, columns.m_fElementWidth, 15.0f), sKeyLine, Dim(), FontSize - 3, true)->m_Overflow = plUiTextOverflow::Ellipsis;

  plUiText* pMessage = Label(pRow, plRectFloat(columns.m_fProblem, 3.0f, columns.m_fProblemWidth, s_fRowHeight - 6.0f), issue.m_sMessage, Muted(), FontSize - 2);
  pMessage->m_bWrap = true;
  pMessage->m_Overflow = plUiTextOverflow::Ellipsis;

  BuildFixes(pRow, columns, uiIssue, uiRow);

  auto* pLine = Panel(pRow);
  Place(pLine, plRectFloat(0, s_fRowHeight - 1.0f, columns.m_fWidth, 1.0f));
  pLine->m_Color = Line();
}

void plAiValidationView::BuildFixes(plUiElement* pRow, const Columns& columns, plUInt32 uiIssue, plUInt32 uiRow)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiValidationIssue& issue = m_Report.m_Issues[uiIssue];
  if (issue.m_Fixes.IsEmpty())
    return;

  // the first fix as a button; the others behind a more button
  const bool bMore = issue.m_Fixes.GetCount() > 1;
  const float fMore = bMore ? SmallButtonSize + 4.0f : 0.0f;
  const float y = (s_fRowHeight - s_fFixButtonHeight) * 0.5f;

  const plAiValidationFix& fix = issue.m_Fixes[0];
  const float fButton = plMath::Min(TextWidth(fix.m_sLabel, FontSize - 2) + 20.0f, columns.m_fFixWidth - fMore);
  const plString sButton = Name("Va.Row", uiRow, ".Fix");

  plUiButton* pButton = AddTextButton(pRow, plRectFloat(columns.m_fFix, y, fButton, s_fFixButtonHeight), sButton, fix.m_sLabel, fix.m_sTooltip, [this, uiIssue, sButton]()
    { RunFix(uiIssue, 0, sButton); });
  static_cast<plUiText*>(pButton->m_Children[0])->m_uiFontSize = FontSize - 2;

  if (!bMore)
    return;

  const plString sMore = Name("Va.Row", uiRow, ".More");
  AddIconButton(pRow, plRectFloat(columns.m_fFix + fButton + 4.0f, (s_fRowHeight - SmallButtonSize) * 0.5f, SmallButtonSize, SmallButtonSize), sMore, Icon::More, "More fixes", [this, uiIssue, sMore]()
    { ShowMoreMenu(sMore, uiIssue); });
}

void plAiValidationView::BuildEmpty(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  bool bAny = false;
  for (const plAiValidationIssue& issue : m_Report.m_Issues)
    bAny |= IsInScope(issue);

  plUiPanel* pEmpty = Panel(pBody);
  pEmpty->m_vOffsets.w = 44.0f;
  pEmpty->m_Color = plColorGammaUB(0, 0, 0, 0);

  if (bAny)
    Label(pEmpty, plRectFloat(16.0f, 0, fWidth - 32.0f, 44.0f), "No problems match the filters.", Dim(), FontSize - 1);
  else
    Label(pEmpty, plRectFloat(16.0f, 0, fWidth - 32.0f, 44.0f), "No problems.", GoodColor(), FontSize - 1);
}

void plAiValidationView::AddChip(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sLabel, bool bOn, plStringView sTooltip, Action onClick)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  auto* pChip = Panel(pParent);
  Place(pChip, rect);
  pChip->m_Color = bOn ? Selection() : Mix(Surface(), Raised(), 0.4f);
  pChip->m_fBorderWidth = 1.0f;
  pChip->m_BorderColor = bOn ? Mix(Selection(), Accent(), 0.55f) : Border();
  pChip->m_vCornerRadius = plVec4(4);

  plUiText* pLabel = Label(pChip, plRectFloat(0, 0, rect.width, rect.height), sLabel, Foreground(), FontSize - 2);
  pLabel->m_HAlign = plUiTextHAlign::Center;

  AddClickArea(pParent, rect, sName, sTooltip, std::move(onClick));
}

bool plAiValidationView::IsInScope(const plAiValidationIssue& issue) const
{
  return m_bDependencies || !issue.m_bDependency;
}

bool plAiValidationView::IsListed(const plAiValidationIssue& issue) const
{
  if (!IsInScope(issue))
    return false;

  if (issue.m_Severity == plAiValidationSeverity::Error && !m_bErrors)
    return false;

  if (issue.m_Severity != plAiValidationSeverity::Error && !m_bWarnings)
    return false;

  if (m_sSearch.IsEmpty())
    return true;

  return ContainsNoCase(issue.m_sAsset, m_sSearch) || ContainsNoCase(issue.m_sElement, m_sSearch) || ContainsNoCase(issue.m_sKey, m_sSearch) || ContainsNoCase(issue.m_sMessage, m_sSearch);
}

void plAiValidationView::SetDependencies(bool bDependencies)
{
  m_bDependencies = bDependencies;
  Invalidate();
}

void plAiValidationView::SelectIssue(plUInt32 uiIssue)
{
  if (uiIssue >= m_Report.m_Issues.GetCount())
    return;

  const plAiValidationIssue& issue = m_Report.m_Issues[uiIssue];
  m_sSelected = issue.GetId();
  m_sFixError.Clear();

  m_pWindow->RevealValidationElement(issue.m_Reveal);
  Invalidate();
}

void plAiValidationView::RunFix(plUInt32 uiIssue, plUInt32 uiFix, plStringView sAnchor)
{
  if (uiIssue >= m_Report.m_Issues.GetCount() || uiFix >= m_Report.m_Issues[uiIssue].m_Fixes.GetCount())
    return;

  const plAiValidationIssue& issue = m_Report.m_Issues[uiIssue];
  const plAiValidationFix& fix = issue.m_Fixes[uiFix];
  m_sFixError.Clear();

  // edits of other assets and shared behaviors list what they touch first; choices need a pick
  const bool bOtherAsset = fix.m_Kind != plAiValidationFix::OpenAsset && plAiValidationFixes::TargetsOtherAsset(m_pWindow->GetDocument(), issue, fix);
  const bool bChoose = fix.m_sValue.IsEmpty() && !fix.m_Choices.IsEmpty();

  if (bOtherAsset || bChoose || !fix.m_AffectedAssets.IsEmpty())
  {
    ShowFixMenu(sAnchor, issue, fix);
    return;
  }

  ApplyFix(issue, fix, fix.m_sValue);
}

void plAiValidationView::ShowFixMenu(plStringView sAnchor, const plAiValidationIssue& issue, const plAiValidationFix& fix)
{
  plEditorUiMenu menu;
  bool bInfo = false;

  if (fix.m_Kind != plAiValidationFix::OpenAsset && plAiValidationFixes::TargetsOtherAsset(m_pWindow->GetDocument(), issue, fix))
  {
    plStringBuilder sHeader("Changes ", plAiValidationFixes::GetTargetName(issue, fix));
    menu.AddHeader(sHeader);
    bInfo = true;
  }

  if (!fix.m_AffectedAssets.IsEmpty())
  {
    menu.AddHeader("Affects");

    for (const plString& sAsset : fix.m_AffectedAssets)
      menu.AddAction(sAsset, plInvalidIndex).m_bEnabled = false;

    bInfo = true;
  }

  if (bInfo)
    menu.AddSeparator();

  // one action with the fix's value, or one per choice
  plHybridArray<plString, 8> choices;

  if (!fix.m_sValue.IsEmpty() || fix.m_Choices.IsEmpty())
  {
    choices.PushBack(fix.m_sValue);
    menu.AddAction(GetActionLabel(fix), 0);
  }
  else
  {
    for (plUInt32 i = 0; i < fix.m_Choices.GetCount(); ++i)
    {
      choices.PushBack(fix.m_Choices[i]);
      menu.AddAction(fix.m_Choices[i].IsEmpty() ? plStringView("<default>") : fix.m_Choices[i].GetView(), i);
    }
  }

  ShowMenu(sAnchor, menu, [this, issue, fix, choices](plUInt32 uiId)
    {
    if (uiId < choices.GetCount())
      ApplyFix(issue, fix, choices[uiId]); });
}

void plAiValidationView::ShowMoreMenu(plStringView sAnchor, plUInt32 uiIssue)
{
  if (uiIssue >= m_Report.m_Issues.GetCount())
    return;

  const plAiValidationIssue& issue = m_Report.m_Issues[uiIssue];

  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < issue.m_Fixes.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(GetActionLabel(issue.m_Fixes[i]), i);
    item.m_sTooltip = issue.m_Fixes[i].m_sTooltip;
  }

  const plString sAnchorName = sAnchor;
  ShowMenu(sAnchor, menu, [this, uiIssue, sAnchorName](plUInt32 uiFix)
    { Defer([this, uiIssue, uiFix, sAnchorName]()
        { RunFix(uiIssue, uiFix, sAnchorName); }); });
}

void plAiValidationView::ApplyFix(const plAiValidationIssue& issue, const plAiValidationFix& fix, const plString& sChoice)
{
  // pickers and document windows open outside the UI dispatch
  Defer([this, issue, fix, sChoice]()
    {
    const plStatus status = plAiValidationFixes::Apply(m_pWindow->GetDocument(), issue, fix, sChoice);
    m_sFixError = status.Failed() ? status.m_sMessage : plString();
    Revalidate(); });
}
