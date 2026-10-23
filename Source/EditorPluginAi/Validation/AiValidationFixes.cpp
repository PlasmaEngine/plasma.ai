#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiArchetypeAsset.h>
#include <EditorPluginAi/Validation/AiValidationFixes.h>

#include <AiPlugin/UtilityAI/Decision/AiBehaviorLogic.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/Assets/AssetDocumentManager.h>
#include <EditorFramework/Assets/EngineAssetPicker.h>
#include <EditorFramework/EditorApp/EditorApp.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GameEngine/Gameplay/BlackboardComponent.h>
#include <ToolsFoundation/Document/DocumentManager.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

namespace
{
  /// The asset the fix edits or opens: a template or state machine named by the fix, else the issue's asset.
  plStringView GetTargetGuid(const plAiValidationIssue& issue, const plAiValidationFix& fix)
  {
    if (!fix.m_sTargetAsset.IsEmpty())
      return fix.m_sTargetAsset;

    return issue.m_sAssetGuid;
  }

  plString GetAbsolutePath(const plUuid& guid)
  {
    auto asset = plAssetCurator::GetSingleton()->GetSubAsset(guid);
    if (!asset.isValid() || asset->m_pAssetInfo == nullptr)
      return {};

    return asset->m_pAssetInfo->m_Path.GetAbsolutePath();
  }

  /// The asset's document, opened in its window when it is not open yet.
  plDocument* OpenAssetDocument(const plUuid& guid)
  {
    if (plDocument* pOpen = plDocumentManager::GetDocumentByGuid(guid))
    {
      pOpen->GetDocumentManager()->EnsureWindowRequested(pOpen);
      return pOpen;
    }

    plAssetDocumentManager* pManager = nullptr;
    plString sDocumentType;
    plString sPath;
    {
      auto asset = plAssetCurator::GetSingleton()->GetSubAsset(guid);
      if (!asset.isValid() || asset->m_pAssetInfo == nullptr)
        return nullptr;

      pManager = asset->m_pAssetInfo->GetManager();
      sDocumentType = asset->m_Data.m_sSubAssetsDocumentTypeName.GetView();
      sPath = asset->m_pAssetInfo->m_Path.GetAbsolutePath();
    }

    plDocument* pDocument = nullptr;
    if (pManager == nullptr || pManager->OpenDocument(sDocumentType, sPath, pDocument).Failed())
      return nullptr;

    return pDocument;
  }

  const plDocumentObject* GetRoot(const plDocument* pDocument)
  {
    const auto& children = pDocument->GetObjectManager()->GetRootObject()->GetChildren();
    return children.IsEmpty() ? nullptr : children[0];
  }

  const plDocumentObject* GetArrayElement(const plDocument* pDocument, const plDocumentObject* pParent, plStringView sProperty, plUInt32 uiIndex)
  {
    if (pParent == nullptr)
      return nullptr;

    plDynamicArray<plVariant> values;
    pDocument->GetObjectAccessor()->GetValuesByName(pParent, sProperty, values).IgnoreResult();

    if (uiIndex >= values.GetCount() || !values[uiIndex].IsA<plUuid>())
      return nullptr;

    return pDocument->GetObjectAccessor()->GetObject(values[uiIndex].Get<plUuid>());
  }

  const plDocumentObject* GetChild(const plDocument* pDocument, const plDocumentObject* pParent, plStringView sProperty)
  {
    if (pParent == nullptr)
      return nullptr;

    return pDocument->GetObjectAccessor()->GetChildObjectByName(pParent, sProperty, plVariant());
  }

  /// Sets one property as one undoable step.
  plStatus SetValue(const plDocument* pDocument, const plDocumentObject* pObject, plStringView sProperty, const plVariant& value, plStringView sDescription)
  {
    if (pObject == nullptr)
      return plStatus("The element no longer exists.");

    plObjectAccessorBase* pAccessor = pDocument->GetObjectAccessor();
    pAccessor->StartTransaction(sDescription);

    const plStatus status = pAccessor->SetValueByName(pObject, sProperty, value);

    if (status.Failed())
      pAccessor->CancelTransaction();
    else
      pAccessor->FinishTransaction();

    return status;
  }

