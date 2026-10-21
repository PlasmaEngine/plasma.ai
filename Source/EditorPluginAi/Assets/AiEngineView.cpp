#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEngineView.h>

#include <AiPlugin/UtilityAI/Decision/AiScoring.h>
#include <AiPlugin/Utils/AiResponseCurve.h>
#include <Core/Utils/Blackboard.h>
#include <EditorUi/Surface.h>
#include <Foundation/Math/ColorScheme.h>
#include <Foundation/Tracks/Curve1D.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>

#include <QTimer>

namespace
{
  plString Entry(plStringView sName, plStringView sSuffix)
  {
    plStringBuilder entry(sName, sSuffix);
    return entry;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plAiEngineUi

namespace plAiEngineUi
{
  plColorGammaUB AiColor()
  {
    return plColorScheme::GetCategoryColor("AI", plColorScheme::CategoryColorUsage::BorderColor);
  }

  plColorGammaUB GoodColor()
  {
    return plEditorUi::Axis(1);
  }

  plColorGammaUB GridColor()
  {
    return Mix(plEditorUi::Surface(), plEditorUi::Raised(), 0.7f);
  }

  plColorGammaUB WithAlpha(const plColorGammaUB& color, plUInt8 uiAlpha)
  {
    return plColorGammaUB(color.r, color.g, color.b, uiAlpha);
  }

  plColorGammaUB Mix(const plColorGammaUB& a, const plColorGammaUB& b, float t)
  {
    auto lerp = [t](plUInt8 x, plUInt8 y)
    { return static_cast<plUInt8>(plMath::Clamp(x + (y - x) * t, 0.0f, 255.0f)); };

    return plColorGammaUB(lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a));
  }

  plColorGammaUB CategoryColor(plInt32 iCategory)
  {
    auto category = [](const char* szName) -> plColorGammaUB
    { return plColorScheme::GetCategoryColor(szName, plColorScheme::CategoryColorUsage::BorderColor); };

    switch (iCategory)
    {
      case plAiBehaviorCategory::Interrupt:
        return plEditorUi::Danger();
      case plAiBehaviorCategory::Combat:
        return category("Physics");
      case plAiBehaviorCategory::Command:
        return category("Gameplay");
      case plAiBehaviorCategory::Investigate:
        return category("Lighting");
      case plAiBehaviorCategory::ActiveIdle:
        return category("Animation");
      case plAiBehaviorCategory::Idle:
        return category("Utilities");
      default:
        return category("Prefabs");
    }
  }

  plArrayPtr<const plInt32> GetCategoryLadder()
  {
    static const plInt32 s_Ladder[] = {
      plAiBehaviorCategory::Interrupt,
      plAiBehaviorCategory::Combat,
      plAiBehaviorCategory::Command,
      plAiBehaviorCategory::Investigate,
      plAiBehaviorCategory::ActiveIdle,
      plAiBehaviorCategory::Idle,
      plAiBehaviorCategory::Fallback,
    };

    return plMakeArrayPtr(s_Ladder);
  }

  void Place(plUiElement* pElement, const plRectFloat& rect)
  {
    pElement->m_vAnchorMin = pElement->m_vAnchorMax = plVec2::MakeZero();
    pElement->m_vOffsets = plVec4(rect.x, rect.y, rect.width, rect.height);
  }

  plUiText* Label(plUiElement* pParent, const plRectFloat& rect, plStringView sText, const plColorGammaUB& color, plUInt32 uiFontSize, bool bCode, plStringView sBinding)
  {
    plUiText* pText = plEditorUi::Text(pParent, sText, sBinding);
    Place(pText, rect);
    pText->m_Color = color;
    pText->m_uiFontSize = uiFontSize;

    if (bCode)
      pText->m_hFont = plEditorUi::GetCodeFont();

    return pText;
  }

  plUiBoundText* TintedLabel(plUiElement* pParent, const plRectFloat& rect, plStringView sBinding, plStringView sTintBinding, plUInt32 uiFontSize, bool bCode)
  {
    plUiBoundText* pText = PL_DEFAULT_NEW(plUiBoundText);
    pParent->m_Children.PushBack(pText);
    Place(pText, rect);
    pText->m_sTextBinding = sBinding;
    pText->m_sTintBinding = sTintBinding;
    pText->m_hFont = bCode ? plEditorUi::GetCodeFont() : plEditorUi::GetDefaultFont();
    pText->m_uiFontSize = uiFontSize;
    pText->m_Color = plColorGammaUB(255, 255, 255, 255);
    pText->m_VAlign = plUiTextVAlign::Center;
    pText->m_Overflow = plUiTextOverflow::Ellipsis;
    return pText;
  }

  float TextWidth(plStringView sText, plUInt32 uiFontSize)
  {
    return plMath::Ceil(plEditorUi::MeasureText(sText, uiFontSize)) + 2.0f;
  }

  float CodeWidth(plStringView sText, plUInt32 uiFontSize)
  {
    return plMath::Ceil(plEditorUi::MeasureCodeText(sText, uiFontSize)) + 2.0f;
  }

  float BadgeWidth(plStringView sText)
  {
    return CodeWidth(sText, plEditorUi::MinFontSize) + 12.0f;
  }

  float Badge(plUiElement* pParent, float x, float y, plStringView sText, const plColorGammaUB& color)
  {
    const float fWidth = BadgeWidth(sText);

    // an opaque 13 % tint: translucent fills blend in linear space and come out much stronger
    auto* pBadge = plEditorUi::Panel(pParent);
    Place(pBadge, plRectFloat(x, y, fWidth, BadgeHeight));
    pBadge->m_Color = Mix(plEditorUi::Surface(), color, 0.13f);
    pBadge->m_vCornerRadius = plVec4(3);

    auto* pText = plEditorUi::Text(pBadge, sText);
    plEditorUi::Fill(pText, plVec4(6, 0, 4, 0));
    pText->m_hFont = plEditorUi::GetCodeFont();
    pText->m_uiFontSize = plEditorUi::MinFontSize;
    pText->m_Color = color;
    pText->m_Overflow = plUiTextOverflow::Overflow;

    return fWidth;
  }

  plUiPanel* Section(plUiElement* pParent, plStringView sTitle, const plColorGammaUB& color, plStringView sCount, plStringView sHint, float fRightReserve)
  {
    using namespace plEditorUi;

    auto* pHeader = Panel(pParent);
    pHeader->m_vAnchorMin = pHeader->m_vAnchorMax = plVec2::MakeZero();
    pHeader->m_vOffsets = plVec4(0, 0, 0, SectionHeight);
    pHeader->m_Color = Plate();
    pHeader->m_bClipChildren = true;

    auto* pEdge = Panel(pHeader);
    pEdge->m_vAnchorMax.x = 0;
    pEdge->m_vOffsets = plVec4(0, 0, 3, 0);
    pEdge->m_Color = color;

    plStringBuilder sUpper(sTitle);
    sUpper.ToUpper();

    float x = 10.0f;
    const float fTitle = TextWidth(sUpper, MinFontSize + 1);
    Label(pHeader, plRectFloat(x, 0, fTitle, SectionHeight), sUpper, Muted(), MinFontSize + 1)->m_Overflow = plUiTextOverflow::Overflow;
    x += fTitle + 8.0f;

    if (!sCount.IsEmpty())
    {
      const float fCount = CodeWidth(sCount, MinFontSize);
      Label(pHeader, plRectFloat(x, 0, fCount, SectionHeight), sCount, Dim(), MinFontSize, true)->m_Overflow = plUiTextOverflow::Overflow;
      x += fCount + 8.0f;
    }

    if (!sHint.IsEmpty())
    {
      auto* pHint = Label(pHeader, plRectFloat(x, 0, 0, SectionHeight), sHint, Dim(), FontSize - 2);
      pHint->m_vAnchorMax.x = 1.0f;
      pHint->m_vOffsets.z = -8.0f - fRightReserve;
    }

    return pHeader;
  }

  void DrawCurve(plUiDrawListWriter& ref_writer, const plRectFloat& rect, const plCurve1D* pCurve, bool bLegacyDomain, bool bSelected)
  {
    using namespace plEditorUi;

    ref_writer.Rect(rect, Background(), 4.0f);
    ref_writer.RectOutline(rect, bSelected ? Mix(Background(), Accent(), 0.5f) : Line(), 1.0f, 4.0f);

    const plColorGammaUB grid = GridColor();
    ref_writer.Line(plVec2(rect.x + rect.width * 0.5f, rect.y + 1.0f), plVec2(rect.x + rect.width * 0.5f, rect.y + rect.height - 1.0f), grid);
    ref_writer.Line(plVec2(rect.x + 1.0f, rect.y + rect.height * 0.5f), plVec2(rect.x + rect.width - 1.0f, rect.y + rect.height * 0.5f), grid);

    // 48 samples, 2 px inside the frame
    constexpr plUInt32 uiSamples = 48;
    plVec2 points[uiSamples];
    const plRectFloat inner(rect.x + 2.0f, rect.y + 2.0f, rect.width - 4.0f, rect.height - 4.0f);

    for (plUInt32 i = 0; i < uiSamples; ++i)
    {
      const float t = static_cast<float>(i) / (uiSamples - 1);
      float fValue = t;

      if (pCurve != nullptr && !pCurve->IsEmpty())
        fValue = plAiEvaluateResponseCurve(*pCurve, t, bLegacyDomain);

      points[i] = plVec2(inner.x + t * inner.width, inner.y + (1.0f - plMath::Saturate(fValue)) * inner.height);
    }

    ref_writer.Polyline(plMakeArrayPtr(points), AiColor(), 2.0f);
  }

  void DrawNoCurve(plUiDrawListWriter& ref_writer, const plRectFloat& rect)
  {
    using namespace plEditorUi;

    // a dashed outline: four dashed edges
    const plColorGammaUB border = Border();
    ref_writer.Line(plVec2(rect.x, rect.y), plVec2(rect.x + rect.width, rect.y), border, 1.0f, 3.0f, 2.0f);
    ref_writer.Line(plVec2(rect.x, rect.y + rect.height), plVec2(rect.x + rect.width, rect.y + rect.height), border, 1.0f, 3.0f, 2.0f);
    ref_writer.Line(plVec2(rect.x, rect.y), plVec2(rect.x, rect.y + rect.height), border, 1.0f, 3.0f, 2.0f);
    ref_writer.Line(plVec2(rect.x + rect.width, rect.y), plVec2(rect.x + rect.width, rect.y + rect.height), border, 1.0f, 3.0f, 2.0f);

    ref_writer.Text(rect, "no curve", Dim(), MinFontSize, plUiTextHAlign::Center);
  }

  void DrawStar(plUiDrawListWriter& ref_writer, const plVec2& vCenter, float fRadius, const plColorGammaUB& color)
  {
    // ten corners alternating between the outer and the inner radius, as a triangle fan
    plVec2 corners[10];
    for (plUInt32 i = 0; i < 10; ++i)
    {
      const float fRadiusAt = (i % 2 == 0) ? fRadius : fRadius * 0.45f;
      const plAngle angle = plAngle::MakeFromDegree(-90.0f + i * 36.0f);
      corners[i] = vCenter + plVec2(plMath::Cos(angle), plMath::Sin(angle)) * fRadiusAt;
    }

    plVec2 triangles[30];
    for (plUInt32 i = 0; i < 10; ++i)
    {
      triangles[i * 3 + 0] = vCenter;
      triangles[i * 3 + 1] = corners[i];
      triangles[i * 3 + 2] = corners[(i + 1) % 10];
    }

    ref_writer.Triangles(plMakeArrayPtr(triangles), color);
  }

  plString FormatNumber(double fValue, plUInt32 uiDecimals)
  {
    plStringBuilder sText;
    sText.SetFormat("{}", plArgF(fValue, uiDecimals));
    return sText;
  }

  plString FormatTrimmed(double fValue, plUInt32 uiMaxDecimals)
  {
    plStringBuilder sText;
    sText.SetFormat("{}", plArgF(fValue, uiMaxDecimals));

    if (sText.FindSubString(".") == nullptr)
      return sText;

    // drop trailing zeros, keep one decimal
    while (sText.EndsWith("0") && !sText.EndsWith(".0"))
      sText.Shrink(0, 1);

    return sText;
  }

  bool ParseNumber(plStringView sText, double& out_fValue)
  {
    plStringBuilder sTrimmed(sText);
    sTrimmed.Trim(" \t");
    return !sTrimmed.IsEmpty() && plConversionUtils::StringToFloat(sTrimmed, out_fValue).Succeeded();
  }

  bool ParseClamped(plStringView sText, double fMin, double fMax, double& out_fValue)
  {
    if (!ParseNumber(sText, out_fValue))
      return false;

    out_fValue = plMath::Clamp(out_fValue, fMin, fMax);
    return true;
  }
} // namespace plAiEngineUi

//////////////////////////////////////////////////////////////////////////
// plAiFlowLayout

plAiFlowLayout::plAiFlowLayout(float fLeft, float fTop, float fRight, float fLineHeight, float fGap)
  : m_fLeft(fLeft)
  , m_fRight(fRight)
  , m_fX(fLeft)
  , m_fY(fTop)
  , m_fLineHeight(fLineHeight)
  , m_fGap(fGap)
{
}

float plAiFlowLayout::GetWidth(std::initializer_list<float> widths) const
{
  float fTotal = 0.0f;
  for (float fWidth : widths)
    fTotal += fWidth;

  return fTotal + m_fGap * (widths.size() > 0 ? widths.size() - 1 : 0);
}

void plAiFlowLayout::Group(float fWidth)
{
  if (!m_bLineEmpty && m_fX + fWidth > m_fRight)
    Wrap();
}

plRectFloat plAiFlowLayout::Take(float fWidth, float fHeight)
{
  if (!m_bLineEmpty && m_fX + fWidth > m_fRight)
    Wrap();

  const plRectFloat rect(m_fX, m_fY + (m_fLineHeight - fHeight) * 0.5f, fWidth, fHeight);
  m_fX += fWidth + m_fGap;
  m_bLineEmpty = false;
  return rect;
}

void plAiFlowLayout::AlignRight(float fWidth)
{
  if (!m_bLineEmpty && m_fX + fWidth > m_fRight)
    Wrap();

  m_fX = plMath::Max(m_fX, m_fRight - fWidth);
}

void plAiFlowLayout::Wrap()
{
  m_fY += m_fLineHeight + m_fGap;
  m_fX = m_fLeft;
  m_bLineEmpty = true;
}

//////////////////////////////////////////////////////////////////////////
// plQtAiEngineView

plQtAiEngineView::plQtAiEngineView(const char* szName)
  : plEditorUiContent(szName)
  , m_pAlive(std::make_shared<bool>(true))
{
  if (IsReady())
    m_PointerSubscription = GetSurface()->m_PointerEvents.AddEventHandler(plMakeDelegate(&plQtAiEngineView::OnPointer, this));
}

plQtAiEngineView::~plQtAiEngineView()
{
  if (IsReady())
    GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);
}

