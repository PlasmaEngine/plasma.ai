#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiBehaviorAsset.h>
#include <EditorPluginAi/Assets/AiBehaviorAssetWindow.moc.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <AiPlugin/UtilityAI/Decision/AiBehaviorLogic.h>
#include <AiPlugin/UtilityAI/Decision/AiInput.h>

#include <EditorFramework/Assets/AssetBrowserDlg.moc.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <Foundation/Math/ColorScheme.h>
#include <GuiFoundation/Dialogs/CurveEditDlg.moc.h>
#include <GuiFoundation/DockPanels/DocumentPanel.moc.h>
#include <GuiFoundation/PropertyGrid/Implementation/PropertyWidget.moc.h>
#include <GuiFoundation/Widgets/SearchableMenu.moc.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QToolButton>

namespace
{
  struct CurvePresetPoint
  {
    double x;
    double v;
  };

  struct CurvePreset
  {
    const char* szName;
    bool bSmooth;
    plUInt32 uiCount;
    CurvePresetPoint pts[4];
  };

  static const CurvePreset s_CurvePresets[] = {
    {"Linear Rise", false, 2, {{0.0, 0.0}, {1.0, 1.0}}},
    {"Linear Fall", false, 2, {{0.0, 1.0}, {1.0, 0.0}}},
    {"Smooth Rise", true, 2, {{0.0, 0.0}, {1.0, 1.0}}},
    {"Smooth Fall", true, 2, {{0.0, 1.0}, {1.0, 0.0}}},
    {"Gate (floor 0.05)", false, 4, {{0.0, 0.05}, {0.4, 0.05}, {0.75, 1.0}, {1.0, 1.0}}},
    {"Bias (floor 0.35)", true, 2, {{0.0, 1.0}, {1.0, 0.35}}},
    {"Bell", true, 3, {{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}}},
    {"Constant 1", false, 2, {{0.0, 1.0}, {1.0, 1.0}}},
    {"Clear (use normalized input)", false, 0, {}},
  };

  // ladder order, highest priority first
  static const plInt32 s_CategoryLadder[] = {
    plAiBehaviorCategory::Interrupt,
    plAiBehaviorCategory::Combat,
    plAiBehaviorCategory::Command,
    plAiBehaviorCategory::Investigate,
    plAiBehaviorCategory::ActiveIdle,
    plAiBehaviorCategory::Idle,
    plAiBehaviorCategory::Fallback,
  };

