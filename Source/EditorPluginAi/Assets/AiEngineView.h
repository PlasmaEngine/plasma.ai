#pragma once

#include <EditorUi/Content.h>
#include <EditorUi/Menu.h>
#include <EditorUi/SearchMenu.h>
#include <EditorUi/Surface.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Containers/Map.h>
#include <GameUiPlugin/Elements/UiDrawList.h>

#include <functional>
#include <memory>

class plCurve1D;
struct plMsgUiEvent;

/// \brief Theme values and painters shared by the engine-drawn AI editors (Wave 5 mockup tokens).
namespace plAiEngineUi
{
  /// A header row: 24 px controls with 3 px above and below.
  inline constexpr float HeaderRowHeight = 30.0f;
  inline constexpr float BadgeHeight = 16.0f;
  inline constexpr float SmallButtonSize = 20.0f;
  inline constexpr float CheckBoxSize = 14.0f;
  inline constexpr float ChoiceHeight = 22.0f;

  /// The AI category colour (plColorScheme).
  plColorGammaUB AiColor();

  /// Values that pass easily: the theme's green.
  plColorGammaUB GoodColor();

  /// The faint lines of curve thumbnails and canvases.
  plColorGammaUB GridColor();

  plColorGammaUB WithAlpha(const plColorGammaUB& color, plUInt8 uiAlpha);
  plColorGammaUB Mix(const plColorGammaUB& a, const plColorGammaUB& b, float t);

  /// The edge colour of a behavior category band: Interrupt danger, Combat physics, Command gameplay, Investigate
  /// lighting, ActiveIdle animation, Idle utilities, Fallback prefabs.
  plColorGammaUB CategoryColor(plInt32 iCategory);

  /// The behavior categories in ladder order, highest priority first.
  plArrayPtr<const plInt32> GetCategoryLadder();

  /// Places an element at rect, in offsets from its parent's top-left corner.
  void Place(plUiElement* pElement, const plRectFloat& rect);

  /// One line of text at rect in the default font, or in the code font for numbers and summaries.
  plUiText* Label(plUiElement* pParent, const plRectFloat& rect, plStringView sText, const plColorGammaUB& color, plUInt32 uiFontSize = plEditorUi::FontSize - 1, bool bCode = false, plStringView sBinding = {});

  /// A label whose text and colour come from bindings (sTintBinding holds a plColor).
  plUiBoundText* TintedLabel(plUiElement* pParent, const plRectFloat& rect, plStringView sBinding, plStringView sTintBinding, plUInt32 uiFontSize = plEditorUi::FontSize - 1, bool bCode = false);

  float TextWidth(plStringView sText, plUInt32 uiFontSize = plEditorUi::FontSize - 1);
  float CodeWidth(plStringView sText, plUInt32 uiFontSize = plEditorUi::FontSize - 2);

  /// A small uppercase tag in the code face on a faint tint of its colour. Returns its width.
  float BadgeWidth(plStringView sText);
  float Badge(plUiElement* pParent, float x, float y, plStringView sText, const plColorGammaUB& color);

  /// A section header as one row of a stacked body: a 3 px edge in color, the uppercase title, an optional count
  /// in the code face and an optional hint. Tools go at its right end, in the fRightReserve px the hint leaves free.
  plUiPanel* Section(plUiElement* pParent, plStringView sTitle, const plColorGammaUB& color, plStringView sCount = {}, plStringView sHint = {}, float fRightReserve = 0.0f);

  inline constexpr float SectionHeight = 26.0f;

  /// A response curve thumbnail in rect: the well, its centre cross and the curve in the AI colour. A null or empty
  /// curve draws the identity. Selected thumbnails get an accent border.
  void DrawCurve(plUiDrawListWriter& ref_writer, const plRectFloat& rect, const plCurve1D* pCurve, bool bLegacyDomain, bool bSelected);

  /// The dashed "no curve" placeholder of filter-only tests.
  void DrawNoCurve(plUiDrawListWriter& ref_writer, const plRectFloat& rect);

  /// A five-pointed star (the winner mark; the fonts have no star glyph).
  void DrawStar(plUiDrawListWriter& ref_writer, const plVec2& vCenter, float fRadius, const plColorGammaUB& color);

