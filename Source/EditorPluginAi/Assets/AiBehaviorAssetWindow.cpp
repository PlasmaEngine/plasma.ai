#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiBehaviorAsset.h>
#include <EditorPluginAi/Assets/AiBehaviorAssetWindow.h>
#include <EditorPluginAi/Assets/AiBehaviorViews.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <AiPlugin/UtilityAI/Decision/AiBehaviorLogic.h>
#include <AiPlugin/UtilityAI/Decision/AiInput.h>

#include <EditorFramework/Assets/EngineAssetPicker.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/EditorApp/EditorApp.h>
#include <Foundation/Math/ColorScheme.h>
#include <GuiFoundation/EngineUi/EngineCurveWindow.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

namespace
{
  struct CurvePresetPoint
  {
    double x;
    double v;
  };

  struct CurvePreset
  {
    const char* szName;
    bool bSmooth;
    plUInt32 uiCount;
    CurvePresetPoint pts[4];
  };

  static const CurvePreset s_CurvePresets[] = {
    {"Linear Rise", false, 2, {{0.0, 0.0}, {1.0, 1.0}}},
    {"Linear Fall", false, 2, {{0.0, 1.0}, {1.0, 0.0}}},
    {"Smooth Rise", true, 2, {{0.0, 0.0}, {1.0, 1.0}}},
    {"Smooth Fall", true, 2, {{0.0, 1.0}, {1.0, 0.0}}},
    {"Gate (floor 0.05)", false, 4, {{0.0, 0.05}, {0.4, 0.05}, {0.75, 1.0}, {1.0, 1.0}}},
    {"Bias (floor 0.35)", true, 2, {{0.0, 1.0}, {1.0, 0.35}}},
    {"Bell", true, 3, {{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}}},
    {"Constant 1", false, 2, {{0.0, 1.0}, {1.0, 1.0}}},
    {"Clear (use normalized input)", false, 0, {}},
  };
} // namespace

plAiBehaviorAssetDocumentWindow::plAiBehaviorAssetDocumentWindow(plDocument* pDocument)
  : plAiAssetDocumentWindow(pDocument, "AiBehaviorAsset", "AiBehavior")
{
  m_pBehaviorDocument = static_cast<plAiBehaviorAssetDocument*>(pDocument);

  // center: header and consideration cards; right: the score preview, with the raw properties
  m_pBehaviorView = AddPanel<plAiBehaviorView>({"AiBehaviorAssetDockWidget", "Behavior"}, this);
  m_pScoreView = AddPanel<plAiScorePreviewView>({"AiBehaviorScorePreviewDockWidget", "Score Preview", plEditorUiDockRole::Right}, this);
  AddRawPropertiesPanel("AiBehaviorRawPropertyDockWidget");

  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);
}

plAiBehaviorAssetDocumentWindow::~plAiBehaviorAssetDocumentWindow()
{
  // the views call back into this window
  DestroyPanels();
}

const plDocumentObject* plAiBehaviorAssetDocumentWindow::GetPropertiesObject() const
{
  return GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
}

void plAiBehaviorAssetDocumentWindow::GetConsiderationGuids(plDynamicArray<plUuid>& out_guids) const
{
  out_guids.Clear();

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();

  plDynamicArray<plVariant> values;
  pAccessor->GetValuesByName(GetPropertiesObject(), "Considerations", values).IgnoreResult();

  for (const plVariant& v : values)
  {
    if (v.IsA<plUuid>())
      out_guids.PushBack(v.Get<plUuid>());
  }
}