  static QIcon MakeSwatchIcon(const QColor& color)
  {
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(0.5, 0.5, 11, 11), 2, 2);
    return QIcon(pm);
  }

  static QString MonoStyle()
  {
    return QStringLiteral("font-family: Consolas, 'Cascadia Mono', monospace;");
  }

  static const char* InputGroup(const plRTTI* pRtti)
  {
    const plStringView sName = pRtti->GetTypeName();

    if (sName.FindSubString("DistanceToTarget") || sName.FindSubString("TargetConfidence") || sName.FindSubString("AngleToTarget"))
      return "Target";
    if (sName.FindSubString("Blackboard") || sName.FindSubString("DistanceToPosition"))
      return "Blackboard";
    if (sName.FindSubString("TimeSince"))
      return "Timing";
    if (sName.FindSubString("Cover") || sName.FindSubString("Exposure"))
      return "Tactical";
    if (sName.FindSubString("Squad"))
      return "Squad";
    return "Misc";
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plQtAiConsiderationCard
//////////////////////////////////////////////////////////////////////////

plQtAiConsiderationCard::plQtAiConsiderationCard(QWidget* pParent, plQtAiBehaviorAssetDocumentWindow* pWindow, const plUuid& objectGuid)
  : QFrame(pParent)
  , m_pWindow(pWindow)
  , m_ObjectGuid(objectGuid)
{
  setObjectName("AiConsiderationCard");
  setProperty("cardSelected", false);
  setCursor(Qt::PointingHandCursor);

  QHBoxLayout* pMain = new QHBoxLayout(this);
  pMain->setContentsMargins(10, 7, 8, 7);
  pMain->setSpacing(12);

  // input identity
  {
    QVBoxLayout* pId = new QVBoxLayout();
    pId->setContentsMargins(0, 0, 0, 0);
    pId->setSpacing(2);

    QHBoxLayout* pTypeRow = new QHBoxLayout();
    pTypeRow->setContentsMargins(0, 0, 0, 0);
    pTypeRow->setSpacing(6);

    m_pTypeLabel = new QLabel(this);
    {
      QFont f = m_pTypeLabel->font();
      f.setBold(true);
      m_pTypeLabel->setFont(f);
    }

    m_pTargetChip = new QLabel("TARGET", this);
    m_pTargetChip->setStyleSheet("background-color: #c2a05c; color: #241d10; border-radius: 7px; padding: 0px 6px; font-size: 9px; font-weight: bold;");
    m_pTargetChip->setVisible(false);

    pTypeRow->addWidget(m_pTypeLabel);
    pTypeRow->addWidget(m_pTargetChip);
    pTypeRow->addStretch();

    m_pSummaryLabel = new QLabel(this);
    m_pSummaryLabel->setStyleSheet(QString("color: #9aa0a8; %1 font-size: 10px;").arg(MonoStyle()));

    pId->addLayout(pTypeRow);
    pId->addWidget(m_pSummaryLabel);

    QWidget* pIdHost = new QWidget(this);
    pIdHost->setLayout(pId);
    pIdHost->setMinimumWidth(220);
    pMain->addWidget(pIdHost, 3);
  }

  // normalization range
  {
    QHBoxLayout* pRange = new QHBoxLayout();
    pRange->setContentsMargins(0, 0, 0, 0);
    pRange->setSpacing(4);

    QLabel* pIn = new QLabel("in", this);
    pIn->setStyleSheet("color: #9aa0a8; font-size: 10px;");

    m_pMin = new QDoubleSpinBox(this);
    m_pMin->setRange(-100000.0, 100000.0);
    m_pMin->setDecimals(2);
    m_pMin->setSingleStep(0.1);
    m_pMin->setToolTip("InputMin - raw values at or below this normalize to 0");

    QLabel* pArrow = new QLabel(QString::fromUtf8("\xE2\x86\x92"), this); // →

    m_pMax = new QDoubleSpinBox(this);
    m_pMax->setRange(-100000.0, 100000.0);
    m_pMax->setDecimals(2);
    m_pMax->setSingleStep(0.1);
    m_pMax->setToolTip("InputMax - raw values at or above this normalize to 1");

    pRange->addWidget(pIn);
    pRange->addWidget(m_pMin);
    pRange->addWidget(pArrow);
    pRange->addWidget(m_pMax);

    QWidget* pRangeHost = new QWidget(this);
    pRangeHost->setLayout(pRange);
    pMain->addWidget(pRangeHost, 0);

    connect(m_pMin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &plQtAiConsiderationCard::onMinEdited);
    connect(m_pMax, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &plQtAiConsiderationCard::onMaxEdited);
  }

  // response curve
  {
    m_pCurveButton = new plQtCurve1DButtonWidget(this);
    m_pCurveButton->setMinimumSize(140, 40);
    m_pCurveButton->setMaximumHeight(44);
    m_pCurveButton->setToolTip("Click to edit the response curve.\nRight-click for presets.");
    m_pCurveButton->setContextMenuPolicy(Qt::CustomContextMenu);
    pMain->addWidget(m_pCurveButton, 2);

    connect(m_pCurveButton, &plQtCurve1DButtonWidget::clicked, this, &plQtAiConsiderationCard::onCurveClicked);
    connect(m_pCurveButton, &QWidget::customContextMenuRequested, this, &plQtAiConsiderationCard::onCurveContextMenu);
  }

  // order / remove
  {
    QVBoxLayout* pOps = new QVBoxLayout();
    pOps->setContentsMargins(0, 0, 0, 0);
    pOps->setSpacing(1);

    m_pUp = new QToolButton(this);
    m_pUp->setText(QString::fromUtf8("\xE2\x96\xB2")); // ▲
    m_pUp->setAutoRaise(true);
    m_pUp->setToolTip("Move up");

    m_pDown = new QToolButton(this);
    m_pDown->setText(QString::fromUtf8("\xE2\x96\xBC")); // ▼
    m_pDown->setAutoRaise(true);
    m_pDown->setToolTip("Move down");

    pOps->addWidget(m_pUp);
    pOps->addWidget(m_pDown);

    pMain->addLayout(pOps);

    m_pRemove = new QToolButton(this);
    m_pRemove->setText(QString::fromUtf8("\xE2\x9C\x95")); // ✕
    m_pRemove->setAutoRaise(true);
    m_pRemove->setToolTip("Remove this consideration");
    m_pRemove->setStyleSheet("color: #e05555;");
    pMain->addWidget(m_pRemove);

    connect(m_pUp, &QToolButton::clicked, this, [this]() { m_pWindow->MoveConsideration(m_ObjectGuid, -1); });
    connect(m_pDown, &QToolButton::clicked, this, [this]() { m_pWindow->MoveConsideration(m_ObjectGuid, +1); });
    connect(m_pRemove, &QToolButton::clicked, this, [this]() { m_pWindow->RemoveConsideration(m_ObjectGuid); });
  }
}

void plQtAiConsiderationCard::RefreshFromNative(const plAiConsiderationObject* pNative, plUInt32 uiIndex, plUInt32 uiCount, bool bSelected)
{
  m_bUpdating = true;

  const plRTTI* pInputRtti = (pNative && pNative->m_pInput) ? pNative->m_pInput->GetDynamicRTTI() : nullptr;
  m_pTypeLabel->setText(plAiAssetUi::PrettyInputName(pInputRtti));
  m_pTargetChip->setVisible(pNative && pNative->m_pInput && pNative->m_pInput->NeedsTarget());
  m_pSummaryLabel->setText(pNative ? plAiAssetUi::InputParamSummary(pNative->m_pInput) : QString());

  if (pNative)
  {
    if (!m_pMin->hasFocus())
      m_pMin->setValue(pNative->m_fInputMin);
    if (!m_pMax->hasFocus())
      m_pMax->setValue(pNative->m_fInputMax);
  }

  m_pUp->setEnabled(uiIndex > 0);
  m_pDown->setEnabled(uiIndex + 1 < uiCount);

  // curve preview through the shared button widget (extents locked 0..1, default 1)
  {
    plObjectAccessorBase* pAccessor = m_pWindow->GetDocument()->GetObjectAccessor();
    if (const plDocumentObject* pObject = pAccessor->GetObject(m_ObjectGuid))
    {
      const plDocumentObject* pCurve = pAccessor->GetChildObjectByName(pObject, "ResponseCurve", plVariant());
      if (pCurve != nullptr)
      {
        m_pCurveButton->UpdatePreview(pAccessor, pCurve, plAiAssetUi::CurveColor(), 0.0, true, 1.0, true, 1.0, 0.0, 1.0);
      }
    }
  }

  if (property("cardSelected").toBool() != bSelected)
  {
    setProperty("cardSelected", bSelected);
    style()->unpolish(this);
    style()->polish(this);
  }

  m_bUpdating = false;
}

void plQtAiConsiderationCard::mousePressEvent(QMouseEvent* pEvent)
{
  m_pWindow->SelectConsideration(m_ObjectGuid);
  QFrame::mousePressEvent(pEvent);
}

void plQtAiConsiderationCard::onMinEdited(double fValue)
{
  if (m_bUpdating)
    return;
  m_pWindow->SetConsiderationRange(m_ObjectGuid, true, fValue);
}

void plQtAiConsiderationCard::onMaxEdited(double fValue)
{
  if (m_bUpdating)
    return;
  m_pWindow->SetConsiderationRange(m_ObjectGuid, false, fValue);
}

void plQtAiConsiderationCard::onCurveClicked()
{
  m_pWindow->SelectConsideration(m_ObjectGuid);
  m_pWindow->OpenCurveEditor(m_ObjectGuid);
}

void plQtAiConsiderationCard::onCurveContextMenu(const QPoint& pos)
{
  QMenu menu(this);

  QMenu* pPresets = menu.addMenu("Presets");
  for (plUInt32 i = 0; i < plQtAiBehaviorAssetDocumentWindow::GetCurvePresetCount(); ++i)
  {
    QAction* pAction = pPresets->addAction(plQtAiBehaviorAssetDocumentWindow::GetCurvePresetName(i));
    const plUInt32 uiPreset = i;
    connect(pAction, &QAction::triggered, this, [this, uiPreset]() { m_pWindow->ApplyCurvePreset(m_ObjectGuid, uiPreset); });
  }

  QAction* pEdit = menu.addAction("Edit Curve...");
  connect(pEdit, &QAction::triggered, this, [this]() { m_pWindow->OpenCurveEditor(m_ObjectGuid); });

  menu.exec(m_pCurveButton->mapToGlobal(pos));
}

//////////////////////////////////////////////////////////////////////////
// plQtAiUtilityBar
//////////////////////////////////////////////////////////////////////////

plQtAiUtilityBar::plQtAiUtilityBar(QWidget* pParent)
  : QWidget(pParent)
{
  setMinimumHeight(18);
  setMaximumHeight(18);
}

void plQtAiUtilityBar::SetState(float fUtility, float fCommitThreshold, bool bVetoed)
{
  m_fUtility = fUtility;
  m_fCommitThreshold = fCommitThreshold;
  m_bVetoed = bVetoed;
  update();
}

void plQtAiUtilityBar::paintEvent(QPaintEvent* pEvent)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
  p.setPen(QPen(QColor(21, 22, 23)));
  p.setBrush(QColor(31, 32, 33));
  p.drawRoundedRect(r, 3, 3);

  const QColor accent = plAiAssetUi::AccentColor();

  if (m_fUtility > 0.0f)
  {
    QRectF fill = r.adjusted(1, 1, -1, -1);
    fill.setWidth(fill.width() * plMath::Clamp(m_fUtility, 0.0f, 1.0f));

    QLinearGradient grad(fill.topLeft(), fill.topRight());
    grad.setColorAt(0.0, plAiAssetUi::AccentColorDim());
    grad.setColorAt(1.0, accent);
    p.setPen(Qt::NoPen);
    p.setBrush(m_bVetoed ? QBrush(QColor(90, 52, 56)) : QBrush(grad));
    p.drawRoundedRect(fill, 2, 2);
  }

  if (m_fCommitThreshold >= 0.0f)
  {
    const double x = r.left() + r.width() * plMath::Clamp(m_fCommitThreshold, 0.0f, 1.0f);
    QPen pen(QColor(252, 196, 25)); // scheme yellow
    pen.setStyle(Qt::DashLine);
    pen.setWidthF(1.5);
    p.setPen(pen);
    p.drawLine(QPointF(x, r.top() + 1), QPointF(x, r.bottom() - 1));
  }
}

