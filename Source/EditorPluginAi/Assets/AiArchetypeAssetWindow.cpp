#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiArchetypeAsset.h>
#include <EditorPluginAi/Assets/AiArchetypeAssetWindow.moc.h>

#include <AiPlugin/UtilityAI/Decision/AiScoring.h>

#include <EditorFramework/Assets/AssetBrowserDlg.moc.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <GuiFoundation/DockPanels/DocumentPanel.moc.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

#include <QBoxLayout>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>

namespace
{
  // ladder order, highest priority first; one extra pseudo-band for unresolved references
  static const plInt32 s_LadderBands[] = {
    plAiBehaviorCategory::Interrupt,
    plAiBehaviorCategory::Combat,
    plAiBehaviorCategory::Command,
    plAiBehaviorCategory::Investigate,
    plAiBehaviorCategory::ActiveIdle,
    plAiBehaviorCategory::Idle,
    plAiBehaviorCategory::Fallback,
  };

  constexpr plUInt32 s_uiUnresolvedBand = PL_ARRAY_SIZE(s_LadderBands);

  static QString MonoStyle()
  {
    return QStringLiteral("font-family: Consolas, 'Cascadia Mono', monospace;");
  }

  static plUInt32 BandForCategory(plInt32 iCategory)
  {
    for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_LadderBands); ++i)
    {
      if (s_LadderBands[i] == iCategory)
        return i;
    }

    return s_uiUnresolvedBand;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plQtAiPerceptionTimeline
//////////////////////////////////////////////////////////////////////////

plQtAiPerceptionTimeline::plQtAiPerceptionTimeline(QWidget* pParent)
  : QWidget(pParent)
{
  setMinimumHeight(120);
  setMaximumHeight(140);
}

void plQtAiPerceptionTimeline::SetValues(float fGainPerSec, float fDecayPerSec, float fMemorySeconds)
{
  m_fGain = plMath::Max(0.01f, fGainPerSec);
  m_fDecay = plMath::Max(0.0f, fDecayPerSec);
  m_fMemory = plMath::Max(0.0f, fMemorySeconds);
  update();
}

void plQtAiPerceptionTimeline::paintEvent(QPaintEvent* pEvent)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
  p.setPen(QPen(QColor(21, 22, 23)));
  p.setBrush(QColor(31, 32, 33));
  p.drawRoundedRect(r, 3, 3);

  const double fPadL = 6, fPadR = 6, fPadT = 14, fPadB = 16;
  const QRectF plot(r.left() + fPadL, r.top() + fPadT, r.width() - fPadL - fPadR, r.height() - fPadT - fPadB);

  const double tLost = 2.0;
  const double tForget = tLost + m_fMemory;
  const double tEnd = plMath::Clamp(tForget + 2.0, 8.0, 60.0);

  auto X = [&](double t) { return plot.left() + (t / tEnd) * plot.width(); };
  auto Y = [&](double c) { return plot.bottom() - plMath::Clamp(c, 0.0, 1.0) * plot.height(); };

  QFont small = font();
  small.setPointSizeF(plMath::Max(6.5, small.pointSizeF() - 2.0));
  p.setFont(small);

  // sighted region
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(81, 207, 102, 18));
  p.drawRect(QRectF(QPointF(X(0), plot.top()), QPointF(X(tLost), plot.bottom())));

  // reference lines: 0.6 squad share (yellow), 0.15 tutorial gates (blue)
  {
    QPen refPen(QColor(111, 90, 28));
    refPen.setStyle(Qt::DashLine);
    p.setPen(refPen);
    p.drawLine(QPointF(plot.left(), Y(0.6)), QPointF(plot.right(), Y(0.6)));
    p.setPen(QColor(252, 196, 25));
    p.drawText(QPointF(plot.right() - 24, Y(0.6) - 2), "0.60");

    refPen.setColor(QColor(56, 83, 107));
    p.setPen(refPen);
    p.drawLine(QPointF(plot.left(), Y(0.15)), QPointF(plot.right(), Y(0.15)));
    p.setPen(QColor(116, 192, 252));
    p.drawText(QPointF(plot.right() - 24, Y(0.15) - 2), "0.15");
  }

  // confidence curve: rise while sighted, decay afterwards, forgotten at the memory cutoff
  {
    QPainterPath path;
    const int iSamples = 120;
    const double fPeak = plMath::Min(1.0, m_fGain * tLost);

    for (int i = 0; i <= iSamples; ++i)
    {
      const double t = (static_cast<double>(i) / iSamples) * tEnd;
      double c = 0.0;

      if (t <= tLost)
        c = plMath::Min(1.0, m_fGain * t);
      else if (t <= tForget)
        c = plMath::Max(0.0, fPeak - m_fDecay * (t - tLost));

      const QPointF pt(X(t), Y(c));
      if (i == 0)
        path.moveTo(pt);
      else
        path.lineTo(pt);
    }

    QPen curvePen(QColor(116, 192, 252));
    curvePen.setWidthF(2.0);
    p.setPen(curvePen);
    p.drawPath(path);
  }

  // markers
  {
    QPen markPen(QColor(71, 73, 75));
    markPen.setStyle(Qt::DashLine);
    p.setPen(markPen);
    p.drawLine(QPointF(X(tLost), plot.top()), QPointF(X(tLost), plot.bottom()));

    markPen.setColor(QColor(125, 74, 74));
    p.setPen(markPen);
    p.drawLine(QPointF(X(tForget), plot.top()), QPointF(X(tForget), plot.bottom()));

    p.setPen(QColor(196, 123, 123));
    p.drawText(QPointF(X(tForget) + 3, plot.top() + 8), "forgotten");

    p.setPen(QColor(122, 127, 132));
    p.drawText(QPointF(plot.left(), r.bottom() - 4), "0 s");
    p.drawText(QPointF(X(tLost) - 20, r.bottom() - 4), "sight lost 2 s");
    p.drawText(QPointF(plot.right() - 24, r.bottom() - 4), QString("%1 s").arg(QString::number(tEnd, 'f', 0)));
  }
}