void plQtAiEngineView::Invalidate()
{
  m_bRebuild = true;
  RequestRedraw();
}

bool plQtAiEngineView::ActivateControl(plStringView sName)
{
  auto it = m_Controls.Find(sName);
  if (!it.IsValid() || !it.Value().m_bEnabled || !it.Value().m_OnClick)
    return false;

  Action action = it.Value().m_OnClick;
  action();
  return true;
}

bool plQtAiEngineView::CommitControl(plStringView sName, plStringView sText)
{
  auto it = m_Controls.Find(sName);
  if (!it.IsValid() || it.Value().m_Kind != Kind::Field)
    return false;

  GetBoard().SetEntryValue(it.Value().m_sBinding, plString(sText));
  CommitField(it.Key());
  return true;
}

bool plQtAiEngineView::HasControl(plStringView sName) const
{
  return m_Controls.Find(sName).IsValid();
}

void plQtAiEngineView::OnUiUpdate()
{
  if (!IsReady())
    return;

  if ((m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize) && !IsRebuildBlocked())
    Rebuild();

  PollFocus();
  PollSliders();
  OnViewUpdate();
}

void plQtAiEngineView::Rebuild()
{
  using namespace plEditorUi;

  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();

  // a field being edited keeps its text and focus
  m_sKeepFocus = GetSurface()->GetCanvas().GetFocusedElementName();
  m_Controls.Clear();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  BuildContent(pRoot, plVec2(plMath::Max(m_vBuiltSize.x, 120.0f), plMath::Max(m_vBuiltSize.y, 80.0f)));

  SetDocument(Finish(pRoot));

  if (!m_sKeepFocus.IsEmpty() && m_Controls.Contains(m_sKeepFocus))
    GetSurface()->GetCanvas().FocusElement(m_sKeepFocus, GetSurface()->GetLogicalSize());

  m_sKeepFocus.Clear();
}