//////////////////////////////////////////////////////////////////////////
// plQtAiScorePreviewPanel
//////////////////////////////////////////////////////////////////////////

plQtAiScorePreviewPanel::plQtAiScorePreviewPanel(QWidget* pParent, plAiBehaviorAssetDocument* pDocument)
  : QWidget(pParent)
  , m_pDocument(pDocument)
{
  setMinimumWidth(300);

  QVBoxLayout* pMain = new QVBoxLayout(this);
  pMain->setContentsMargins(8, 8, 8, 8);
  pMain->setSpacing(6);

  auto addSectionLabel = [&](const char* szText) {
    QLabel* pLabel = new QLabel(szText, this);
    pLabel->setStyleSheet(QString("color: #8f959b; font-size: 9px; letter-spacing: 1px; %1").arg(MonoStyle()));
    pMain->addWidget(pLabel);
    return pLabel;
  };

  addSectionLabel("SIMULATED SITUATION");

  m_pRowsHost = new QWidget(this);
  m_pRowsLayout = new QVBoxLayout(m_pRowsHost);
  m_pRowsLayout->setContentsMargins(0, 0, 0, 0);
  m_pRowsLayout->setSpacing(3);
  pMain->addWidget(m_pRowsHost);

  {
    QHBoxLayout* pScaleRow = new QHBoxLayout();
    QLabel* pLabel = new QLabel("Archetype ×scale", this);
    pLabel->setStyleSheet("color: #a2a7ab; font-size: 11px;");
    m_pWeightScale = new QDoubleSpinBox(this);
    m_pWeightScale->setRange(0.0, 10.0);
    m_pWeightScale->setSingleStep(0.05);
    m_pWeightScale->setValue(1.0);
    m_pWeightScale->setDecimals(2);
    m_pWeightScale->setToolTip("Simulates the WeightScale an archetype applies to this behavior.");
    pScaleRow->addWidget(pLabel);
    pScaleRow->addStretch();
    pScaleRow->addWidget(m_pWeightScale);
    pMain->addLayout(pScaleRow);

    connect(m_pWeightScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &plQtAiScorePreviewPanel::onAnyInputChanged);
  }

  {
    QHBoxLayout* pToggles = new QHBoxLayout();
    m_pRunning = new QCheckBox("running", this);
    m_pRunning->setToolTip("When active, a challenger must beat the current score plus the commit bonus.");
    m_pCooldown = new QCheckBox("on cooldown", this);
    m_pCooldown->setToolTip("ScoreUtility returns 0 while TimeSinceDeactivation < Cooldown.");
    pToggles->addWidget(m_pRunning);
    pToggles->addWidget(m_pCooldown);
    pToggles->addStretch();
    pMain->addLayout(pToggles);

    connect(m_pRunning, &QCheckBox::toggled, this, &plQtAiScorePreviewPanel::onAnyInputChanged);
    connect(m_pCooldown, &QCheckBox::toggled, this, &plQtAiScorePreviewPanel::onAnyInputChanged);
  }

  {
    QFrame* pLine = new QFrame(this);
    pLine->setFrameShape(QFrame::HLine);
    pLine->setStyleSheet("color: #3c3f45;");
    pMain->addWidget(pLine);
  }

  addSectionLabel("RESULT — MIRRORS ScoreUtility");

  {
    QGridLayout* pGrid = new QGridLayout();
    pGrid->setContentsMargins(0, 0, 0, 0);
    pGrid->setHorizontalSpacing(8);
    pGrid->setVerticalSpacing(2);

    auto addMathRow = [&](int iRow, const char* szLabel, QLabel*& out_pValue) {
      QLabel* pLabel = new QLabel(szLabel, this);
      pLabel->setStyleSheet(QString("color: #8f959b; font-size: 10px; %1").arg(MonoStyle()));
      out_pValue = new QLabel("-", this);
      out_pValue->setStyleSheet(QString("color: #e2e4e6; font-size: 10px; %1").arg(MonoStyle()));
      out_pValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      pGrid->addWidget(pLabel, iRow, 0);
      pGrid->addWidget(out_pValue, iRow, 1);
    };

    addMathRow(0, "product of factors", m_pProduct);
    addMathRow(1, "after n-axis compensation", m_pCompensated);

    pMain->addLayout(pGrid);
  }

  {
    QHBoxLayout* pBig = new QHBoxLayout();
    m_pUtilityValue = new QLabel("1.00", this);
    m_pUtilityValue->setStyleSheet(QString("color: white; font-size: 20px; font-weight: bold; %1").arg(MonoStyle()));
    m_pCategoryLabel = new QLabel(this);
    m_pCategoryLabel->setStyleSheet(QString("font-size: 10px; %1").arg(MonoStyle()));
    pBig->addWidget(m_pUtilityValue);
    pBig->addWidget(m_pCategoryLabel);
    pBig->addStretch();
    pMain->addLayout(pBig);
  }

  m_pBar = new plQtAiUtilityBar(this);
  pMain->addWidget(m_pBar);

  m_pCommitNote = new QLabel(this);
  m_pCommitNote->setWordWrap(true);
  m_pCommitNote->setStyleSheet(QString("color: #6d737a; font-size: 10px; %1").arg(MonoStyle()));
  pMain->addWidget(m_pCommitNote);

  pMain->addStretch();
}

