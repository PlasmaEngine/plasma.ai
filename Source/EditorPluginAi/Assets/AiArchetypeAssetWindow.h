#pragma once

#include <EditorPluginAi/Assets/AiAssetWindow.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Types/Uuid.h>

class plAiArchetypeAssetDocument;
class plAiArchetypeStackView;
class plAiPerceptionView;
class plDocumentObject;

/// \brief Custom document window for AI Archetype assets.
///
/// Shows the referenced behaviors grouped into the seven category bands they will actually
/// arbitrate in (base weight x WeightScale = effective weight resolved in place), the perception
/// settings as a live confidence timeline, and a read-only peek into the selected behavior.
/// The stack and the perception panel are engine-drawn.
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
  void GetBehaviorGuids(plDynamicArray<plUuid>& out_guids) const;
  const char* GetBehaviorRef(plUInt32 uiIndex) const;

  plAiArchetypeAssetDocument* GetArchetypeDocument() const { return m_pArchetypeDocument; }
  const plUuid& GetSelectedRow() const { return m_SelectedRow; }

protected:
  virtual void OnAssetPropertiesChanged() override;
  virtual void OnAssetStructureChanged() override;

private:
  void InvalidateViews();
  const plDocumentObject* GetPropertiesObject() const;

  plAiArchetypeAssetDocument* m_pArchetypeDocument = nullptr;

  // engine-drawn panels
  plAiArchetypeStackView* m_pStackView = nullptr;
  plAiPerceptionView* m_pPerceptionView = nullptr;

  plUuid m_SelectedRow;
};
