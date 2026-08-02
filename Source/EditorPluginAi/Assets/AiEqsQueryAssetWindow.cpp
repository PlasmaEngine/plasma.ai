#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEqsQueryAsset.h>
#include <EditorPluginAi/Assets/AiEqsQueryAssetWindow.moc.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <AiPlugin/Eqs/EqsContext.h>
#include <AiPlugin/Eqs/EqsGenerator.h>
#include <AiPlugin/Eqs/EqsTest.h>

#include <Foundation/Math/ColorScheme.h>
#include <Foundation/Math/Random.h>
#include <GuiFoundation/Dialogs/CurveEditDlg.moc.h>
#include <GuiFoundation/DockPanels/DocumentPanel.moc.h>
#include <GuiFoundation/PropertyGrid/Implementation/PropertyWidget.moc.h>
#include <GuiFoundation/UIServices/DynamicStringEnum.h>
#include <GuiFoundation/Widgets/SearchableMenu.moc.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>

namespace
{
  static QString MonoStyle()
  {
    return QStringLiteral("font-family: Consolas, 'Cascadia Mono', monospace;");
  }

  static QString PrettyEqsName(const plRTTI* pRtti)
  {
    if (pRtti == nullptr)
      return QStringLiteral("<none>");

    plStringBuilder sName = pRtti->GetTypeName();
    sName.TrimWordStart("plAiEqsTest_");
    sName.TrimWordStart("plAiEqsGenerator_");
    sName.TrimWordStart("plAiEqsContext_");

    QString sIn = QString::fromUtf8(sName.GetData());
    QString sOut;
    sOut.reserve(sIn.size() + 6);

    for (int i = 0; i < sIn.size(); ++i)
    {
      if (i > 0 && sIn[i].isUpper() && !sIn[i - 1].isUpper())
        sOut.append(' ');

      sOut.append(sIn[i]);
    }

    return sOut;
  }

  static const char* TestGroup(const plRTTI* pRtti)
  {
    const plStringView sName = pRtti->GetTypeName();

    if (sName.FindSubString("Distance") || sName.FindSubString("Direction"))
      return "Distance & Direction";
    if (sName.FindSubString("Cover") || sName.FindSubString("Unclaimed"))
      return "Cover";
    if (sName.FindSubString("LineOfSight"))
      return "Visibility";
    if (sName.FindSubString("Reachable") || sName.FindSubString("PathLength"))
      return "Pathfinding";
    if (sName.FindSubString("Blackboard"))
      return "Blackboard";
    return "Other";
  }

  static QString TestSummary(const plAiEqsTest* pTest)
  {
    if (pTest == nullptr)
      return QStringLiteral("select a test type");

    if (auto pDist = plDynamicCast<const plAiEqsTest_Distance*>(pTest))
    {
      return QString("band %1 \xE2\x86\x92 %2 m \xC2\xB7 vs %3").arg(pDist->m_fBandMin, 0, 'f', 1).arg(pDist->m_fBandMax, 0, 'f', 1).arg(QString::fromUtf8(pDist->GetContext()));
    }

    if (auto pDir = plDynamicCast<const plAiEqsTest_Direction*>(pTest))
    {
      return QString("desired angle %1\xC2\xB0 around %2").arg(pDir->m_DesiredAngle.GetDegree(), 0, 'f', 0).arg(QString::fromUtf8(pDir->GetContext()));
    }

    if (auto pQuality = plDynamicCast<const plAiEqsTest_CoverQuality*>(pTest))
    {
      const char* szQuality = pQuality->m_MinQuality == plAiCoverQuality::High ? "High" : (pQuality->m_MinQuality == plAiCoverQuality::Low ? "Low" : "None");
      return QString("min quality: %1 \xC2\xB7 scored by tier").arg(szQuality);
    }

    if (auto pFacing = plDynamicCast<const plAiEqsTest_CoverFacing*>(pTest))
    {
      return QString("wall must face context \xC2\xB7 min dot %1").arg(pFacing->m_fMinDot, 0, 'f', 2);
    }

    if (plDynamicCast<const plAiEqsTest_Unclaimed*>(pTest) != nullptr)
    {
      return QStringLiteral("cover/slot payload not claimed by others");
    }

    if (auto pLos = plDynamicCast<const plAiEqsTest_LineOfSight*>(pTest))
    {
      const char* szCombine = pLos->m_Combine == plAiEqsContextCombine::Max ? "Max" : (pLos->m_Combine == plAiEqsContextCombine::Average ? "Average" : "Min");
      return QString("prefer %1 \xC2\xB7 eye heights %2 / %3 \xC2\xB7 combine: %4")
        .arg(pLos->m_bPreferVisible ? "VISIBLE" : "BLOCKED")
        .arg(pLos->m_fItemEyeHeight, 0, 'f', 2)
        .arg(pLos->m_fContextEyeHeight, 0, 'f', 2)
        .arg(szCombine);
    }

    if (plDynamicCast<const plAiEqsTest_ReachableApprox*>(pTest) != nullptr)
    {
      return QStringLiteral("navmesh raycast from the querier \xC2\xB7 detour scores 0.25");
    }

    if (auto pPath = plDynamicCast<const plAiEqsTest_PathLength*>(pTest))
    {
      return QString("path length band %1 \xE2\x86\x92 %2 m \xC2\xB7 unreachable fails").arg(pPath->m_fBandMin, 0, 'f', 1).arg(pPath->m_fBandMax, 0, 'f', 1);
    }

    return QString();
  }

  static QString GeneratorSummary(const plAiEqsGenerator* pGen)
  {
    if (pGen == nullptr)
      return QStringLiteral("choose a generator - it produces the candidate set");

    const QString sCtx = QString::fromUtf8(pGen->GetCenterContext());

    if (plDynamicCast<const plAiEqsGenerator_Ring*>(pGen) != nullptr)
      return QString("golden-angle rings %1 \xE2\x86\x92 %2 m around %3, navmesh-projected").arg(pGen->m_fRadiusMin, 0, 'f', 1).arg(pGen->m_fRadiusMax, 0, 'f', 1).arg(sCtx);

    if (auto pGrid = plDynamicCast<const plAiEqsGenerator_Grid*>(pGen))
      return QString("grid, spacing %1 m, extent %2 m around %3, navmesh-projected").arg(pGrid->m_fSpacing, 0, 'f', 1).arg(pGen->m_fRadiusMax, 0, 'f', 1).arg(sCtx);

    if (plDynamicCast<const plAiEqsGenerator_NavmeshRandom*>(pGen) != nullptr)
      return QString("random reachable points %1 \xE2\x86\x92 %2 m around %3").arg(pGen->m_fRadiusMin, 0, 'f', 1).arg(pGen->m_fRadiusMax, 0, 'f', 1).arg(sCtx);

    if (plDynamicCast<const plAiEqsGenerator_CoverPoints*>(pGen) != nullptr)
      return QString("baked cover points within %1 m of %2 \xC2\xB7 AreaNotReady while sectors stream").arg(pGen->m_fRadiusMax, 0, 'f', 1).arg(sCtx);

    if (auto pSo = plDynamicCast<const plAiEqsGenerator_SmartObjects*>(pGen))
    {
      const QString sType = plStringUtils::IsNullOrEmpty(pSo->GetType()) ? QStringLiteral("any") : QString::fromUtf8(pSo->GetType());
      return QString("free '%1' smart object slots within %2 m of %3").arg(sType).arg(pGen->m_fRadiusMax, 0, 'f', 1).arg(sCtx);
    }

    return QString();
  }

  static const char* ContextChipText(const plAiEqsTest* pTest)
  {
    if (pTest == nullptr)
      return nullptr;

    const char* szContext = pTest->GetContextProperty();

    if (plStringUtils::IsNullOrEmpty(szContext))
      return nullptr;

    return szContext;
  }

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

  static const CurvePreset s_EqsCurvePresets[] = {
    {"Linear Rise", false, 2, {{0.0, 0.0}, {1.0, 1.0}}},
    {"Linear Fall", false, 2, {{0.0, 1.0}, {1.0, 0.0}}},
    {"Smooth Rise", true, 2, {{0.0, 0.0}, {1.0, 1.0}}},
    {"Smooth Fall", true, 2, {{0.0, 1.0}, {1.0, 0.0}}},
    {"Square (emphasize best)", true, 3, {{0.0, 0.0}, {0.5, 0.25}, {1.0, 1.0}}},
    {"Square Root (lenient)", true, 3, {{0.0, 0.0}, {0.5, 0.7}, {1.0, 1.0}}},
    {"Bell", true, 3, {{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}}},
    {"Constant 1", false, 2, {{0.0, 1.0}, {1.0, 1.0}}},
    {"Clear (use raw score)", false, 0, {}},
  };

  static bool SegmentsIntersect(const plVec2& p1, const plVec2& p2, const plVec2& q1, const plVec2& q2)
  {
    auto cross = [](const plVec2& a, const plVec2& b) { return a.x * b.y - a.y * b.x; };

    const plVec2 r = p2 - p1;
    const plVec2 s = q2 - q1;
    const float rxs = cross(r, s);

    if (plMath::Abs(rxs) < 0.00001f)
      return false; // parallel

    const float t = cross(q1 - p1, s) / rxs;
    const float u = cross(q1 - p1, r) / rxs;

    return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
  }

  static float PreviewTrapezoid(float fValue, float fFullMin, float fFullMax)
  {
    if (fValue >= fFullMin && fValue <= fFullMax)
      return 1.0f;

    const float fFalloff = plMath::Max(0.5f, (fFullMax - fFullMin) * 0.25f);

    if (fValue < fFullMin)
      return plMath::Max(0.0f, 1.0f - (fFullMin - fValue) / fFalloff);

    return plMath::Max(0.0f, 1.0f - (fValue - fFullMax) / fFalloff);
  }