void plQtAiScorePreviewPanel::RebuildRows()
{
  m_Rows.Clear();

  // delete all previous row widgets
  while (QLayoutItem* pItem = m_pRowsLayout->takeAt(0))
  {
    if (pItem->widget())
      pItem->widget()->deleteLater();
    delete pItem;
  }

  const plAiBehaviorAssetObject* pProps = m_pDocument->GetProperties();

  for (plUInt32 i = 0; i < pProps->m_Considerations.GetCount(); ++i)
  {
    const plAiConsiderationObject* pCons = pProps->m_Considerations[i];
    if (pCons == nullptr || pCons->m_pInput == nullptr)
      continue;

    QWidget* pRowHost = new QWidget(m_pRowsHost);
    QGridLayout* pGrid = new QGridLayout(pRowHost);
    pGrid->setContentsMargins(0, 0, 0, 0);
    pGrid->setHorizontalSpacing(6);
    pGrid->setVerticalSpacing(0);

    Row& row = m_Rows.ExpandAndGetRef();
    row.m_fMin = pCons->m_fInputMin;
    row.m_fMax = pCons->m_fInputMax;

    row.m_pName = new QLabel(plAiAssetUi::PrettyInputName(pCons->m_pInput->GetDynamicRTTI()), pRowHost);
    row.m_pName->setStyleSheet("color: #a2a7ab; font-size: 10px;");
    row.m_pName->setMinimumWidth(110);

    row.m_pSlider = new QSlider(Qt::Horizontal, pRowHost);
    row.m_pSlider->setRange(0, 1000);
    row.m_pSlider->setValue(700);

    row.m_pRaw = new QLabel(pRowHost);
    row.m_pRaw->setStyleSheet(QString("color: #e2e4e6; font-size: 10px; %1").arg(MonoStyle()));
    row.m_pRaw->setMinimumWidth(38);
    row.m_pRaw->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    row.m_pOut = new QLabel(pRowHost);
    row.m_pOut->setStyleSheet(QString("font-size: 10px; %1").arg(MonoStyle()));
    row.m_pOut->setMinimumWidth(38);
    row.m_pOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.m_pOut->setToolTip("Curve output for this consideration (its factor in the product)");

    pGrid->addWidget(row.m_pName, 0, 0);
    pGrid->addWidget(row.m_pSlider, 0, 1);
    pGrid->addWidget(row.m_pRaw, 0, 2);
    pGrid->addWidget(row.m_pOut, 0, 3);

    m_pRowsLayout->addWidget(pRowHost);

    connect(row.m_pSlider, &QSlider::valueChanged, this, &plQtAiScorePreviewPanel::onAnyInputChanged);
  }

  if (m_Rows.IsEmpty())
  {
    QLabel* pEmpty = new QLabel("add considerations to simulate their scoring here", m_pRowsHost);
    pEmpty->setStyleSheet("color: #6d737a; font-size: 10px; font-style: italic;");
    m_pRowsLayout->addWidget(pEmpty);
  }

  RefreshValues();
}

void plQtAiScorePreviewPanel::RefreshValues()
{
  const plAiBehaviorAssetObject* pProps = m_pDocument->GetProperties();

  // rows were created only for considerations with an input, in order
  plUInt32 uiRow = 0;
  for (plUInt32 i = 0; i < pProps->m_Considerations.GetCount() && uiRow < m_Rows.GetCount(); ++i)
  {
    const plAiConsiderationObject* pCons = pProps->m_Considerations[i];
    if (pCons == nullptr || pCons->m_pInput == nullptr)
      continue;

    Row& row = m_Rows[uiRow];
    row.m_fMin = pCons->m_fInputMin;
    row.m_fMax = pCons->m_fInputMax;
    row.m_pName->setText(plAiAssetUi::PrettyInputName(pCons->m_pInput->GetDynamicRTTI()));
    ++uiRow;
  }

  Recompute();
}

void plQtAiScorePreviewPanel::onAnyInputChanged()
{
  Recompute();
}

void plQtAiScorePreviewPanel::Recompute()
{
  const plAiBehaviorAssetObject* pProps = m_pDocument->GetProperties();

  plHybridArray<float, 8> factors;
  bool bVetoed = false;

  plUInt32 uiRow = 0;
  for (plUInt32 i = 0; i < pProps->m_Considerations.GetCount() && uiRow < m_Rows.GetCount(); ++i)
  {
    const plAiConsiderationObject* pCons = pProps->m_Considerations[i];
    if (pCons == nullptr || pCons->m_pInput == nullptr)
      continue;

    Row& row = m_Rows[uiRow];
    ++uiRow;

    const float t = row.m_pSlider->value() / 1000.0f;
    const float fRaw = plMath::Lerp(row.m_fMin, row.m_fMax, t);

    plCurve1D curve;
    plAiAssetUi::BuildRuntimeCurve(pCons->m_ResponseCurve, curve);

    const float fOut = plAiAssetUi::EvaluateConsideration(curve, fRaw, row.m_fMin, row.m_fMax);
    factors.PushBack(fOut);

    row.m_pRaw->setText(QString::number(fRaw, 'f', 2));
    row.m_pOut->setText(QString::number(fOut, 'f', 2));

    if (fOut <= 0.0005f)
    {
      bVetoed = true;
      row.m_pOut->setStyleSheet(QString("color: #ff6b6b; font-size: 10px; %1").arg(MonoStyle()));
    }
    else if (fOut < 0.5f)
    {
      row.m_pOut->setStyleSheet(QString("color: #fcc419; font-size: 10px; %1").arg(MonoStyle()));
    }
    else
    {
      row.m_pOut->setStyleSheet(QString("color: #51cf66; font-size: 10px; %1").arg(MonoStyle()));
    }
  }

  // mirror plAiUtilityEvaluator::ScoreUtility
  float fProduct = 1.0f;
  for (float f : factors)
    fProduct *= f;

  float fCompensated = fProduct;
  if (factors.GetCount() > 1 && fProduct > 0.0f)
  {
    const float fModification = 1.0f - (1.0f / static_cast<float>(factors.GetCount()));
    fCompensated = fProduct + ((1.0f - fProduct) * fProduct * fModification);
  }

  const float fScale = static_cast<float>(m_pWeightScale->value());
  float fUtility = plMath::Clamp(fCompensated * pProps->m_fWeight * fScale, 0.0f, 1.0f);

  const bool bCooldown = m_pCooldown->isChecked();
  if (bCooldown)
    fUtility = 0.0f;

  m_pProduct->setText(QString::number(fProduct, 'f', 3));
  m_pCompensated->setText(QString::number(fCompensated, 'f', 3));
  m_pUtilityValue->setText(QString::number(fUtility, 'f', 2));

  const plInt32 iCategory = pProps->m_Category.GetValue();
  m_pCategoryLabel->setText(QString("%1 + utility").arg(plAiAssetUi::CategoryName(iCategory)));
  {
    const QColor c = plAiAssetUi::CategoryColor(iCategory);
    m_pCategoryLabel->setStyleSheet(QString("color: %1; font-size: 10px; %2").arg(c.name()).arg(MonoStyle()));
  }

  float fCommitThreshold = -1.0f;
  if (bCooldown)
  {
    m_pCommitNote->setText("cooldown veto - ScoreUtility returns 0 while TimeSinceDeactivation < Cooldown");
    m_pCommitNote->setStyleSheet(QString("color: #ff6b6b; font-size: 10px; %1").arg(MonoStyle()));
  }
  else if (m_pRunning->isChecked())
  {
    fCommitThreshold = plMath::Min(1.0f, fUtility + pProps->m_fCommitBonus);
    m_pCommitNote->setText(QString("running: a challenger must beat %1 + %2 commit bonus to take over")
                             .arg(QString::number(fUtility, 'f', 2))
                             .arg(QString::number(pProps->m_fCommitBonus, 'f', 2)));
    m_pCommitNote->setStyleSheet(QString("color: #fcc419; font-size: 10px; %1").arg(MonoStyle()));
  }
  else if (bVetoed)
  {
    m_pCommitNote->setText("vetoed - a factor is 0, the behavior scores 0 no matter the others");
    m_pCommitNote->setStyleSheet(QString("color: #ff6b6b; font-size: 10px; %1").arg(MonoStyle()));
  }
  else
  {
    m_pCommitNote->setText("enable 'running' to see the commit-bonus switch threshold");
    m_pCommitNote->setStyleSheet(QString("color: #6d737a; font-size: 10px; %1").arg(MonoStyle()));
  }

  m_pBar->SetState(fUtility, fCommitThreshold, bVetoed || bCooldown);
}