//////////////////////////////////////////////////////////////////////////
// plQtAiEffectiveWeightBar
//////////////////////////////////////////////////////////////////////////

plQtAiEffectiveWeightBar::plQtAiEffectiveWeightBar(QWidget* pParent)
  : QWidget(pParent)
{
  setMinimumSize(70, 10);
  setMaximumHeight(10);
  setToolTip("effective weight = base weight × archetype scale (full bar = 2.0)");
}

void plQtAiEffectiveWeightBar::SetValue(float fEffectiveWeight, const QColor& color)
{
  m_fEffective = fEffectiveWeight;
  m_Color = color;
  update();
}

void plQtAiEffectiveWeightBar::paintEvent(QPaintEvent* pEvent)
{
  QPainter p(this);

  const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
  p.setPen(QPen(QColor(21, 22, 23)));
  p.setBrush(QColor(31, 32, 33));
  p.drawRect(r);

  const float fFraction = plMath::Clamp(m_fEffective / 2.0f, 0.0f, 1.0f);
  if (fFraction > 0.0f)
  {
    QRectF fill = r.adjusted(1, 1, -1, -1);
    fill.setWidth(fill.width() * fFraction);
    p.setPen(Qt::NoPen);
    p.setBrush(m_Color);
    p.drawRect(fill);
  }
}

//////////////////////////////////////////////////////////////////////////
// plQtAiCurveMiniView
//////////////////////////////////////////////////////////////////////////

plQtAiCurveMiniView::plQtAiCurveMiniView(QWidget* pParent)
  : QWidget(pParent)
{
  setFixedSize(64, 22);
}

void plQtAiCurveMiniView::SetCurve(const plCurve1D& curve, bool bLegacyDomain)
{
  m_Curve = curve;
  m_bLegacyDomain = bLegacyDomain;
  update();
}

void plQtAiCurveMiniView::paintEvent(QPaintEvent* pEvent)
{
  QPainter p(this);
  plAiAssetUi::PaintCurve(p, rect(), &m_Curve, plAiAssetUi::CurveColor(), m_bLegacyDomain);
}

//////////////////////////////////////////////////////////////////////////
// plQtAiBehaviorStackRow
//////////////////////////////////////////////////////////////////////////

plQtAiBehaviorStackRow::plQtAiBehaviorStackRow(QWidget* pParent, plQtAiArchetypeAssetDocumentWindow* pWindow, const plUuid& objectGuid)
  : QFrame(pParent)
  , m_pWindow(pWindow)
  , m_ObjectGuid(objectGuid)
{
  setObjectName("AiBehaviorRow");
  setProperty("rowSelected", false);
  setCursor(Qt::PointingHandCursor);

  QHBoxLayout* pLayout = new QHBoxLayout(this);
  pLayout->setContentsMargins(9, 5, 6, 5);
  pLayout->setSpacing(10);

  {
    QLabel* pIcon = new QLabel(this);
    pIcon->setPixmap(QIcon(":/AssetIcons/AiBehavior.svg").pixmap(14, 14));
    pLayout->addWidget(pIcon);
  }

  m_pName = new QLabel(this);
  {
    QFont f = m_pName->font();
    f.setBold(true);
    m_pName->setFont(f);
  }
  m_pName->setMinimumWidth(170);
  pLayout->addWidget(m_pName, 2);

  m_pBaseWeight = new QLabel(this);
  m_pBaseWeight->setStyleSheet(QString("color: #9aa0a8; font-size: 10px; %1").arg(MonoStyle()));
  pLayout->addWidget(m_pBaseWeight);

  {
    QLabel* pTimes = new QLabel(QString::fromUtf8("\xC3\x97" "scale"), this); // ×scale
    pTimes->setStyleSheet("color: #9aa0a8; font-size: 10px;");
    pLayout->addWidget(pTimes);
  }

  m_pScale = new QDoubleSpinBox(this);
  m_pScale->setRange(0.0, 10.0);
  m_pScale->setSingleStep(0.05);
  m_pScale->setDecimals(2);
  m_pScale->setToolTip("This archetype's WeightScale for the behavior.");
  pLayout->addWidget(m_pScale);
  connect(m_pScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double fValue) {
    if (!m_bUpdating)
      m_pWindow->SetWeightScale(m_ObjectGuid, fValue);
  });

  m_pBar = new plQtAiEffectiveWeightBar(this);
  pLayout->addWidget(m_pBar, 1);

  m_pTag = new QLabel(this);
  m_pTag->setStyleSheet(QString("color: #8fb6d8; font-size: 9px; %1").arg(MonoStyle()));
  pLayout->addWidget(m_pTag);

  QToolButton* pOpen = new QToolButton(this);
  pOpen->setText(QString::fromUtf8("\xE2\x86\x97 open")); // ↗
  pOpen->setAutoRaise(true);
  pOpen->setToolTip("Open the behavior asset");
  pLayout->addWidget(pOpen);
  connect(pOpen, &QToolButton::clicked, this, [this]() { m_pWindow->OpenBehavior(m_ObjectGuid); });

  QToolButton* pRemove = new QToolButton(this);
  pRemove->setText(QString::fromUtf8("\xE2\x9C\x95")); // ✕
  pRemove->setAutoRaise(true);
  pRemove->setToolTip("Remove this behavior from the archetype");
  pRemove->setStyleSheet("color: #e05555;");
  pLayout->addWidget(pRemove);
  connect(pRemove, &QToolButton::clicked, this, [this]() { m_pWindow->RemoveBehavior(m_ObjectGuid); });
}