  /// "1.50" with the given decimals.
  plString FormatNumber(double fValue, plUInt32 uiDecimals);

  /// Up to uiMaxDecimals, without trailing zeros but with at least one decimal: "4.0", "0.25".
  plString FormatTrimmed(double fValue, plUInt32 uiMaxDecimals);

  /// Parses a number field's text; false when it is not a number.
  bool ParseNumber(plStringView sText, double& out_fValue);

  /// Parses and clamps a number field's text; false when it is not a number.
  bool ParseClamped(plStringView sText, double fMin, double fMax, double& out_fValue);
} // namespace plAiEngineUi

/// \brief Places controls left to right and wraps to a new line when a group does not fit (the mockup's flex-wrap).
class plAiFlowLayout
{
public:
  plAiFlowLayout(float fLeft, float fTop, float fRight, float fLineHeight, float fGap);

  /// The width of controls side by side with the gap between them.
  float GetWidth(std::initializer_list<float> widths) const;

  /// Keeps the next fWidth on one line: wraps first when it does not fit.
  void Group(float fWidth);

  /// The next control, vertically centred on the line.
  plRectFloat Take(float fWidth, float fHeight = plEditorUi::ControlHeight);

  /// The following group of fWidth ends at the right edge (on a new line when it does not fit).
  void AlignRight(float fWidth);

  float GetLineTop() const { return m_fY; }
  float GetBottom() const { return m_fY + m_fLineHeight; }
  float GetRight() const { return m_fRight; }

private:
  void Wrap();

  float m_fLeft = 0.0f;
  float m_fRight = 0.0f;
  float m_fX = 0.0f;
  float m_fY = 0.0f;
  float m_fLineHeight = 0.0f;
  float m_fGap = 0.0f;
  bool m_bLineEmpty = true;
};

/// \brief Base of the engine-drawn AI editor views.
///
/// The view rebuilds its document when Invalidate is called or its size changes. Controls register what they do
/// under their element name while the document is built: clicks, right-clicks, committed field text and slider
/// values. Text and number fields commit once per edit, on Enter or when focus leaves them (W5-D5); a field that
/// has focus keeps its text and focus across a rebuild.
///
/// A view is an editor UI content: the window owns it as a panel (plDocumentWindow::AddPanel), which the engine shell
/// shows in the document's frame.
class plAiEngineView : public plEditorUiContent
{
public:
  using Action = std::function<void()>;
  using TextAction = std::function<void(const plString&)>;
  using PickAction = std::function<void(plUInt32)>;
  using ValueAction = std::function<void(float)>;

  explicit plAiEngineView(const char* szName);
  ~plAiEngineView();

  /// Rebuilds the document in the next UI frame.
  void Invalidate();

  /// Does what clicking the control does (tests and keyboard paths).
  bool ActivateControl(plStringView sName);

  /// Commits text into a field as typing and Enter would.
  bool CommitControl(plStringView sName, plStringView sText);