plQtAiEngineView::Control& plQtAiEngineView::Register(plStringView sName, Kind kind, const plRectFloat& rect, plStringView sTooltip, bool bEnabled)
{
  Control& control = m_Controls[sName];
  control = Control();
  control.m_Kind = kind;
  control.m_Rect = rect;
  control.m_bEnabled = bEnabled;

  plEditorUi::SetToolTip(GetBoard(), sName, sTooltip);
  GetBoard().SetEntryValue(Entry(sName, ".Off"), !bEnabled);
  return control;
}

plUiTextInput* plQtAiEngineView::AddTextField(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sText, plStringView sPlaceholder, plStringView sTooltip, TextAction onCommit)
{
  Control& control = Register(sName, Kind::Field, rect, sTooltip, true);
  control.m_sBinding = Entry(sName, ".Text");
  control.m_sCommitted = sText;
  control.m_OnCommit = std::move(onCommit);

  if (sName != m_sKeepFocus)
    GetBoard().SetEntryValue(control.m_sBinding, plString(sText));

  plUiTextInput* pInput = plEditorUi::Input(pParent, sName, control.m_sBinding, sPlaceholder, Entry(sName, ".Submit"));
  plAiEngineUi::Place(pInput, rect);
  pInput->m_BorderColor = plEditorUi::Line();
  pInput->m_vCornerRadius = plVec4(4);
  pInput->m_uiFontSize = plEditorUi::FontSize - 1;
  return pInput;
}