//////////////////////////////////////////////////////////////////////////
// plQtAiBehaviorAssetDocumentWindow
//////////////////////////////////////////////////////////////////////////

plQtAiBehaviorAssetDocumentWindow::plQtAiBehaviorAssetDocumentWindow(plDocument* pDocument)
  : plQtAiAssetDocumentWindow(pDocument, "AiBehaviorAsset", "AiBehavior")
{
  m_pBehaviorDocument = static_cast<plAiBehaviorAssetDocument*>(pDocument);

  // central: header band + consideration cards
  {
    plQtDocumentPanel* pCentralPanel = new plQtDocumentPanel(this, pDocument);
    pCentralPanel->setObjectName("AiBehaviorAssetDockWidget");
    pCentralPanel->setWindowTitle("Behavior");
    pCentralPanel->show();

    QWidget* pMain = new QWidget(pCentralPanel);
    QVBoxLayout* pMainLayout = new QVBoxLayout(pMain);
    pMainLayout->setContentsMargins(0, 0, 0, 0);
    pMainLayout->setSpacing(0);

    pMainLayout->addWidget(CreateHeaderBand(pMain));

    // considerations header row
    {
      QWidget* pConsHead = new QWidget(pMain);
      QHBoxLayout* pHeadLayout = new QHBoxLayout(pConsHead);
      pHeadLayout->setContentsMargins(12, 8, 12, 4);

      QLabel* pTitle = new QLabel("Considerations", pConsHead);
      {
        QFont f = pTitle->font();
        f.setBold(true);
        pTitle->setFont(f);
      }

      QLabel* pMeta = new QLabel("multiplied - any 0 vetoes the behavior", pConsHead);
      pMeta->setStyleSheet("color: #8f959b; font-size: 10px;");

      m_pAddButton = new QPushButton("+ Add Consideration", pConsHead);
      connect(m_pAddButton, &QPushButton::clicked, this, &plQtAiBehaviorAssetDocumentWindow::onAddConsideration);

      pHeadLayout->addWidget(pTitle);
      pHeadLayout->addWidget(pMeta);
      pHeadLayout->addStretch();
      pHeadLayout->addWidget(m_pAddButton);

      pMainLayout->addWidget(pConsHead);
    }

    // scrollable card list
    {
      QScrollArea* pScroll = new QScrollArea(pMain);
      pScroll->setWidgetResizable(true);
      pScroll->setFrameShape(QFrame::NoFrame);

      m_pCardsHost = new QWidget(pScroll);
      m_pCardsLayout = new QVBoxLayout(m_pCardsHost);
      m_pCardsLayout->setContentsMargins(12, 4, 12, 12);
      m_pCardsLayout->setSpacing(6);
      m_pCardsLayout->addStretch();

      pScroll->setWidget(m_pCardsHost);
      pMainLayout->addWidget(pScroll, 1);
    }

    // card styling: neutral panels, AI-category accent on selection
    {
      const QColor bg = palette().color(QPalette::Window).lighter(115);
      const QColor border = palette().color(QPalette::Window).darker(160);
      pMain->setStyleSheet(QString("QFrame#AiConsiderationCard { background-color: %1; border: 1px solid %2; border-radius: 4px; }"
                                   "QFrame#AiConsiderationCard[cardSelected=\"true\"] { border: 1px solid %3; }")
                             .arg(bg.name())
                             .arg(border.name())
                             .arg(plAiAssetUi::AccentColor().name()));
    }

    pCentralPanel->setWidget(pMain, ads::CDockWidget::ForceNoScrollArea);
    m_pDockManager->addDockWidgetTab(ads::CenterDockWidgetArea, pCentralPanel);
  }

  // right: score preview + raw properties
  {
    plQtDocumentPanel* pScorePanel = new plQtDocumentPanel(this, pDocument);
    pScorePanel->setObjectName("AiBehaviorScorePreviewDockWidget");
    pScorePanel->setWindowTitle("Score Preview");
    pScorePanel->show();

    m_pScorePanel = new plQtAiScorePreviewPanel(pScorePanel, m_pBehaviorDocument);
    pScorePanel->setWidget(m_pScorePanel);

    m_pDockManager->addDockWidgetTab(ads::RightDockWidgetArea, pScorePanel);

    plQtDocumentPanel* pRawPanel = new plQtDocumentPanel(this, pDocument);
    pRawPanel->setObjectName("AiBehaviorRawPropertyDockWidget");
    pRawPanel->setWindowTitle("Raw Properties");
    pRawPanel->show();
    pRawPanel->setWidget(CreateRawPropertiesWidget(pRawPanel), ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidgetTab(ads::RightDockWidgetArea, pRawPanel);
  }

  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);

  FinishWindowCreation();

  RefreshHeader();
  RebuildCards();
  m_pScorePanel->RebuildRows();
}

plQtAiBehaviorAssetDocumentWindow::~plQtAiBehaviorAssetDocumentWindow() = default;