void plQtAiBehaviorStackRow::Refresh(const plAiBehaviorPeek& peek, const char* szBehaviorRef, float fWeightScale, bool bSelected)
{
  m_bUpdating = true;

  if (peek.m_bValid)
  {
    m_pName->setText(peek.m_sName);
    m_pBaseWeight->setText(QString("weight %1").arg(QString::number(peek.m_fWeight, 'f', 2)));
    m_pBar->SetValue(peek.m_fWeight * fWeightScale, plAiAssetUi::CategoryColor(peek.m_iCategory));

    QString sTag;
    if (!peek.m_Considerations.IsEmpty())
    {
      sTag = QString("%1 considerations").arg(peek.m_Considerations.GetCount());
      for (const auto& cons : peek.m_Considerations)
      {
        if (cons.m_bNeedsTarget)
        {
          sTag += QStringLiteral(" \xC2\xB7 target"); // · target
          break;
        }
      }
    }
    if (peek.m_fCooldownSeconds > 0.0)
      sTag += QString(" \xC2\xB7 cooldown %1s").arg(QString::number(peek.m_fCooldownSeconds, 'f', 1));

    m_pTag->setText(sTag);
    setToolTip(peek.m_sAbsPath);
  }
  else
  {
    m_pName->setText(plStringUtils::IsNullOrEmpty(szBehaviorRef) ? "<no behavior set>" : szBehaviorRef);
    m_pBaseWeight->setText("weight ?");
    m_pBar->SetValue(0.0f, QColor(96, 100, 105));
    m_pTag->setText("unresolved - asset not found");
    setToolTip("The referenced behavior asset could not be resolved.");
  }

  if (!m_pScale->hasFocus())
    m_pScale->setValue(fWeightScale);

  if (property("rowSelected").toBool() != bSelected)
  {
    setProperty("rowSelected", bSelected);
    style()->unpolish(this);
    style()->polish(this);
  }

  m_bUpdating = false;
}

void plQtAiBehaviorStackRow::mousePressEvent(QMouseEvent* pEvent)
{
  m_pWindow->SelectBehaviorRow(m_ObjectGuid);
  QFrame::mousePressEvent(pEvent);
}

//////////////////////////////////////////////////////////////////////////
// plQtAiArchetypeAssetDocumentWindow
//////////////////////////////////////////////////////////////////////////

