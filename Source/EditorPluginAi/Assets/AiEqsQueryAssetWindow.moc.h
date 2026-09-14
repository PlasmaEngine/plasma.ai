#pragma once

#include <AiPlugin/Eqs/EqsTypes.h>
#include <EditorPluginAi/Assets/AiAssetWindow.moc.h>

#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Math/Vec2.h>
#include <Foundation/Types/Uuid.h>

#include <QFrame>
#include <QLabel>

class plAiEqsQueryAssetDocument;
class plAiEqsQueryAssetObject;
class plAiEqsTestObject;
class plAiEqsGenerator;
class plQtAiEqsQueryAssetDocumentWindow;
class plQtCurve1DButtonWidget;
class plDocumentObject;

class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QSlider;
class QLabel;
class QToolButton;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;

/// \brief Small colored tag ("CHEAP", "FILTER", "THREAT") used on the EQS cards.
class plQtAiEqsChip : public QLabel
{
  Q_OBJECT

public:
  enum class Style
  {
    Cheap,
    Expensive,
    Filter,
    Score,
    FilterAndScore,
    Context,
    Payload
  };

  explicit plQtAiEqsChip(QWidget* pParent);

  void SetChip(Style style, const QString& sText);
};

/// \brief One test row in the EQS query editor: identity + chips, weight and the response curve.
class plQtAiEqsTestCard : public QFrame
{
  Q_OBJECT

public:
  plQtAiEqsTestCard(QWidget* pParent, plQtAiEqsQueryAssetDocumentWindow* pWindow, const plUuid& objectGuid);

  const plUuid& GetObjectGuid() const { return m_ObjectGuid; }

  void RefreshFromNative(const plAiEqsTestObject* pNative, plUInt32 uiIndex, plUInt32 uiCount, bool bSelected);

protected:
  virtual void mousePressEvent(QMouseEvent* pEvent) override;

private Q_SLOTS:
  void onWeightEdited(double fValue);
  void onCurveClicked();
  void onCurveContextMenu(const QPoint& pos);

private:
  plQtAiEqsQueryAssetDocumentWindow* m_pWindow = nullptr;
  plUuid m_ObjectGuid;
  bool m_bUpdating = false;

  QLabel* m_pTypeLabel = nullptr;
  plQtAiEqsChip* m_pContextChip = nullptr;
  plQtAiEqsChip* m_pPurposeChip = nullptr;
  plQtAiEqsChip* m_pCostChip = nullptr;
  QLabel* m_pSummaryLabel = nullptr;
  QWidget* m_pWeightHost = nullptr;
  QDoubleSpinBox* m_pWeight = nullptr;
  plQtCurve1DButtonWidget* m_pCurveButton = nullptr;
  QLabel* m_pNoCurve = nullptr;
  QToolButton* m_pUp = nullptr;
  QToolButton* m_pDown = nullptr;
  QToolButton* m_pRemove = nullptr;
  QComboBox* m_pCondition = nullptr;
  QComboBox* m_pMissingData = nullptr;
  QDoubleSpinBox* m_pFilterMin = nullptr;
  QDoubleSpinBox* m_pFilterMax = nullptr;
  QCheckBox* m_pInvertFilter = nullptr;
  QCheckBox* m_pInvertScore = nullptr;
};

/// \brief The generator card: type, payload chip, center context and radius band.
class plQtAiEqsGeneratorCard : public QFrame
{
  Q_OBJECT

public:
  plQtAiEqsGeneratorCard(QWidget* pParent, plQtAiEqsQueryAssetDocumentWindow* pWindow);

  void RefreshFromNative(const plAiEqsGenerator* pNative, const plAiEqsQueryAssetObject* pProps);

protected:
  virtual void mousePressEvent(QMouseEvent* pEvent) override;

private Q_SLOTS:
  void onAroundChanged(int iIndex);
  void onRadiusMinEdited(double fValue);
  void onRadiusMaxEdited(double fValue);
  void onChangeClicked();

private:
  plQtAiEqsQueryAssetDocumentWindow* m_pWindow = nullptr;
  bool m_bUpdating = false;

