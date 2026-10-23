#pragma once

#include <EditorPluginAi/Assets/AiAssetWindow.h>

#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Types/Uuid.h>

class plAiBehaviorAssetDocument;
class plAiConsiderationObject;
class plAiBehaviorView;
class plAiScorePreviewView;
class plDocumentObject;

/// \brief Custom document window for AI Behavior assets.
///
/// Header band (name, category, weight, commit bonus, cooldown, logic), consideration cards with
/// always-visible response curves, a live Score Preview panel and the classic property grid as a
/// "Raw Properties" panel. All edits go through the document object model (undo/redo intact).
/// The cards and the preview are engine-drawn.
class plAiBehaviorAssetDocumentWindow : public plAiAssetDocumentWindow
{
public:
  plAiBehaviorAssetDocumentWindow(plDocument* pDocument);
  ~plAiBehaviorAssetDocumentWindow();

  // called by the consideration cards
  void SelectConsideration(const plUuid& guid);
  void RemoveConsideration(const plUuid& guid);
  void MoveConsideration(const plUuid& guid, plInt32 iDirection);
  void SetConsiderationRange(const plUuid& guid, bool bMin, double fValue);
  void OpenCurveEditor(const plUuid& guid);
  void ApplyCurvePreset(const plUuid& guid, plUInt32 uiPreset);
  void AddConsiderationOfType(const plRTTI* pType);
  void SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription);
  void GetConsiderationGuids(plDynamicArray<plUuid>& out_guids) const;

  /// 0 no logic, 1 state machine, 2 action queue.
  void ChangeLogicType(plInt32 iType);
  void BrowseStateMachine();
  void OpenStateMachine();

  plAiBehaviorAssetDocument* GetBehaviorDocument() const { return m_pBehaviorDocument; }
  const plUuid& GetSelectedConsideration() const { return m_SelectedConsideration; }

  static plUInt32 GetCurvePresetCount();
  static const char* GetCurvePresetName(plUInt32 uiPreset);

protected:
  virtual void OnAssetPropertiesChanged() override;
  virtual void OnAssetStructureChanged() override;

private:
  const plDocumentObject* GetPropertiesObject() const;
  const plDocumentObject* GetConsiderationObject(const plUuid& guid, plInt32* out_pIndex = nullptr) const;

  plAiBehaviorAssetDocument* m_pBehaviorDocument = nullptr;

  // engine-drawn panels
  plAiBehaviorView* m_pBehaviorView = nullptr;
  plAiScorePreviewView* m_pScoreView = nullptr;

  plUuid m_SelectedConsideration;
};