plQtAiArchetypeAssetDocumentWindow::plQtAiArchetypeAssetDocumentWindow(plDocument* pDocument)
  : plQtAiAssetDocumentWindow(pDocument, "AiArchetypeAsset", "AiArchetype")
{
  m_pArchetypeDocument = static_cast<plAiArchetypeAssetDocument*>(pDocument);

  // central: header band + category ladder
  {
    plQtDocumentPanel* pCentralPanel = new plQtDocumentPanel(this, pDocument);
    pCentralPanel->setObjectName("AiArchetypeAssetDockWidget");
    pCentralPanel->setWindowTitle("Archetype");
    pCentralPanel->show();

    QWidget* pMain = new QWidget(pCentralPanel);
    QVBoxLayout* pMainLayout = new QVBoxLayout(pMain);
    pMainLayout->setContentsMargins(0, 0, 0, 0);
    pMainLayout->setSpacing(0);

    pMainLayout->addWidget(CreateHeaderBand(pMain));

    // stack header
    {
      QWidget* pHead = new QWidget(pMain);
      QHBoxLayout* pHeadLayout = new QHBoxLayout(pHead);
      pHeadLayout->setContentsMargins(12, 8, 12, 4);

      QLabel* pTitle = new QLabel("Behavior stack", pHead);
      {
        QFont f = pTitle->font();
        f.setBold(true);
        pTitle->setFont(f);
      }

      QLabel* pMeta = new QLabel("grouped by the category each behavior asset declares - higher band always wins", pHead);
      pMeta->setStyleSheet("color: #8f959b; font-size: 10px;");

      QPushButton* pAdd = new QPushButton("+ Add Behavior", pHead);
      connect(pAdd, &QPushButton::clicked, this, &plQtAiArchetypeAssetDocumentWindow::onAddBehavior);

      pHeadLayout->addWidget(pTitle);
      pHeadLayout->addWidget(pMeta);
      pHeadLayout->addStretch();
      pHeadLayout->addWidget(pAdd);

      pMainLayout->addWidget(pHead);
    }

    // scrollable ladder
    {
      QScrollArea* pScroll = new QScrollArea(pMain);
      pScroll->setWidgetResizable(true);
      pScroll->setFrameShape(QFrame::NoFrame);

      m_pStackHost = new QWidget(pScroll);
      QVBoxLayout* pStackLayout = new QVBoxLayout(m_pStackHost);
      pStackLayout->setContentsMargins(12, 4, 12, 12);
      pStackLayout->setSpacing(2);

      auto addBand = [&](const QColor& railColor, const char* szName, const char* szEmptyText) {
        Band& band = m_Bands.ExpandAndGetRef();

        band.m_pHost = new QWidget(m_pStackHost);
        QHBoxLayout* pBandLayout = new QHBoxLayout(band.m_pHost);
        pBandLayout->setContentsMargins(0, 2, 0, 2);
        pBandLayout->setSpacing(8);

        // label column with colored rail
        QWidget* pLabelHost = new QWidget(band.m_pHost);
        pLabelHost->setFixedWidth(104);
        QHBoxLayout* pLabelLayout = new QHBoxLayout(pLabelHost);
        pLabelLayout->setContentsMargins(0, 0, 0, 0);
        pLabelLayout->setSpacing(6);

        QFrame* pRail = new QFrame(pLabelHost);
        pRail->setFixedWidth(3);
        pRail->setStyleSheet(QString("background-color: %1; border-radius: 1px;").arg(railColor.name()));

        QLabel* pName = new QLabel(szName, pLabelHost);
        pName->setStyleSheet(QString("color: #9aa0a8; font-size: 10px; letter-spacing: 1px; %1").arg(MonoStyle()));

        pLabelLayout->addWidget(pRail);
        pLabelLayout->addWidget(pName);
        pLabelLayout->addStretch();

        QWidget* pRowsHost = new QWidget(band.m_pHost);
        band.m_pRows = new QVBoxLayout(pRowsHost);
        band.m_pRows->setContentsMargins(0, 0, 0, 0);
        band.m_pRows->setSpacing(4);

        band.m_pEmpty = new QLabel(szEmptyText, pRowsHost);
        band.m_pEmpty->setStyleSheet("color: #565a5e; font-size: 10px; font-style: italic;");
        band.m_pRows->addWidget(band.m_pEmpty);

        pBandLayout->addWidget(pLabelHost);
        pBandLayout->addWidget(pRowsHost, 1);

        pStackLayout->addWidget(band.m_pHost);

        // thin separator
        QFrame* pSep = new QFrame(m_pStackHost);
        pSep->setFrameShape(QFrame::HLine);
        pSep->setStyleSheet("color: #232425;");
        pStackLayout->addWidget(pSep);
      };

      for (plInt32 iCat : s_LadderBands)
      {
        plStringBuilder sUpper = plAiAssetUi::CategoryName(iCat);
        sUpper.ToUpper();
        addBand(plAiAssetUi::CategoryColor(iCat), sUpper, "no behaviors");
      }

      addBand(QColor(96, 100, 105), "UNRESOLVED", "all references resolve");

      pStackLayout->addStretch();

      pScroll->setWidget(m_pStackHost);
      pMainLayout->addWidget(pScroll, 1);
    }

    // row styling: neutral panels, AI-category accent on selection
    {
      const QColor bg = palette().color(QPalette::Window).lighter(115);
      const QColor border = palette().color(QPalette::Window).darker(160);
      pMain->setStyleSheet(QString("QFrame#AiBehaviorRow { background-color: %1; border: 1px solid %2; border-radius: 4px; }"
                                   "QFrame#AiBehaviorRow[rowSelected=\"true\"] { border: 1px solid %3; }")
                             .arg(bg.name())
                             .arg(border.name())
                             .arg(plAiAssetUi::AccentColor().name()));
    }

    pCentralPanel->setWidget(pMain, ads::CDockWidget::ForceNoScrollArea);
    m_pDockManager->addDockWidgetTab(ads::CenterDockWidgetArea, pCentralPanel);
  }

  // right: perception + peek panel, raw properties tab
  {
    plQtDocumentPanel* pInsightPanel = new plQtDocumentPanel(this, pDocument);
    pInsightPanel->setObjectName("AiArchetypeInsightDockWidget");
    pInsightPanel->setWindowTitle("Perception & Peek");
    pInsightPanel->show();
    pInsightPanel->setWidget(CreateInsightPanel(pInsightPanel));

    m_pDockManager->addDockWidgetTab(ads::RightDockWidgetArea, pInsightPanel);

    plQtDocumentPanel* pRawPanel = new plQtDocumentPanel(this, pDocument);
    pRawPanel->setObjectName("AiArchetypeRawPropertyDockWidget");
    pRawPanel->setWindowTitle("Raw Properties");
    pRawPanel->show();
    pRawPanel->setWidget(CreateRawPropertiesWidget(pRawPanel), ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidgetTab(ads::RightDockWidgetArea, pRawPanel);
  }

  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);

  FinishWindowCreation();

  RefreshHeader();
  RebuildStack();
}

