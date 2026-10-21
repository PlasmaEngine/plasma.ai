#pragma once

#include <Foundation/Basics.h>
#include <GuiFoundation/DocumentWindow/DocumentWindow.moc.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

#include <QLabel>

class QWidget;

/// \brief A response curve preview that opens the curve editor when clicked.
///
/// The cards paint the curve into a pixmap and set it here; the label scales it to its size.
class plQtAiCurveButton : public QLabel
{
  Q_OBJECT

public:
  explicit plQtAiCurveButton(QWidget* pParent);

Q_SIGNALS:
  void clicked();

protected:
  virtual void mouseReleaseEvent(QMouseEvent* pEvent) override;
};

/// \brief Shared shell for the AI Behavior and AI Archetype asset windows.
///
/// Sets up the menu bar and tool bar, and pushes the current asset state to the engine process
/// on every property change, so that running scenes (play-the-game / simulate) pick up tuning
/// changes live. Derived windows build their custom central UI and dock panels on top.
class plQtAiAssetDocumentWindow : public plQtDocumentWindow
{
  Q_OBJECT

public:
  /// \param sResourceType the asset type name registered via plResourceManager::RegisterResourceForAssetType
  plQtAiAssetDocumentWindow(plDocument* pDocument, const char* szUniqueName, const char* szResourceType);
  ~plQtAiAssetDocumentWindow();

  /// \brief Whether the cards and previews are engine-drawn; -engineAiEditors false keeps the Qt widgets.
  static bool IsEngineUiEnabled();

protected:
  /// \brief Creates the classic property-grid widget (asset status indicator + grid), for use as a "Raw Properties" dock tab.
  QWidget* CreateRawPropertiesWidget(QWidget* pParent);

  /// \brief Called (coalesced per event burst) whenever document properties change. Derived windows refresh their custom UI here.
  virtual void OnAssetPropertiesChanged() {}

  /// \brief Called (coalesced) whenever objects are added/removed/moved. Derived windows rebuild structural UI here.
  virtual void OnAssetStructureChanged() {}

  const plString& GetUniqueWindowName() const { return m_sUniqueName; }

private Q_SLOTS:
  void FlushUiRefresh();

private:
  void UpdatePreview();
  void RestoreResource();

  void PropertyEventHandler(const plDocumentObjectPropertyEvent& e);
  void StructureEventHandler(const plDocumentObjectStructureEvent& e);
  void QueueUiRefresh(bool bStructure);

  plString m_sUniqueName;
  plString m_sResourceType;
  bool m_bRefreshQueued = false;
  bool m_bStructureChanged = false;
};