  static QColor ScoreColor(float fScore)
  {
    // green -> red ramp like the runtime visualization
    const float t = plMath::Saturate(fScore);
    return QColor(plMath::Lerp(223, 88, t), plMath::Lerp(96, 214, t), plMath::Lerp(96, 130, t));
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plQtAiEqsChip
//////////////////////////////////////////////////////////////////////////

plQtAiEqsChip::plQtAiEqsChip(QWidget* pParent)
  : QLabel(pParent)
{
  setVisible(false);
}

void plQtAiEqsChip::SetChip(Style style, const QString& sText)
{
  const char* szColors = "background-color: #2b4232; color: #7fd79a;"; // cheap

  switch (style)
  {
    case Style::Cheap:
      szColors = "background-color: #2b4232; color: #7fd79a;";
      break;
    case Style::Expensive:
      szColors = "background-color: #4a3a24; color: #e8b45a;";
      break;
    case Style::Filter:
      szColors = "background-color: #4a2b2b; color: #ef8f8f;";
      break;
    case Style::Score:
      szColors = "background-color: #243a52; color: #7db8f0;";
      break;
    case Style::FilterAndScore:
      szColors = "background-color: #3a2f4a; color: #c39aef;";
      break;
    case Style::Context:
      szColors = "background-color: #c2a05c; color: #241d10;";
      break;
    case Style::Payload:
      szColors = "background-color: #2a3d45; color: #6ec7d8;";
      break;
  }

  setStyleSheet(QString("%1 border-radius: 7px; padding: 0px 7px; font-size: 9px; font-weight: bold;").arg(szColors));
  setText(sText);
  setVisible(!sText.isEmpty());
}

//////////////////////////////////////////////////////////////////////////
// plQtAiEqsTestCard
//////////////////////////////////////////////////////////////////////////

plQtAiEqsTestCard::plQtAiEqsTestCard(QWidget* pParent, plQtAiEqsQueryAssetDocumentWindow* pWindow, const plUuid& objectGuid)
  : QFrame(pParent)
  , m_pWindow(pWindow)
  , m_ObjectGuid(objectGuid)
{
  setObjectName("AiEqsCard");
  setProperty("cardSelected", false);
  setCursor(Qt::PointingHandCursor);

  QHBoxLayout* pMain = new QHBoxLayout(this);
  pMain->setContentsMargins(10, 7, 8, 7);
  pMain->setSpacing(12);

  // identity
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

    m_pContextChip = new plQtAiEqsChip(this);
    m_pPurposeChip = new plQtAiEqsChip(this);
    m_pCostChip = new plQtAiEqsChip(this);

    pTypeRow->addWidget(m_pTypeLabel);
    pTypeRow->addWidget(m_pContextChip);
    pTypeRow->addWidget(m_pPurposeChip);
    pTypeRow->addWidget(m_pCostChip);
    pTypeRow->addStretch();

    m_pSummaryLabel = new QLabel(this);
    m_pSummaryLabel->setStyleSheet(QString("color: #9aa0a8; %1 font-size: 10px;").arg(MonoStyle()));

    pId->addLayout(pTypeRow);
    pId->addWidget(m_pSummaryLabel);

    QWidget* pIdHost = new QWidget(this);
    pIdHost->setLayout(pId);
    pIdHost->setMinimumWidth(230);
    pMain->addWidget(pIdHost, 3);
  }

  // weight
  {
    QHBoxLayout* pWeight = new QHBoxLayout();
    pWeight->setContentsMargins(0, 0, 0, 0);
    pWeight->setSpacing(4);

    QLabel* pWt = new QLabel("wt", this);
    pWt->setStyleSheet("color: #9aa0a8; font-size: 10px;");

    m_pWeight = new QDoubleSpinBox(this);
    m_pWeight->setRange(0.0, 10.0);
    m_pWeight->setDecimals(2);
    m_pWeight->setSingleStep(0.1);
    m_pWeight->setToolTip("Weight of this test in the weight-normalized score sum.");

    pWeight->addWidget(pWt);
    pWeight->addWidget(m_pWeight);

    m_pWeightHost = new QWidget(this);
    m_pWeightHost->setLayout(pWeight);
    pMain->addWidget(m_pWeightHost, 0);

    connect(m_pWeight, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &plQtAiEqsTestCard::onWeightEdited);
  }

  // response curve (scoring tests) / placeholder (hard filters)
  {
    m_pCurveButton = new plQtCurve1DButtonWidget(this);
    m_pCurveButton->setMinimumSize(100, 36);
    m_pCurveButton->setMaximumHeight(40);
    m_pCurveButton->setToolTip("Response curve remapping the test's raw [0,1] score.\nClick to edit, right-click for presets.");
    m_pCurveButton->setContextMenuPolicy(Qt::CustomContextMenu);
    pMain->addWidget(m_pCurveButton, 2);

    m_pNoCurve = new QLabel("no curve", this);
    m_pNoCurve->setAlignment(Qt::AlignCenter);
    m_pNoCurve->setMinimumSize(100, 36);
    m_pNoCurve->setStyleSheet("color: #5c6067; font-size: 9px; border: 1px dashed #33363c; border-radius: 3px;");
    m_pNoCurve->setToolTip("Hard filters judge the raw score - no response curve.");
    pMain->addWidget(m_pNoCurve, 2);
    m_pNoCurve->setVisible(false);

    connect(m_pCurveButton, &plQtCurve1DButtonWidget::clicked, this, &plQtAiEqsTestCard::onCurveClicked);
    connect(m_pCurveButton, &QWidget::customContextMenuRequested, this, &plQtAiEqsTestCard::onCurveContextMenu);
  }

  // order / remove
  {
    QVBoxLayout* pOps = new QVBoxLayout();
    pOps->setContentsMargins(0, 0, 0, 0);
    pOps->setSpacing(1);

    m_pUp = new QToolButton(this);
    m_pUp->setText(QString::fromUtf8("\xE2\x96\xB2")); // ▲
    m_pUp->setAutoRaise(true);
    m_pUp->setToolTip("Move up (cheap tests always run before expensive ones)");

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
    m_pRemove->setToolTip("Remove this test");
    m_pRemove->setStyleSheet("color: #e05555;");
    pMain->addWidget(m_pRemove);

    connect(m_pUp, &QToolButton::clicked, this, [this]() { m_pWindow->MoveTest(m_ObjectGuid, -1); });
    connect(m_pDown, &QToolButton::clicked, this, [this]() { m_pWindow->MoveTest(m_ObjectGuid, +1); });
    connect(m_pRemove, &QToolButton::clicked, this, [this]() { m_pWindow->RemoveTest(m_ObjectGuid); });
  }
}

void plQtAiEqsTestCard::RefreshFromNative(const plAiEqsTestObject* pNative, plUInt32 uiIndex, plUInt32 uiCount, bool bSelected)
{
  m_bUpdating = true;

  const plAiEqsTest* pTest = (pNative != nullptr) ? pNative->m_pTest : nullptr;

  m_pTypeLabel->setText(PrettyEqsName(pTest != nullptr ? pTest->GetDynamicRTTI() : nullptr));
  m_pSummaryLabel->setText(TestSummary(pTest));

  if (const char* szContext = ContextChipText(pTest))
  {
    m_pContextChip->SetChip(plQtAiEqsChip::Style::Context, QString::fromUtf8(szContext).toUpper());
  }
  else
  {
    m_pContextChip->SetChip(plQtAiEqsChip::Style::Context, QString());
  }

  bool bScores = true;

  if (pTest != nullptr)
  {
    switch (pTest->m_Purpose.GetValue())
    {
      case plAiEqsTestPurpose::FilterOnly:
        m_pPurposeChip->SetChip(plQtAiEqsChip::Style::Filter, "FILTER");
        bScores = false;
        break;
      case plAiEqsTestPurpose::ScoreOnly:
        m_pPurposeChip->SetChip(plQtAiEqsChip::Style::Score, "SCORE");
        break;
      default:
        m_pPurposeChip->SetChip(plQtAiEqsChip::Style::FilterAndScore, "FILTER + SCORE");
        break;
    }

    if (pTest->GetCost() == plAiEqsTest::Cost::Expensive)
    {
      const bool bNavmesh = plDynamicCast<const plAiEqsTest_ReachableApprox*>(pTest) != nullptr || plDynamicCast<const plAiEqsTest_PathLength*>(pTest) != nullptr;
      m_pCostChip->SetChip(plQtAiEqsChip::Style::Expensive, bNavmesh ? "NAVMESH" : "RAYCAST");
    }
    else
    {
      m_pCostChip->SetChip(plQtAiEqsChip::Style::Cheap, "CHEAP");
    }

    if (!m_pWeight->hasFocus())
      m_pWeight->setValue(pTest->m_fWeight);
  }
  else
  {
    m_pPurposeChip->SetChip(plQtAiEqsChip::Style::Filter, QString());
    m_pCostChip->SetChip(plQtAiEqsChip::Style::Cheap, QString());
  }

  m_pWeightHost->setVisible(bScores);
  m_pCurveButton->setVisible(bScores);
  m_pNoCurve->setVisible(!bScores);

  if (bScores)
  {
    plObjectAccessorBase* pAccessor = m_pWindow->GetDocument()->GetObjectAccessor();
    if (const plDocumentObject* pObject = pAccessor->GetObject(m_ObjectGuid))
    {
      const plDocumentObject* pCurve = pAccessor->GetChildObjectByName(pObject, "ScoreCurve", plVariant());
      if (pCurve != nullptr)
      {
        m_pCurveButton->UpdatePreview(pAccessor, pCurve, plAiAssetUi::CurveColor(), 0.0, true, 1.0, true, 1.0, 0.0, 1.0);
      }
    }
  }

  m_pUp->setEnabled(uiIndex > 0);
  m_pDown->setEnabled(uiIndex + 1 < uiCount);

  if (property("cardSelected").toBool() != bSelected)
  {
    setProperty("cardSelected", bSelected);
    style()->unpolish(this);
    style()->polish(this);
  }

  m_bUpdating = false;
}

void plQtAiEqsTestCard::mousePressEvent(QMouseEvent* pEvent)
{
  m_pWindow->SelectObject(m_ObjectGuid);
  QFrame::mousePressEvent(pEvent);
}

void plQtAiEqsTestCard::onWeightEdited(double fValue)
{
  if (m_bUpdating)
    return;
  m_pWindow->SetTestWeight(m_ObjectGuid, fValue);
}

void plQtAiEqsTestCard::onCurveClicked()
{
  m_pWindow->SelectObject(m_ObjectGuid);
  m_pWindow->OpenCurveEditor(m_ObjectGuid);
}

void plQtAiEqsTestCard::onCurveContextMenu(const QPoint& pos)
{
  QMenu menu(this);

  QMenu* pPresets = menu.addMenu("Presets");
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_EqsCurvePresets); ++i)
  {
    QAction* pAction = pPresets->addAction(s_EqsCurvePresets[i].szName);
    const plUInt32 uiPreset = i;
    connect(pAction, &QAction::triggered, this, [this, uiPreset]() { m_pWindow->ApplyCurvePreset(m_ObjectGuid, uiPreset); });
  }

  QAction* pEdit = menu.addAction("Edit Curve...");
  connect(pEdit, &QAction::triggered, this, [this]() { m_pWindow->OpenCurveEditor(m_ObjectGuid); });

  menu.exec(m_pCurveButton->mapToGlobal(pos));
}

//////////////////////////////////////////////////////////////////////////
// plQtAiEqsGeneratorCard
//////////////////////////////////////////////////////////////////////////