plQtAiArchetypeAssetDocumentWindow::~plQtAiArchetypeAssetDocumentWindow() = default;

QWidget* plQtAiArchetypeAssetDocumentWindow::CreateHeaderBand(QWidget* pParent)
{
  QWidget* pBand = new QWidget(pParent);

  QHBoxLayout* pLayout = new QHBoxLayout(pBand);
  pLayout->setContentsMargins(12, 8, 12, 8);
  pLayout->setSpacing(14);

  auto addField = [&](const char* szLabel, QWidget* pWidget) {
    QLabel* pLabel = new QLabel(szLabel, pBand);
    pLabel->setStyleSheet("color: #8f959b; font-size: 10px;");
    pLayout->addWidget(pLabel);
    pLayout->addWidget(pWidget);
  };

  // AI category chip
  {
    QLabel* pChip = new QLabel(pBand);
    pChip->setFixedSize(26, 26);
    QPixmap pm(26, 26);
    pm.fill(Qt::transparent);
    {
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(Qt::NoPen);
      p.setBrush(plAiAssetUi::AccentColor());
      p.drawRoundedRect(QRectF(0.5, 0.5, 25, 25), 4, 4);
      p.setPen(QPen(plAiAssetUi::AccentColorDim(), 2.5));
      for (int x = -26; x < 26; x += 6)
        p.drawLine(x, 26, x + 26, 0);
    }
    pChip->setPixmap(pm);
    pLayout->addWidget(pChip);
  }

  m_pTeam = new QLineEdit(pBand);
  m_pTeam->setMinimumWidth(100);
  m_pTeam->setToolTip("Faction/team this archetype belongs to (perception friend-or-foe).");
  addField("Team", m_pTeam);
  connect(m_pTeam, &QLineEdit::editingFinished, this, [this]() {
    SetRootValue("Team", plVariant(m_pTeam->text().toUtf8().data()), "Change Team");
  });

  m_pSensor = new QLineEdit(pBand);
  m_pSensor->setMinimumWidth(100);
  m_pSensor->setToolTip("Name of the child object carrying the sensor components.");
  addField("Sensor object", m_pSensor);
  connect(m_pSensor, &QLineEdit::editingFinished, this, [this]() {
    SetRootValue("SensorObjectName", plVariant(m_pSensor->text().toUtf8().data()), "Change Sensor Object");
  });

  // blackboard template asset row
  {
    QWidget* pBbRow = new QWidget(pBand);
    QHBoxLayout* pBbLayout = new QHBoxLayout(pBbRow);
    pBbLayout->setContentsMargins(0, 0, 0, 0);
    pBbLayout->setSpacing(2);

    m_pBlackboard = new QLineEdit(pBbRow);
    m_pBlackboard->setReadOnly(true);
    m_pBlackboard->setMinimumWidth(170);
    m_pBlackboard->setPlaceholderText("<no blackboard template>");

    QToolButton* pBrowse = new QToolButton(pBbRow);
    pBrowse->setText("...");
    pBrowse->setToolTip("Select a blackboard template asset");
    connect(pBrowse, &QToolButton::clicked, this, [this]() { BrowseBlackboardTemplate(); });

    pBbLayout->addWidget(m_pBlackboard);
    pBbLayout->addWidget(pBrowse);

    addField("Blackboard template", pBbRow);
  }

  pLayout->addStretch();

  return pBand;
}