  /// Whether a control with this element name exists in the current document.
  bool HasControl(plStringView sName) const;

protected:
  /// Builds the content into the root panel, which fills the view.
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) = 0;

  /// Pointer events of the surface, after the registered controls saw them (draw-list views hit-test here).
  virtual void OnViewPointer(const plEditorUiPointerEvent& e) {}

  /// Every UI frame, after a pending rebuild.
  virtual void OnViewUpdate() {}

  /// While true, rebuilding waits (a document transaction is open).
  virtual bool IsRebuildBlocked() const { return false; }

  virtual void OnUiUpdate() override;
  virtual void OnUiEvent(const plMsgUiEvent& event) override;
  virtual void OnFocusLost() override;

  plBlackboard& GetBoard() { return GetSurface()->GetBlackboard(); }

  // Controls. Each one is placed at rect inside pParent and registered under sName.

  plUiTextInput* AddTextField(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sText, plStringView sPlaceholder, plStringView sTooltip, TextAction onCommit);
  plUiTextInput* AddNumberField(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sText, plStringView sUnit, plStringView sTooltip, TextAction onCommit);

  /// A dropdown: the field well, a choice and a chevron; an optional dot before the choice. bValueRight puts the
  /// choice at the right end, as the choice fields inside cards do.
  plUiButton* AddDropdown(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sText, plStringView sTooltip, Action onClick, bool bEnabled = true, bool bValueRight = false, plColorGammaUB dotColor = plColorGammaUB(0, 0, 0, 0));

  /// A borderless icon button (20 or 24 px); disabled ones dim their icon.
  plUiButton* AddIconButton(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plEditorUi::Icon icon, plStringView sTooltip, Action onClick, bool bEnabled = true);

  /// A text button: a plate with a hairline border, the label and an optional icon in front.
  plUiButton* AddTextButton(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sLabel, plStringView sTooltip, Action onClick, bool bAddIcon = false, bool bEnabled = true);

  /// A check box (selection fill and accent tick when on) with its label; the whole rect toggles.
  plUiButton* AddCheckBox(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sLabel, bool bChecked, plStringView sTooltip, Action onToggle, bool bEnabled = true, plColorGammaUB labelColor = plColorGammaUB(0, 0, 0, 0));

  /// An invisible button over rect: selects a card, opens a curve. onContext runs on a right-click.
  plUiButton* AddClickArea(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sTooltip, Action onClick, Action onContext = {});

  /// A slider over [fMin, fMax]; onValue runs while it is dragged.
  plUiSlider* AddSlider(plUiElement* pParent, const plRectFloat& rect, plStringView sName, float fMin, float fMax, float fValue, plStringView sTooltip, ValueAction onValue);

  /// A drawing at rect that shows what is published to sName.
  plUiDrawList* AddDrawList(plUiElement* pParent, const plRectFloat& rect, plStringView sName);

  /// Registers what clicking an element built elsewhere does (the action of an empty state).
  void AddAction(plStringView sName, plStringView sTooltip, Action onClick);

  /// A vertically scrolling body at rect; its scroll position survives rebuilds.
  plUiScrollView* AddScrollBody(plUiElement* pParent, const plRectFloat& rect, plStringView sName, float fPadding, float fSpacing);

  void SetChecked(plStringView sName, bool bChecked);
  void SetSliderValue(plStringView sName, float fValue);

  /// A menu below a control (or at the last right-click when the control was right-clicked).
  void ShowMenu(plStringView sAnchor, const plEditorUiMenu& menu, PickAction onPick);

  /// A searchable type menu below a control.
  void ShowSearchMenu(plStringView sAnchor, plArrayPtr<const plEditorUiSearchMenuItem> items, plStringView sPlaceholder, PickAction onPick);

  /// Runs after the current UI dispatch: modal dialogs, edits that rebuild the document.
  void Defer(Action action);

private:
  enum class Kind : plUInt8
  {
    Button,
    Field,
    CheckBox,
    Slider,
  };

  struct Control
  {
    Kind m_Kind = Kind::Button;
    plString m_sBinding;
    plString m_sCommitted;
    float m_fValue = 0.0f;
    plRectFloat m_Rect = plRectFloat(0, 0, 0, 0);
    bool m_bEnabled = true;
    Action m_OnClick;
    Action m_OnContext;
    TextAction m_OnCommit;
    ValueAction m_OnValue;
  };

  Control& Register(plStringView sName, Kind kind, const plRectFloat& rect, plStringView sTooltip, bool bEnabled);
  void Rebuild();
  void CommitField(const plString& sName);
  void PollFocus();
  void PollSliders();
  void OnPointer(const plEditorUiPointerEvent& e);
  plVec2 GetMenuPosition(plStringView sAnchor) const;
  void OnMenuPick(plUInt32 uiId);

  plMap<plString, Control> m_Controls;
  plString m_sFocusedField;
  plString m_sKeepFocus;
  plString m_sPressedElement;
  plRectFloat m_PressedRect = plRectFloat(0, 0, 0, 0);
  plString m_sContextElement;
  plVec2 m_vContextPosition = plVec2::MakeZero();
  PickAction m_MenuPick;
  plEventSubscriptionID m_PointerSubscription = 0;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bRebuild = true;

  /// Deferred actions run only while the view lives.
  std::shared_ptr<bool> m_pAlive;
};