plUiTextInput* plQtAiEngineView::AddNumberField(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sText, plStringView sUnit, plStringView sTooltip, TextAction onCommit)
{
  Control& control = Register(sName, Kind::Field, rect, sTooltip, true);
  control.m_sBinding = Entry(sName, ".Text");
  control.m_sCommitted = sText;
  control.m_OnCommit = std::move(onCommit);

  if (sName != m_sKeepFocus)
    GetBoard().SetEntryValue(control.m_sBinding, plString(sText));

  return plEditorUi::NumberField(pParent, rect, sName, control.m_sBinding, Entry(sName, ".Submit"), {}, sUnit);
}

plUiButton* plQtAiEngineView::AddDropdown(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sText, plStringView sTooltip, Action onClick, bool bEnabled, bool bValueRight, plColorGammaUB dotColor)
{
  using namespace plEditorUi;

  Control& control = Register(sName, Kind::Button, rect, sTooltip, bEnabled);
  control.m_OnClick = std::move(onClick);

  constexpr float fChevron = 18.0f;

  plUiButton* pButton = TextButton(pParent, sText, sName);
  plAiEngineUi::Place(pButton, rect);
  pButton->m_Color = Field();
  pButton->m_HoverColor = Field();
  pButton->m_BorderColor = Line();
  pButton->m_sDisabledBinding = Entry(sName, ".Off");

  auto* pText = static_cast<plUiText*>(pButton->m_Children[0]);
  pText->m_HAlign = bValueRight ? plUiTextHAlign::Right : plUiTextHAlign::Left;
  pText->m_vOffsets.z = -fChevron - 2.0f;
  pText->m_Color = bEnabled ? Foreground() : Dim();

  if (dotColor.a > 0)
  {
    auto* pDot = Panel(pButton);
    pDot->m_vAnchorMin = pDot->m_vAnchorMax = plVec2(0, 0.5f);
    pDot->m_vOffsets = plVec4(8, -4, 8, 8);
    pDot->m_Color = dotColor;
    pDot->m_vCornerRadius = plVec4(4);
    pText->m_vOffsets.x += 14.0f;
  }

  auto* pChevron = SvgImage(pButton, IconPath(Icon::Expand));
  pChevron->m_vAnchorMin = pChevron->m_vAnchorMax = plVec2(1, 0.5f);
  pChevron->m_vOffsets = plVec4(-fChevron + 2.0f, -6, 12, 12);
  pChevron->m_Color = Dim();

  return pButton;
}