QWidget* plQtAiArchetypeAssetDocumentWindow::CreateInsightPanel(QWidget* pParent)
{
  QWidget* pPanel = new QWidget(pParent);
  pPanel->setMinimumWidth(300);

  QVBoxLayout* pLayout = new QVBoxLayout(pPanel);
  pLayout->setContentsMargins(8, 8, 8, 8);
  pLayout->setSpacing(6);

  auto addSectionLabel = [&](const char* szText) {
    QLabel* pLabel = new QLabel(szText, pPanel);
    pLabel->setStyleSheet(QString("color: #8f959b; font-size: 9px; letter-spacing: 1px; %1").arg(MonoStyle()));
    pLayout->addWidget(pLabel);
  };

  addSectionLabel("TARGET CONFIDENCE OVER TIME");

  m_pTimeline = new plQtAiPerceptionTimeline(pPanel);
  pLayout->addWidget(m_pTimeline);

  auto addSpinRow = [&](const char* szLabel, double fMin, double fMax, double fStep, int iDecimals, const char* szSuffix) {
    QHBoxLayout* pRow = new QHBoxLayout();
    QLabel* pLabel = new QLabel(szLabel, pPanel);
    pLabel->setStyleSheet("color: #a2a7ab; font-size: 11px;");
    QDoubleSpinBox* pSpin = new QDoubleSpinBox(pPanel);
    pSpin->setRange(fMin, fMax);
    pSpin->setSingleStep(fStep);
    pSpin->setDecimals(iDecimals);
    if (!plStringUtils::IsNullOrEmpty(szSuffix))
      pSpin->setSuffix(szSuffix);
    pRow->addWidget(pLabel);
    pRow->addStretch();
    pRow->addWidget(pSpin);
    pLayout->addLayout(pRow);
    return pSpin;
  };

  m_pGain = addSpinRow("Confidence gain /s", 0.0, 100.0, 0.5, 2, "");
  m_pDecay = addSpinRow("Confidence decay /s", 0.0, 100.0, 0.05, 2, "");
  m_pMemory = addSpinRow("Target memory", 0.0, 600.0, 0.5, 1, " s");

  connect(m_pGain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double fValue) {
    SetRootValue("ConfidenceGainPerSec", plVariant(static_cast<float>(fValue)), "Change Confidence Gain");
  });
  connect(m_pDecay, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double fValue) {
    SetRootValue("ConfidenceDecayPerSec", plVariant(static_cast<float>(fValue)), "Change Confidence Decay");
  });
  connect(m_pMemory, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double fValue) {
    SetRootValue("TargetMemoryDuration", plVariant(plTime::Seconds(fValue)), "Change Target Memory");
  });

  {
    QLabel* pRefNote = new QLabel("reference lines: 0.60 squad share \xC2\xB7 0.15 tutorial gates", pPanel);
    pRefNote->setStyleSheet(QString("color: #6d737a; font-size: 9px; %1").arg(MonoStyle()));
    pLayout->addWidget(pRefNote);
  }

  {
    QFrame* pLine = new QFrame(pPanel);
    pLine->setFrameShape(QFrame::HLine);
    pLine->setStyleSheet("color: #3c3f45;");
    pLayout->addWidget(pLine);
  }

  m_pPeekTitle = new QLabel("SELECTED BEHAVIOR", pPanel);
  m_pPeekTitle->setStyleSheet(QString("color: #8f959b; font-size: 9px; letter-spacing: 1px; %1").arg(MonoStyle()));
  pLayout->addWidget(m_pPeekTitle);

  m_pPeekHost = new QWidget(pPanel);
  m_pPeekLayout = new QVBoxLayout(m_pPeekHost);
  m_pPeekLayout->setContentsMargins(0, 0, 0, 0);
  m_pPeekLayout->setSpacing(3);
  pLayout->addWidget(m_pPeekHost);

  pLayout->addStretch();

  QScrollArea* pScroll = new QScrollArea(pParent);
  pScroll->setWidgetResizable(true);
  pScroll->setFrameShape(QFrame::NoFrame);
  pScroll->setWidget(pPanel);

  return pScroll;
}

const plDocumentObject* plQtAiArchetypeAssetDocumentWindow::GetPropertiesObject() const
{
  return GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
}

void plQtAiArchetypeAssetDocumentWindow::GetBehaviorGuids(plDynamicArray<plUuid>& out_guids) const
{
  out_guids.Clear();

  plDynamicArray<plVariant> values;
  GetDocument()->GetObjectAccessor()->GetValuesByName(GetPropertiesObject(), "Behaviors", values).IgnoreResult();

  for (const plVariant& v : values)
  {
    if (v.IsA<plUuid>())
      out_guids.PushBack(v.Get<plUuid>());
  }
}

const char* plQtAiArchetypeAssetDocumentWindow::GetBehaviorRef(plUInt32 uiIndex) const
{
  const auto& behaviors = m_pArchetypeDocument->GetProperties()->m_Behaviors;
  if (uiIndex >= behaviors.GetCount())
    return "";

  return behaviors[uiIndex].m_sBehavior;
}

void plQtAiArchetypeAssetDocumentWindow::SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription)
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

void plQtAiArchetypeAssetDocumentWindow::RefreshHeader()
{
  m_bUpdatingUi = true;

  const plAiArchetypeAssetObject* pProps = m_pArchetypeDocument->GetProperties();

  if (!m_pTeam->hasFocus())
    m_pTeam->setText(QString::fromUtf8(pProps->m_sTeam.GetData()));
  if (!m_pSensor->hasFocus())
    m_pSensor->setText(QString::fromUtf8(pProps->m_sSensorObjectName.GetData()));

  {
    QString sDisplay = QString::fromUtf8(pProps->m_sBlackboardTemplate.GetData());
    if (!pProps->m_sBlackboardTemplate.IsEmpty())
    {
      auto asset = plAssetCurator::GetSingleton()->FindSubAsset(pProps->m_sBlackboardTemplate);
      if (asset.isValid())
      {
        const plStringView sName = asset->GetName();
        sDisplay = QString::fromUtf8(sName.GetStartPointer(), static_cast<int>(sName.GetElementCount()));
      }
    }
    m_pBlackboard->setText(sDisplay);
    m_pBlackboard->setToolTip(QString::fromUtf8(pProps->m_sBlackboardTemplate.GetData()));
  }

  if (!m_pGain->hasFocus())
    m_pGain->setValue(pProps->m_fConfidenceGainPerSecond);
  if (!m_pDecay->hasFocus())
    m_pDecay->setValue(pProps->m_fConfidenceDecayPerSecond);
  if (!m_pMemory->hasFocus())
    m_pMemory->setValue(pProps->m_TargetMemoryDuration.GetSeconds());

  m_pTimeline->SetValues(pProps->m_fConfidenceGainPerSecond, pProps->m_fConfidenceDecayPerSecond, static_cast<float>(pProps->m_TargetMemoryDuration.GetSeconds()));

  m_bUpdatingUi = false;
}