QWidget* plQtAiBehaviorAssetDocumentWindow::CreateHeaderBand(QWidget* pParent)
{
  QWidget* pBand = new QWidget(pParent);
  pBand->setObjectName("AiHeaderBand");

  QHBoxLayout* pLayout = new QHBoxLayout(pBand);
  pLayout->setContentsMargins(12, 8, 12, 8);
  pLayout->setSpacing(14);

  auto addField = [&](const char* szLabel, QWidget* pWidget) {
    QLabel* pLabel = new QLabel(szLabel, pBand);
    pLabel->setStyleSheet("color: #8f959b; font-size: 10px;");
    pLayout->addWidget(pLabel);
    pLayout->addWidget(pWidget);
  };

  // AI category chip (crosshatch, like the component headers)
  {
    QLabel* pChip = new QLabel(pBand);
    pChip->setFixedSize(26, 26);
    QPixmap pm(26, 26);
    pm.fill(Qt::transparent);
    {
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing, true);
      const QColor accent = plAiAssetUi::AccentColor();
      const QColor dim = plAiAssetUi::AccentColorDim();
      p.setPen(Qt::NoPen);
      p.setBrush(accent);
      p.drawRoundedRect(QRectF(0.5, 0.5, 25, 25), 4, 4);
      p.setPen(QPen(dim, 2.5));
      for (int x = -26; x < 26; x += 6)
        p.drawLine(x, 26, x + 26, 0);
    }
    pChip->setPixmap(pm);
    pChip->setToolTip("AI category asset (plColorScheme::GetCategoryColor(\"AI\"))");
    pLayout->addWidget(pChip);
  }

  m_pName = new QLineEdit(pBand);
  m_pName->setPlaceholderText("<asset file name>");
  m_pName->setToolTip("Display name used in debug overlays; falls back to the asset name.");
  m_pName->setMinimumWidth(160);
  addField("Name", m_pName);
  connect(m_pName, &QLineEdit::editingFinished, this, [this]() {
    SetRootValue("Name", plVariant(m_pName->text().toUtf8().data()), "Change Behavior Name");
  });

  m_pCategory = new QComboBox(pBand);
  {
    QString sTip = "Priority category - the dominant part of the final score.\nA behavior in a higher category ALWAYS wins against one in a lower category:\n";
    for (plInt32 iCat : s_CategoryLadder)
    {
      m_pCategory->addItem(MakeSwatchIcon(plAiAssetUi::CategoryColor(iCat)), plAiAssetUi::CategoryName(iCat), iCat);
      sTip += QString("\n  %1").arg(plAiAssetUi::CategoryName(iCat));
    }
    m_pCategory->setToolTip(sTip);
  }
  addField("Category", m_pCategory);
  connect(m_pCategory, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int iIndex) {
    if (iIndex >= 0)
      SetRootValue("Category", plVariant(m_pCategory->itemData(iIndex).toInt()), "Change Category");
  });

  m_pWeight = new QDoubleSpinBox(pBand);
  m_pWeight->setRange(0.0, 10.0);
  m_pWeight->setSingleStep(0.05);
  m_pWeight->setDecimals(2);
  m_pWeight->setToolTip("Base utility multiplier. Archetypes additionally apply their WeightScale.");
  addField("Weight", m_pWeight);
  connect(m_pWeight, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double fValue) {
    SetRootValue("Weight", plVariant(static_cast<float>(fValue)), "Change Weight");
  });

  m_pCommitBonus = new QDoubleSpinBox(pBand);
  m_pCommitBonus->setRange(0.0, 1.0);
  m_pCommitBonus->setSingleStep(0.05);
  m_pCommitBonus->setDecimals(2);
  m_pCommitBonus->setToolTip("Stickiness: while this behavior runs, a challenger must beat its score by this much.");
  addField("Commit", m_pCommitBonus);
  connect(m_pCommitBonus, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double fValue) {
    SetRootValue("CommitBonus", plVariant(static_cast<float>(fValue)), "Change Commit Bonus");
  });

  m_pCooldown = new QDoubleSpinBox(pBand);
  m_pCooldown->setRange(0.0, 3600.0);
  m_pCooldown->setSingleStep(0.5);
  m_pCooldown->setDecimals(1);
  m_pCooldown->setSuffix(" s");
  m_pCooldown->setToolTip("After deactivation the behavior scores 0 for this long.");
  addField("Cooldown", m_pCooldown);
  connect(m_pCooldown, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double fValue) {
    SetRootValue("Cooldown", plVariant(plTime::Seconds(fValue)), "Change Cooldown");
  });

  m_pLogicType = new QComboBox(pBand);
  m_pLogicType->addItem("<no logic>");
  m_pLogicType->addItem("State Machine");
  m_pLogicType->addItem("Action Queue");
  m_pLogicType->setToolTip("What runs while this behavior is active.");
  addField("Logic", m_pLogicType);
  connect(m_pLogicType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int iIndex) {
    ChangeLogicType(iIndex);
  });

  // state machine asset row
  {
    m_pSmRow = new QWidget(pBand);
    QHBoxLayout* pSmLayout = new QHBoxLayout(m_pSmRow);
    pSmLayout->setContentsMargins(0, 0, 0, 0);
    pSmLayout->setSpacing(2);

    m_pSmPath = new QLineEdit(m_pSmRow);
    m_pSmPath->setReadOnly(true);
    m_pSmPath->setMinimumWidth(180);
    m_pSmPath->setPlaceholderText("<no state machine>");

    QToolButton* pBrowse = new QToolButton(m_pSmRow);
    pBrowse->setText("...");
    pBrowse->setToolTip("Select a state machine asset");
    connect(pBrowse, &QToolButton::clicked, this, [this]() { BrowseStateMachine(); });

    QToolButton* pOpen = new QToolButton(m_pSmRow);
    pOpen->setText(QString::fromUtf8("\xE2\x86\x97")); // ↗
    pOpen->setToolTip("Open the state machine asset");
    connect(pOpen, &QToolButton::clicked, this, [this]() { OpenStateMachine(); });

    pSmLayout->addWidget(m_pSmPath);
    pSmLayout->addWidget(pBrowse);
    pSmLayout->addWidget(pOpen);

    pLayout->addWidget(m_pSmRow);
  }

  pLayout->addStretch();

  return pBand;
}

const plDocumentObject* plQtAiBehaviorAssetDocumentWindow::GetPropertiesObject() const
{
  return GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
}

void plQtAiBehaviorAssetDocumentWindow::GetConsiderationGuids(plDynamicArray<plUuid>& out_guids) const
{
  out_guids.Clear();

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();

  plDynamicArray<plVariant> values;
  pAccessor->GetValuesByName(GetPropertiesObject(), "Considerations", values).IgnoreResult();

  for (const plVariant& v : values)
  {
    if (v.IsA<plUuid>())
      out_guids.PushBack(v.Get<plUuid>());
  }
}

const plDocumentObject* plQtAiBehaviorAssetDocumentWindow::GetConsiderationObject(const plUuid& guid, plInt32* out_pIndex) const
{
  plDynamicArray<plUuid> guids;
  GetConsiderationGuids(guids);

  if (out_pIndex != nullptr)
    *out_pIndex = -1;

  for (plUInt32 i = 0; i < guids.GetCount(); ++i)
  {
    if (guids[i] == guid)
    {
      if (out_pIndex != nullptr)
        *out_pIndex = static_cast<plInt32>(i);

      return GetDocument()->GetObjectAccessor()->GetObject(guid);
    }
  }

  return nullptr;
}

