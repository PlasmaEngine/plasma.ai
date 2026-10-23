#pragma once

#include <AiPlugin/Eqs/EqsTypes.h>
#include <EditorPluginAi/Assets/AiAssetWindow.h>
#include <EditorPluginAi/Assets/AiEqsPreviewSimulation.h>

#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Math/Vec2.h>
#include <Foundation/Types/Uuid.h>

class plAiEqsQueryAssetDocument;
class plAiEqsQueryAssetObject;
class plAiEqsTestObject;
class plAiEqsGenerator;
class plAiEqsQueryView;
class plAiEqsPreviewView;
class plDocumentObject;

/// \brief Custom document window for AI EQS Query assets.
///
/// Header band (name, run mode, candidates, max results, navmesh), context strip, generator card,
/// test cards with always-visible response curves, a live Query Preview panel and the classic
/// property grid as a "Raw Properties" panel. All edits go through the document object model
/// (undo/redo intact). The cards and the preview are engine-drawn.
class plAiEqsQueryAssetDocumentWindow : public plAiAssetDocumentWindow
{
public:
  plAiEqsQueryAssetDocumentWindow(plDocument* pDocument);
  ~plAiEqsQueryAssetDocumentWindow();

  // called by the views
  void SelectObject(const plUuid& guid);
  void RemoveTest(const plUuid& guid);
  void MoveTest(const plUuid& guid, plInt32 iDirection);
  void SetTestWeight(const plUuid& guid, double fValue);
  void SetTestProperty(const plUuid& guid, const char* szProperty, const plVariant& value);
  void OpenCurveEditor(const plUuid& guid);
  void ApplyCurvePreset(const plUuid& guid, plUInt32 uiPreset);
  void SelectGenerator();
  void ChangeGeneratorTypeTo(const plRTTI* pType);
  void SetGeneratorValue(const char* szProperty, const plVariant& value, const char* szDescription);
  void AddContextSlotOfType(const plRTTI* pType);
  void RemoveContextSlot(const plUuid& guid);
  void SelectContextSlot(const plUuid& guid);
  void GetContextSlotNames(plDynamicArray<plString>& out_names) const;
  void AddTestOfType(const plRTTI* pType);
  void SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription);
  void GetArrayGuids(const char* szProperty, plDynamicArray<plUuid>& out_guids) const;

  plAiEqsQueryAssetDocument* GetEqsDocument() const { return m_pEqsDocument; }
  const plUuid& GetSelectedObject() const { return m_SelectedObject; }

  /// Selects the test card, context slot or generator for the properties.
  virtual void RevealValidationElement(const plAiValidationElement& element) override;

protected:
  virtual void OnAssetPropertiesChanged() override;
  virtual void OnAssetStructureChanged() override;

private:
  const plDocumentObject* GetPropertiesObject() const;
  const plDocumentObject* GetGeneratorObject() const;
  const plDocumentObject* GetArrayObject(const char* szProperty, const plUuid& guid, plInt32* out_pIndex = nullptr) const;

  plAiEqsQueryAssetDocument* m_pEqsDocument = nullptr;

  // engine-drawn panels
  plAiEqsQueryView* m_pQueryView = nullptr;
  plAiEqsPreviewView* m_pPreviewView = nullptr;

  plUuid m_SelectedObject;
};