void plQtAiArchetypeAssetDocumentWindow::RebuildStack()
{
  for (plQtAiBehaviorStackRow* pRow : m_Rows)
  {
    pRow->deleteLater();
  }
  m_Rows.Clear();

  plDynamicArray<plUuid> guids;
  GetBehaviorGuids(guids);

  // keep selection valid
  {
    bool bFound = false;
    for (const plUuid& guid : guids)
      bFound |= (guid == m_SelectedRow);

    if (!bFound)
      m_SelectedRow = plUuid();
  }

  plHybridArray<plUInt32, 8> bandCounts;
  bandCounts.SetCount(m_Bands.GetCount(), 0);

  for (plUInt32 i = 0; i < guids.GetCount(); ++i)
  {
    const char* szRef = GetBehaviorRef(i);
    const plAiBehaviorPeek& peek = plAiBehaviorPeekCache::Get(szRef);

    const plUInt32 uiBand = peek.m_bValid ? BandForCategory(peek.m_iCategory) : s_uiUnresolvedBand;

    plQtAiBehaviorStackRow* pRow = new plQtAiBehaviorStackRow(m_Bands[uiBand].m_pHost, this, guids[i]);
    m_Bands[uiBand].m_pRows->insertWidget(static_cast<int>(bandCounts[uiBand]), pRow);
    ++bandCounts[uiBand];

    m_Rows.PushBack(pRow);
  }

  for (plUInt32 uiBand = 0; uiBand < m_Bands.GetCount(); ++uiBand)
  {
    m_Bands[uiBand].m_pEmpty->setVisible(bandCounts[uiBand] == 0);
  }

  RefreshStack();
  RefreshPeek();
}

void plQtAiArchetypeAssetDocumentWindow::RefreshStack()
{
  plDynamicArray<plUuid> guids;
  GetBehaviorGuids(guids);

  const auto& behaviors = m_pArchetypeDocument->GetProperties()->m_Behaviors;

  for (plQtAiBehaviorStackRow* pRow : m_Rows)
  {
    for (plUInt32 i = 0; i < guids.GetCount(); ++i)
    {
      if (guids[i] == pRow->GetObjectGuid() && i < behaviors.GetCount())
      {
        const plAiBehaviorPeek& peek = plAiBehaviorPeekCache::Get(behaviors[i].m_sBehavior);
        pRow->Refresh(peek, behaviors[i].m_sBehavior, behaviors[i].m_fWeightScale, pRow->GetObjectGuid() == m_SelectedRow);
        break;
      }
    }
  }
}

void plQtAiArchetypeAssetDocumentWindow::RefreshPeek()
{
  // clear old entries
  while (QLayoutItem* pItem = m_pPeekLayout->takeAt(0))
  {
    if (pItem->widget())
      pItem->widget()->deleteLater();
    delete pItem;
  }

  QString sRef;
  {
    plDynamicArray<plUuid> guids;
    GetBehaviorGuids(guids);

    const auto& behaviors = m_pArchetypeDocument->GetProperties()->m_Behaviors;

    for (plUInt32 i = 0; i < guids.GetCount() && i < behaviors.GetCount(); ++i)
    {
      if (guids[i] == m_SelectedRow)
      {
        sRef = QString::fromUtf8(behaviors[i].m_sBehavior.GetData());
        break;
      }
    }
  }

  if (sRef.isEmpty())
  {
    m_pPeekTitle->setText("SELECTED BEHAVIOR");
    QLabel* pHint = new QLabel("select a behavior row to peek at its considerations", m_pPeekHost);
    pHint->setStyleSheet("color: #6d737a; font-size: 10px; font-style: italic;");
    m_pPeekLayout->addWidget(pHint);
    return;
  }

  const plAiBehaviorPeek& peek = plAiBehaviorPeekCache::Get(sRef.toUtf8().data());

  if (!peek.m_bValid)
  {
    m_pPeekTitle->setText("SELECTED BEHAVIOR (UNRESOLVED)");
    return;
  }

  m_pPeekTitle->setText(QString("SELECTED - %1").arg(peek.m_sName.toUpper()));

  // summary line
  {
    QLabel* pSummary = new QLabel(QString("%1 \xC2\xB7 weight %2 \xC2\xB7 commit %3 \xC2\xB7 %4")
                                    .arg(plAiAssetUi::CategoryName(peek.m_iCategory))
                                    .arg(QString::number(peek.m_fWeight, 'f', 2))
                                    .arg(QString::number(peek.m_fCommitBonus, 'f', 2))
                                    .arg(peek.m_sLogic.isEmpty() ? "no logic" : peek.m_sLogic),
      m_pPeekHost);
    pSummary->setStyleSheet(QString("color: %1; font-size: 10px; %2").arg(plAiAssetUi::CategoryColor(peek.m_iCategory).name()).arg(MonoStyle()));
    m_pPeekLayout->addWidget(pSummary);
  }

  for (const auto& cons : peek.m_Considerations)
  {
    QWidget* pRow = new QWidget(m_pPeekHost);
    QHBoxLayout* pRowLayout = new QHBoxLayout(pRow);
    pRowLayout->setContentsMargins(0, 0, 0, 0);
    pRowLayout->setSpacing(6);

    QLabel* pLabel = new QLabel(QString("%1 [%2 \xE2\x86\x92 %3]").arg(cons.m_sLabel).arg(cons.m_fInputMin).arg(cons.m_fInputMax), pRow);
    pLabel->setStyleSheet(QString("color: #9aa0a8; font-size: 9px; %1").arg(MonoStyle()));
    pLabel->setWordWrap(false);

    plQtAiCurveMiniView* pCurve = new plQtAiCurveMiniView(pRow);
    pCurve->SetCurve(cons.m_Curve, cons.m_bLegacyCurveDomain);

    pRowLayout->addWidget(pLabel, 1);
    pRowLayout->addWidget(pCurve);

    m_pPeekLayout->addWidget(pRow);
  }

  if (peek.m_Considerations.IsEmpty())
  {
    QLabel* pNone = new QLabel("no considerations - scores its raw weight", m_pPeekHost);
    pNone->setStyleSheet("color: #6d737a; font-size: 10px; font-style: italic;");
    m_pPeekLayout->addWidget(pNone);
  }

  {
    QLabel* pHint = new QLabel("read-only peek \xC2\xB7 use the row's open button to edit", m_pPeekHost);
    pHint->setStyleSheet("color: #565a5e; font-size: 9px;");
    m_pPeekLayout->addWidget(pHint);
  }
}

