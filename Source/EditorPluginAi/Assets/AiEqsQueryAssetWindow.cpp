#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEqsQueryAsset.h>
#include <EditorPluginAi/Assets/AiEqsQueryAssetWindow.h>
#include <EditorPluginAi/Assets/AiEqsQueryViews.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <AiPlugin/Eqs/EqsContext.h>
#include <AiPlugin/Eqs/EqsGenerator.h>
#include <AiPlugin/Eqs/EqsTest.h>

#include <Foundation/Math/ColorScheme.h>
#include <GuiFoundation/EngineUi/EngineCurveWindow.h>
#include <GuiFoundation/UIServices/DynamicStringEnum.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

plAiEqsQueryAssetDocumentWindow::plAiEqsQueryAssetDocumentWindow(plDocument* pDocument)
  : plAiAssetDocumentWindow(pDocument, "AiEqsQueryAsset", "AiEqsQuery")
{
  m_pEqsDocument = static_cast<plAiEqsQueryAssetDocument*>(pDocument);

  // center: header, context strip, generator card, test cards and the status row; right: the query preview, with the
  // raw properties
  m_pQueryView = AddPanel<plAiEqsQueryView>({"AiEqsQueryAssetDockWidget", "EQS Query"}, this);
  m_pPreviewView = AddPanel<plAiEqsPreviewView>({"AiEqsQueryPreviewDockWidget", "Query Preview", plEditorUiDockRole::Right}, this);
  AddRawPropertiesPanel("AiEqsQueryRawPropertyDockWidget");

  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);
}

plAiEqsQueryAssetDocumentWindow::~plAiEqsQueryAssetDocumentWindow()
{
  // the views call back into this window
  DestroyPanels();
}

const plDocumentObject* plAiEqsQueryAssetDocumentWindow::GetPropertiesObject() const
{
  return GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
}

const plDocumentObject* plAiEqsQueryAssetDocumentWindow::GetGeneratorObject() const
{
  return GetDocument()->GetObjectAccessor()->GetChildObjectByName(GetPropertiesObject(), "Generator", plVariant());
}

void plAiEqsQueryAssetDocumentWindow::GetArrayGuids(const char* szProperty, plDynamicArray<plUuid>& out_guids) const
{
  out_guids.Clear();

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();

  plDynamicArray<plVariant> values;
  pAccessor->GetValuesByName(GetPropertiesObject(), szProperty, values).IgnoreResult();

  for (const plVariant& v : values)
  {
    if (v.IsA<plUuid>())
      out_guids.PushBack(v.Get<plUuid>());
  }
}

const plDocumentObject* plAiEqsQueryAssetDocumentWindow::GetArrayObject(const char* szProperty, const plUuid& guid, plInt32* out_pIndex) const
{
  plDynamicArray<plUuid> guids;
  GetArrayGuids(szProperty, guids);

  if (out_pIndex != nullptr)
    *out_pIndex = -1;

  for (plUInt32 i = 0; i < guids.GetCount(); ++i)
  {
    if (guids[i] == guid)
    {
      if (out_pIndex != nullptr)
        *out_pIndex = static_cast<plInt32>(i);

      return GetDocument()->GetObjectAccessor()->GetObject(guid);
    }
  }

  return nullptr;
}

void plAiEqsQueryAssetDocumentWindow::SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription)
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

