#pragma once

#include <EditorPluginAi/Assets/AiAssetWindow.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Types/Uuid.h>

class plAiArchetypeAssetDocument;
class plAiArchetypeStackView;
class plAiArchetypeSettingsView;
class plDocumentObject;

/// \brief Custom document window for AI Archetype assets.
///
/// Shows the referenced behaviors grouped into the seven category bands they will actually
/// arbitrate in (base weight x WeightScale = effective weight resolved in place) with a read-only
/// peek into the selected behavior, and the senses, reactivity and scoring settings with a live
/// memory preview. Both panels are engine-drawn.
class plAiArchetypeAssetDocumentWindow : public plAiAssetDocumentWindow
{
public:
  plAiArchetypeAssetDocumentWindow(plDocument* pDocument);
  ~plAiArchetypeAssetDocumentWindow();

  // called by the stack rows
  void SelectBehaviorRow(const plUuid& guid);
  void SetWeightScale(const plUuid& guid, double fValue);
  void OpenBehavior(const plUuid& guid);
  void RemoveBehavior(const plUuid& guid);
  void AddBehavior();
  void BrowseBlackboardTemplate();
  void SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription);

  /// \brief Sets a property of the "Hearing", "Damage" or "Custom" sense in one undoable step.
  void SetSenseValue(const char* szSense, const char* szProperty, const plVariant& value, const char* szDescription);

  /// \brief Sets the scoring version in one undoable step.
  void SetScoringVersion(plUInt8 uiVersion);

  void GetBehaviorGuids(plDynamicArray<plUuid>& out_guids) const;
  const char* GetBehaviorRef(plUInt32 uiIndex) const;

  plAiArchetypeAssetDocument* GetArchetypeDocument() const { return m_pArchetypeDocument; }
  const plUuid& GetSelectedRow() const { return m_SelectedRow; }

  /// Selects the behavior row for the properties.
  virtual void RevealValidationElement(const plAiValidationElement& element) override;

protected:
  virtual void OnAssetPropertiesChanged() override;
  virtual void OnAssetStructureChanged() override;

private:
  void InvalidateViews();
  const plDocumentObject* GetPropertiesObject() const;

  plAiArchetypeAssetDocument* m_pArchetypeDocument = nullptr;

  // engine-drawn panels
  plAiArchetypeStackView* m_pStackView = nullptr;
  plAiArchetypeSettingsView* m_pSettingsView = nullptr;

  plUuid m_SelectedRow;
};