  /// A picked asset's GUID string; false on Cancel.
  bool PickAsset(plStringView sFilter, plString& out_sGuid)
  {
    plEngineAssetPicker::Result picked;
    if (!plEngineAssetPicker::PickAsset(nullptr, plUuid(), sFilter, picked) || !picked.m_Guid.IsValid())
      return false;

    plStringBuilder sGuid;
    plConversionUtils::ToString(picked.m_Guid, sGuid);
    out_sGuid = sGuid;
    return true;
  }

  plStatus ChooseKey(const plDocument* pDocument, const plAiValidationIssue& issue, plStringView sKey)
  {
    const plDocumentObject* pConsideration = GetArrayElement(pDocument, GetRoot(pDocument), "Considerations", issue.m_Element.m_uiIndex);
    const plDocumentObject* pInput = GetChild(pDocument, pConsideration, "Input");

    return SetValue(pDocument, pInput, "Entry", plVariant(plString(sKey)), "Choose Blackboard Key");
  }

  bool TemplateHasKey(const plDocument* pDocument, const plDocumentObject* pRoot, plStringView sKey)
  {
    plObjectAccessorBase* pAccessor = pDocument->GetObjectAccessor();

    plDynamicArray<plVariant> entries;
    pAccessor->GetValuesByName(pRoot, "Entries", entries).IgnoreResult();

    for (const plVariant& entry : entries)
    {
      const plDocumentObject* pEntry = entry.IsA<plUuid>() ? pAccessor->GetObject(entry.Get<plUuid>()) : nullptr;
      if (pEntry == nullptr)
        continue;

      plVariant name;
      pAccessor->GetValueByName(pEntry, "Name", name).IgnoreResult();

      if (name.ConvertTo<plString>().GetView().IsEqual_NoCase(sKey))
        return true;
    }

    return false;
  }

  /// Adds the key with a value of the type the input reads; never a second key of the same name.
  plStatus AddKeyToTemplate(const plDocument* pTemplate, const plAiValidationFix& fix)
  {
    const plDocumentObject* pRoot = GetRoot(pTemplate);
    if (pRoot == nullptr)
      return plStatus("The template could not be read.");

    if (TemplateHasKey(pTemplate, pRoot, fix.m_sKey))
      return plStatus(plFmt("The template already has a key named {}.", fix.m_sKey));

    plHashedString sName;
    sName.Assign(fix.m_sKey);

    const plVariant initialValue = fix.m_sValueType == "Vec3" ? plVariant(plVec3::MakeZero()) : plVariant(0.0f);

    plObjectAccessorBase* pAccessor = pTemplate->GetObjectAccessor();
    pAccessor->StartTransaction("Add Blackboard Key");

    plUuid entryGuid = plUuid::MakeUuid();
    plStatus status = pAccessor->AddObjectByName(pRoot, "Entries", pAccessor->GetCountByName(pRoot, "Entries"), plGetStaticRTTI<plBlackboardEntry>(), entryGuid);

    const plDocumentObject* pEntry = status.Succeeded() ? pAccessor->GetObject(entryGuid) : nullptr;

    if (pEntry != nullptr)
      status = pAccessor->SetValueByName(pEntry, "Name", plVariant(sName));

    if (pEntry != nullptr && status.Succeeded())
      status = pAccessor->SetValueByName(pEntry, "InitialValue", initialValue);

    if (pEntry == nullptr || status.Failed())
    {
      pAccessor->CancelTransaction();
      return status.Failed() ? status : plStatus("The key could not be added.");
    }

    pAccessor->FinishTransaction();
    return plStatus(PL_SUCCESS);
  }