  QLabel* m_pNameLabel = nullptr;
  plQtAiEqsChip* m_pPayloadChip = nullptr;
  QComboBox* m_pAround = nullptr;
  QDoubleSpinBox* m_pRadiusMin = nullptr;
  QDoubleSpinBox* m_pRadiusMax = nullptr;
  QPushButton* m_pChange = nullptr;
  QLabel* m_pSummaryLabel = nullptr;
};

/// \brief The horizontal context strip: one chip per context slot plus '+ Add Context'.
class plQtAiEqsContextStrip : public QWidget
{
  Q_OBJECT

public:
  plQtAiEqsContextStrip(QWidget* pParent, plQtAiEqsQueryAssetDocumentWindow* pWindow);

  void Rebuild();

private:
  plQtAiEqsQueryAssetDocumentWindow* m_pWindow = nullptr;
  QHBoxLayout* m_pLayout = nullptr;
  plHybridArray<QWidget*, 8> m_Chips;
  QPushButton* m_pAddButton = nullptr;
};

/// \brief One locally simulated candidate of the preview panel.
struct plAiEqsPreviewCandidate
{
  plVec2 m_vPosition = plVec2::MakeZero(); ///< meters, querier-relative plane
  plVec2 m_vWallDir = plVec2::MakeZero();  ///< synthetic cover wall direction
  plUInt8 m_uiPayload = 0;                 ///< plAiEqsPayloadType
  plUInt8 m_uiCoverQuality = 0;            ///< synthetic plAiCoverQuality
  plHybridArray<float, 12> m_TestScores;   ///< curved per-test scores
  plHybridArray<plAiEqsTestTrace, 12> m_TestTrace;
  float m_fFinal = 0.0f;
  bool m_bDiscarded = false;
  bool m_bWinner = false;
};

/// \brief Top-down scatter canvas: draggable querier/threat/occluder, clickable candidates.
class plQtAiEqsScatterWidget : public QWidget
{
  Q_OBJECT

public:
  explicit plQtAiEqsScatterWidget(QWidget* pParent);

  void SetCandidates(const plHybridArray<plAiEqsPreviewCandidate, 64>& candidates, plInt32 iSelected);

  plVec2 m_vQuerier = plVec2(-4.0f, -3.0f);  ///< meters
  plVec2 m_vThreat = plVec2(5.0f, 4.0f);     ///< meters
  plVec2 m_vOccluderA = plVec2(0.0f, 1.5f);  ///< LOS blocker segment start
  plVec2 m_vOccluderB = plVec2(2.5f, -1.0f); ///< LOS blocker segment end
  bool m_bAllowDrag = true;

Q_SIGNALS:
  void situationChanged();
  void candidateClicked(int iIndex);

protected:
  virtual void paintEvent(QPaintEvent* pEvent) override;
  virtual void mousePressEvent(QMouseEvent* pEvent) override;
  virtual void mouseMoveEvent(QMouseEvent* pEvent) override;
  virtual void mouseReleaseEvent(QMouseEvent* pEvent) override;

private:
  QPointF ToScreen(const plVec2& vMeters) const;
  plVec2 ToMeters(const QPointF& screen) const;

  plHybridArray<plAiEqsPreviewCandidate, 64> m_Candidates;
  plInt32 m_iSelected = -1;
  int m_iDragTarget = -1; ///< 0 querier, 1 threat, 2 occluder A, 3 occluder B
};

/// \brief Live preview: runs the query pipeline (synthetic generation, real scoring math) against
/// a simulated top-down situation, entirely inside the editor process.
class plQtAiEqsPreviewPanel : public QWidget
{
  Q_OBJECT

public:
  plQtAiEqsPreviewPanel(QWidget* pParent, plAiEqsQueryAssetDocument* pDocument);

  /// \brief Re-runs the local pipeline (properties or situation changed).
  void Recompute();

  /// \brief Structure changed: breakdown rows are rebuilt, then Recompute().
  void RebuildRows();

private Q_SLOTS:
  void onSituationChanged();
  void onCandidateClicked(int iIndex);
  void onThreatDistanceChanged(int iValue);

private:
  void RefreshBreakdown();

  plAiEqsQueryAssetDocument* m_pDocument = nullptr;

