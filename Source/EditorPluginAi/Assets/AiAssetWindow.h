#pragma once

#include <EditorUi/Runtime.h>
#include <Foundation/Basics.h>
#include <GuiFoundation/DocumentWindow/DocumentWindow.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

class plAiValidationView;
struct plAiValidationElement;

/// \brief Shared shell for the AI Behavior, AI Archetype and AI EQS Query asset windows.
///
/// Declares the menu bar and tool bar, and pushes the current asset state to the engine process
/// on every property change, so that running scenes (play-the-game / simulate) pick up tuning
/// changes live. Derived windows add their engine-drawn panels on top.
class plAiAssetDocumentWindow : public plDocumentWindow
{
public:
  /// \param sResourceType the asset type name registered via plResourceManager::RegisterResourceForAssetType
  plAiAssetDocumentWindow(plDocument* pDocument, const char* szUniqueName, const char* szResourceType);
  ~plAiAssetDocumentWindow();

  /// \brief Selects an element of the asset for the properties (a Validation row was clicked). Selects the asset's
  /// properties by default.
  virtual void RevealValidationElement(const plAiValidationElement& element);

protected:
  /// \brief Adds the classic property grid (with the asset status banner) as the "Raw Properties" panel.
  void AddRawPropertiesPanel(plStringView sPanelId);

  /// \brief Adds the "Validation" panel, which validates again on every document refresh.
  void AddValidationPanel(plStringView sPanelId);

  /// \brief The asset's properties object (the first child of the document root).
  const plDocumentObject* GetAssetPropertiesObject() const;

  /// \brief Called (coalesced per event burst) whenever document properties change. Derived windows refresh their custom UI here.
  virtual void OnAssetPropertiesChanged() {}

  /// \brief Called (coalesced) whenever objects are added/removed/moved. Derived windows rebuild structural UI here.
  virtual void OnAssetStructureChanged() {}

  const plString& GetUniqueWindowName() const { return m_sUniqueName; }

private:
  void FlushUiRefresh();
  void UpdatePreview();
  void RestoreResource();

  void PropertyEventHandler(const plDocumentObjectPropertyEvent& e);
  void StructureEventHandler(const plDocumentObjectStructureEvent& e);
  void QueueUiRefresh(bool bStructure);

  plString m_sUniqueName;
  plString m_sResourceType;
  plAiValidationView* m_pValidationView = nullptr;
  bool m_bRefreshQueued = false;
  bool m_bStructureChanged = false;
  plEditorUiTimerGroup m_Timers; ///< the queued UI refresh
};