void plQtAiBehaviorAssetDocumentWindow::SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription)
{
  if (m_bUpdatingUi)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction(szDescription);

  if (pAccessor->SetValueByName(GetPropertiesObject(), szProperty, value).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiBehaviorAssetDocumentWindow::RefreshHeader()
{
  m_bUpdatingUi = true;

  const plAiBehaviorAssetObject* pProps = m_pBehaviorDocument->GetProperties();

  if (!m_pName->hasFocus())
    m_pName->setText(QString::fromUtf8(pProps->m_sName.GetData()));

  {
    const int iIndex = m_pCategory->findData(pProps->m_Category.GetValue());
    if (iIndex >= 0 && iIndex != m_pCategory->currentIndex())
      m_pCategory->setCurrentIndex(iIndex);
  }

  if (!m_pWeight->hasFocus())
    m_pWeight->setValue(pProps->m_fWeight);
  if (!m_pCommitBonus->hasFocus())
    m_pCommitBonus->setValue(pProps->m_fCommitBonus);
  if (!m_pCooldown->hasFocus())
    m_pCooldown->setValue(pProps->m_CooldownDuration.GetSeconds());

  plInt32 iLogicType = 0;
  QString sSmFile;

  if (const plAiBehaviorLogic_StateMachine* pSm = plDynamicCast<const plAiBehaviorLogic_StateMachine*>(pProps->m_pLogic))
  {
    iLogicType = 1;
    sSmFile = QString::fromUtf8(pSm->GetStateMachineFile());
  }
  else if (plDynamicCast<const plAiBehaviorLogic_ActionQueue*>(pProps->m_pLogic) != nullptr)
  {
    iLogicType = 2;
  }

  if (m_pLogicType->currentIndex() != iLogicType)
    m_pLogicType->setCurrentIndex(iLogicType);

  m_pSmRow->setVisible(iLogicType == 1);

  // show the asset name instead of the raw guid where possible
  if (iLogicType == 1)
  {
    QString sDisplay = sSmFile;
    if (!sSmFile.isEmpty())
    {
      auto asset = plAssetCurator::GetSingleton()->FindSubAsset(sSmFile.toUtf8().data());
      if (asset.isValid())
      {
        const plStringView sName = asset->GetName();
        sDisplay = QString::fromUtf8(sName.GetStartPointer(), static_cast<int>(sName.GetElementCount()));
      }
    }
    m_pSmPath->setText(sDisplay);
    m_pSmPath->setToolTip(sSmFile);
  }

  m_bUpdatingUi = false;
}

void plQtAiBehaviorAssetDocumentWindow::RebuildCards()
{
  // remove old cards
  for (plQtAiConsiderationCard* pCard : m_Cards)
  {
    m_pCardsLayout->removeWidget(pCard);
    pCard->deleteLater();
  }
  m_Cards.Clear();

  plDynamicArray<plUuid> guids;
  GetConsiderationGuids(guids);

  // keep selection valid
  {
    bool bFound = false;
    for (const plUuid& guid : guids)
      bFound |= (guid == m_SelectedConsideration);

    if (!bFound)
      m_SelectedConsideration = plUuid();
  }

  for (plUInt32 i = 0; i < guids.GetCount(); ++i)
  {
    plQtAiConsiderationCard* pCard = new plQtAiConsiderationCard(m_pCardsHost, this, guids[i]);
    m_pCardsLayout->insertWidget(static_cast<int>(i), pCard);
    m_Cards.PushBack(pCard);
  }

  RefreshCards();
}

void plQtAiBehaviorAssetDocumentWindow::RefreshCards()
{
  const plAiBehaviorAssetObject* pProps = m_pBehaviorDocument->GetProperties();
  const plUInt32 uiCount = plMath::Min(m_Cards.GetCount(), pProps->m_Considerations.GetCount());

  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    m_Cards[i]->RefreshFromNative(pProps->m_Considerations[i], i, uiCount, m_Cards[i]->GetObjectGuid() == m_SelectedConsideration);
  }
}

void plQtAiBehaviorAssetDocumentWindow::OnAssetPropertiesChanged()
{
  RefreshHeader();
  RefreshCards();
  m_pScorePanel->RefreshValues();
}

void plQtAiBehaviorAssetDocumentWindow::OnAssetStructureChanged()
{
  RefreshHeader();
  RebuildCards();
  m_pScorePanel->RebuildRows();
}

void plQtAiBehaviorAssetDocumentWindow::SelectConsideration(const plUuid& guid)
{
  m_SelectedConsideration = guid;

  if (const plDocumentObject* pObject = GetConsiderationObject(guid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObject);
  }

  RefreshCards();
}

void plQtAiBehaviorAssetDocumentWindow::RemoveConsideration(const plUuid& guid)
{
  const plDocumentObject* pObject = GetConsiderationObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Remove Consideration");

  if (pAccessor->RemoveObject(pObject).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiBehaviorAssetDocumentWindow::MoveConsideration(const plUuid& guid, plInt32 iDirection)
{
  plInt32 iIndex = -1;
  const plDocumentObject* pObject = GetConsiderationObject(guid, &iIndex);
  if (pObject == nullptr || iIndex < 0)
    return;

  plDynamicArray<plUuid> guids;
  GetConsiderationGuids(guids);

  if (iDirection < 0 && iIndex == 0)
    return;
  if (iDirection > 0 && iIndex + 1 >= static_cast<plInt32>(guids.GetCount()))
    return;

  // MoveObject uses insert-before semantics: moving down needs +2
  const plInt32 iNewIndex = (iDirection < 0) ? (iIndex - 1) : (iIndex + 2);

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Reorder Considerations");

  if (pAccessor->MoveObjectByName(pObject, GetPropertiesObject(), "Considerations", plVariant(iNewIndex)).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiBehaviorAssetDocumentWindow::SetConsiderationRange(const plUuid& guid, bool bMin, double fValue)
{
  if (m_bUpdatingUi)
    return;

  const plDocumentObject* pObject = GetConsiderationObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction(bMin ? "Change Input Min" : "Change Input Max");

  if (pAccessor->SetValueByName(pObject, bMin ? "InputMin" : "InputMax", plVariant(static_cast<float>(fValue))).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiBehaviorAssetDocumentWindow::OpenCurveEditor(const plUuid& guid)
{
  const plDocumentObject* pObject = GetConsiderationObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  const plDocumentObject* pCurve = pAccessor->GetChildObjectByName(pObject, "ResponseCurve", plVariant());
  if (pCurve == nullptr)
    return;

  plQtCurveEditDlg dlg(pAccessor, pCurve, this);
  dlg.restoreGeometry(plQtCurveEditDlg::GetLastDialogGeometry());
  dlg.SetCurveColor(plColorScheme::LightUI(plColorScheme::Blue));
  dlg.SetCurveExtents(0.0, true, 1.0, true);
  dlg.SetCurveRanges(0.0, 1.0);
  dlg.exec();
}

plUInt32 plQtAiBehaviorAssetDocumentWindow::GetCurvePresetCount()
{
  return PL_ARRAY_SIZE(s_CurvePresets);
}

const char* plQtAiBehaviorAssetDocumentWindow::GetCurvePresetName(plUInt32 uiPreset)
{
  return s_CurvePresets[uiPreset].szName;
}

void plQtAiBehaviorAssetDocumentWindow::ApplyCurvePreset(const plUuid& guid, plUInt32 uiPreset)
{
  if (uiPreset >= PL_ARRAY_SIZE(s_CurvePresets))
    return;

  const CurvePreset& preset = s_CurvePresets[uiPreset];

  const plDocumentObject* pObject = GetConsiderationObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  const plDocumentObject* pCurve = pAccessor->GetChildObjectByName(pObject, "ResponseCurve", plVariant());
  if (pCurve == nullptr)
    return;

  pAccessor->StartTransaction("Apply Curve Preset");

  bool bOk = true;

  // remove existing control points
  {
    plDynamicArray<plVariant> values;
    pAccessor->GetValuesByName(pCurve, "ControlPoints", values).IgnoreResult();

    for (plUInt32 i = values.GetCount(); i > 0; --i)
    {
      if (values[i - 1].IsA<plUuid>())
      {
        if (const plDocumentObject* pCp = pAccessor->GetObject(values[i - 1].Get<plUuid>()))
        {
          bOk &= pAccessor->RemoveObject(pCp).Succeeded();
        }
      }
    }
  }

  // insert the preset points (ticks: 4800 per second, extents locked to 0..1 seconds)
  const plInt64 iTangentMode = preset.bSmooth ? plCurveTangentMode::Auto : plCurveTangentMode::Linear;

  for (plUInt32 i = 0; i < preset.uiCount && bOk; ++i)
  {
    plUuid cpGuid = plUuid::MakeUuid();
    bOk &= pAccessor->AddObjectByName(pCurve, "ControlPoints", i, plCurveControlPointData::GetStaticRTTI(), cpGuid).Succeeded();

    if (const plDocumentObject* pCp = pAccessor->GetObject(cpGuid))
    {
      bOk &= pAccessor->SetValueByName(pCp, "Tick", plVariant(static_cast<plInt64>(preset.pts[i].x * 4800.0 + 0.5))).Succeeded();
      bOk &= pAccessor->SetValueByName(pCp, "Value", plVariant(preset.pts[i].v)).Succeeded();
      bOk &= pAccessor->SetValueByName(pCp, "LeftTangentMode", plVariant(iTangentMode)).Succeeded();
      bOk &= pAccessor->SetValueByName(pCp, "RightTangentMode", plVariant(iTangentMode)).Succeeded();
    }
    else
    {
      bOk = false;
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plQtAiBehaviorAssetDocumentWindow::onAddConsideration()
{
  QMenu menu(this);
  const plRTTI* pChosen = nullptr;

  plQtSearchableMenu* pSearch = new plQtSearchableMenu(&menu);

  plRTTI::ForEachDerivedType(
    plAiInput::GetStaticRTTI(),
    [&](const plRTTI* pRtti) {
      if (pRtti == plAiInput::GetStaticRTTI())
        return;

      plStringBuilder sPath;
      sPath.SetFormat("{}/{}", InputGroup(pRtti), plAiAssetUi::PrettyInputName(pRtti).toUtf8().data());

      pSearch->AddItem(plAiAssetUi::PrettyInputName(pRtti).toUtf8().data(), sPath, QVariant::fromValue<void*>(const_cast<plRTTI*>(pRtti)));
    },
    plRTTI::ForEachOptions::ExcludeNotConcrete);

  connect(pSearch, &plQtSearchableMenu::MenuItemTriggered, &menu, [&](const QString&, const QVariant& variant) {
    pChosen = static_cast<const plRTTI*>(variant.value<void*>());
    menu.close();
  });

  menu.addAction(pSearch);
  pSearch->Finalize(QString());

  menu.exec(m_pAddButton->mapToGlobal(QPoint(0, m_pAddButton->height())));

  if (pChosen == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Add Consideration");

  bool bOk = true;

  plUuid consGuid = plUuid::MakeUuid();
  bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "Considerations", pAccessor->GetCountByName(GetPropertiesObject(), "Considerations"), plAiConsiderationObject::GetStaticRTTI(), consGuid).Succeeded();

  if (bOk)
  {
    if (const plDocumentObject* pConsObj = pAccessor->GetObject(consGuid))
    {
      plUuid inputGuid = plUuid::MakeUuid();
      bOk &= pAccessor->AddObjectByName(pConsObj, "Input", plVariant(), pChosen, inputGuid).Succeeded();
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
    SelectConsideration(consGuid);
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plQtAiBehaviorAssetDocumentWindow::ChangeLogicType(plInt32 iType)
{
  if (m_bUpdatingUi)
    return;

  const plAiBehaviorAssetObject* pProps = m_pBehaviorDocument->GetProperties();

  // no-op when the type already matches
  plInt32 iCurrent = 0;
  if (plDynamicCast<const plAiBehaviorLogic_StateMachine*>(pProps->m_pLogic) != nullptr)
    iCurrent = 1;
  else if (plDynamicCast<const plAiBehaviorLogic_ActionQueue*>(pProps->m_pLogic) != nullptr)
    iCurrent = 2;

  if (iCurrent == iType)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Change Behavior Logic");

  bool bOk = true;

  if (const plDocumentObject* pLogic = pAccessor->GetChildObjectByName(GetPropertiesObject(), "Logic", plVariant()))
  {
    bOk &= pAccessor->RemoveObject(pLogic).Succeeded();
  }

  const plRTTI* pType = nullptr;
  if (iType == 1)
    pType = plGetStaticRTTI<plAiBehaviorLogic_StateMachine>();
  else if (iType == 2)
    pType = plGetStaticRTTI<plAiBehaviorLogic_ActionQueue>();

  if (pType != nullptr && bOk)
  {
    plUuid guid = plUuid::MakeUuid();
    bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "Logic", plVariant(), pType, guid).Succeeded();
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plQtAiBehaviorAssetDocumentWindow::BrowseStateMachine()
{
  const plAiBehaviorAssetObject* pProps = m_pBehaviorDocument->GetProperties();
  const plAiBehaviorLogic_StateMachine* pSm = plDynamicCast<const plAiBehaviorLogic_StateMachine*>(pProps->m_pLogic);
  if (pSm == nullptr)
    return;

  plUuid currentGuid;
  if (plConversionUtils::IsStringUuid(pSm->GetStateMachineFile()))
  {
    currentGuid = plConversionUtils::ConvertStringToUuid(pSm->GetStateMachineFile());
  }

  plQtAssetBrowserDlg dlg(this, currentGuid, "CompatibleAsset_StateMachine");
  if (dlg.exec() == 0 || !dlg.GetSelectedAssetGuid().IsValid())
    return;

  plStringBuilder sGuid;
  plConversionUtils::ToString(dlg.GetSelectedAssetGuid(), sGuid);

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();

  const plDocumentObject* pLogic = pAccessor->GetChildObjectByName(GetPropertiesObject(), "Logic", plVariant());
  if (pLogic == nullptr)
    return;

  pAccessor->StartTransaction("Set State Machine");

  if (pAccessor->SetValueByName(pLogic, "StateMachine", plVariant(sGuid.GetData())).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiBehaviorAssetDocumentWindow::OpenStateMachine()
{
  const plAiBehaviorAssetObject* pProps = m_pBehaviorDocument->GetProperties();
  const plAiBehaviorLogic_StateMachine* pSm = plDynamicCast<const plAiBehaviorLogic_StateMachine*>(pProps->m_pLogic);
  if (pSm == nullptr || plStringUtils::IsNullOrEmpty(pSm->GetStateMachineFile()))
    return;

  auto asset = plAssetCurator::GetSingleton()->FindSubAsset(pSm->GetStateMachineFile());
  if (!asset.isValid() || asset->m_pAssetInfo == nullptr)
    return;

  plQtEditorApp::GetSingleton()->OpenDocumentQueued(asset->m_pAssetInfo->m_Path.GetAbsolutePath());
}