void plAiEqsQueryAssetDocumentWindow::SetGeneratorValue(const char* szProperty, const plVariant& value, const char* szDescription)
{
  const plDocumentObject* pGenerator = GetGeneratorObject();

  if (pGenerator == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction(szDescription);

  if (pAccessor->SetValueByName(pGenerator, szProperty, value).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiEqsQueryAssetDocumentWindow::OnAssetPropertiesChanged()
{
  m_pQueryView->Invalidate();
  m_pPreviewView->Recompute();
}

void plAiEqsQueryAssetDocumentWindow::OnAssetStructureChanged()
{
  // keep the selection valid
  plDynamicArray<plUuid> guids;
  GetArrayGuids("Tests", guids);

  if (!guids.Contains(m_SelectedObject))
    m_SelectedObject = plUuid();

  m_pQueryView->Invalidate();
  m_pPreviewView->Invalidate();
}

void plAiEqsQueryAssetDocumentWindow::SelectObject(const plUuid& guid)
{
  m_SelectedObject = guid;

  if (const plDocumentObject* pObject = GetDocument()->GetObjectAccessor()->GetObject(guid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObject);
  }

  m_pQueryView->Invalidate();
}

void plAiEqsQueryAssetDocumentWindow::SelectGenerator()
{
  if (const plDocumentObject* pGenerator = GetGeneratorObject())
  {
    GetDocument()->GetSelectionManager()->SetSelection(pGenerator);
  }
}

void plAiEqsQueryAssetDocumentWindow::SelectContextSlot(const plUuid& guid)
{
  if (const plDocumentObject* pObject = GetDocument()->GetObjectAccessor()->GetObject(guid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObject);
  }
}

void plAiEqsQueryAssetDocumentWindow::GetContextSlotNames(plDynamicArray<plString>& out_names) const
{
  out_names.Clear();

  const plAiEqsQueryAssetObject* pProps = m_pEqsDocument->GetProperties();

  for (const plAiEqsContextSlotObject* pSlot : pProps->m_ContextSlots)
  {
    if (pSlot != nullptr && !pSlot->m_sName.IsEmpty())
    {
      out_names.PushBack(pSlot->m_sName);
    }
  }
}

void plAiEqsQueryAssetDocumentWindow::RemoveTest(const plUuid& guid)
{
  const plDocumentObject* pObject = GetArrayObject("Tests", guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Remove Test");

  if (pAccessor->RemoveObject(pObject).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiEqsQueryAssetDocumentWindow::MoveTest(const plUuid& guid, plInt32 iDirection)
{
  plInt32 iIndex = -1;
  const plDocumentObject* pObject = GetArrayObject("Tests", guid, &iIndex);
  if (pObject == nullptr || iIndex < 0)
    return;

  plDynamicArray<plUuid> guids;
  GetArrayGuids("Tests", guids);

  if (iDirection < 0 && iIndex == 0)
    return;
  if (iDirection > 0 && iIndex + 1 >= static_cast<plInt32>(guids.GetCount()))
    return;

  // MoveObject uses insert-before semantics: moving down needs +2
  const plInt32 iNewIndex = (iDirection < 0) ? (iIndex - 1) : (iIndex + 2);

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Reorder Tests");

  if (pAccessor->MoveObjectByName(pObject, GetPropertiesObject(), "Tests", plVariant(iNewIndex)).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiEqsQueryAssetDocumentWindow::SetTestWeight(const plUuid& guid, double fValue)
{
  SetTestProperty(guid, "Weight", static_cast<float>(fValue));
}

void plAiEqsQueryAssetDocumentWindow::SetTestProperty(const plUuid& guid, const char* szProperty, const plVariant& value)
{
  const plDocumentObject* pObject = GetArrayObject("Tests", guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  const plDocumentObject* pTest = pAccessor->GetChildObjectByName(pObject, "Test", plVariant());
  if (pTest == nullptr)
    return;

  pAccessor->StartTransaction("Change EQS Test");

  if (pAccessor->SetValueByName(pTest, szProperty, value).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiEqsQueryAssetDocumentWindow::OpenCurveEditor(const plUuid& guid)
{
  const plDocumentObject* pObject = GetArrayObject("Tests", guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  const plDocumentObject* pCurve = pAccessor->GetChildObjectByName(pObject, "ScoreCurve", plVariant());
  if (pCurve == nullptr)
    return;

  plEngineCurveWindow::Options options;
  options.m_Color = plColorScheme::LightUI(plColorScheme::Blue);
  options.m_fLowerExtent = 0.0;
  options.m_bLowerExtentFixed = true;
  options.m_fUpperExtent = 1.0;
  options.m_bUpperExtentFixed = true;
  options.m_fLowerRange = 0.0;
  options.m_fUpperRange = 1.0;
  plEngineCurveWindow::Edit(pAccessor, pCurve, options);
}

void plAiEqsQueryAssetDocumentWindow::ApplyCurvePreset(const plUuid& guid, plUInt32 uiPreset)
{
  if (uiPreset >= plAiEqsUi::GetCurvePresetCount())
    return;

  plVec2d points[4];
  bool bSmooth = false;
  const plUInt32 uiPointCount = plAiEqsUi::GetCurvePresetPoints(uiPreset, points, PL_ARRAY_SIZE(points), bSmooth);

  const plDocumentObject* pObject = GetArrayObject("Tests", guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  const plDocumentObject* pCurve = pAccessor->GetChildObjectByName(pObject, "ScoreCurve", plVariant());
  if (pCurve == nullptr)
    return;

  pAccessor->StartTransaction("Apply Curve Preset");

  bool bOk = true;

  // remove existing control points
  {
    plDynamicArray<plVariant> values;
    pAccessor->GetValuesByName(pCurve, "ControlPoints", values).IgnoreResult();

    for (plUInt32 i = values.GetCount(); i > 0; --i)
    {
      if (values[i - 1].IsA<plUuid>())
      {
        if (const plDocumentObject* pCp = pAccessor->GetObject(values[i - 1].Get<plUuid>()))
        {
          bOk &= pAccessor->RemoveObject(pCp).Succeeded();
        }
      }
    }
  }

  // insert the preset points (ticks: 4800 per second, extents locked to 0..1)
  const plInt64 iTangentMode = bSmooth ? plCurveTangentMode::Auto : plCurveTangentMode::Linear;

  for (plUInt32 i = 0; i < uiPointCount && bOk; ++i)
  {
    plUuid cpGuid = plUuid::MakeUuid();
    bOk &= pAccessor->AddObjectByName(pCurve, "ControlPoints", i, plCurveControlPointData::GetStaticRTTI(), cpGuid).Succeeded();

    if (const plDocumentObject* pCp = pAccessor->GetObject(cpGuid))
    {
      bOk &= pAccessor->SetValueByName(pCp, "Tick", plVariant(static_cast<plInt64>(points[i].x * 4800.0 + 0.5))).Succeeded();
      bOk &= pAccessor->SetValueByName(pCp, "Value", plVariant(points[i].y)).Succeeded();
      bOk &= pAccessor->SetValueByName(pCp, "LeftTangentMode", plVariant(iTangentMode)).Succeeded();
      bOk &= pAccessor->SetValueByName(pCp, "RightTangentMode", plVariant(iTangentMode)).Succeeded();
    }
    else
    {
      bOk = false;
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plAiEqsQueryAssetDocumentWindow::AddTestOfType(const plRTTI* pChosen)
{
  if (pChosen == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Add Test");

  bool bOk = true;

  plUuid testGuid = plUuid::MakeUuid();
  bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "Tests", pAccessor->GetCountByName(GetPropertiesObject(), "Tests"), plAiEqsTestObject::GetStaticRTTI(), testGuid).Succeeded();

  if (bOk)
  {
    if (const plDocumentObject* pTestObj = pAccessor->GetObject(testGuid))
    {
      plUuid innerGuid = plUuid::MakeUuid();
      bOk &= pAccessor->AddObjectByName(pTestObj, "Test", plVariant(), pChosen, innerGuid).Succeeded();
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
    SelectObject(testGuid);
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plAiEqsQueryAssetDocumentWindow::ChangeGeneratorTypeTo(const plRTTI* pChosen)
{
  if (pChosen == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Change Generator");

  bool bOk = true;

  if (const plDocumentObject* pGenerator = GetGeneratorObject())
  {
    bOk &= pAccessor->RemoveObject(pGenerator).Succeeded();
  }

  plUuid genGuid = plUuid::MakeUuid();
  bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "Generator", plVariant(), pChosen, genGuid).Succeeded();

  if (bOk)
  {
    pAccessor->FinishTransaction();
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plAiEqsQueryAssetDocumentWindow::AddContextSlotOfType(const plRTTI* pChosen)
{
  if (pChosen == nullptr)
    return;

  // default slot name: 'Threat' for the perceived target (the common case), else derived from the type
  plStringBuilder sDefaultName;

  if (pChosen == plGetStaticRTTI<plAiEqsContext_PerceivedTarget>())
  {
    sDefaultName = "Threat";
  }
  else
  {
    sDefaultName = pChosen->GetTypeName();
    sDefaultName.TrimWordStart("plAiEqsContext_");
  }

  // avoid duplicate names
  {
    plDynamicArray<plString> names;
    GetContextSlotNames(names);

    plStringBuilder sUnique = sDefaultName;
    plUInt32 uiSuffix = 2;

    while (names.Contains(plString(sUnique)) || sUnique == "Querier")
    {
      sUnique.SetFormat("{}{}", sDefaultName, uiSuffix);
      ++uiSuffix;
    }

    sDefaultName = sUnique;
  }

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Add Context");

  bool bOk = true;

  plUuid slotGuid = plUuid::MakeUuid();
  bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "ContextSlots", pAccessor->GetCountByName(GetPropertiesObject(), "ContextSlots"), plAiEqsContextSlotObject::GetStaticRTTI(), slotGuid).Succeeded();

  if (bOk)
  {
    if (const plDocumentObject* pSlotObj = pAccessor->GetObject(slotGuid))
    {
      bOk &= pAccessor->SetValueByName(pSlotObj, "Name", plVariant(sDefaultName.GetData())).Succeeded();

      plUuid ctxGuid = plUuid::MakeUuid();
      bOk &= pAccessor->AddObjectByName(pSlotObj, "Context", plVariant(), pChosen, ctxGuid).Succeeded();
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
    SelectContextSlot(slotGuid);
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plAiEqsQueryAssetDocumentWindow::RemoveContextSlot(const plUuid& guid)
{
  const plDocumentObject* pObject = GetArrayObject("ContextSlots", guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Remove Context");

  if (pAccessor->RemoveObject(pObject).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}
