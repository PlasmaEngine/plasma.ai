#pragma once

#include <EditorPluginAi/Assets/AiAssetWindow.moc.h>

#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Types/Uuid.h>

#include <QFrame>

class plAiBehaviorAssetDocument;
class plAiConsiderationObject;
class plQtAiBehaviorAssetDocumentWindow;
class plQtCurve1DButtonWidget;
class plDocumentObject;

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QSlider;
class QLabel;
class QToolButton;
class QPushButton;
class QVBoxLayout;

/// \brief One consideration row in the AI behavior editor: input + normalization range + always-visible response curve.
class plQtAiConsiderationCard : public QFrame
{
  Q_OBJECT

public:
  plQtAiConsiderationCard(QWidget* pParent, plQtAiBehaviorAssetDocumentWindow* pWindow, const plUuid& objectGuid);

  const plUuid& GetObjectGuid() const { return m_ObjectGuid; }

  /// \brief Updates all displayed values from the native object mirror.
  void RefreshFromNative(const plAiConsiderationObject* pNative, plUInt32 uiIndex, plUInt32 uiCount, bool bSelected);

protected:
  virtual void mousePressEvent(QMouseEvent* pEvent) override;

private Q_SLOTS:
  void onMinEdited(double fValue);
  void onMaxEdited(double fValue);
  void onCurveClicked();
  void onCurveContextMenu(const QPoint& pos);

private:
  plQtAiBehaviorAssetDocumentWindow* m_pWindow = nullptr;
  plUuid m_ObjectGuid;
  bool m_bUpdating = false;

  QLabel* m_pTypeLabel = nullptr;
  QLabel* m_pTargetChip = nullptr;
  QLabel* m_pSummaryLabel = nullptr;
  QDoubleSpinBox* m_pMin = nullptr;
  QDoubleSpinBox* m_pMax = nullptr;
  plQtCurve1DButtonWidget* m_pCurveButton = nullptr;
  QToolButton* m_pUp = nullptr;
  QToolButton* m_pDown = nullptr;
  QToolButton* m_pRemove = nullptr;
};

/// \brief Bar visualizing the composed utility, with an optional commit-bonus switch threshold marker.
class plQtAiUtilityBar : public QWidget
{
  Q_OBJECT

public:
  explicit plQtAiUtilityBar(QWidget* pParent);

  void SetState(float fUtility, float fCommitThreshold, bool bVetoed);

protected:
  virtual void paintEvent(QPaintEvent* pEvent) override;

private:
  float m_fUtility = 1.0f;
  float m_fCommitThreshold = -1.0f;
  bool m_bVetoed = false;
};

/// \brief Live scoring panel: simulated raw inputs per consideration, evaluated with the exact
/// plAiUtilityEvaluator::ScoreUtility math (product with veto, n-axis compensation, weight, clamp).
class plQtAiScorePreviewPanel : public QWidget
{
  Q_OBJECT

public:
  plQtAiScorePreviewPanel(QWidget* pParent, plAiBehaviorAssetDocument* pDocument);

  /// \brief Recreates the per-consideration slider rows (structure changed).
  void RebuildRows();

  /// \brief Re-reads ranges/curves/weight and recomputes (property changed).
  void RefreshValues();

private Q_SLOTS:
  void onAnyInputChanged();

private:
  void Recompute();

  struct Row
  {
    QLabel* m_pName = nullptr;
    QSlider* m_pSlider = nullptr;
    QLabel* m_pRaw = nullptr;
    QLabel* m_pOut = nullptr;
    float m_fMin = 0.0f;
    float m_fMax = 1.0f;
  };

  plAiBehaviorAssetDocument* m_pDocument = nullptr;
  QWidget* m_pRowsHost = nullptr;
  QVBoxLayout* m_pRowsLayout = nullptr;
  plHybridArray<Row, 8> m_Rows;

  QDoubleSpinBox* m_pWeightScale = nullptr;
  QCheckBox* m_pRunning = nullptr;
  QCheckBox* m_pCooldown = nullptr;

  QLabel* m_pProduct = nullptr;
  QLabel* m_pCompensated = nullptr;
  QLabel* m_pUtilityValue = nullptr;
  QLabel* m_pCategoryLabel = nullptr;
  QLabel* m_pCommitNote = nullptr;
  plQtAiUtilityBar* m_pBar = nullptr;
};

/// \brief Custom document window for AI Behavior assets.
///
/// Header band (name, category, weight, commit bonus, cooldown, logic), consideration cards with
/// always-visible response curves, a live Score Preview dock and the classic property grid as a
/// "Raw Properties" tab. All edits go through the document object model (undo/redo intact).
class plQtAiBehaviorAssetDocumentWindow : public plQtAiAssetDocumentWindow
{
  Q_OBJECT

public:
  plQtAiBehaviorAssetDocumentWindow(plDocument* pDocument);
  ~plQtAiBehaviorAssetDocumentWindow();

  // called by the consideration cards
  void SelectConsideration(const plUuid& guid);
  void RemoveConsideration(const plUuid& guid);
  void MoveConsideration(const plUuid& guid, plInt32 iDirection);
  void SetConsiderationRange(const plUuid& guid, bool bMin, double fValue);
  void OpenCurveEditor(const plUuid& guid);
  void ApplyCurvePreset(const plUuid& guid, plUInt32 uiPreset);

  static plUInt32 GetCurvePresetCount();
  static const char* GetCurvePresetName(plUInt32 uiPreset);

protected:
  virtual void OnAssetPropertiesChanged() override;
  virtual void OnAssetStructureChanged() override;

private Q_SLOTS:
  void onAddConsideration();

private:
  QWidget* CreateHeaderBand(QWidget* pParent);
  void RefreshHeader();
  void RebuildCards();
  void RefreshCards();
  void SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription);
  const plDocumentObject* GetPropertiesObject() const;
  const plDocumentObject* GetConsiderationObject(const plUuid& guid, plInt32* out_pIndex = nullptr) const;
  void GetConsiderationGuids(plDynamicArray<plUuid>& out_guids) const;
  void ChangeLogicType(plInt32 iType);
  void BrowseStateMachine();
  void OpenStateMachine();

  plAiBehaviorAssetDocument* m_pBehaviorDocument = nullptr;

  // header band
  QLineEdit* m_pName = nullptr;
  QComboBox* m_pCategory = nullptr;
  QDoubleSpinBox* m_pWeight = nullptr;
  QDoubleSpinBox* m_pCommitBonus = nullptr;
  QDoubleSpinBox* m_pCooldown = nullptr;
  QComboBox* m_pLogicType = nullptr;
  QWidget* m_pSmRow = nullptr;
  QLineEdit* m_pSmPath = nullptr;

  // consideration list
  QWidget* m_pCardsHost = nullptr;
  QVBoxLayout* m_pCardsLayout = nullptr;
  QPushButton* m_pAddButton = nullptr;
  plHybridArray<plQtAiConsiderationCard*, 8> m_Cards;
  plUuid m_SelectedConsideration;

  plQtAiScorePreviewPanel* m_pScorePanel = nullptr;

  bool m_bUpdatingUi = false;
};