void plQtAiArchetypeAssetDocumentWindow::OnAssetPropertiesChanged()
{
  RefreshHeader();
  RefreshStack();
  RefreshPeek();
}

void plQtAiArchetypeAssetDocumentWindow::OnAssetStructureChanged()
{
  RefreshHeader();
  RebuildStack();
}

void plQtAiArchetypeAssetDocumentWindow::SelectBehaviorRow(const plUuid& guid)
{
  m_SelectedRow = guid;

  if (const plDocumentObject* pObject = GetDocument()->GetObjectAccessor()->GetObject(guid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObject);
  }

  RefreshStack();
  RefreshPeek();
}

void plQtAiArchetypeAssetDocumentWindow::SetWeightScale(const plUuid& guid, double fValue)
{
  if (m_bUpdatingUi)
    return;

  const plDocumentObject* pObject = GetDocument()->GetObjectAccessor()->GetObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Change Weight Scale");

  if (pAccessor->SetValueByName(pObject, "WeightScale", plVariant(static_cast<float>(fValue))).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiArchetypeAssetDocumentWindow::OpenBehavior(const plUuid& guid)
{
  plDynamicArray<plUuid> guids;
  GetBehaviorGuids(guids);

  for (plUInt32 i = 0; i < guids.GetCount(); ++i)
  {
    if (guids[i] != guid)
      continue;

    const plAiBehaviorPeek& peek = plAiBehaviorPeekCache::Get(GetBehaviorRef(i));
    if (peek.m_bValid && !peek.m_sAbsPath.isEmpty())
    {
      plQtEditorApp::GetSingleton()->OpenDocumentQueued(peek.m_sAbsPath.toUtf8().data());
    }
    return;
  }
}

void plQtAiArchetypeAssetDocumentWindow::RemoveBehavior(const plUuid& guid)
{
  const plDocumentObject* pObject = GetDocument()->GetObjectAccessor()->GetObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Remove Behavior");

  if (pAccessor->RemoveObject(pObject).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiArchetypeAssetDocumentWindow::onAddBehavior()
{
  plQtAssetBrowserDlg dlg(this, plUuid(), "CompatibleAsset_AiBehavior");
  if (dlg.exec() == 0 || !dlg.GetSelectedAssetGuid().IsValid())
    return;

  plStringBuilder sGuid;
  plConversionUtils::ToString(dlg.GetSelectedAssetGuid(), sGuid);

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Add Behavior");

  bool bOk = true;

  plUuid rowGuid = plUuid::MakeUuid();
  bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "Behaviors", pAccessor->GetCountByName(GetPropertiesObject(), "Behaviors"), plGetStaticRTTI<plAiArchetypeBehaviorObject>(), rowGuid).Succeeded();

  if (bOk)
  {
    if (const plDocumentObject* pRow = pAccessor->GetObject(rowGuid))
    {
      bOk &= pAccessor->SetValueByName(pRow, "Behavior", plVariant(sGuid.GetData())).Succeeded();
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
    SelectBehaviorRow(rowGuid);
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plQtAiArchetypeAssetDocumentWindow::BrowseBlackboardTemplate()
{
  const plAiArchetypeAssetObject* pProps = m_pArchetypeDocument->GetProperties();

  plUuid currentGuid;
  if (plConversionUtils::IsStringUuid(pProps->m_sBlackboardTemplate))
  {
    currentGuid = plConversionUtils::ConvertStringToUuid(pProps->m_sBlackboardTemplate);
  }

  plQtAssetBrowserDlg dlg(this, currentGuid, "CompatibleAsset_BlackboardTemplate");
  if (dlg.exec() == 0 || !dlg.GetSelectedAssetGuid().IsValid())
    return;

  plStringBuilder sGuid;
  plConversionUtils::ToString(dlg.GetSelectedAssetGuid(), sGuid);

  SetRootValue("BlackboardTemplate", plVariant(sGuid.GetData()), "Set Blackboard Template");
}