plQtAiEqsGeneratorCard::plQtAiEqsGeneratorCard(QWidget* pParent, plQtAiEqsQueryAssetDocumentWindow* pWindow)
  : QFrame(pParent)
  , m_pWindow(pWindow)
{
  setObjectName("AiEqsGeneratorCard");
  setCursor(Qt::PointingHandCursor);

  QVBoxLayout* pMain = new QVBoxLayout(this);
  pMain->setContentsMargins(10, 8, 10, 8);
  pMain->setSpacing(4);

  QHBoxLayout* pRow = new QHBoxLayout();
  pRow->setContentsMargins(0, 0, 0, 0);
  pRow->setSpacing(10);

  m_pNameLabel = new QLabel(this);
  {
    QFont f = m_pNameLabel->font();
    f.setBold(true);
    m_pNameLabel->setFont(f);
  }

  m_pPayloadChip = new plQtAiEqsChip(this);

  QLabel* pAround = new QLabel("Around", this);
  pAround->setStyleSheet("color: #8f959b; font-size: 10px;");

  m_pAround = new QComboBox(this);
  m_pAround->setToolTip("Which context the generator centers on.");

  QLabel* pRadius = new QLabel("Radius", this);
  pRadius->setStyleSheet("color: #8f959b; font-size: 10px;");

  m_pRadiusMin = new QDoubleSpinBox(this);
  m_pRadiusMin->setRange(0.0, 200.0);
  m_pRadiusMin->setDecimals(1);
  m_pRadiusMin->setSingleStep(0.5);

  QLabel* pArrow = new QLabel(QString::fromUtf8("\xE2\x86\x92"), this); // →

  m_pRadiusMax = new QDoubleSpinBox(this);
  m_pRadiusMax->setRange(0.5, 500.0);
  m_pRadiusMax->setDecimals(1);
  m_pRadiusMax->setSingleStep(0.5);

  QLabel* pMeters = new QLabel("m", this);
  pMeters->setStyleSheet("color: #8f959b; font-size: 10px;");

  m_pChange = new QPushButton("Change...", this);
  m_pChange->setToolTip("Replace the generator with a different type.");

  pRow->addWidget(m_pNameLabel);
  pRow->addWidget(m_pPayloadChip);
  pRow->addSpacing(6);
  pRow->addWidget(pAround);
  pRow->addWidget(m_pAround);
  pRow->addWidget(pRadius);
  pRow->addWidget(m_pRadiusMin);
  pRow->addWidget(pArrow);
  pRow->addWidget(m_pRadiusMax);
  pRow->addWidget(pMeters);
  pRow->addStretch();
  pRow->addWidget(m_pChange);

  m_pSummaryLabel = new QLabel(this);
  m_pSummaryLabel->setStyleSheet(QString("color: #9aa0a8; %1 font-size: 10px;").arg(MonoStyle()));

  pMain->addLayout(pRow);
  pMain->addWidget(m_pSummaryLabel);

  connect(m_pAround, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &plQtAiEqsGeneratorCard::onAroundChanged);
  connect(m_pRadiusMin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &plQtAiEqsGeneratorCard::onRadiusMinEdited);
  connect(m_pRadiusMax, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &plQtAiEqsGeneratorCard::onRadiusMaxEdited);
  connect(m_pChange, &QPushButton::clicked, this, &plQtAiEqsGeneratorCard::onChangeClicked);
}

void plQtAiEqsGeneratorCard::RefreshFromNative(const plAiEqsGenerator* pNative, const plAiEqsQueryAssetObject* pProps)
{
  m_bUpdating = true;

  m_pNameLabel->setText(pNative != nullptr ? PrettyEqsName(pNative->GetDynamicRTTI()) : QStringLiteral("<no generator>"));
  m_pSummaryLabel->setText(GeneratorSummary(pNative));

  if (pNative != nullptr)
  {
    switch (pNative->GetPayloadType())
    {
      case plAiEqsPayloadType::CoverPoint:
        m_pPayloadChip->SetChip(plQtAiEqsChip::Style::Payload, "PAYLOAD: COVER");
        break;
      case plAiEqsPayloadType::SmartObjectSlot:
        m_pPayloadChip->SetChip(plQtAiEqsChip::Style::Payload, "PAYLOAD: SMART OBJECT");
        break;
      case plAiEqsPayloadType::GameObject:
        m_pPayloadChip->SetChip(plQtAiEqsChip::Style::Payload, "PAYLOAD: OBJECT");
        break;
      default:
        m_pPayloadChip->SetChip(plQtAiEqsChip::Style::Payload, QString());
        break;
    }
  }
  else
  {
    m_pPayloadChip->SetChip(plQtAiEqsChip::Style::Payload, QString());
  }

  // center context choices: Querier + all slot names
  {
    m_pAround->clear();
    m_pAround->addItem("Querier");

    plDynamicArray<plString> names;
    m_pWindow->GetContextSlotNames(names);

    for (const plString& sName : names)
    {
      if (sName != "Querier")
      {
        m_pAround->addItem(QString::fromUtf8(sName.GetData()));
      }
    }

    if (pNative != nullptr)
    {
      const QString sCurrent = QString::fromUtf8(pNative->GetCenterContext());
      int iIndex = m_pAround->findText(sCurrent);

      if (iIndex < 0 && !sCurrent.isEmpty())
      {
        m_pAround->addItem(sCurrent);
        iIndex = m_pAround->count() - 1;
      }

      m_pAround->setCurrentIndex(plMath::Max(iIndex, 0));
    }
  }

  if (pNative != nullptr)
  {
    if (!m_pRadiusMin->hasFocus())
      m_pRadiusMin->setValue(pNative->m_fRadiusMin);
    if (!m_pRadiusMax->hasFocus())
      m_pRadiusMax->setValue(pNative->m_fRadiusMax);
  }

  m_pAround->setEnabled(pNative != nullptr);
  m_pRadiusMin->setEnabled(pNative != nullptr);
  m_pRadiusMax->setEnabled(pNative != nullptr);

  m_bUpdating = false;
}

void plQtAiEqsGeneratorCard::mousePressEvent(QMouseEvent* pEvent)
{
  m_pWindow->SelectGenerator();
  QFrame::mousePressEvent(pEvent);
}

void plQtAiEqsGeneratorCard::onAroundChanged(int iIndex)
{
  if (m_bUpdating || iIndex < 0)
    return;

  m_pWindow->SetGeneratorValue("CenterContext", plVariant(m_pAround->itemText(iIndex).toUtf8().data()), "Change Generator Center");
}

void plQtAiEqsGeneratorCard::onRadiusMinEdited(double fValue)
{
  if (m_bUpdating)
    return;
  m_pWindow->SetGeneratorValue("RadiusMin", plVariant(static_cast<float>(fValue)), "Change Radius Min");
}

void plQtAiEqsGeneratorCard::onRadiusMaxEdited(double fValue)
{
  if (m_bUpdating)
    return;
  m_pWindow->SetGeneratorValue("RadiusMax", plVariant(static_cast<float>(fValue)), "Change Radius Max");
}

void plQtAiEqsGeneratorCard::onChangeClicked()
{
  m_pWindow->ChangeGeneratorType();
}

//////////////////////////////////////////////////////////////////////////
// plQtAiEqsContextStrip
//////////////////////////////////////////////////////////////////////////

plQtAiEqsContextStrip::plQtAiEqsContextStrip(QWidget* pParent, plQtAiEqsQueryAssetDocumentWindow* pWindow)
  : QWidget(pParent)
  , m_pWindow(pWindow)
{
  m_pLayout = new QHBoxLayout(this);
  m_pLayout->setContentsMargins(12, 6, 12, 6);
  m_pLayout->setSpacing(8);

  QLabel* pLabel = new QLabel("CONTEXTS", this);
  pLabel->setStyleSheet(QString("color: #8f959b; font-size: 9px; letter-spacing: 1px; %1").arg(MonoStyle()));
  m_pLayout->addWidget(pLabel);

  m_pLayout->addStretch();

  m_pAddButton = new QPushButton("+ Add Context", this);
  m_pAddButton->setFlat(true);
  m_pAddButton->setStyleSheet("color: #7d838c; border: 1px dashed #3a3f4a; border-radius: 12px; padding: 2px 10px; font-size: 10px;");
  m_pAddButton->setToolTip("Add a named context slot ('relative to what?') that generators and tests can reference.");
  m_pLayout->addWidget(m_pAddButton);

  connect(m_pAddButton, &QPushButton::clicked, this, [this]() { m_pWindow->AddContextSlot(); });
}

void plQtAiEqsContextStrip::Rebuild()
{
  for (QWidget* pChip : m_Chips)
  {
    m_pLayout->removeWidget(pChip);
    pChip->deleteLater();
  }
  m_Chips.Clear();

  int iInsert = 1; // after the CONTEXTS label

  auto addChip = [&](const QString& sName, const QString& sType, const plUuid& guid, bool bBuiltIn) {
    QFrame* pChip = new QFrame(this);
    pChip->setStyleSheet("QFrame { background-color: #1f2125; border: 1px solid #3a3f4a; border-radius: 12px; }");

    QHBoxLayout* pChipLayout = new QHBoxLayout(pChip);
    pChipLayout->setContentsMargins(4, 2, bBuiltIn ? 10 : 4, 2);
    pChipLayout->setSpacing(2);

    // clicking the chip body selects the slot object in the property grid (edit name/type there)
    QPushButton* pBody = new QPushButton(QString("%1  %2").arg(sName).arg(sType), pChip);
    pBody->setFlat(true);
    pBody->setCursor(bBuiltIn ? Qt::ArrowCursor : Qt::PointingHandCursor);
    pBody->setEnabled(!bBuiltIn);
    pBody->setStyleSheet("QPushButton { color: #e8d5a8; font-weight: 600; font-size: 10px; border: none; background: transparent; padding: 0px 4px; }"
                         "QPushButton:disabled { color: #e8d5a8; }");
    pBody->setToolTip(bBuiltIn ? "The querying agent - always available." : "Click to edit this context slot in Raw Properties.");
    pChipLayout->addWidget(pBody);

    if (!bBuiltIn)
    {
      const plUuid chipGuid = guid;
      connect(pBody, &QPushButton::clicked, this, [this, chipGuid]() { m_pWindow->SelectContextSlot(chipGuid); });

      QToolButton* pRemove = new QToolButton(pChip);
      pRemove->setText(QString::fromUtf8("\xE2\x9C\x95")); // ✕
      pRemove->setAutoRaise(true);
      pRemove->setToolTip("Remove this context slot");
      pRemove->setStyleSheet("color: #7d838c; border: none; background: transparent;");
      pChipLayout->addWidget(pRemove);

      connect(pRemove, &QToolButton::clicked, this, [this, chipGuid]() { m_pWindow->RemoveContextSlot(chipGuid); });
    }

    m_pLayout->insertWidget(iInsert, pChip);
    ++iInsert;
    m_Chips.PushBack(pChip);
  };

  addChip("Querier", QString::fromUtf8("\xC2\xB7 built-in"), plUuid(), true);

  // one chip per authored slot
  plDynamicArray<plUuid> guids;
  m_pWindow->GetArrayGuids("ContextSlots", guids);

  const plAiEqsQueryAssetObject* pProps = m_pWindow->GetEqsDocument()->GetProperties();

  for (plUInt32 i = 0; i < guids.GetCount(); ++i)
  {
    QString sName = "<slot>";
    QString sType = "?";

    if (i < pProps->m_ContextSlots.GetCount() && pProps->m_ContextSlots[i] != nullptr)
    {
      const plAiEqsContextSlotObject* pSlot = pProps->m_ContextSlots[i];
      sName = QString::fromUtf8(pSlot->m_sName.GetData());
      sType = QString("= %1").arg(PrettyEqsName(pSlot->m_pContext != nullptr ? pSlot->m_pContext->GetDynamicRTTI() : nullptr));
    }

    addChip(sName, sType, guids[i], false);
  }
}