  plStatus ChooseContext(const plDocument* pDocument, const plAiValidationIssue& issue, plStringView sContext)
  {
    const plDocumentObject* pRoot = GetRoot(pDocument);
    const plVariant value(plString(sContext).GetData());

    if (issue.m_Element.m_Kind == plAiValidationElement::Generator)
      return SetValue(pDocument, GetChild(pDocument, pRoot, "Generator"), "CenterContext", value, "Choose Context");

    const plDocumentObject* pTestObject = GetArrayElement(pDocument, pRoot, "Tests", issue.m_Element.m_uiIndex);
    return SetValue(pDocument, GetChild(pDocument, pTestObject, "Test"), "Context", value, "Choose Context");
  }

  plStatus ChooseBehavior(const plDocument* pDocument, const plAiValidationIssue& issue)
  {
    plString sGuid;
    if (!PickAsset("CompatibleAsset_AiBehavior", sGuid))
      return plStatus(PL_SUCCESS);

    const plDocumentObject* pRow = GetArrayElement(pDocument, GetRoot(pDocument), "Behaviors", issue.m_Element.m_uiIndex);
    return SetValue(pDocument, pRow, "Behavior", plVariant(sGuid.GetData()), "Choose Behavior");
  }

  plStatus AddBehavior(const plDocument* pDocument)
  {
    plString sGuid;
    if (!PickAsset("CompatibleAsset_AiBehavior", sGuid))
      return plStatus(PL_SUCCESS);

    const plDocumentObject* pRoot = GetRoot(pDocument);
    if (pRoot == nullptr)
      return plStatus("The archetype could not be read.");

    plObjectAccessorBase* pAccessor = pDocument->GetObjectAccessor();
    pAccessor->StartTransaction("Add Behavior");

    plUuid rowGuid = plUuid::MakeUuid();
    plStatus status = pAccessor->AddObjectByName(pRoot, "Behaviors", pAccessor->GetCountByName(pRoot, "Behaviors"), plGetStaticRTTI<plAiArchetypeBehaviorObject>(), rowGuid);

    const plDocumentObject* pRow = status.Succeeded() ? pAccessor->GetObject(rowGuid) : nullptr;

    if (pRow != nullptr)
      status = pAccessor->SetValueByName(pRow, "Behavior", plVariant(sGuid.GetData()));

    if (pRow == nullptr || status.Failed())
    {
      pAccessor->CancelTransaction();
      return status.Failed() ? status : plStatus("The behavior could not be added.");
    }

    pAccessor->FinishTransaction();
    return plStatus(PL_SUCCESS);
  }

  plStatus ChooseTemplate(const plDocument* pDocument)
  {
    plString sGuid;
    if (!PickAsset("CompatibleAsset_BlackboardTemplate", sGuid))
      return plStatus(PL_SUCCESS);

    return SetValue(pDocument, GetRoot(pDocument), "BlackboardTemplate", plVariant(sGuid.GetData()), "Set Blackboard Template");
  }

  plStatus ChooseStateMachine(const plDocument* pDocument)
  {
    plString sGuid;
    if (!PickAsset("CompatibleAsset_StateMachine", sGuid))
      return plStatus(PL_SUCCESS);

    const plDocumentObject* pLogic = GetChild(pDocument, GetRoot(pDocument), "Logic");
    return SetValue(pDocument, pLogic, "StateMachine", plVariant(sGuid.GetData()), "Set State Machine");
  }

  plStatus AddStateMachineLogic(const plDocument* pDocument)
  {
    const plDocumentObject* pRoot = GetRoot(pDocument);
    if (pRoot == nullptr)
      return plStatus("The behavior could not be read.");

    if (GetChild(pDocument, pRoot, "Logic") != nullptr)
      return plStatus(PL_SUCCESS);

    plObjectAccessorBase* pAccessor = pDocument->GetObjectAccessor();
    pAccessor->StartTransaction("Change Behavior Logic");

    plUuid guid = plUuid::MakeUuid();
    const plStatus status = pAccessor->AddObjectByName(pRoot, "Logic", plVariant(), plGetStaticRTTI<plAiBehaviorLogic_StateMachine>(), guid);

    if (status.Failed())
      pAccessor->CancelTransaction();
    else
      pAccessor->FinishTransaction();

    return status;
  }