plUiButton* plQtAiEngineView::AddIconButton(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plEditorUi::Icon icon, plStringView sTooltip, Action onClick, bool bEnabled)
{
  using namespace plEditorUi;

  Control& control = Register(sName, Kind::Button, rect, sTooltip, bEnabled);
  control.m_OnClick = std::move(onClick);

  plUiButton* pButton = Button(pParent, plStringView(), sName);
  plAiEngineUi::Place(pButton, rect);
  pButton->m_Color = plColorGammaUB(0, 0, 0, 0);
  pButton->m_hStyle = GetGhostStyle();
  pButton->m_sDisabledBinding = Entry(sName, ".Off");
  pButton->ClearChildren();

  // 14 px glyphs in small buttons, 16 px in toolbar-sized ones
  const float fIcon = rect.width < 22.0f ? 14.0f : 16.0f;
  plUiImage* pIcon = ButtonIcon(pButton, icon);
  pIcon->m_vOffsets = plVec4(-fIcon * 0.5f, -fIcon * 0.5f, fIcon, fIcon);
  pIcon->m_Color = bEnabled ? Muted() : plAiEngineUi::Mix(Surface(), Dim(), 0.5f);

  return pButton;
}

plUiButton* plQtAiEngineView::AddTextButton(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sLabel, plStringView sTooltip, Action onClick, bool bAddIcon, bool bEnabled)
{
  using namespace plEditorUi;

  Control& control = Register(sName, Kind::Button, rect, sTooltip, bEnabled);
  control.m_OnClick = std::move(onClick);

  plUiButton* pButton = TextButton(pParent, sLabel, sName);
  plAiEngineUi::Place(pButton, rect);
  pButton->m_sDisabledBinding = Entry(sName, ".Off");

  auto* pText = static_cast<plUiText*>(pButton->m_Children[0]);
  pText->m_Color = bEnabled ? Foreground() : Dim();

  if (bAddIcon)
  {
    pText->m_HAlign = plUiTextHAlign::Left;
    pText->m_vOffsets.x = 28.0f;

    plUiImage* pIcon = SvgImage(pButton, IconPath(Icon::Add));
    pIcon->m_vAnchorMin = pIcon->m_vAnchorMax = plVec2(0, 0.5f);
    pIcon->m_vOffsets = plVec4(9, -7, 14, 14);
    pIcon->m_Color = Muted();
  }

  return pButton;
}