//////////////////////////////////////////////////////////////////////////
// plQtAiEqsScatterWidget
//////////////////////////////////////////////////////////////////////////

plQtAiEqsScatterWidget::plQtAiEqsScatterWidget(QWidget* pParent)
  : QWidget(pParent)
{
  setMinimumHeight(235);
  setMouseTracking(false);
}

void plQtAiEqsScatterWidget::SetCandidates(const plHybridArray<plAiEqsPreviewCandidate, 64>& candidates, plInt32 iSelected)
{
  m_Candidates = candidates;
  m_iSelected = iSelected;
  update();
}

QPointF plQtAiEqsScatterWidget::ToScreen(const plVec2& vMeters) const
{
  // 15m half-extent mapped onto the widget, Y up
  const double fScale = plMath::Min(width(), height()) / 30.0;
  return QPointF(width() * 0.5 + vMeters.x * fScale, height() * 0.5 - vMeters.y * fScale);
}

plVec2 plQtAiEqsScatterWidget::ToMeters(const QPointF& screen) const
{
  const double fScale = plMath::Min(width(), height()) / 30.0;
  return plVec2(static_cast<float>((screen.x() - width() * 0.5) / fScale), static_cast<float>((height() * 0.5 - screen.y()) / fScale));
}

void plQtAiEqsScatterWidget::paintEvent(QPaintEvent* pEvent)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  // background + grid
  p.fillRect(rect(), QColor(27, 29, 33));
  {
    QPen gridPen(QColor(80, 90, 110, 30));
    gridPen.setWidthF(1.0);
    p.setPen(gridPen);

    const double fScale = plMath::Min(width(), height()) / 30.0;
    const double fStep = 2.0 * fScale; // 2m grid

    for (double x = std::fmod(width() * 0.5, fStep); x < width(); x += fStep)
      p.drawLine(QPointF(x, 0), QPointF(x, height()));
    for (double y = std::fmod(height() * 0.5, fStep); y < height(); y += fStep)
      p.drawLine(QPointF(0, y), QPointF(width(), y));
  }

  // occluder segment (the local stand-in for level geometry blocking line of sight)
  {
    const QPointF a = ToScreen(m_vOccluderA);
    const QPointF b = ToScreen(m_vOccluderB);

    QPen wallPen(QColor(150, 150, 160));
    wallPen.setWidthF(3.0);
    p.setPen(wallPen);
    p.drawLine(a, b);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(150, 150, 160));
    p.drawEllipse(a, 3.5, 3.5);
    p.drawEllipse(b, 3.5, 3.5);

    p.setPen(QColor(120, 125, 135));
    p.drawText(QRectF((a + b) / 2.0 + QPointF(6, -14), QSizeF(80, 14)), Qt::AlignLeft, "occluder");
  }

  // candidates
  for (plUInt32 i = 0; i < m_Candidates.GetCount(); ++i)
  {
    const plAiEqsPreviewCandidate& cand = m_Candidates[i];
    const QPointF pos = ToScreen(cand.m_vPosition);

    if (cand.m_bDiscarded)
    {
      QPen crossPen(QColor(86, 91, 99));
      crossPen.setWidthF(1.5);
      p.setPen(crossPen);
      p.drawLine(pos + QPointF(-3.5, -3.5), pos + QPointF(3.5, 3.5));
      p.drawLine(pos + QPointF(-3.5, 3.5), pos + QPointF(3.5, -3.5));
      continue;
    }

    const QColor color = ScoreColor(cand.m_fFinal);

    if (cand.m_bWinner)
    {
      p.setPen(QPen(QColor(88, 214, 163), 1.5));
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(pos, 8.0, 8.0);

      p.setPen(QColor(88, 214, 163));
      p.drawText(QRectF(pos + QPointF(-30, -26), QSizeF(60, 14)), Qt::AlignHCenter, QString("%1 \xE2\x98\x85").arg(cand.m_fFinal, 0, 'f', 2));
    }

    p.setPen(static_cast<plInt32>(i) == m_iSelected ? QPen(Qt::white, 1.5) : QPen(Qt::NoPen));
    p.setBrush(cand.m_bWinner ? QColor(88, 214, 163) : color);
    p.drawEllipse(pos, 4.5, 4.5);
  }

  // querier + threat markers
  {
    const QPointF q = ToScreen(m_vQuerier);
    p.setPen(QPen(QColor(79, 163, 255), 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(q, 7.0, 7.0);
    p.setBrush(QColor(79, 163, 255));
    p.drawEllipse(q, 3.0, 3.0);
    p.setPen(QColor(140, 180, 230));
    p.drawText(QRectF(q + QPointF(-30, 9), QSizeF(60, 14)), Qt::AlignHCenter, "Querier");

    const QPointF t = ToScreen(m_vThreat);
    QPainterPath tri;
    tri.moveTo(t + QPointF(0, -7));
    tri.lineTo(t + QPointF(6.5, 5));
    tri.lineTo(t + QPointF(-6.5, 5));
    tri.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(232, 112, 112));
    p.drawPath(tri);
    p.setPen(QColor(230, 150, 150));
    p.drawText(QRectF(t + QPointF(-30, 9), QSizeF(60, 14)), Qt::AlignHCenter, "Threat");
  }
}

void plQtAiEqsScatterWidget::mousePressEvent(QMouseEvent* pEvent)
{
  const QPointF pos = pEvent->pos();

  struct Target
  {
    QPointF m_Pos;
    int m_iId;
  };

  const Target targets[] = {
    {ToScreen(m_vQuerier), 0},
    {ToScreen(m_vThreat), 1},
    {ToScreen(m_vOccluderA), 2},
    {ToScreen(m_vOccluderB), 3},
  };

  if (m_bAllowDrag)
  {
    for (const Target& target : targets)
    {
      if (QLineF(pos, target.m_Pos).length() < 10.0)
      {
        m_iDragTarget = target.m_iId;
        return;
      }
    }
  }

  // otherwise: candidate selection
  for (plUInt32 i = 0; i < m_Candidates.GetCount(); ++i)
  {
    if (m_Candidates[i].m_bDiscarded)
      continue;

    if (QLineF(pos, ToScreen(m_Candidates[i].m_vPosition)).length() < 7.0)
    {
      Q_EMIT candidateClicked(static_cast<int>(i));
      return;
    }
  }
}

void plQtAiEqsScatterWidget::mouseMoveEvent(QMouseEvent* pEvent)
{
  if (m_iDragTarget < 0)
    return;

  const plVec2 vMeters = ToMeters(pEvent->pos());

  switch (m_iDragTarget)
  {
    case 0:
      m_vQuerier = vMeters;
      break;
    case 1:
      m_vThreat = vMeters;
      break;
    case 2:
      m_vOccluderA = vMeters;
      break;
    case 3:
      m_vOccluderB = vMeters;
      break;
  }

  Q_EMIT situationChanged();
}

void plQtAiEqsScatterWidget::mouseReleaseEvent(QMouseEvent* pEvent)
{
  m_iDragTarget = -1;
}

//////////////////////////////////////////////////////////////////////////
// plQtAiEqsPreviewPanel
//////////////////////////////////////////////////////////////////////////

plQtAiEqsPreviewPanel::plQtAiEqsPreviewPanel(QWidget* pParent, plAiEqsQueryAssetDocument* pDocument)
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
  };

  addSectionLabel("SIMULATED SITUATION");

  {
    QHBoxLayout* pRow = new QHBoxLayout();
    pRow->setSpacing(8);

    QLabel* pLabel = new QLabel("Threat distance", this);
    pLabel->setStyleSheet("color: #a2a7ab; font-size: 11px;");

    m_pThreatDistance = new QSlider(Qt::Horizontal, this);
    m_pThreatDistance->setRange(10, 250); // 1.0 .. 25.0 m
    m_pThreatDistance->setValue(92);

    m_pThreatDistanceValue = new QLabel("9.2 m", this);
    m_pThreatDistanceValue->setStyleSheet(QString("color: #e2e4e6; font-size: 10px; %1").arg(MonoStyle()));
    m_pThreatDistanceValue->setMinimumWidth(44);
    m_pThreatDistanceValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    pRow->addWidget(pLabel);
    pRow->addWidget(m_pThreatDistance, 1);
    pRow->addWidget(m_pThreatDistanceValue);
    pMain->addLayout(pRow);

    connect(m_pThreatDistance, &QSlider::valueChanged, this, &plQtAiEqsPreviewPanel::onThreatDistanceChanged);
  }

  {
    QHBoxLayout* pToggles = new QHBoxLayout();
    pToggles->setSpacing(12);

    m_pAllowDrag = new QCheckBox("drag querier / threat", this);
    m_pAllowDrag->setChecked(true);
    m_pAllowDrag->setToolTip("Drag the markers and the occluder endpoints directly in the canvas.");

    m_pSceneSim = new QCheckBox("use scene simulation", this);
    m_pSceneSim->setEnabled(false);
    m_pSceneSim->setToolTip("Streams results back from a simulating scene (enable AI.EQS.VisualizeQueries and use a plAiEqsQueryTestComponent for now).");

    pToggles->addWidget(m_pAllowDrag);
    pToggles->addWidget(m_pSceneSim);
    pToggles->addStretch();
    pMain->addLayout(pToggles);

    connect(m_pAllowDrag, &QCheckBox::toggled, this, [this](bool bChecked) { m_pScatter->m_bAllowDrag = bChecked; });
  }

  addSectionLabel("CANDIDATES - TOP-DOWN");

  m_pScatter = new plQtAiEqsScatterWidget(this);
  pMain->addWidget(m_pScatter, 1);

  connect(m_pScatter, &plQtAiEqsScatterWidget::situationChanged, this, &plQtAiEqsPreviewPanel::onSituationChanged);
  connect(m_pScatter, &plQtAiEqsScatterWidget::candidateClicked, this, &plQtAiEqsPreviewPanel::onCandidateClicked);

  {
    QLabel* pLegend = new QLabel(QString::fromUtf8("\xE2\x97\x8F winner   \xE2\x97\x8F\xE2\x86\x92\xE2\x97\x8F score high\xE2\x86\x92low   \xE2\x9C\x95 filtered out"), this);
    pLegend->setStyleSheet("color: #8f959b; font-size: 9px;");
    pMain->addWidget(pLegend);
  }

  addSectionLabel("SELECTED CANDIDATE - SCORE BREAKDOWN");

  m_pRowsHost = new QWidget(this);
  m_pRowsLayout = new QVBoxLayout(m_pRowsHost);
  m_pRowsLayout->setContentsMargins(0, 0, 0, 0);
  m_pRowsLayout->setSpacing(3);
  pMain->addWidget(m_pRowsHost);

  m_pFinalValue = new QLabel(this);
  m_pFinalValue->setStyleSheet(QString("color: white; font-size: 13px; font-weight: bold; %1").arg(MonoStyle()));
  pMain->addWidget(m_pFinalValue);

  m_pStats = new QLabel(this);
  m_pStats->setStyleSheet(QString("color: #7d838c; font-size: 9px; %1").arg(MonoStyle()));
  m_pStats->setWordWrap(true);
  pMain->addWidget(m_pStats);
}

