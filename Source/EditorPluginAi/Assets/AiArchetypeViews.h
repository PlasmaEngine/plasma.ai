#pragma once

#include <EditorPluginAi/Assets/AiEngineView.h>

#include <Foundation/Types/Uuid.h>

class plQtAiArchetypeAssetDocumentWindow;
struct plAiBehaviorPeek;

/// \brief The engine-drawn behavior stack: the header (team, sensor object, blackboard template, Add Behavior) and
/// the referenced behaviors grouped into the category bands they arbitrate in, plus the unresolved ones.
class plQtAiArchetypeStackView : public plQtAiEngineView
{
public:
  plQtAiArchetypeStackView(plQtAiArchetypeAssetDocumentWindow* pWindow);
  ~plQtAiArchetypeStackView();

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;
  virtual bool IsRebuildBlocked() const override;

private:
  float BuildHeader(plUiElement* pRoot, float fWidth);
  void BuildRow(plUiElement* pBody, float fWidth, plUInt32 uiIndex, const plUuid& guid, const plAiBehaviorPeek& peek, plStringView sReference, float fWeightScale);

  plQtAiArchetypeAssetDocumentWindow* m_pWindow = nullptr;
};

/// \brief The engine-drawn "Perception & Peek" dock: the target confidence timeline with its three settings and
/// a read-only peek into the selected behavior's considerations.
class plQtAiPerceptionView : public plQtAiEngineView
{
public:
  plQtAiPerceptionView(plQtAiArchetypeAssetDocumentWindow* pWindow);
  ~plQtAiPerceptionView();

protected:
  virtual void BuildContent(plUiPanel* pRoot, const plVec2& vSize) override;
  virtual bool IsRebuildBlocked() const override;

private:
  void DrawTimeline(float fWidth, float fHeight);
  float BuildPeek(plUiElement* pBody, float fWidth);

  plQtAiArchetypeAssetDocumentWindow* m_pWindow = nullptr;
};