plUiButton* plQtAiEngineView::AddCheckBox(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sLabel, bool bChecked, plStringView sTooltip, Action onToggle, bool bEnabled, plColorGammaUB labelColor)
{
  using namespace plEditorUi;

  Control& control = Register(sName, Kind::CheckBox, rect, sTooltip, bEnabled);
  control.m_OnClick = std::move(onToggle);
  SetChecked(sName, bChecked);

  plUiButton* pButton = Button(pParent, plStringView(), sName);
  plAiEngineUi::Place(pButton, rect);
  pButton->m_Color = plColorGammaUB(0, 0, 0, 0);
  pButton->m_hStyle = GetGhostStyle();
  pButton->m_sDisabledBinding = Entry(sName, ".Off");
  pButton->ClearChildren();

  const float fBox = plAiEngineUi::CheckBoxSize;
  auto* pBox = Panel(pButton);
  pBox->m_vAnchorMin = pBox->m_vAnchorMax = plVec2(0, 0.5f);
  pBox->m_vOffsets = plVec4(0, -fBox * 0.5f, fBox, fBox);
  pBox->m_Color = Field();
  pBox->m_fBorderWidth = 1.0f;
  pBox->m_BorderColor = Border();
  pBox->m_vCornerRadius = plVec4(3);
  pBox->m_fOpacity = bEnabled ? 1.0f : 0.5f;

  // checked: the selection fill with an accent edge and tick
  auto* pPlate = Panel(pBox);
  pPlate->m_Color = Selection();
  pPlate->m_fBorderWidth = 1.0f;
  pPlate->m_BorderColor = plAiEngineUi::WithAlpha(Accent(), 140);
  pPlate->m_vCornerRadius = plVec4(3);
  pPlate->m_sVisibleBinding = Entry(sName, ".On");

  auto* pMark = SvgImage(pBox, IconPath(Icon::Check));
  Fill(pMark, plVec4(1));
  pMark->m_Color = Accent();
  pMark->m_sVisibleBinding = Entry(sName, ".On");

  if (!sLabel.IsEmpty())
  {
    auto* pText = Text(pButton, sLabel);
    pText->m_vOffsets.x = fBox + 6.0f;
    pText->m_vOffsets.z = 0.0f;
    pText->m_uiFontSize = FontSize - 1;
    pText->m_Color = !bEnabled ? Dim() : (labelColor.a > 0 ? labelColor : Muted());
  }

  return pButton;
}

plUiButton* plQtAiEngineView::AddClickArea(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sTooltip, Action onClick, Action onContext)
{
  Control& control = Register(sName, Kind::Button, rect, sTooltip, true);
  control.m_OnClick = std::move(onClick);
  control.m_OnContext = std::move(onContext);

  plUiButton* pButton = plEditorUi::Button(pParent, plStringView(), sName);
  plAiEngineUi::Place(pButton, rect);
  pButton->m_Color = pButton->m_HoverColor = pButton->m_PressedColor = plColorGammaUB(0, 0, 0, 0);
  pButton->ClearChildren();
  return pButton;
}