void plQtAiEqsPreviewPanel::onThreatDistanceChanged(int iValue)
{
  if (m_bUpdating)
    return;

  // move the threat along its current direction from the querier
  plVec2 vDir = m_pScatter->m_vThreat - m_pScatter->m_vQuerier;

  if (vDir.NormalizeIfNotZero(plVec2(1, 0)).Failed())
  {
    vDir = plVec2(1, 0);
  }

  m_pScatter->m_vThreat = m_pScatter->m_vQuerier + vDir * (iValue / 10.0f);
  Recompute();
}

void plQtAiEqsPreviewPanel::onSituationChanged()
{
  Recompute();
}

void plQtAiEqsPreviewPanel::onCandidateClicked(int iIndex)
{
  m_iSelected = iIndex;
  m_pScatter->SetCandidates(m_Candidates, m_iSelected);
  RefreshBreakdown();
}

void plQtAiEqsPreviewPanel::RebuildRows()
{
  m_Rows.Clear();

  while (QLayoutItem* pItem = m_pRowsLayout->takeAt(0))
  {
    if (pItem->widget())
      pItem->widget()->deleteLater();
    delete pItem;
  }

  const plAiEqsQueryAssetObject* pProps = m_pDocument->GetProperties();

  for (const plAiEqsTestObject* pTestObj : pProps->m_Tests)
  {
    if (pTestObj == nullptr || pTestObj->m_pTest == nullptr)
      continue;

    QWidget* pRowHost = new QWidget(m_pRowsHost);
    QHBoxLayout* pRow = new QHBoxLayout(pRowHost);
    pRow->setContentsMargins(0, 0, 0, 0);
    pRow->setSpacing(8);

    BreakdownRow& row = m_Rows.ExpandAndGetRef();

    row.m_pName = new QLabel(PrettyEqsName(pTestObj->m_pTest->GetDynamicRTTI()), pRowHost);
    row.m_pName->setStyleSheet(QString("color: #a2a7ab; font-size: 9.5px; %1").arg(MonoStyle()));
    row.m_pName->setMinimumWidth(110);

    row.m_pBarHost = new QWidget(pRowHost);
    row.m_pBarHost->setFixedHeight(8);
    row.m_pBarHost->setStyleSheet("background-color: #1c1e22; border-radius: 4px;");

    row.m_pBarFill = new QWidget(row.m_pBarHost);
    row.m_pBarFill->setStyleSheet("background-color: #4fa3ff; border-radius: 4px;");
    row.m_pBarFill->setGeometry(0, 0, 0, 8);

    row.m_pValue = new QLabel(pRowHost);
    row.m_pValue->setStyleSheet(QString("color: #c8ccd2; font-size: 9.5px; %1").arg(MonoStyle()));
    row.m_pValue->setMinimumWidth(58);
    row.m_pValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    pRow->addWidget(row.m_pName);
    pRow->addWidget(row.m_pBarHost, 1);
    pRow->addWidget(row.m_pValue);

    m_pRowsLayout->addWidget(pRowHost);
  }

  if (m_Rows.IsEmpty())
  {
    QLabel* pEmpty = new QLabel("add tests to see the per-candidate breakdown here", m_pRowsHost);
    pEmpty->setStyleSheet("color: #6d737a; font-size: 10px; font-style: italic;");
    m_pRowsLayout->addWidget(pEmpty);
  }

  Recompute();
}

void plQtAiEqsPreviewPanel::Recompute()
{
  const plAiEqsQueryAssetObject* pProps = m_pDocument->GetProperties();

  m_bUpdating = true;

  // keep the distance slider in sync with the dragged threat
  {
    const float fDist = (m_pScatter->m_vThreat - m_pScatter->m_vQuerier).GetLength();
    m_pThreatDistance->setValue(plMath::Clamp(static_cast<int>(fDist * 10.0f + 0.5f), 10, 250));
    m_pThreatDistanceValue->setText(QString("%1 m").arg(fDist, 0, 'f', 1));
  }

  m_bUpdating = false;

  m_Candidates.Clear();

  const plVec2 vQuerier = m_pScatter->m_vQuerier;
  const plVec2 vThreat = m_pScatter->m_vThreat;

  auto resolveContext = [&](const char* szName) -> plVec2 {
    if (plStringUtils::IsEqual_NoCase(szName, "Querier"))
      return vQuerier;

    // any authored slot resolves to the threat marker in the local simulation
    return vThreat;
  };

  // ---- synthetic generation (seeded - stable across recomputes) ----

  const plAiEqsGenerator* pGen = pProps->m_pGenerator;
  const plUInt32 uiMaxCandidates = plMath::Clamp<plUInt32>(pProps->m_uiCandidates, 4, 64);

  plRandom rng;
  rng.Initialize(0xEC5D47u);

  if (pGen != nullptr)
  {
    const plVec2 vCenter = resolveContext(pGen->GetCenterContext());
    const float fRadiusMin = pGen->m_fRadiusMin;
    const float fRadiusMax = plMath::Max(pGen->m_fRadiusMax, fRadiusMin + 0.1f);

    const bool bCover = plDynamicCast<const plAiEqsGenerator_CoverPoints*>(pGen) != nullptr;
    const bool bSmartObjects = plDynamicCast<const plAiEqsGenerator_SmartObjects*>(pGen) != nullptr;
    const bool bGrid = plDynamicCast<const plAiEqsGenerator_Grid*>(pGen) != nullptr;
    const bool bRandom = plDynamicCast<const plAiEqsGenerator_NavmeshRandom*>(pGen) != nullptr;

    if (bGrid)
    {
      const plAiEqsGenerator_Grid* pGrid = static_cast<const plAiEqsGenerator_Grid*>(pGen);
      float fSpacing = plMath::Max(0.25f, pGrid->m_fSpacing);

      const plUInt32 uiPerAxis = static_cast<plUInt32>(plMath::Floor(plMath::Sqrt(static_cast<float>(uiMaxCandidates))));
      fSpacing = plMath::Max(fSpacing, (2.0f * fRadiusMax) / plMath::Max(1u, uiPerAxis - 1));

      for (float y = -fRadiusMax; y <= fRadiusMax + 0.01f && m_Candidates.GetCount() < uiMaxCandidates; y += fSpacing)
      {
        for (float x = -fRadiusMax; x <= fRadiusMax + 0.01f && m_Candidates.GetCount() < uiMaxCandidates; x += fSpacing)
        {
          const float fDistSqr = x * x + y * y;

          if (fDistSqr < plMath::Square(fRadiusMin) || fDistSqr > plMath::Square(fRadiusMax))
            continue;

          auto& cand = m_Candidates.ExpandAndGetRef();
          cand.m_vPosition = vCenter + plVec2(x, y);
        }
      }
    }
    else
    {
      const plUInt32 uiCount = bSmartObjects ? plMath::Min<plUInt32>(6, uiMaxCandidates) : uiMaxCandidates;
      const float fGoldenAngle = 2.39996323f;

      for (plUInt32 i = 0; i < uiCount; ++i)
      {
        float fAngle, fRadiusT;

        if (bRandom || bSmartObjects)
        {
          fAngle = static_cast<float>(rng.DoubleZeroToOneExclusive()) * 2.0f * plMath::Pi<float>();
          fRadiusT = plMath::Sqrt(static_cast<float>(rng.DoubleZeroToOneExclusive()));
        }
        else
        {
          fAngle = i * fGoldenAngle;
          fRadiusT = plMath::Sqrt((i + 0.5f) / uiCount);
        }

        const float fRadius = plMath::Lerp(fRadiusMin, fRadiusMax, fRadiusT);

        auto& cand = m_Candidates.ExpandAndGetRef();
        cand.m_vPosition = vCenter + plVec2(plMath::Cos(plAngle::MakeFromRadian(fAngle)), plMath::Sin(plAngle::MakeFromRadian(fAngle))) * fRadius;

        if (bCover)
        {
          cand.m_uiPayload = 1; // CoverPoint

          // synthetic wall: points away from the generator center, jittered
          plVec2 vWall = cand.m_vPosition - vCenter;
          vWall.NormalizeIfNotZero(plVec2(1, 0)).IgnoreResult();
          const plAngle jitter = plAngle::MakeFromRadian((static_cast<float>(rng.DoubleZeroToOneExclusive()) - 0.5f) * 2.5f);
          const float fSin = plMath::Sin(jitter);
          const float fCos = plMath::Cos(jitter);
          cand.m_vWallDir = plVec2(vWall.x * fCos - vWall.y * fSin, vWall.x * fSin + vWall.y * fCos);
          cand.m_uiCoverQuality = (rng.DoubleZeroToOneExclusive() < 0.4) ? 2 : 1; // High : Low
        }
        else if (bSmartObjects)
        {
          cand.m_uiPayload = 2; // SmartObjectSlot
        }
      }
    }
  }

  // ---- tests: cheap first (mirrors the runtime pipeline), real scoring math ----

  struct OrderedTest
  {
    PL_DECLARE_POD_TYPE();

    const plAiEqsTestObject* m_pObj;
    plUInt32 m_uiRowIndex;
  };

  plHybridArray<OrderedTest, 12> order;

  {
    plUInt32 uiRow = 0;

    plHybridArray<OrderedTest, 12> all;

    for (const plAiEqsTestObject* pTestObj : pProps->m_Tests)
    {
      if (pTestObj == nullptr || pTestObj->m_pTest == nullptr)
        continue;

      all.PushBack({pTestObj, uiRow});
      ++uiRow;
    }

    for (const auto& t : all)
    {
      if (t.m_pObj->m_pTest->GetCost() == plAiEqsTest::Cost::Cheap)
        order.PushBack(t);
    }

    for (const auto& t : all)
    {
      if (t.m_pObj->m_pTest->GetCost() == plAiEqsTest::Cost::Expensive)
        order.PushBack(t);
    }
  }

  plUInt32 uiGenerated = m_Candidates.GetCount();

  for (auto& cand : m_Candidates)
  {
    cand.m_TestScores.SetCount(static_cast<plUInt32>(pProps->m_Tests.GetCount()), 0.0f);
  }

  plHybridArray<float, 64> scoreSums;
  plHybridArray<float, 64> weightSums;
  scoreSums.SetCount(m_Candidates.GetCount(), 0.0f);
  weightSums.SetCount(m_Candidates.GetCount(), 0.0f);

  for (const OrderedTest& ordered : order)
  {
    const plAiEqsTest* pTest = ordered.m_pObj->m_pTest;

    plCurve1D curve;
    plAiAssetUi::BuildRuntimeCurve(ordered.m_pObj->m_ScoreCurve, curve);

    for (plUInt32 c = 0; c < m_Candidates.GetCount(); ++c)
    {
      plAiEqsPreviewCandidate& cand = m_Candidates[c];

      if (cand.m_bDiscarded)
        continue;

      float fRaw = 1.0f;

      if (auto pDist = plDynamicCast<const plAiEqsTest_Distance*>(pTest))
      {
        fRaw = PreviewTrapezoid((cand.m_vPosition - resolveContext(pDist->GetContext())).GetLength(), pDist->m_fBandMin, pDist->m_fBandMax);
      }
      else if (auto pDir = plDynamicCast<const plAiEqsTest_Direction*>(pTest))
      {
        const plVec2 vFrom = resolveContext(pDir->GetContext());
        plVec2 vBase = vQuerier - vFrom;
        plVec2 vCand = cand.m_vPosition - vFrom;

        if (vBase.NormalizeIfNotZero(plVec2(1, 0)).Succeeded() && vCand.NormalizeIfNotZero(plVec2(1, 0)).Succeeded())
        {
          const float fAngle = plMath::ACos(plMath::Clamp(vBase.Dot(vCand), -1.0f, 1.0f)).GetRadian();
          fRaw = plMath::Max(0.0f, 1.0f - plMath::Abs(fAngle - pDir->m_DesiredAngle.GetRadian()) / plMath::Pi<float>());
        }
      }
      else if (auto pQuality = plDynamicCast<const plAiEqsTest_CoverQuality*>(pTest))
      {
        if (cand.m_uiPayload == 1)
        {
          if (cand.m_uiCoverQuality < pQuality->m_MinQuality.GetValue())
            fRaw = 0.0f;
          else
            fRaw = (cand.m_uiCoverQuality >= 2) ? 1.0f : 0.5f;
        }
      }
      else if (auto pFacing = plDynamicCast<const plAiEqsTest_CoverFacing*>(pTest))
      {
        if (cand.m_uiPayload == 1)
        {
          plVec2 vToThreat = resolveContext(pFacing->GetContext()) - cand.m_vPosition;

          if (vToThreat.NormalizeIfNotZero(plVec2(1, 0)).Succeeded())
          {
            fRaw = (cand.m_vWallDir.Dot(vToThreat) >= pFacing->m_fMinDot) ? 1.0f : 0.0f;
          }
        }
      }
      else if (plDynamicCast<const plAiEqsTest_Unclaimed*>(pTest) != nullptr)
      {
        fRaw = 1.0f; // everything is unclaimed in the local simulation
      }
      else if (auto pLos = plDynamicCast<const plAiEqsTest_LineOfSight*>(pTest))
      {
        const plVec2 vEye = resolveContext(pLos->GetContext());
        const bool bBlocked = SegmentsIntersect(vEye, cand.m_vPosition, m_pScatter->m_vOccluderA, m_pScatter->m_vOccluderB);
        fRaw = (bBlocked != pLos->m_bPreferVisible) ? 1.0f : 0.0f;
      }
      else if (plDynamicCast<const plAiEqsTest_ReachableApprox*>(pTest) != nullptr)
      {
        fRaw = 1.0f; // no navmesh locally
      }
      else if (auto pPath = plDynamicCast<const plAiEqsTest_PathLength*>(pTest))
      {
        // approximate: euclidean distance with a detour factor
        fRaw = plMath::Max(0.001f, PreviewTrapezoid((cand.m_vPosition - vQuerier).GetLength() * 1.15f, pPath->m_fBandMin, pPath->m_fBandMax));
      }

      const float fCurved = plAiAssetUi::EvaluateConsideration(curve, fRaw, 0.0f, 1.0f);

      cand.m_TestScores[ordered.m_uiRowIndex] = fCurved;

      const auto purpose = static_cast<plAiEqsTestPurpose::Enum>(pTest->m_Purpose.GetValue());

      if (plAiEqsTestPurpose::Filters(purpose) && fRaw <= 0.0f)
      {
        cand.m_bDiscarded = true;
        continue;
      }

      if (plAiEqsTestPurpose::Scores(purpose))
      {
        scoreSums[c] += pTest->m_fWeight * fCurved;
        weightSums[c] += pTest->m_fWeight;
      }
    }
  }

  // ---- finalize ----

  plUInt32 uiSurvivors = 0;
  plInt32 iWinner = -1;
  float fBest = -1.0f;

  for (plUInt32 c = 0; c < m_Candidates.GetCount(); ++c)
  {
    plAiEqsPreviewCandidate& cand = m_Candidates[c];

    if (cand.m_bDiscarded)
      continue;

    ++uiSurvivors;
    cand.m_fFinal = (weightSums[c] > 0.0f) ? (scoreSums[c] / weightSums[c]) : 1.0f;

    if (cand.m_fFinal > fBest)
    {
      fBest = cand.m_fFinal;
      iWinner = static_cast<plInt32>(c);
    }
  }

  if (iWinner >= 0)
  {
    m_Candidates[iWinner].m_bWinner = true;
  }

  if (m_iSelected < 0 || m_iSelected >= static_cast<plInt32>(m_Candidates.GetCount()) || m_Candidates[m_iSelected].m_bDiscarded)
  {
    m_iSelected = iWinner;
  }

  m_pScatter->SetCandidates(m_Candidates, m_iSelected);

  const plUInt32 uiResults = plMath::Min<plUInt32>(uiSurvivors, pProps->m_uiMaxResults);
  m_pStats->setText(QString("generated %1 \xC2\xB7 after filters %2 \xC2\xB7 results %3\nlocal simulation \xC2\xB7 LOS via draggable occluder \xC2\xB7 navmesh tests approximated")
                      .arg(uiGenerated)
                      .arg(uiSurvivors)
                      .arg(uiResults));

  RefreshBreakdown();
}