const plDocumentObject* plAiBehaviorAssetDocumentWindow::GetConsiderationObject(const plUuid& guid, plInt32* out_pIndex) const
{
  plDynamicArray<plUuid> guids;
  GetConsiderationGuids(guids);

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

void plAiBehaviorAssetDocumentWindow::SetRootValue(const char* szProperty, const plVariant& value, const char* szDescription)
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

void plAiBehaviorAssetDocumentWindow::OnAssetPropertiesChanged()
{
  m_pBehaviorView->Invalidate();
  m_pScoreView->Recompute();
}

void plAiBehaviorAssetDocumentWindow::OnAssetStructureChanged()
{
  // keep the selection valid
  plDynamicArray<plUuid> guids;
  GetConsiderationGuids(guids);

  if (!guids.Contains(m_SelectedConsideration))
    m_SelectedConsideration = plUuid();

  m_pBehaviorView->Invalidate();
  m_pScoreView->ResetSliders();
}

void plAiBehaviorAssetDocumentWindow::SelectConsideration(const plUuid& guid)
{
  m_SelectedConsideration = guid;

  if (const plDocumentObject* pObject = GetConsiderationObject(guid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObject);
  }

  m_pBehaviorView->Invalidate();
}

void plAiBehaviorAssetDocumentWindow::RemoveConsideration(const plUuid& guid)
{
  const plDocumentObject* pObject = GetConsiderationObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Remove Consideration");

  if (pAccessor->RemoveObject(pObject).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiBehaviorAssetDocumentWindow::MoveConsideration(const plUuid& guid, plInt32 iDirection)
{
  plInt32 iIndex = -1;
  const plDocumentObject* pObject = GetConsiderationObject(guid, &iIndex);
  if (pObject == nullptr || iIndex < 0)
    return;

  plDynamicArray<plUuid> guids;
  GetConsiderationGuids(guids);

  if (iDirection < 0 && iIndex == 0)
    return;
  if (iDirection > 0 && iIndex + 1 >= static_cast<plInt32>(guids.GetCount()))
    return;

  // MoveObject uses insert-before semantics: moving down needs +2
  const plInt32 iNewIndex = (iDirection < 0) ? (iIndex - 1) : (iIndex + 2);

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Reorder Considerations");

  if (pAccessor->MoveObjectByName(pObject, GetPropertiesObject(), "Considerations", plVariant(iNewIndex)).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiBehaviorAssetDocumentWindow::SetConsiderationRange(const plUuid& guid, bool bMin, double fValue)
{
  const plDocumentObject* pObject = GetConsiderationObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction(bMin ? "Change Input Min" : "Change Input Max");

  if (pAccessor->SetValueByName(pObject, bMin ? "InputMin" : "InputMax", plVariant(static_cast<float>(fValue))).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiBehaviorAssetDocumentWindow::OpenCurveEditor(const plUuid& guid)
{
  const plDocumentObject* pObject = GetConsiderationObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  const plDocumentObject* pCurve = pAccessor->GetChildObjectByName(pObject, "ResponseCurve", plVariant());
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

plUInt32 plAiBehaviorAssetDocumentWindow::GetCurvePresetCount()
{
  return PL_ARRAY_SIZE(s_CurvePresets);
}

const char* plAiBehaviorAssetDocumentWindow::GetCurvePresetName(plUInt32 uiPreset)
{
  return s_CurvePresets[uiPreset].szName;
}

void plAiBehaviorAssetDocumentWindow::ApplyCurvePreset(const plUuid& guid, plUInt32 uiPreset)
{
  if (uiPreset >= PL_ARRAY_SIZE(s_CurvePresets))
    return;

  const CurvePreset& preset = s_CurvePresets[uiPreset];

  const plDocumentObject* pObject = GetConsiderationObject(guid);
  if (pObject == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  const plDocumentObject* pCurve = pAccessor->GetChildObjectByName(pObject, "ResponseCurve", plVariant());
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

  // insert the preset points (ticks: 4800 per second, extents locked to 0..1 seconds)
  const plInt64 iTangentMode = preset.bSmooth ? plCurveTangentMode::Auto : plCurveTangentMode::Linear;

  for (plUInt32 i = 0; i < preset.uiCount && bOk; ++i)
  {
    plUuid cpGuid = plUuid::MakeUuid();
    bOk &= pAccessor->AddObjectByName(pCurve, "ControlPoints", i, plCurveControlPointData::GetStaticRTTI(), cpGuid).Succeeded();

    if (const plDocumentObject* pCp = pAccessor->GetObject(cpGuid))
    {
      bOk &= pAccessor->SetValueByName(pCp, "Tick", plVariant(static_cast<plInt64>(preset.pts[i].x * 4800.0 + 0.5))).Succeeded();
      bOk &= pAccessor->SetValueByName(pCp, "Value", plVariant(preset.pts[i].v)).Succeeded();
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

void plAiBehaviorAssetDocumentWindow::AddConsiderationOfType(const plRTTI* pChosen)
{
  if (pChosen == nullptr)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Add Consideration");

  bool bOk = true;

  plUuid consGuid = plUuid::MakeUuid();
  bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "Considerations", pAccessor->GetCountByName(GetPropertiesObject(), "Considerations"), plAiConsiderationObject::GetStaticRTTI(), consGuid).Succeeded();

  if (bOk)
  {
    if (const plDocumentObject* pConsObj = pAccessor->GetObject(consGuid))
    {
      plUuid inputGuid = plUuid::MakeUuid();
      bOk &= pAccessor->AddObjectByName(pConsObj, "Input", plVariant(), pChosen, inputGuid).Succeeded();
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
    SelectConsideration(consGuid);
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plAiBehaviorAssetDocumentWindow::ChangeLogicType(plInt32 iType)
{
  const plAiBehaviorAssetObject* pProps = m_pBehaviorDocument->GetProperties();

  // no-op when the type already matches
  plInt32 iCurrent = 0;
  if (plDynamicCast<const plAiBehaviorLogic_StateMachine*>(pProps->m_pLogic) != nullptr)
    iCurrent = 1;
  else if (plDynamicCast<const plAiBehaviorLogic_ActionQueue*>(pProps->m_pLogic) != nullptr)
    iCurrent = 2;

  if (iCurrent == iType)
    return;

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Change Behavior Logic");

  bool bOk = true;

  if (const plDocumentObject* pLogic = pAccessor->GetChildObjectByName(GetPropertiesObject(), "Logic", plVariant()))
  {
    bOk &= pAccessor->RemoveObject(pLogic).Succeeded();
  }

  const plRTTI* pType = nullptr;
  if (iType == 1)
    pType = plGetStaticRTTI<plAiBehaviorLogic_StateMachine>();
  else if (iType == 2)
    pType = plGetStaticRTTI<plAiBehaviorLogic_ActionQueue>();

  if (pType != nullptr && bOk)
  {
    plUuid guid = plUuid::MakeUuid();
    bOk &= pAccessor->AddObjectByName(GetPropertiesObject(), "Logic", plVariant(), pType, guid).Succeeded();
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

void plAiBehaviorAssetDocumentWindow::BrowseStateMachine()
{
  const plAiBehaviorAssetObject* pProps = m_pBehaviorDocument->GetProperties();
  const plAiBehaviorLogic_StateMachine* pSm = plDynamicCast<const plAiBehaviorLogic_StateMachine*>(pProps->m_pLogic);
  if (pSm == nullptr)
    return;

  plUuid currentGuid;
  if (plConversionUtils::IsStringUuid(pSm->GetStateMachineFile()))
  {
    currentGuid = plConversionUtils::ConvertStringToUuid(pSm->GetStateMachineFile());
  }

  plEngineAssetPicker::Result picked;
  if (!plEngineAssetPicker::PickAsset(nullptr, currentGuid, "CompatibleAsset_StateMachine", picked) || !picked.m_Guid.IsValid())
    return;

  plStringBuilder sGuid;
  plConversionUtils::ToString(picked.m_Guid, sGuid);

  plObjectAccessorBase* pAccessor = GetDocument()->GetObjectAccessor();

  const plDocumentObject* pLogic = pAccessor->GetChildObjectByName(GetPropertiesObject(), "Logic", plVariant());
  if (pLogic == nullptr)
    return;

  pAccessor->StartTransaction("Set State Machine");

  if (pAccessor->SetValueByName(pLogic, "StateMachine", plVariant(sGuid.GetData())).Failed())
  {
    pAccessor->CancelTransaction();
  }
  else
  {
    pAccessor->FinishTransaction();
  }
}

void plAiBehaviorAssetDocumentWindow::OpenStateMachine()
{
  const plAiBehaviorAssetObject* pProps = m_pBehaviorDocument->GetProperties();
  const plAiBehaviorLogic_StateMachine* pSm = plDynamicCast<const plAiBehaviorLogic_StateMachine*>(pProps->m_pLogic);
  if (pSm == nullptr || plStringUtils::IsNullOrEmpty(pSm->GetStateMachineFile()))
    return;

  auto asset = plAssetCurator::GetSingleton()->FindSubAsset(pSm->GetStateMachineFile());
  if (!asset.isValid() || asset->m_pAssetInfo == nullptr)
    return;

  plEditorApp::GetSingleton()->OpenDocumentQueued(asset->m_pAssetInfo->m_Path.GetAbsolutePath());
}