plUiSlider* plQtAiEngineView::AddSlider(plUiElement* pParent, const plRectFloat& rect, plStringView sName, float fMin, float fMax, float fValue, plStringView sTooltip, ValueAction onValue)
{
  using namespace plEditorUi;

  Control& control = Register(sName, Kind::Slider, rect, sTooltip, true);
  control.m_sBinding = Entry(sName, ".Value");
  control.m_fValue = fValue;
  control.m_OnValue = std::move(onValue);
  GetBoard().SetEntryValue(control.m_sBinding, fValue);

  plUiSlider* pSlider = PL_DEFAULT_NEW(plUiSlider);
  pParent->m_Children.PushBack(pSlider);
  pSlider->m_sName = sName;
  plAiEngineUi::Place(pSlider, plRectFloat(rect.x, rect.y + rect.height * 0.5f - 4.0f, rect.width, 8.0f));
  pSlider->m_fMin = fMin;
  pSlider->m_fMax = fMax;
  pSlider->m_sValueBinding = control.m_sBinding;
  pSlider->m_Color = Raised();
  pSlider->m_FillColor = Border();
  pSlider->m_HandleColor = Muted();
  pSlider->m_vCornerRadius = plVec4(4);
  return pSlider;
}

plUiDrawList* plQtAiEngineView::AddDrawList(plUiElement* pParent, const plRectFloat& rect, plStringView sName)
{
  plUiDrawList* pList = plEditorUi::DrawList(pParent, sName, sName);
  plAiEngineUi::Place(pList, rect);
  return pList;
}

void plQtAiEngineView::AddAction(plStringView sName, plStringView sTooltip, Action onClick)
{
  Control& control = Register(sName, Kind::Button, plRectFloat(0, 0, 0, 0), sTooltip, true);
  control.m_OnClick = std::move(onClick);
}

plUiScrollView* plQtAiEngineView::AddScrollBody(plUiElement* pParent, const plRectFloat& rect, plStringView sName, float fPadding, float fSpacing)
{
  plUiScrollView* pScroll = PL_DEFAULT_NEW(plUiScrollView);
  pParent->m_Children.PushBack(pScroll);
  pScroll->m_sName = sName;
  plAiEngineUi::Place(pScroll, rect);
  pScroll->m_Direction = plUiStackDirection::Vertical;
  pScroll->m_Alignment = plUiStackAlignment::Stretch;
  pScroll->m_fSpacing = fSpacing;
  pScroll->m_vPadding = plVec4(fPadding);
  pScroll->m_Color = plColorGammaUB(0, 0, 0, 0);
  pScroll->m_bClipChildren = true;
  return pScroll;
}

void plQtAiEngineView::SetChecked(plStringView sName, bool bChecked)
{
  GetBoard().SetEntryValue(Entry(sName, ".On"), bChecked);
}

void plQtAiEngineView::SetSliderValue(plStringView sName, float fValue)
{
  auto it = m_Controls.Find(sName);
  if (!it.IsValid() || it.Value().m_Kind != Kind::Slider)
    return;

  it.Value().m_fValue = fValue;
  GetBoard().SetEntryValue(it.Value().m_sBinding, fValue);
}

void plQtAiEngineView::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  // fields commit on Enter
  if (sId.EndsWith(".Submit"))
  {
    const plString sName(plStringView(sId.GetStartPointer(), sId.GetEndPointer() - 7));
    if (m_Controls.Contains(sName))
      CommitField(sName);
    return;
  }

  auto it = m_Controls.Find(sId);
  if (!it.IsValid() || !it.Value().m_bEnabled || !it.Value().m_OnClick)
    return;

  // the action may rebuild the document and with it the control
  Action action = it.Value().m_OnClick;
  action();
}

void plQtAiEngineView::CommitField(const plString& sName)
{
  auto it = m_Controls.Find(sName);
  if (!it.IsValid() || it.Value().m_Kind != Kind::Field)
    return;

  Control& control = it.Value();
  const plString sText = GetBoard().GetEntryValue(plTempHashedString(control.m_sBinding), plString()).ConvertTo<plString>();
  if (sText == control.m_sCommitted)
    return;

  control.m_sCommitted = sText;

  if (control.m_OnCommit)
  {
    TextAction action = control.m_OnCommit;
    action(sText);
  }
}