void plQtAiEqsPreviewPanel::RefreshBreakdown()
{
  const plAiEqsQueryAssetObject* pProps = m_pDocument->GetProperties();

  const plAiEqsPreviewCandidate* pSelected = (m_iSelected >= 0 && m_iSelected < static_cast<plInt32>(m_Candidates.GetCount())) ? &m_Candidates[m_iSelected] : nullptr;

  plUInt32 uiRow = 0;

  for (const plAiEqsTestObject* pTestObj : pProps->m_Tests)
  {
    if (pTestObj == nullptr || pTestObj->m_pTest == nullptr)
      continue;

    if (uiRow >= m_Rows.GetCount())
      break;

    BreakdownRow& row = m_Rows[uiRow];
    const plAiEqsTest* pTest = pTestObj->m_pTest;
    const bool bScores = pTest->m_Purpose != plAiEqsTestPurpose::FilterOnly;

    float fScore = 0.0f;

    if (pSelected != nullptr && uiRow < pSelected->m_TestScores.GetCount())
    {
      fScore = pSelected->m_TestScores[uiRow];
    }

    const int iWidth = static_cast<int>(row.m_pBarHost->width() * plMath::Saturate(fScore));
    row.m_pBarFill->setGeometry(0, 0, iWidth, 8);
    row.m_pBarFill->setStyleSheet(bScores ? "background-color: #4fa3ff; border-radius: 4px;" : "background-color: #375a41; border-radius: 4px;");

    if (pSelected == nullptr)
    {
      row.m_pValue->setText("-");
    }
    else if (bScores)
    {
      row.m_pValue->setText(QString::fromUtf8("%1 \xC3\x97%2").arg(fScore, 0, 'f', 2).arg(pTest->m_fWeight, 0, 'f', 1));
    }
    else
    {
      row.m_pValue->setText(fScore > 0.0f ? "pass" : "fail");
    }

    ++uiRow;
  }

  if (pSelected != nullptr)
  {
    m_pFinalValue->setText(QString("final  %1%2").arg(pSelected->m_fFinal, 0, 'f', 2).arg(pSelected->m_bWinner ? QString::fromUtf8("  \xE2\x98\x85 winner") : QString()));
  }
  else
  {
    m_pFinalValue->setText("no surviving candidate");
  }
}

//////////////////////////////////////////////////////////////////////////
// plQtAiEqsQueryAssetDocumentWindow
//////////////////////////////////////////////////////////////////////////

