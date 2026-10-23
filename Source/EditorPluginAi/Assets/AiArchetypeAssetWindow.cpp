#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiArchetypeAsset.h>
#include <EditorPluginAi/Assets/AiArchetypeAssetWindow.h>
#include <EditorPluginAi/Assets/AiArchetypeViews.h>

#include <AiPlugin/UtilityAI/Decision/AiScoring.h>

#include <EditorFramework/Assets/EngineAssetPicker.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/EditorApp/EditorApp.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

plAiArchetypeAssetDocumentWindow::plAiArchetypeAssetDocumentWindow(plDocument* pDocument)
  : plAiAssetDocumentWindow(pDocument, "AiArchetypeAsset", "AiArchetype")
{
  m_pArchetypeDocument = static_cast<plAiArchetypeAssetDocument*>(pDocument);

  // center: header and the behavior stack; right: perception and peek, with the raw properties
  m_pStackView = AddPanel<plAiArchetypeStackView>({"AiArchetypeAssetDockWidget", "Behavior stack"}, this);
  m_pPerceptionView = AddPanel<plAiPerceptionView>({"AiArchetypeInsightDockWidget", "Perception & Peek", plEditorUiDockRole::Right}, this);
  AddRawPropertiesPanel("AiArchetypeRawPropertyDockWidget");

  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);
}

plAiArchetypeAssetDocumentWindow::~plAiArchetypeAssetDocumentWindow()
{
  // the views call back into this window
  DestroyPanels();
}

void plAiArchetypeAssetDocumentWindow::InvalidateViews()
{
  m_pStackView->Invalidate();
  m_pPerceptionView->Invalidate();
}

const plDocumentObject* plAiArchetypeAssetDocumentWindow::GetPropertiesObject() const
{
  return GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
}

void plAiArchetypeAssetDocumentWindow::GetBehaviorGuids(plDynamicArray<plUuid>& out_guids) const
{
  out_guids.Clear();

  plDynamicArray<plVariant> values;
  GetDocument()->GetObjectAccessor()->GetValuesByName(GetPropertiesObject(), "Behaviors", values).IgnoreResult();

  for (const plVariant& v : values)
  {
    if (v.IsA<plUuid>())
      out_guids.PushBack(v.Get<plUuid>());
  }
}

const char* plAiArchetypeAssetDocumentWindow::GetBehaviorRef(plUInt32 uiIndex) const
{
  const auto& behaviors = m_pArchetypeDocument->GetProperties()->m_Behaviors;
  if (uiIndex >= behaviors.GetCount())
    return "";

  return behaviors[uiIndex].m_sBehavior;
}

void plAiArchetypeAssetDocumentWindow::SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription)
{
  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction(szDescription);

  if (pAccessor->SetValueByName(GetPropertiesObject(), szProperty, value).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiArchetypeAssetDocumentWindow::OnAssetPropertiesChanged()
{
  InvalidateViews();
}

void plAiArchetypeAssetDocumentWindow::OnAssetStructureChanged()
{
  // keep the selection valid
  plDynamicArray<plUuid> guids;
  GetBehaviorGuids(guids);

  if (!guids.Contains(m_SelectedRow))
    m_SelectedRow = plUuid();

  InvalidateViews();
}

void plAiArchetypeAssetDocumentWindow::SelectBehaviorRow(const plUuid& guid)
{
  m_SelectedRow = guid;

  if (const plDocumentObject* pObject = GetDocument()->GetObjectAccessor()->GetObject(guid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObject);
  }

  InvalidateViews();
}

void plAiArchetypeAssetDocumentWindow::SetWeightScale(const plUuid& guid, double fValue)
{
  const plDocumentObject* pObject = GetDocument()->GetObjectAccessor()->GetObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Change Weight Scale");

  if (pAccessor->SetValueByName(pObject, "WeightScale", plVariant(static_cast<float>(fValue))).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiArchetypeAssetDocumentWindow::OpenBehavior(const plUuid& guid)
{
  plDynamicArray<plUuid> guids;
  GetBehaviorGuids(guids);

  for (plUInt32 i = 0; i < guids.GetCount(); ++i)
  {
    if (guids[i] != guid)
      continue;

    const plAiBehaviorPeek& peek = plAiBehaviorPeekCache::Get(GetBehaviorRef(i));
    if (peek.m_bValid && !peek.m_sAbsPath.IsEmpty())
    {
      plEditorApp::GetSingleton()->OpenDocumentQueued(peek.m_sAbsPath);
    }
    return;
  }
}

void plAiArchetypeAssetDocumentWindow::RemoveBehavior(const plUuid& guid)
{
  const plDocumentObject* pObject = GetDocument()->GetObjectAccessor()->GetObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Remove Behavior");

  if (pAccessor->RemoveObject(pObject).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiArchetypeAssetDocumentWindow::AddBehavior()
{
  plEngineAssetPicker::Result picked;
  if (!plEngineAssetPicker::PickAsset(nullptr, plUuid(), "CompatibleAsset_AiBehavior", picked) || !picked.m_Guid.IsValid())
    return;

  plStringBuilder sGuid;
  plConversionUtils::ToString(picked.m_Guid, sGuid);

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Add Behavior");

  bool bOk = true;

  plUuid rowGuid = plUuid::MakeUuid();
  bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "Behaviors", pAccessor->GetCountByName(GetPropertiesObject(), "Behaviors"), plGetStaticRTTI<plAiArchetypeBehaviorObject>(), rowGuid).Succeeded();

  if (bOk)
  {
    if (const plDocumentObject* pRow = pAccessor->GetObject(rowGuid))
    {
      bOk &= pAccessor->SetValueByName(pRow, "Behavior", plVariant(sGuid.GetData())).Succeeded();
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
    SelectBehaviorRow(rowGuid);
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plAiArchetypeAssetDocumentWindow::BrowseBlackboardTemplate()
{
  const plAiArchetypeAssetObject* pProps = m_pArchetypeDocument->GetProperties();

  plUuid currentGuid;
  if (plConversionUtils::IsStringUuid(pProps->m_sBlackboardTemplate))
  {
    currentGuid = plConversionUtils::ConvertStringToUuid(pProps->m_sBlackboardTemplate);
  }

  plEngineAssetPicker::Result picked;
  if (!plEngineAssetPicker::PickAsset(nullptr, currentGuid, "CompatibleAsset_BlackboardTemplate", picked) || !picked.m_Guid.IsValid())
    return;

  plStringBuilder sGuid;
  plConversionUtils::ToString(picked.m_Guid, sGuid);

  SetRootValue("BlackboardTemplate", plVariant(sGuid.GetData()), "Set Blackboard Template");
}