  plStatus OpenAsset(plStringView sAsset)
  {
    plUuid guid;
    if (!plConversionUtils::TryConvertStringToUuid(sAsset, guid))
      return plStatus("The asset does not exist.");

    const plString sPath = GetAbsolutePath(guid);
    if (sPath.IsEmpty())
      return plStatus("The asset does not exist.");

    plEditorApp::GetSingleton()->OpenDocumentQueued(sPath);
    return plStatus(PL_SUCCESS);
  }

  plStatus ApplyToDocument(const plDocument* pDocument, const plAiValidationIssue& issue, const plAiValidationFix& fix, plStringView sChoice)
  {
    switch (fix.m_Kind)
    {
      case plAiValidationFix::ChooseKey:
        return ChooseKey(pDocument, issue, sChoice);

      case plAiValidationFix::AddKeyToTemplate:
        return AddKeyToTemplate(pDocument, fix);

      case plAiValidationFix::AssignConfig:
        return SetValue(pDocument, GetRoot(pDocument), fix.m_sProperty, plVariant(plString(sChoice).GetData()), "Assign Navigation Config");

      case plAiValidationFix::ChooseContext:
        return ChooseContext(pDocument, issue, sChoice);

      case plAiValidationFix::ChooseBehavior:
        return ChooseBehavior(pDocument, issue);

      case plAiValidationFix::AddBehavior:
        return AddBehavior(pDocument);

      case plAiValidationFix::ChooseTemplate:
        return ChooseTemplate(pDocument);

      case plAiValidationFix::ChooseStateMachine:
        return ChooseStateMachine(pDocument);

      case plAiValidationFix::AddStateMachineLogic:
        return AddStateMachineLogic(pDocument);

      case plAiValidationFix::SetWeight:
      {
        double fWeight = 1.0;
        plConversionUtils::StringToFloat(sChoice, fWeight).IgnoreResult();
        return SetValue(pDocument, GetRoot(pDocument), "Weight", plVariant(static_cast<float>(fWeight)), "Change Weight");
      }

      case plAiValidationFix::SetWeightScale:
      {
        double fScale = 1.0;
        plConversionUtils::StringToFloat(sChoice, fScale).IgnoreResult();

        const plDocumentObject* pRow = GetArrayElement(pDocument, GetRoot(pDocument), "Behaviors", issue.m_Element.m_uiIndex);
        return SetValue(pDocument, pRow, "WeightScale", plVariant(static_cast<float>(fScale)), "Change Weight Scale");
      }

      case plAiValidationFix::OpenAsset:
        return OpenAsset(fix.m_sTargetAsset);
    }

    return plStatus(PL_SUCCESS);
  }
} // namespace

namespace plAiValidationFixes
{
  bool TargetsOtherAsset(const plDocument* pDocument, const plAiValidationIssue& issue, const plAiValidationFix& fix)
  {
    plUuid guid;
    if (!plConversionUtils::TryConvertStringToUuid(GetTargetGuid(issue, fix), guid))
      return false;

    return guid != pDocument->GetGuid();
  }

  plString GetTargetName(const plAiValidationIssue& issue, const plAiValidationFix& fix)
  {
    if (!fix.m_sTargetName.IsEmpty())
      return fix.m_sTargetName;

    return issue.m_sAsset;
  }

  plStatus Apply(const plDocument* pDocument, const plAiValidationIssue& issue, const plAiValidationFix& fix, plStringView sChoice)
  {
    if (fix.m_Kind == plAiValidationFix::OpenAsset || !TargetsOtherAsset(pDocument, issue, fix))
      return ApplyToDocument(pDocument, issue, fix, sChoice);

    // another asset: its window opens and holds the undo step
    plUuid guid;
    if (!plConversionUtils::TryConvertStringToUuid(GetTargetGuid(issue, fix), guid))
      return plStatus("The asset does not exist.");

    const plDocument* pTarget = OpenAssetDocument(guid);
    if (pTarget == nullptr)
      return plStatus(plFmt("{} could not be opened.", GetTargetName(issue, fix)));

    return ApplyToDocument(pTarget, issue, fix, sChoice);
  }
} // namespace plAiValidationFixes