plQtAiEqsQueryAssetDocumentWindow::plQtAiEqsQueryAssetDocumentWindow(plDocument* pDocument)
  : plQtAiAssetDocumentWindow(pDocument, "AiEqsQueryAsset", "AiEqsQuery")
{
  m_pEqsDocument = static_cast<plAiEqsQueryAssetDocument*>(pDocument);

  // central: header band + context strip + generator card + test cards
  {
    plQtDocumentPanel* pCentralPanel = new plQtDocumentPanel(this, pDocument);
    pCentralPanel->setObjectName("AiEqsQueryAssetDockWidget");
    pCentralPanel->setWindowTitle("EQS Query");
    pCentralPanel->show();

    QWidget* pMain = new QWidget(pCentralPanel);
    QVBoxLayout* pMainLayout = new QVBoxLayout(pMain);
    pMainLayout->setContentsMargins(0, 0, 0, 0);
    pMainLayout->setSpacing(0);

    pMainLayout->addWidget(CreateHeaderBand(pMain));

    m_pContextStrip = new plQtAiEqsContextStrip(pMain, this);
    pMainLayout->addWidget(m_pContextStrip);

    // generator section
    {
      QWidget* pGenHead = new QWidget(pMain);
      QHBoxLayout* pHeadLayout = new QHBoxLayout(pGenHead);
      pHeadLayout->setContentsMargins(12, 10, 12, 2);

      QLabel* pTitle = new QLabel("Generator", pGenHead);
      {
        QFont f = pTitle->font();
        f.setBold(true);
        pTitle->setFont(f);
      }

      QLabel* pMeta = new QLabel("produces the candidate set", pGenHead);
      pMeta->setStyleSheet("color: #8f959b; font-size: 10px;");

      pHeadLayout->addWidget(pTitle);
      pHeadLayout->addWidget(pMeta);
      pHeadLayout->addStretch();

      pMainLayout->addWidget(pGenHead);

      QWidget* pGenHost = new QWidget(pMain);
      QVBoxLayout* pGenLayout = new QVBoxLayout(pGenHost);
      pGenLayout->setContentsMargins(12, 2, 12, 2);

      m_pGeneratorCard = new plQtAiEqsGeneratorCard(pGenHost, this);
      pGenLayout->addWidget(m_pGeneratorCard);

      pMainLayout->addWidget(pGenHost);
    }

    // tests header row
    {
      QWidget* pTestsHead = new QWidget(pMain);
      QHBoxLayout* pHeadLayout = new QHBoxLayout(pTestsHead);
      pHeadLayout->setContentsMargins(12, 8, 12, 4);

      QLabel* pTitle = new QLabel("Tests", pTestsHead);
      {
        QFont f = pTitle->font();
        f.setBold(true);
        pTitle->setFont(f);
      }

      QLabel* pMeta = new QLabel("run in order - cheap before expensive, hard filters discard", pTestsHead);
      pMeta->setStyleSheet("color: #8f959b; font-size: 10px;");

      m_pAddButton = new QPushButton("+ Add Test", pTestsHead);
      connect(m_pAddButton, &QPushButton::clicked, this, &plQtAiEqsQueryAssetDocumentWindow::onAddTest);

      pHeadLayout->addWidget(pTitle);
      pHeadLayout->addWidget(pMeta);
      pHeadLayout->addStretch();
      pHeadLayout->addWidget(m_pAddButton);

      pMainLayout->addWidget(pTestsHead);
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

    // card styling: neutral panels, AI-category accent on selection, generator card with a green rail
    {
      const QColor bg = palette().color(QPalette::Window).lighter(115);
      const QColor border = palette().color(QPalette::Window).darker(160);
      pMain->setStyleSheet(QString("QFrame#AiEqsCard { background-color: %1; border: 1px solid %2; border-radius: 4px; }"
                                   "QFrame#AiEqsCard[cardSelected=\"true\"] { border: 1px solid %3; }"
                                   "QFrame#AiEqsGeneratorCard { background-color: %1; border: 1px solid %2; border-left: 3px solid #58d6a3; border-radius: 4px; }")
                             .arg(bg.name())
                             .arg(border.name())
                             .arg(plAiAssetUi::AccentColor().name()));
    }

    pCentralPanel->setWidget(pMain, ads::CDockWidget::ForceNoScrollArea);
    m_pDockManager->addDockWidgetTab(ads::CenterDockWidgetArea, pCentralPanel);
  }

  // right: query preview + raw properties
  {
    plQtDocumentPanel* pPreviewPanel = new plQtDocumentPanel(this, pDocument);
    pPreviewPanel->setObjectName("AiEqsQueryPreviewDockWidget");
    pPreviewPanel->setWindowTitle("Query Preview");
    pPreviewPanel->show();

    m_pPreviewPanel = new plQtAiEqsPreviewPanel(pPreviewPanel, m_pEqsDocument);
    pPreviewPanel->setWidget(m_pPreviewPanel);

    m_pDockManager->addDockWidgetTab(ads::RightDockWidgetArea, pPreviewPanel);

    plQtDocumentPanel* pRawPanel = new plQtDocumentPanel(this, pDocument);
    pRawPanel->setObjectName("AiEqsQueryRawPropertyDockWidget");
    pRawPanel->setWindowTitle("Raw Properties");
    pRawPanel->show();
    pRawPanel->setWidget(CreateRawPropertiesWidget(pRawPanel), ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidgetTab(ads::RightDockWidgetArea, pRawPanel);
  }

  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);

  FinishWindowCreation();

  RefreshHeader();
  m_pContextStrip->Rebuild();
  RebuildCards();
  m_pPreviewPanel->RebuildRows();
}

plQtAiEqsQueryAssetDocumentWindow::~plQtAiEqsQueryAssetDocumentWindow() = default;

QWidget* plQtAiEqsQueryAssetDocumentWindow::CreateHeaderBand(QWidget* pParent)
{
  QWidget* pBand = new QWidget(pParent);
  pBand->setObjectName("AiEqsHeaderBand");

  QHBoxLayout* pLayout = new QHBoxLayout(pBand);
  pLayout->setContentsMargins(12, 8, 12, 8);
  pLayout->setSpacing(14);

  auto addField = [&](const char* szLabel, QWidget* pWidget) {
    QLabel* pLabel = new QLabel(szLabel, pBand);
    pLabel->setStyleSheet("color: #8f959b; font-size: 10px;");
    pLayout->addWidget(pLabel);
    pLayout->addWidget(pWidget);
  };

  // AI category chip (crosshatch, like the other AI asset editors)
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
  m_pName->setMinimumWidth(140);
  addField("Name", m_pName);
  connect(m_pName, &QLineEdit::editingFinished, this, [this]() {
    SetRootValue("Name", plVariant(m_pName->text().toUtf8().data()), "Change Query Name");
  });

  m_pRunMode = new QComboBox(pBand);
  m_pRunMode->addItem("Single Best", plAiEqsRunMode::SingleBest);
  m_pRunMode->addItem("Random of Top %", plAiEqsRunMode::RandomOfTopPercent);
  m_pRunMode->addItem("All Matching", plAiEqsRunMode::AllMatching);
  m_pRunMode->setToolTip("How the query picks its result from the scored candidates.\n'Random of Top %' spreads agents out (percentage in Raw Properties).");
  addField("Run Mode", m_pRunMode);
  connect(m_pRunMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int iIndex) {
    if (iIndex >= 0)
      SetRootValue("RunMode", plVariant(m_pRunMode->itemData(iIndex).toInt()), "Change Run Mode");
  });

  m_pCandidates = new QSpinBox(pBand);
  m_pCandidates->setRange(4, 64);
  m_pCandidates->setToolTip("How many candidates the generator produces (hard cap 64).");
  addField("Candidates", m_pCandidates);
  connect(m_pCandidates, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int iValue) {
    SetRootValue("Candidates", plVariant(static_cast<plUInt8>(iValue)), "Change Candidates");
  });

  m_pMaxResults = new QSpinBox(pBand);
  m_pMaxResults->setRange(1, 16);
  m_pMaxResults->setToolTip("Top-N results kept for the consumer (fallbacks for claim races).");
  addField("Max Results", m_pMaxResults);
  connect(m_pMaxResults, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int iValue) {
    SetRootValue("MaxResults", plVariant(static_cast<plUInt8>(iValue)), "Change Max Results");
  });

  m_pNavmesh = new QComboBox(pBand);
  m_pNavmesh->setToolTip("Which navmesh config candidates are projected onto. <default> = the first configured navmesh.");
  addField("Navmesh", m_pNavmesh);
  connect(m_pNavmesh, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int iIndex) {
    if (iIndex >= 0)
      SetRootValue("NavmeshConfig", plVariant(m_pNavmesh->itemData(iIndex).toString().toUtf8().data()), "Change Navmesh Config");
  });

  pLayout->addStretch();

  return pBand;
}

const plDocumentObject* plQtAiEqsQueryAssetDocumentWindow::GetPropertiesObject() const
{
  return GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
}

const plDocumentObject* plQtAiEqsQueryAssetDocumentWindow::GetGeneratorObject() const
{
  return GetDocument()->GetObjectAccessor()->GetChildObjectByName(GetPropertiesObject(), "Generator", plVariant());
}

void plQtAiEqsQueryAssetDocumentWindow::GetArrayGuids(const char* szProperty, plDynamicArray<plUuid>& out_guids) const
{
  out_guids.Clear();

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();

  plDynamicArray<plVariant> values;
  pAccessor->GetValuesByName(GetPropertiesObject(), szProperty, values).IgnoreResult();

  for (const plVariant& v : values)
  {
    if (v.IsA<plUuid>())
      out_guids.PushBack(v.Get<plUuid>());
  }
}

const plDocumentObject* plQtAiEqsQueryAssetDocumentWindow::GetArrayObject(const char* szProperty, const plUuid& guid, plInt32* out_pIndex) const
{
  plDynamicArray<plUuid> guids;
  GetArrayGuids(szProperty, guids);

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

void plQtAiEqsQueryAssetDocumentWindow::SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription)
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