  QSlider* m_pThreatDistance = nullptr;
  QLabel* m_pThreatDistanceValue = nullptr;
  QCheckBox* m_pAllowDrag = nullptr;
  QCheckBox* m_pSceneSim = nullptr;

  plQtAiEqsScatterWidget* m_pScatter = nullptr;

  struct BreakdownRow
  {
    QLabel* m_pName = nullptr;
    QWidget* m_pBarHost = nullptr;
    QWidget* m_pBarFill = nullptr;
    QLabel* m_pValue = nullptr;
  };

  QWidget* m_pRowsHost = nullptr;
  QVBoxLayout* m_pRowsLayout = nullptr;
  plHybridArray<BreakdownRow, 12> m_Rows;
  QLabel* m_pFinalValue = nullptr;
  QLabel* m_pStats = nullptr;

  plHybridArray<plAiEqsPreviewCandidate, 64> m_Candidates;
  plInt32 m_iSelected = -1;
  bool m_bUpdating = false;
};

/// \brief Custom document window for AI EQS Query assets.
///
/// Header band (name, run mode, candidates, max results, navmesh), context strip, generator card,
/// test cards with always-visible response curves, a live Query Preview dock and the classic
/// property grid as a "Raw Properties" tab. All edits go through the document object model
/// (undo/redo intact).
class plQtAiEqsQueryAssetDocumentWindow : public plQtAiAssetDocumentWindow
{
  Q_OBJECT

public:
  plQtAiEqsQueryAssetDocumentWindow(plDocument* pDocument);
  ~plQtAiEqsQueryAssetDocumentWindow();

  // called by cards / strip
  void SelectObject(const plUuid& guid);
  void RemoveTest(const plUuid& guid);
  void MoveTest(const plUuid& guid, plInt32 iDirection);
  void SetTestWeight(const plUuid& guid, double fValue);
  void SetTestProperty(const plUuid& guid, const char* szProperty, const plVariant& value);
  void OpenCurveEditor(const plUuid& guid);
  void ApplyCurvePreset(const plUuid& guid, plUInt32 uiPreset);
  void SelectGenerator();
  void ChangeGeneratorType();
  void SetGeneratorValue(const char* szProperty, const plVariant& value, const char* szDescription);
  void AddContextSlot();
  void RemoveContextSlot(const plUuid& guid);
  void SelectContextSlot(const plUuid& guid);
  void GetContextSlotNames(plDynamicArray<plString>& out_names) const;

  plAiEqsQueryAssetDocument* GetEqsDocument() const { return m_pEqsDocument; }
  const plUuid& GetSelectedObject() const { return m_SelectedObject; }

protected:
  virtual void OnAssetPropertiesChanged() override;
  virtual void OnAssetStructureChanged() override;

private Q_SLOTS:
  void onAddTest();

private:
  friend class plQtAiEqsContextStrip;

  QWidget* CreateHeaderBand(QWidget* pParent);
  void RefreshHeader();
  void RebuildCards();
  void RefreshCards();
  void SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription);
  const plDocumentObject* GetPropertiesObject() const;
  const plDocumentObject* GetGeneratorObject() const;
  void GetArrayGuids(const char* szProperty, plDynamicArray<plUuid>& out_guids) const;
  const plDocumentObject* GetArrayObject(const char* szProperty, const plUuid& guid, plInt32* out_pIndex = nullptr) const;

  plAiEqsQueryAssetDocument* m_pEqsDocument = nullptr;

  // header band
  QLineEdit* m_pName = nullptr;
  QComboBox* m_pRunMode = nullptr;
  QSpinBox* m_pCandidates = nullptr;
  QSpinBox* m_pMaxResults = nullptr;
  QComboBox* m_pNavmesh = nullptr;

  plQtAiEqsContextStrip* m_pContextStrip = nullptr;
  plQtAiEqsGeneratorCard* m_pGeneratorCard = nullptr;

  // test list
  QWidget* m_pCardsHost = nullptr;
  QVBoxLayout* m_pCardsLayout = nullptr;
  QPushButton* m_pAddButton = nullptr;
  plHybridArray<plQtAiEqsTestCard*, 8> m_Cards;
  plUuid m_SelectedObject;

  plQtAiEqsPreviewPanel* m_pPreviewPanel = nullptr;

  bool m_bUpdatingUi = false;
};
