#pragma once

#include <EditorPluginAi/Assets/AiAssetWindow.moc.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Types/Uuid.h>

#include <QFrame>

class plAiArchetypeAssetDocument;
class plQtAiArchetypeAssetDocumentWindow;
class plDocumentObject;

class QLineEdit;
class QDoubleSpinBox;
class QLabel;
class QToolButton;
class QPushButton;
class QVBoxLayout;

/// \brief Paints the confidence-over-time curve produced by the archetype's perception settings
/// (gain while sighted, decay after sight is lost, record forgotten at the memory cutoff).
class plQtAiPerceptionTimeline : public QWidget
{
  Q_OBJECT

public:
  explicit plQtAiPerceptionTimeline(QWidget* pParent);

  void SetValues(float fGainPerSec, float fDecayPerSec, float fMemorySeconds);

protected:
  virtual void paintEvent(QPaintEvent* pEvent) override;

private:
  float m_fGain = 4.0f;
  float m_fDecay = 0.5f;
  float m_fMemory = 10.0f;
};

/// \brief Small horizontal bar showing base weight x archetype scale, colored by behavior category.
class plQtAiEffectiveWeightBar : public QWidget
{
  Q_OBJECT

public:
  explicit plQtAiEffectiveWeightBar(QWidget* pParent);

  void SetValue(float fEffectiveWeight, const QColor& color);

protected:
  virtual void paintEvent(QPaintEvent* pEvent) override;

private:
  float m_fEffective = 1.0f;
  QColor m_Color;
};

/// \brief Read-only mini view of a response curve (used by the behavior peek).
class plQtAiCurveMiniView : public QWidget
{
  Q_OBJECT

public:
  explicit plQtAiCurveMiniView(QWidget* pParent);

  void SetCurve(const plCurve1D& curve, bool bLegacyDomain = false);

protected:
  virtual void paintEvent(QPaintEvent* pEvent) override;

private:
  plCurve1D m_Curve;
  bool m_bLegacyDomain = false;
};

/// \brief One referenced behavior in the archetype's category ladder.
class plQtAiBehaviorStackRow : public QFrame
{
  Q_OBJECT

public:
  plQtAiBehaviorStackRow(QWidget* pParent, plQtAiArchetypeAssetDocumentWindow* pWindow, const plUuid& objectGuid);

  const plUuid& GetObjectGuid() const { return m_ObjectGuid; }

  void Refresh(const plAiBehaviorPeek& peek, const char* szBehaviorRef, float fWeightScale, bool bSelected);

protected:
  virtual void mousePressEvent(QMouseEvent* pEvent) override;

private:
  plQtAiArchetypeAssetDocumentWindow* m_pWindow = nullptr;
  plUuid m_ObjectGuid;
  bool m_bUpdating = false;

  QLabel* m_pName = nullptr;
  QLabel* m_pBaseWeight = nullptr;
  QDoubleSpinBox* m_pScale = nullptr;
  plQtAiEffectiveWeightBar* m_pBar = nullptr;
  QLabel* m_pTag = nullptr;
};

/// \brief Custom document window for AI Archetype assets.
///
/// Shows the referenced behaviors grouped into the seven category bands they will actually
/// arbitrate in (base weight x WeightScale = effective weight resolved in place), the perception
/// settings as a live confidence timeline, and a read-only peek into the selected behavior.
class plQtAiArchetypeAssetDocumentWindow : public plQtAiAssetDocumentWindow
{
  Q_OBJECT

public:
  plQtAiArchetypeAssetDocumentWindow(plDocument* pDocument);
  ~plQtAiArchetypeAssetDocumentWindow();

  // called by the stack rows
  void SelectBehaviorRow(const plUuid& guid);
  void SetWeightScale(const plUuid& guid, double fValue);
  void OpenBehavior(const plUuid& guid);
  void RemoveBehavior(const plUuid& guid);

protected:
  virtual void OnAssetPropertiesChanged() override;
  virtual void OnAssetStructureChanged() override;

private Q_SLOTS:
  void onAddBehavior();

private:
  struct Band
  {
    QWidget* m_pHost = nullptr;
    QVBoxLayout* m_pRows = nullptr;
    QLabel* m_pEmpty = nullptr;
  };

  QWidget* CreateHeaderBand(QWidget* pParent);
  QWidget* CreateInsightPanel(QWidget* pParent);
  void RefreshHeader();
  void RebuildStack();
  void RefreshStack();
  void RefreshPeek();
  void SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription);
  const plDocumentObject* GetPropertiesObject() const;
  void GetBehaviorGuids(plDynamicArray<plUuid>& out_guids) const;
  const char* GetBehaviorRef(plUInt32 uiIndex) const;
  void BrowseBlackboardTemplate();

  plAiArchetypeAssetDocument* m_pArchetypeDocument = nullptr;

  // header
  QLineEdit* m_pTeam = nullptr;
  QLineEdit* m_pSensor = nullptr;
  QLineEdit* m_pBlackboard = nullptr;

  // stack
  QWidget* m_pStackHost = nullptr;
  plHybridArray<Band, 8> m_Bands; // ladder top..bottom + unresolved
  plHybridArray<plQtAiBehaviorStackRow*, 16> m_Rows;
  plUuid m_SelectedRow;

  // insight panel
  QDoubleSpinBox* m_pGain = nullptr;
  QDoubleSpinBox* m_pDecay = nullptr;
  QDoubleSpinBox* m_pMemory = nullptr;
  plQtAiPerceptionTimeline* m_pTimeline = nullptr;
  QLabel* m_pPeekTitle = nullptr;
  QWidget* m_pPeekHost = nullptr;
  QVBoxLayout* m_pPeekLayout = nullptr;

  bool m_bUpdatingUi = false;
};