void plQtAiEqsQueryAssetDocumentWindow::SetGeneratorValue(const char* szProperty, const plVariant& value, const char* szDescription)
{
  if (m_bUpdatingUi)
    return;

  const plDocumentObject* pGenerator = GetGeneratorObject();

  if (pGenerator == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction(szDescription);

  if (pAccessor->SetValueByName(pGenerator, szProperty, value).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiEqsQueryAssetDocumentWindow::RefreshHeader()
{
  m_bUpdatingUi = true;

  const plAiEqsQueryAssetObject* pProps = m_pEqsDocument->GetProperties();

  if (!m_pName->hasFocus())
    m_pName->setText(QString::fromUtf8(pProps->m_sName.GetData()));

  {
    const int iIndex = m_pRunMode->findData(pProps->m_RunMode.GetValue());
    if (iIndex >= 0 && iIndex != m_pRunMode->currentIndex())
      m_pRunMode->setCurrentIndex(iIndex);
  }

  if (!m_pCandidates->hasFocus())
    m_pCandidates->setValue(pProps->m_uiCandidates);
  if (!m_pMaxResults->hasFocus())
    m_pMaxResults->setValue(pProps->m_uiMaxResults);

  // navmesh configs from the project settings (dynamic string enum, filled on project open)
  {
    m_pNavmesh->blockSignals(true);
    m_pNavmesh->clear();
    m_pNavmesh->addItem("<default>", QString());

    auto& navmeshEnum = plDynamicStringEnum::CreateDynamicEnum("AiNavmeshConfig");

    for (const plString& sValue : navmeshEnum.GetAllValidValues())
    {
      m_pNavmesh->addItem(QString::fromUtf8(sValue.GetData()), QString::fromUtf8(sValue.GetData()));
    }

    int iIndex = m_pNavmesh->findData(QString::fromUtf8(pProps->m_sNavmeshConfig.GetData()));

    if (iIndex < 0 && !pProps->m_sNavmeshConfig.IsEmpty())
    {
      m_pNavmesh->addItem(QString::fromUtf8(pProps->m_sNavmeshConfig.GetData()), QString::fromUtf8(pProps->m_sNavmeshConfig.GetData()));
      iIndex = m_pNavmesh->count() - 1;
    }

    m_pNavmesh->setCurrentIndex(plMath::Max(iIndex, 0));
    m_pNavmesh->blockSignals(false);
  }

  m_pGeneratorCard->RefreshFromNative(pProps->m_pGenerator, pProps);

  m_bUpdatingUi = false;
}

void plQtAiEqsQueryAssetDocumentWindow::RebuildCards()
{
  for (plQtAiEqsTestCard* pCard : m_Cards)
  {
    m_pCardsLayout->removeWidget(pCard);
    pCard->deleteLater();
  }
  m_Cards.Clear();

  plDynamicArray<plUuid> guids;
  GetArrayGuids("Tests", guids);

  // keep selection valid
  {
    bool bFound = false;
    for (const plUuid& guid : guids)
      bFound |= (guid == m_SelectedObject);

    if (!bFound)
      m_SelectedObject = plUuid();
  }

  for (plUInt32 i = 0; i < guids.GetCount(); ++i)
  {
    plQtAiEqsTestCard* pCard = new plQtAiEqsTestCard(m_pCardsHost, this, guids[i]);
    m_pCardsLayout->insertWidget(static_cast<int>(i), pCard);
    m_Cards.PushBack(pCard);
  }

  RefreshCards();
}

void plQtAiEqsQueryAssetDocumentWindow::RefreshCards()
{
  const plAiEqsQueryAssetObject* pProps = m_pEqsDocument->GetProperties();
  const plUInt32 uiCount = plMath::Min(m_Cards.GetCount(), pProps->m_Tests.GetCount());

  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    m_Cards[i]->RefreshFromNative(pProps->m_Tests[i], i, uiCount, m_Cards[i]->GetObjectGuid() == m_SelectedObject);
  }
}

void plQtAiEqsQueryAssetDocumentWindow::OnAssetPropertiesChanged()
{
  RefreshHeader();
  m_pContextStrip->Rebuild();
  RefreshCards();
  m_pPreviewPanel->Recompute();
}

void plQtAiEqsQueryAssetDocumentWindow::OnAssetStructureChanged()
{
  RefreshHeader();
  m_pContextStrip->Rebuild();
  RebuildCards();
  m_pPreviewPanel->RebuildRows();
}

void plQtAiEqsQueryAssetDocumentWindow::SelectObject(const plUuid& guid)
{
  m_SelectedObject = guid;

  if (const plDocumentObject* pObject = GetDocument()->GetObjectAccessor()->GetObject(guid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObject);
  }

  RefreshCards();
}

void plQtAiEqsQueryAssetDocumentWindow::SelectGenerator()
{
  if (const plDocumentObject* pGenerator = GetGeneratorObject())
  {
    GetDocument()->GetSelectionManager()->SetSelection(pGenerator);
  }
}

void plQtAiEqsQueryAssetDocumentWindow::SelectContextSlot(const plUuid& guid)
{
  if (const plDocumentObject* pObject = GetDocument()->GetObjectAccessor()->GetObject(guid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObject);
  }
}

void plQtAiEqsQueryAssetDocumentWindow::GetContextSlotNames(plDynamicArray<plString>& out_names) const
{
  out_names.Clear();

  const plAiEqsQueryAssetObject* pProps = m_pEqsDocument->GetProperties();

  for (const plAiEqsContextSlotObject* pSlot : pProps->m_ContextSlots)
  {
    if (pSlot != nullptr && !pSlot->m_sName.IsEmpty())
    {
      out_names.PushBack(pSlot->m_sName);
    }
  }
}

void plQtAiEqsQueryAssetDocumentWindow::RemoveTest(const plUuid& guid)
{
  const plDocumentObject* pObject = GetArrayObject("Tests", guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Remove Test");

  if (pAccessor->RemoveObject(pObject).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiEqsQueryAssetDocumentWindow::MoveTest(const plUuid& guid, plInt32 iDirection)
{
  plInt32 iIndex = -1;
  const plDocumentObject* pObject = GetArrayObject("Tests", guid, &iIndex);
  if (pObject == nullptr || iIndex < 0)
    return;

  plDynamicArray<plUuid> guids;
  GetArrayGuids("Tests", guids);

  if (iDirection < 0 && iIndex == 0)
    return;
  if (iDirection > 0 && iIndex + 1 >= static_cast<plInt32>(guids.GetCount()))
    return;

  // MoveObject uses insert-before semantics: moving down needs +2
  const plInt32 iNewIndex = (iDirection < 0) ? (iIndex - 1) : (iIndex + 2);

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Reorder Tests");

  if (pAccessor->MoveObjectByName(pObject, GetPropertiesObject(), "Tests", plVariant(iNewIndex)).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiEqsQueryAssetDocumentWindow::SetTestWeight(const plUuid& guid, double fValue)
{
  if (m_bUpdatingUi)
    return;

  const plDocumentObject* pObject = GetArrayObject("Tests", guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  const plDocumentObject* pTest = pAccessor->GetChildObjectByName(pObject, "Test", plVariant());
  if (pTest == nullptr)
    return;

  pAccessor->StartTransaction("Change Test Weight");

  if (pAccessor->SetValueByName(pTest, "Weight", plVariant(static_cast<float>(fValue))).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plQtAiEqsQueryAssetDocumentWindow::OpenCurveEditor(const plUuid& guid)
{
  const plDocumentObject* pObject = GetArrayObject("Tests", guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  const plDocumentObject* pCurve = pAccessor->GetChildObjectByName(pObject, "ScoreCurve", plVariant());
  if (pCurve == nullptr)
    return;

  plQtCurveEditDlg dlg(pAccessor, pCurve, this);
  dlg.restoreGeometry(plQtCurveEditDlg::GetLastDialogGeometry());
  dlg.SetCurveColor(plColorScheme::LightUI(plColorScheme::Blue));
  dlg.SetCurveExtents(0.0, true, 1.0, true);
  dlg.SetCurveRanges(0.0, 1.0);
  dlg.exec();
}

void plQtAiEqsQueryAssetDocumentWindow::ApplyCurvePreset(const plUuid& guid, plUInt32 uiPreset)
{
  if (uiPreset >= PL_ARRAY_SIZE(s_EqsCurvePresets))
    return;

  const CurvePreset& preset = s_EqsCurvePresets[uiPreset];

  const plDocumentObject* pObject = GetArrayObject("Tests", guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  const plDocumentObject* pCurve = pAccessor->GetChildObjectByName(pObject, "ScoreCurve", plVariant());
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

  // insert the preset points (ticks: 4800 per second, extents locked to 0..1)
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

void plQtAiEqsQueryAssetDocumentWindow::onAddTest()
{
  QMenu menu(this);
  const plRTTI* pChosen = nullptr;

  plQtSearchableMenu* pSearch = new plQtSearchableMenu(&menu);

  plRTTI::ForEachDerivedType(
    plAiEqsTest::GetStaticRTTI(),
    [&](const plRTTI* pRtti) {
      if (pRtti == plAiEqsTest::GetStaticRTTI())
        return;

      plStringBuilder sPath;
      sPath.SetFormat("{}/{}", TestGroup(pRtti), PrettyEqsName(pRtti).toUtf8().data());

      pSearch->AddItem(PrettyEqsName(pRtti).toUtf8().data(), sPath, QVariant::fromValue<void*>(const_cast<plRTTI*>(pRtti)));
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
  pAccessor->StartTransaction("Add Test");

  bool bOk = true;

  plUuid testGuid = plUuid::MakeUuid();
  bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "Tests", pAccessor->GetCountByName(GetPropertiesObject(), "Tests"), plAiEqsTestObject::GetStaticRTTI(), testGuid).Succeeded();

  if (bOk)
  {
    if (const plDocumentObject* pTestObj = pAccessor->GetObject(testGuid))
    {
      plUuid innerGuid = plUuid::MakeUuid();
      bOk &= pAccessor->AddObjectByName(pTestObj, "Test", plVariant(), pChosen, innerGuid).Succeeded();
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
    SelectObject(testGuid);
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plQtAiEqsQueryAssetDocumentWindow::ChangeGeneratorType()
{
  QMenu menu(this);
  const plRTTI* pChosen = nullptr;

  plQtSearchableMenu* pSearch = new plQtSearchableMenu(&menu);

  plRTTI::ForEachDerivedType(
    plAiEqsGenerator::GetStaticRTTI(),
    [&](const plRTTI* pRtti) {
      if (pRtti == plAiEqsGenerator::GetStaticRTTI())
        return;

      plStringBuilder sPath;
      sPath.SetFormat("Generators/{}", PrettyEqsName(pRtti).toUtf8().data());

      pSearch->AddItem(PrettyEqsName(pRtti).toUtf8().data(), sPath, QVariant::fromValue<void*>(const_cast<plRTTI*>(pRtti)));
    },
    plRTTI::ForEachOptions::ExcludeNotConcrete);

  connect(pSearch, &plQtSearchableMenu::MenuItemTriggered, &menu, [&](const QString&, const QVariant& variant) {
    pChosen = static_cast<const plRTTI*>(variant.value<void*>());
    menu.close();
  });

  menu.addAction(pSearch);
  pSearch->Finalize(QString());

  menu.exec(QCursor::pos());

  if (pChosen == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Change Generator");

  bool bOk = true;

  if (const plDocumentObject* pGenerator = GetGeneratorObject())
  {
    bOk &= pAccessor->RemoveObject(pGenerator).Succeeded();
  }

  plUuid genGuid = plUuid::MakeUuid();
  bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "Generator", plVariant(), pChosen, genGuid).Succeeded();

  if (bOk)
  {
    pAccessor->FinishTransaction();
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plQtAiEqsQueryAssetDocumentWindow::AddContextSlot()
{
  QMenu menu(this);
  const plRTTI* pChosen = nullptr;

  plQtSearchableMenu* pSearch = new plQtSearchableMenu(&menu);

  plRTTI::ForEachDerivedType(
    plAiEqsContext::GetStaticRTTI(),
    [&](const plRTTI* pRtti) {
      if (pRtti == plAiEqsContext::GetStaticRTTI())
        return;

      plStringBuilder sPath;
      sPath.SetFormat("Contexts/{}", PrettyEqsName(pRtti).toUtf8().data());

      pSearch->AddItem(PrettyEqsName(pRtti).toUtf8().data(), sPath, QVariant::fromValue<void*>(const_cast<plRTTI*>(pRtti)));
    },
    plRTTI::ForEachOptions::ExcludeNotConcrete);

  connect(pSearch, &plQtSearchableMenu::MenuItemTriggered, &menu, [&](const QString&, const QVariant& variant) {
    pChosen = static_cast<const plRTTI*>(variant.value<void*>());
    menu.close();
  });

  menu.addAction(pSearch);
  pSearch->Finalize(QString());

  menu.exec(QCursor::pos());

  if (pChosen == nullptr)
    return;

  // default slot name: 'Threat' for the perceived target (the common case), else derived from the type
  plStringBuilder sDefaultName;

  if (pChosen == plGetStaticRTTI<plAiEqsContext_PerceivedTarget>())
  {
    sDefaultName = "Threat";
  }
  else
  {
    sDefaultName = pChosen->GetTypeName();
    sDefaultName.TrimWordStart("plAiEqsContext_");
  }

  // avoid duplicate names
  {
    plDynamicArray<plString> names;
    GetContextSlotNames(names);

    plStringBuilder sUnique = sDefaultName;
    plUInt32 uiSuffix = 2;

    while (names.Contains(plString(sUnique)) || sUnique == "Querier")
    {
      sUnique.SetFormat("{}{}", sDefaultName, uiSuffix);
      ++uiSuffix;
    }

    sDefaultName = sUnique;
  }

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Add Context");

  bool bOk = true;

  plUuid slotGuid = plUuid::MakeUuid();
  bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "ContextSlots", pAccessor->GetCountByName(GetPropertiesObject(), "ContextSlots"), plAiEqsContextSlotObject::GetStaticRTTI(), slotGuid).Succeeded();

  if (bOk)
  {
    if (const plDocumentObject* pSlotObj = pAccessor->GetObject(slotGuid))
    {
      bOk &= pAccessor->SetValueByName(pSlotObj, "Name", plVariant(sDefaultName.GetData())).Succeeded();

      plUuid ctxGuid = plUuid::MakeUuid();
      bOk &= pAccessor->AddObjectByName(pSlotObj, "Context", plVariant(), pChosen, ctxGuid).Succeeded();
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
    SelectContextSlot(slotGuid);
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plQtAiEqsQueryAssetDocumentWindow::RemoveContextSlot(const plUuid& guid)
{
  const plDocumentObject* pObject = GetArrayObject("ContextSlots", guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Remove Context");

  if (pAccessor->RemoveObject(pObject).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}