void plQtAiEngineView::PollFocus()
{
  // a field commits when focus leaves it, as on Enter
  const plString sFocused(GetSurface()->GetCanvas().GetFocusedElementName());
  if (sFocused == m_sFocusedField)
    return;

  const plString sLeft = m_sFocusedField;
  m_sFocusedField = sFocused;

  if (!sLeft.IsEmpty())
    CommitField(sLeft);
}

void plQtAiEngineView::OnFocusLost()
{
  // another panel or window took the keys: the field being typed in commits, as when focus leaves it
  const plString sLeft = m_sFocusedField;
  m_sFocusedField.Clear();

  if (!sLeft.IsEmpty())
    CommitField(sLeft);
}

void plQtAiEngineView::PollSliders()
{
  // sliders write their entry while they are dragged
  for (auto it = m_Controls.GetIterator(); it.IsValid(); ++it)
  {
    Control& control = it.Value();
    if (control.m_Kind != Kind::Slider)
      continue;

    const float fValue = GetBoard().GetEntryValue(plTempHashedString(control.m_sBinding), control.m_fValue).ConvertTo<float>();
    if (plMath::IsEqual(fValue, control.m_fValue, 0.00001f))
      continue;

    control.m_fValue = fValue;

    if (control.m_OnValue)
    {
      ValueAction action = control.m_OnValue;
      action(fValue);
    }
  }
}

void plQtAiEngineView::OnPointer(const plEditorUiPointerEvent& e)
{
  using Type = plEditorUiPointerEvent::Type;
  using Button = plEditorUiPointerEvent::Button;

  if (e.m_Type == Type::Press)
  {
    m_sPressedElement = e.m_sElement;
    m_PressedRect = e.m_ElementRect;
    m_sContextElement.Clear();

    if (e.m_Button == Button::Right)
    {
      auto it = m_Controls.Find(e.m_sElement);
      if (it.IsValid() && it.Value().m_OnContext)
      {
        m_sContextElement = e.m_sElement;
        m_vContextPosition = e.m_vPosition;

        Action action = it.Value().m_OnContext;
        Defer(std::move(action));
        return;
      }
    }
  }

  OnViewPointer(e);
}

plVec2 plQtAiEngineView::GetMenuPosition(plStringView sAnchor) const
{
  if (!m_sContextElement.IsEmpty() && sAnchor == m_sContextElement)
    return m_vContextPosition;

  if (sAnchor == m_sPressedElement && m_PressedRect.width > 0.0f)
    return plVec2(m_PressedRect.x, m_PressedRect.y + m_PressedRect.height + 2.0f);

  auto it = m_Controls.Find(sAnchor);
  if (it.IsValid())
    return plVec2(it.Value().m_Rect.x, it.Value().m_Rect.y + it.Value().m_Rect.height + 2.0f);

  return GetSurface()->GetLogicalSize() * 0.5f;
}

void plQtAiEngineView::ShowMenu(plStringView sAnchor, const plEditorUiMenu& menu, PickAction onPick)
{
  m_MenuPick = std::move(onPick);
  plEditorUiMenuView::Show(*GetSurface(), GetMenuPosition(sAnchor), menu, plMakeDelegate(&plQtAiEngineView::OnMenuPick, this));
}

void plQtAiEngineView::ShowSearchMenu(plStringView sAnchor, plArrayPtr<const plEditorUiSearchMenuItem> items, plStringView sPlaceholder, PickAction onPick)
{
  m_MenuPick = std::move(onPick);
  plEditorUiSearchMenuView::Show(*GetSurface(), GetMenuPosition(sAnchor), items, sPlaceholder, plMakeDelegate(&plQtAiEngineView::OnMenuPick, this));
}

void plQtAiEngineView::OnMenuPick(plUInt32 uiId)
{
  if (!m_MenuPick)
    return;

  PickAction action = std::move(m_MenuPick);
  m_MenuPick = PickAction();
  action(uiId);
}

void plQtAiEngineView::Defer(Action action)
{
  std::weak_ptr<bool> alive = m_pAlive;
  QTimer::singleShot(0, [alive, action = std::move(action)]()
    {
      if (!alive.expired())
        action(); });
}
