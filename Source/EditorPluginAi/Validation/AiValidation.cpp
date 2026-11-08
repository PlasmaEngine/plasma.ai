#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiArchetypeAsset.h>
#include <EditorPluginAi/Assets/AiBehaviorAsset.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>
#include <EditorPluginAi/Assets/AiEqsPreviewSimulation.h>
#include <EditorPluginAi/Assets/AiEqsQueryAsset.h>
#include <EditorPluginAi/Validation/AiValidation.h>

#include <AiPlugin/Eqs/EqsGenerator.h>
#include <AiPlugin/Eqs/EqsTest.h>
#include <AiPlugin/Navigation/NavigationConfig.h>
#include <AiPlugin/UtilityAI/Decision/AiBehaviorLogic.h>
#include <AiPlugin/UtilityAI/Decision/AiInput.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <Foundation/Serialization/DdlSerializer.h>
#include <Foundation/Serialization/RttiConverter.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <ToolsFoundation/Document/DocumentManager.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

plString plAiValidationIssue::GetId() const
{
  plStringBuilder sId;
  sId.SetFormat("{}|{}|{}|{}|{}|{}|{}", static_cast<plUInt32>(m_Severity), m_sAssetGuid, static_cast<plUInt32>(m_Element.m_Kind), m_Element.m_uiIndex, m_sKey, m_sElement, m_sMessage);
  return sId;
}

//////////////////////////////////////////////////////////////////////////
// Keys and value types

namespace
{
  constexpr const char* s_szNumber = "number";
  constexpr const char* s_szVec3 = "Vec3";

  struct KnownKey
  {
    const char* m_szName;
    const char* m_szType;
  };

  /// The keys the AI framework writes on every agent's board.
  const KnownKey s_FrameworkKeys[] = {
    {"Ai_TargetPosition", "Vec3"},
    {"Ai_TargetConfidence", "float"},
    {"Ai_TargetObject", "object"},
    {"Ai_BehaviorDone", "bool"},
    {"Ai_BehaviorFailed", "bool"},
    {"Ai_LockBehavior", "bool"},
    {"Ai_AlertLevel", "int"},
    {"Ai_Command", "string"},
    {"Ai_CommandActive", "bool"},
    {"Ai_CommandPosition", "Vec3"},
    {"Ai_CommandTarget", "object"},
  };

  /// The default keys the tactical and squad modules and the AI states write; their types vary.
  const char* const s_szSystemKeys[] = {
    "Ai_CoverStatus",
    "Ai_CoverNearby",
    "Ai_TargetExposure",
    "Ai_ThreatHere",
    "Ai_DangerHere",
    "Ai_HasAttackToken",
    "Ai_SquadIntent",
    "Ai_SquadMovePos",
    "Ai_SquadOrderActive",
    "Ai_SquadRegionCenter",
    "Ai_TacticalTarget",
    "Ai_TacticalResult",
    "Ai_PeekTarget",
    "Ai_PeekResult",
    "Ai_WaitResult",
    "Ai_PatrolHome",
    "Ai_PatrolTarget",
    "Ai_PatrolResult",
    "Ai_SmartObjectPos",
    "Ai_SmartObjectResult",
    "Ai_UseResult",
  };

  struct KeyInfo
  {
    plString m_sName;
    plString m_sType; ///< empty when the template gives no initial value
  };

  const KnownKey* FindFrameworkKey(plStringView sKey)
  {
    for (const KnownKey& key : s_FrameworkKeys)
    {
      if (sKey == key.m_szName)
        return &key;
    }

    return nullptr;
  }

  bool IsSystemKey(plStringView sKey)
  {
    for (const char* szKey : s_szSystemKeys)
    {
      if (sKey == szKey)
        return true;
    }

    return false;
  }

  /// The type name the dock shows for a blackboard value.
  plStringView TypeName(const plVariant& value)
  {
    switch (value.GetType())
    {
      case plVariantType::Invalid:
        return {};

      case plVariantType::Bool:
        return "bool";

      case plVariantType::Int8:
      case plVariantType::UInt8:
      case plVariantType::Int16:
      case plVariantType::UInt16:
      case plVariantType::Int32:
      case plVariantType::UInt32:
      case plVariantType::Int64:
      case plVariantType::UInt64:
        return "int";

      case plVariantType::Float:
      case plVariantType::Double:
        return "float";

      case plVariantType::String:
      case plVariantType::StringView:
      case plVariantType::HashedString:
      case plVariantType::TempHashedString:
        return "string";

      case plVariantType::Vector2:
        return "Vec2";

      case plVariantType::Vector3:
        return "Vec3";

      case plVariantType::Vector4:
        return "Vec4";

      default:
        return "value";
    }
  }

  /// Whether an input that reads sExpected ("number" or "Vec3") can use a value of type sActual.
  bool IsCompatible(plStringView sExpected, plStringView sActual)
  {
    if (sActual.IsEmpty() || sActual == "value")
      return true;

    if (sExpected == s_szNumber)
      return sActual == "bool" || sActual == "int" || sActual == "float";

    return sExpected == sActual;
  }

  /// Case-insensitive edit distance.
  plUInt32 EditDistance(plStringView sA, plStringView sB)
  {
    plStringBuilder a(sA);
    plStringBuilder b(sB);
    a.ToLower();
    b.ToLower();

    const char* pA = a.GetData();
    const char* pB = b.GetData();
    const plUInt32 uiA = a.GetElementCount();
    const plUInt32 uiB = b.GetElementCount();

    plHybridArray<plUInt32, 64> row;
    row.SetCount(uiB + 1);

    for (plUInt32 j = 0; j <= uiB; ++j)
      row[j] = j;

    for (plUInt32 i = 1; i <= uiA; ++i)
    {
      plUInt32 uiDiagonal = row[0];
      row[0] = i;

      for (plUInt32 j = 1; j <= uiB; ++j)
      {
        const plUInt32 uiAbove = row[j];
        const plUInt32 uiCost = pA[i - 1] == pB[j - 1] ? 0 : 1;
        row[j] = plMath::Min(plMath::Min(uiAbove + 1, row[j - 1] + 1), uiDiagonal + uiCost);
        uiDiagonal = uiAbove;
      }
    }

    return row[uiB];
  }

  /// The closest name: one that differs only in case, else the nearest within a few edits.
  plString FindClosest(plStringView sName, plArrayPtr<const plString> candidates)
  {
    for (const plString& sCandidate : candidates)
    {
      if (sCandidate.GetView().IsEqual_NoCase(sName))
        return sCandidate;
    }

    const plUInt32 uiLimit = plMath::Max<plUInt32>(2, sName.GetElementCount() / 4);
    plUInt32 uiBest = uiLimit + 1;
    plString sBest;

    for (const plString& sCandidate : candidates)
    {
      const plUInt32 uiDistance = EditDistance(sName, sCandidate);
      if (uiDistance < uiBest)
      {
        uiBest = uiDistance;
        sBest = sCandidate;
      }
    }

    return sBest;
  }

  void AppendUnique(plHybridArray<plString, 8>& ref_values, plStringView sValue)
  {
    if (!ref_values.Contains(sValue))
      ref_values.PushBack(sValue);
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// Assets: resolving references and reading files

namespace
{
  struct AssetRef
  {
    bool m_bFound = false;
    plUuid m_Guid;
    plString m_sGuid;
    plString m_sAbsPath;
    plString m_sName;     ///< the asset name
    plString m_sFileName; ///< the file name with its extension
  };

  AssetRef Resolve(plStringView sReference)
  {
    AssetRef ref;
    if (sReference.IsEmpty())
      return ref;

    auto asset = plAssetCurator::GetSingleton()->FindSubAsset(sReference);
    if (!asset.isValid() || asset->m_pAssetInfo == nullptr)
      return ref;

    plStringBuilder sGuid;
    plConversionUtils::ToString(asset->m_Data.m_Guid, sGuid);

    ref.m_bFound = true;
    ref.m_Guid = asset->m_Data.m_Guid;
    ref.m_sGuid = sGuid;
    ref.m_sAbsPath = asset->m_pAssetInfo->m_Path.GetAbsolutePath();
    ref.m_sName = asset->GetName();
    ref.m_sFileName = plPathUtils::GetFileNameAndExtension(ref.m_sAbsPath);
    return ref;
  }

  AssetRef ResolveDocument(const plDocument& document)
  {
    plStringBuilder sGuid;
    plConversionUtils::ToString(document.GetGuid(), sGuid);

    AssetRef ref;
    ref.m_bFound = true;
    ref.m_Guid = document.GetGuid();
    ref.m_sGuid = sGuid;
    ref.m_sAbsPath = document.GetDocumentPath();
    ref.m_sName = plPathUtils::GetFileName(document.GetDocumentPath());
    ref.m_sFileName = plPathUtils::GetFileNameAndExtension(document.GetDocumentPath());
    return ref;
  }

  /// The GUID a reference string names; invalid when it names no asset.
  plUuid ResolveGuid(plStringView sReference)
  {
    plUuid guid;
    if (plConversionUtils::TryConvertStringToUuid(sReference, guid))
      return guid;

    return Resolve(sReference).m_Guid;
  }

  const plDocumentObject* GetPropertiesObject(const plDocument& document)
  {
    const auto& children = document.GetObjectManager()->GetRootObject()->GetChildren();
    return children.IsEmpty() ? nullptr : children[0];
  }

  struct TemplateData
  {
    plHybridArray<plString, 2> m_Bases;
    plDynamicArray<KeyInfo> m_Keys;
  };

  /// What was read from one asset file; reset when the file changes.
  struct CachedFile
  {
    plTimestamp m_Modified;
    bool m_bRead = false;
    plUniquePtr<plAiBehaviorAssetObject> m_pBehavior;
    plUniquePtr<plAiArchetypeAssetObject> m_pArchetype;
    TemplateData m_Template;
    plInt8 m_iEndsBehavior = -1; ///< state machines: 1 a state ends the behavior, 0 none does, -1 cannot tell
  };

  plMap<plString, CachedFile> s_Files;

  /// The cache entry of a file, emptied when the file changed since it was read; null when the file is gone.
  CachedFile* GetCachedFile(plStringView sAbsPath)
  {
    plFileStats stats;
    if (plOSFile::GetFileStats(sAbsPath, stats).Failed())
      return nullptr;

    CachedFile& file = s_Files[sAbsPath];
    if (file.m_bRead && stats.m_LastModificationTime.Compare(file.m_Modified, plTimestamp::CompareMode::FileTimeEqual))
      return &file;

    file = CachedFile();
    file.m_Modified = stats.m_LastModificationTime;
    return &file;
  }

  plUniquePtr<plAbstractObjectGraph> ReadObjectGraph(plStringView sAbsPath)
  {
    plFileReader file;
    if (file.Open(sAbsPath).Failed())
      return nullptr;

    plUniquePtr<plAbstractObjectGraph> pHeader;
    plUniquePtr<plAbstractObjectGraph> pObjects;
    plUniquePtr<plAbstractObjectGraph> pTypes;

    if (plAbstractGraphDdlSerializer::ReadDocument(file, pHeader, pObjects, pTypes, true).Failed())
      return nullptr;

    return pObjects;
  }

  const plAbstractObjectNode* FindNode(const plAbstractObjectGraph& graph, plStringView sType)
  {
    for (auto it = graph.GetAllNodes().GetIterator(); it.IsValid(); ++it)
    {
      if (it.Value()->GetType() == sType)
        return it.Value();
    }

    return nullptr;
  }

  /// The asset's object model, read from its file.
  template <typename T>
  plUniquePtr<T> ReadAssetObject(plStringView sAbsPath)
  {
    plUniquePtr<plAbstractObjectGraph> pObjects = ReadObjectGraph(sAbsPath);
    if (pObjects == nullptr)
      return nullptr;

    const plAbstractObjectNode* pNode = FindNode(*pObjects, plGetStaticRTTI<T>()->GetTypeName());
    if (pNode == nullptr)
      return nullptr;

    plUniquePtr<T> pObject = PL_DEFAULT_NEW(T);

    plRttiConverterContext context;
    plRttiConverterReader reader(pObjects.Borrow(), &context);
    reader.ApplyPropertiesToObject(pNode, plGetStaticRTTI<T>(), pObject.Borrow());

    return pObject;
  }

  /// The behavior's properties: from its open document, else from its file.
  const plAiBehaviorAssetObject* GetBehavior(const AssetRef& ref)
  {
    if (auto* pOpen = plDynamicCast<plAiBehaviorAssetDocument*>(plDocumentManager::GetDocumentByGuid(ref.m_Guid)))
      return pOpen->GetProperties();

    CachedFile* pFile = GetCachedFile(ref.m_sAbsPath);
    if (pFile == nullptr)
      return nullptr;

    if (!pFile->m_bRead)
    {
      pFile->m_pBehavior = ReadAssetObject<plAiBehaviorAssetObject>(ref.m_sAbsPath);
      pFile->m_bRead = true;
    }

    return pFile->m_pBehavior.Borrow();
  }

  /// The archetype's properties: from its open document, else from its file.
  const plAiArchetypeAssetObject* GetArchetype(const AssetRef& ref)
  {
    if (auto* pOpen = plDynamicCast<plAiArchetypeAssetDocument*>(plDocumentManager::GetDocumentByGuid(ref.m_Guid)))
      return pOpen->GetProperties();

    CachedFile* pFile = GetCachedFile(ref.m_sAbsPath);
    if (pFile == nullptr)
      return nullptr;

    if (!pFile->m_bRead)
    {
      pFile->m_pArchetype = ReadAssetObject<plAiArchetypeAssetObject>(ref.m_sAbsPath);
      pFile->m_bRead = true;
    }

    return pFile->m_pArchetype.Borrow();
  }

  void ReadTemplateFile(plStringView sAbsPath, TemplateData& out_template)
  {
    plUniquePtr<plAbstractObjectGraph> pObjects = ReadObjectGraph(sAbsPath);
    if (pObjects == nullptr)
      return;

    const plAbstractObjectNode* pNode = FindNode(*pObjects, "plBlackboardTemplateAssetObject");
    if (pNode == nullptr)
      return;

    const plAbstractObjectNode::Property* pBases = pNode->FindProperty("BaseTemplates");
    if (pBases != nullptr && pBases->m_Value.IsA<plVariantArray>())
    {
      for (const plVariant& base : pBases->m_Value.Get<plVariantArray>())
        out_template.m_Bases.PushBack(base.ConvertTo<plString>());
    }

    const plAbstractObjectNode::Property* pEntries = pNode->FindProperty("Entries");
    if (pEntries == nullptr || !pEntries->m_Value.IsA<plVariantArray>())
      return;

    for (const plVariant& entry : pEntries->m_Value.Get<plVariantArray>())
    {
      const plAbstractObjectNode* pEntry = entry.IsA<plUuid>() ? pObjects->GetNode(entry.Get<plUuid>()) : nullptr;
      if (pEntry == nullptr)
        continue;

      const plAbstractObjectNode::Property* pName = pEntry->FindProperty("Name");
      const plAbstractObjectNode::Property* pValue = pEntry->FindProperty("InitialValue");
      if (pName == nullptr)
        continue;

      KeyInfo& key = out_template.m_Keys.ExpandAndGetRef();
      key.m_sName = pName->m_Value.ConvertTo<plString>();
      key.m_sType = pValue != nullptr ? TypeName(pValue->m_Value) : plStringView();
    }
  }

  void ReadTemplateDocument(const plDocument& document, TemplateData& out_template)
  {
    const plDocumentObject* pRoot = GetPropertiesObject(document);
    if (pRoot == nullptr)
      return;

    plObjectAccessorBase* pAccessor = document.GetObjectAccessor();
    plDynamicArray<plVariant> values;

    pAccessor->GetValuesByName(pRoot, "BaseTemplates", values).IgnoreResult();
    for (const plVariant& base : values)
      out_template.m_Bases.PushBack(base.ConvertTo<plString>());

    values.Clear();
    pAccessor->GetValuesByName(pRoot, "Entries", values).IgnoreResult();

    for (const plVariant& entry : values)
    {
      const plDocumentObject* pEntry = entry.IsA<plUuid>() ? pAccessor->GetObject(entry.Get<plUuid>()) : nullptr;
      if (pEntry == nullptr)
        continue;

      plVariant name;
      plVariant initial;
      pAccessor->GetValueByName(pEntry, "Name", name).IgnoreResult();
      pAccessor->GetValueByName(pEntry, "InitialValue", initial).IgnoreResult();

      KeyInfo& key = out_template.m_Keys.ExpandAndGetRef();
      key.m_sName = name.ConvertTo<plString>();
      key.m_sType = TypeName(initial);
    }
  }

  /// The template's own keys and base templates: from its open document, else from its file.
  void GetTemplate(const AssetRef& ref, TemplateData& out_template)
  {
    if (const plDocument* pOpen = plDocumentManager::GetDocumentByGuid(ref.m_Guid))
    {
      ReadTemplateDocument(*pOpen, out_template);
      return;
    }

    CachedFile* pFile = GetCachedFile(ref.m_sAbsPath);
    if (pFile == nullptr)
      return;

    if (!pFile->m_bRead)
    {
      ReadTemplateFile(ref.m_sAbsPath, pFile->m_Template);
      pFile->m_bRead = true;
    }

    out_template.m_Bases = pFile->m_Template.m_Bases;
    out_template.m_Keys = pFile->m_Template.m_Keys;
  }

  /// The keys of a template and of its base templates; a template's own keys replace same-named base keys.
  void CollectTemplateKeys(const AssetRef& ref, plDynamicArray<KeyInfo>& inout_keys, plUInt32 uiDepth)
  {
    if (uiDepth > 8)
      return;

    TemplateData data;
    GetTemplate(ref, data);

    for (const plString& sBase : data.m_Bases)
    {
      const AssetRef base = Resolve(sBase);
      if (base.m_bFound)
        CollectTemplateKeys(base, inout_keys, uiDepth + 1);
    }

    for (const KeyInfo& key : data.m_Keys)
    {
      bool bReplaced = false;

      for (KeyInfo& existing : inout_keys)
      {
        if (existing.m_sName == key.m_sName)
        {
          existing = key;
          bReplaced = true;
        }
      }

      if (!bReplaced)
        inout_keys.PushBack(key);
    }
  }

  bool NodeEndsBehavior(const plAbstractObjectNode& node)
  {
    // the navigate state ends the behavior by default
    if (node.GetType() == "plStateMachineState_AiNavigateTo" && node.FindProperty("EndBehaviorWhenDone") == nullptr)
      return true;

    for (const plAbstractObjectNode::Property& prop : node.GetProperties())
    {
      if (prop.m_sPropertyName == "EndBehaviorWhenDone" && prop.m_Value.IsA<bool>() && prop.m_Value.Get<bool>())
        return true;

      if (!prop.m_Value.IsA<plString>() && !prop.m_Value.IsA<plHashedString>())
        continue;

      const plString sValue = prop.m_Value.ConvertTo<plString>();
      if (sValue == "Ai_BehaviorDone" || sValue == "Ai_BehaviorFailed")
        return true;
    }

    return false;
  }

  plInt8 ReadEndsBehavior(plStringView sAbsPath)
  {
    plUniquePtr<plAbstractObjectGraph> pObjects = ReadObjectGraph(sAbsPath);
    if (pObjects == nullptr)
      return -1;

    bool bEnds = false;

    for (auto it = pObjects->GetAllNodes().GetIterator(); it.IsValid(); ++it)
    {
      // scripted and nested states can end the behavior in ways the file does not show
      const plStringView sType = it.Value()->GetType();
      if (sType.FindSubString("Script") != nullptr || sType.FindSubString("Nested") != nullptr)
        return -1;

      bEnds |= NodeEndsBehavior(*it.Value());
    }

    return bEnds ? 1 : 0;
  }

  /// 1 when a state of the state machine ends the behavior, 0 when none does, -1 when that cannot be told.
  plInt8 GetEndsBehavior(const AssetRef& ref)
  {
    CachedFile* pFile = GetCachedFile(ref.m_sAbsPath);
    if (pFile == nullptr)
      return -1;

    if (!pFile->m_bRead)
    {
      pFile->m_iEndsBehavior = ReadEndsBehavior(ref.m_sAbsPath);
      pFile->m_bRead = true;
    }

    return pFile->m_iEndsBehavior;
  }

  /// An archetype of the project: its template and the behaviors it lists.
  struct ArchetypeUse
  {
    AssetRef m_Asset;
    AssetRef m_Template; ///< not found when the archetype has none or it is missing
    plHybridArray<plUuid, 8> m_Behaviors;
  };

  ArchetypeUse MakeArchetypeUse(const AssetRef& asset, const plAiArchetypeAssetObject& props)
  {
    ArchetypeUse use;
    use.m_Asset = asset;
    use.m_Template = Resolve(props.m_sBlackboardTemplate);

    for (const plAiArchetypeBehaviorObject& row : props.m_Behaviors)
      use.m_Behaviors.PushBack(ResolveGuid(row.m_sBehavior));

    return use;
  }

  /// Every archetype the asset curator knows.
  void ScanArchetypes(plDynamicArray<ArchetypeUse>& out_archetypes)
  {
    plDynamicArray<plUuid> guids;
    {
      auto assets = plAssetCurator::GetSingleton()->GetKnownSubAssets();
      for (auto it = assets->GetIterator(); it.IsValid(); ++it)
      {
        if (it.Value().m_Data.m_sSubAssetsDocumentTypeName.GetView() == "AiArchetype")
          guids.PushBack(it.Key());
      }
    }

    for (const plUuid& guid : guids)
    {
      plStringBuilder sGuid;
      plConversionUtils::ToString(guid, sGuid);

      const AssetRef asset = Resolve(sGuid);
      const plAiArchetypeAssetObject* pProps = asset.m_bFound ? GetArchetype(asset) : nullptr;

      if (pProps != nullptr)
        out_archetypes.PushBack(MakeArchetypeUse(asset, *pProps));
    }
  }

  /// The names of the archetypes that list the behavior.
  void GetBehaviorUsers(const plUuid& behavior, plArrayPtr<const ArchetypeUse> archetypes, plHybridArray<plString, 4>& out_names)
  {
    for (const ArchetypeUse& archetype : archetypes)
    {
      if (archetype.m_Behaviors.Contains(behavior))
        out_names.PushBack(archetype.m_Asset.m_sName);
    }
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// Issues

namespace
{
  /// Where issues come from and what their rows select.
  struct IssueSource
  {
    plString m_sAsset;
    plString m_sAssetGuid;
    bool m_bDependency = false;
    bool m_bRevealElement = true;   ///< rows select the issue's own element
    plAiValidationElement m_Reveal; ///< otherwise this element of the validated document
  };

  IssueSource MakeSource(const AssetRef& asset, bool bDependency)
  {
    IssueSource source;
    source.m_sAsset = asset.m_sFileName;
    source.m_sAssetGuid = asset.m_sGuid;
    source.m_bDependency = bDependency;
    return source;
  }

  plAiValidationElement MakeElement(plAiValidationElement::Kind kind, plUInt32 uiIndex = 0)
  {
    plAiValidationElement element;
    element.m_Kind = kind;
    element.m_uiIndex = uiIndex;
    return element;
  }

  plAiValidationIssue& AddIssue(plAiValidationReport& ref_report, const IssueSource& source, plAiValidationSeverity::Enum severity, const plAiValidationElement& element, plStringView sElement, plStringView sKey, plStringView sExpected, plStringView sActual, plStringView sMessage)
  {
    plAiValidationIssue& issue = ref_report.m_Issues.ExpandAndGetRef();
    issue.m_Severity = severity;
    issue.m_sAsset = source.m_sAsset;
    issue.m_sAssetGuid = source.m_sAssetGuid;
    issue.m_bDependency = source.m_bDependency;
    issue.m_sElement = sElement;
    issue.m_sKey = sKey;
    issue.m_sExpected = sExpected;
    issue.m_sActual = sActual;
    issue.m_sMessage = sMessage;
    issue.m_Element = element;
    issue.m_Reveal = source.m_bRevealElement ? element : source.m_Reveal;
    return issue;
  }

  plAiValidationFix& AddFix(plAiValidationIssue& ref_issue, plAiValidationFix::Kind kind, plStringView sLabel, plStringView sTooltip)
  {
    plAiValidationFix& fix = ref_issue.m_Fixes.ExpandAndGetRef();
    fix.m_Kind = kind;
    fix.m_sLabel = sLabel;
    fix.m_sTooltip = sTooltip;
    return fix;
  }

  plString FormatValue(float fValue)
  {
    plStringBuilder sValue;
    sValue.SetFormat("{}", fValue);
    return sValue;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// Behavior rules

namespace
{
  /// The keys one archetype's board starts with: its template and the template's bases.
  struct KeyScope
  {
    plString m_sArchetype;
    plString m_sTemplate;
    plString m_sTemplateGuid;
    plDynamicArray<KeyInfo> m_Keys;
    plHybridArray<plString, 4> m_TemplateUsers; ///< the archetypes that use the template
  };

  bool BuildScope(const ArchetypeUse& archetype, plArrayPtr<const ArchetypeUse> all, KeyScope& out_scope)
  {
    if (!archetype.m_Template.m_bFound)
      return false;

    out_scope.m_sArchetype = archetype.m_Asset.m_sName;
    out_scope.m_sTemplate = archetype.m_Template.m_sName;
    out_scope.m_sTemplateGuid = archetype.m_Template.m_sGuid;
    CollectTemplateKeys(archetype.m_Template, out_scope.m_Keys, 0);

    for (const ArchetypeUse& other : all)
    {
      if (other.m_Template.m_bFound && other.m_Template.m_Guid == archetype.m_Template.m_Guid)
        out_scope.m_TemplateUsers.PushBack(other.m_Asset.m_sName);
    }

    return true;
  }

  struct BehaviorContext
  {
    IssueSource m_Source;
    plArrayPtr<const KeyScope> m_Scopes;
    plArrayPtr<const plString> m_UsedBy; ///< the archetypes that list the behavior
  };

  /// The key a consideration input reads and the value type it needs; false for inputs without an authored key.
  bool GetInputKey(const plAiInput* pInput, plString& out_sKey, plStringView& out_sType)
  {
    if (auto pValue = plDynamicCast<const plAiInput_BlackboardValue*>(pInput))
    {
      out_sKey = pValue->GetEntryName();
      out_sType = s_szNumber;
      return true;
    }

    if (auto pDistance = plDynamicCast<const plAiInput_DistanceToPosition*>(pInput))
    {
      out_sKey = pDistance->GetEntryName();
      out_sType = s_szVec3;
      return true;
    }

    return false;
  }

  /// The keys of a type, from the archetype templates and the framework, sorted.
  void CollectKeyChoices(plArrayPtr<const KeyScope> scopes, plStringView sType, plHybridArray<plString, 8>& out_choices)
  {
    for (const KeyScope& scope : scopes)
    {
      for (const KeyInfo& key : scope.m_Keys)
      {
        if (IsCompatible(sType, key.m_sType))
          AppendUnique(out_choices, key.m_sName);
      }
    }

    for (const KnownKey& key : s_FrameworkKeys)
    {
      if (IsCompatible(sType, key.m_szType))
        AppendUnique(out_choices, key.m_szName);
    }

    out_choices.Sort();
  }

  /// The behavior's other archetypes, listed before a fix edits it (only when it is shared).
  void SetAffected(plAiValidationFix& ref_fix, plArrayPtr<const plString> usedBy)
  {
    if (usedBy.GetCount() < 2)
      return;

    for (const plString& sName : usedBy)
      ref_fix.m_AffectedAssets.PushBack(sName);
  }

  void AddChooseKeyFix(plAiValidationIssue& ref_issue, const BehaviorContext& context, plStringView sType, plStringView sSuggestion)
  {
    if (!sSuggestion.IsEmpty())
    {
      plStringBuilder sLabel("Choose ", sSuggestion);
      plStringBuilder sTooltip("Sets the input's key to ", sSuggestion);

      plAiValidationFix& fix = AddFix(ref_issue, plAiValidationFix::ChooseKey, sLabel, sTooltip);
      fix.m_sValue = sSuggestion;
      SetAffected(fix, context.m_UsedBy);
      return;
    }

    plAiValidationFix& fix = AddFix(ref_issue, plAiValidationFix::ChooseKey, "Choose existing key", "Sets the input's key to one the board has");
    CollectKeyChoices(context.m_Scopes, sType, fix.m_Choices);
    SetAffected(fix, context.m_UsedBy);
  }

  void AddKeyToTemplateFix(plAiValidationIssue& ref_issue, const KeyScope& scope, plStringView sKey, plStringView sType)
  {
    plStringBuilder sTooltip;
    sTooltip.SetFormat("Adds {} ({}) to the template {}", sKey, sType, scope.m_sTemplate);

    plAiValidationFix& fix = AddFix(ref_issue, plAiValidationFix::AddKeyToTemplate, "Add key to template", sTooltip);
    fix.m_sTargetAsset = scope.m_sTemplateGuid;
    fix.m_sTargetName = scope.m_sTemplate;
    fix.m_sKey = sKey;
    fix.m_sValueType = sType;

    for (const plString& sUser : scope.m_TemplateUsers)
      fix.m_AffectedAssets.PushBack(sUser);
  }

  void ValidateFrameworkKey(const KnownKey& key, plStringView sType, const plAiValidationElement& element, plStringView sElement, const BehaviorContext& context, plAiValidationReport& ref_report)
  {
    if (IsCompatible(sType, key.m_szType))
      return;

    plStringBuilder sMessage;
    sMessage.SetFormat("The input reads {} as {}; the AI writes it as {}", key.m_szName, sType, key.m_szType);

    plAiValidationIssue& issue = AddIssue(ref_report, context.m_Source, plAiValidationSeverity::Error, element, sElement, key.m_szName, sType, key.m_szType, sMessage);
    AddChooseKeyFix(issue, context, sType, {});
  }

  void ValidateTemplateKey(const KeyScope& scope, plStringView sKey, plStringView sType, const plAiValidationElement& element, plStringView sElement, const BehaviorContext& context, plAiValidationReport& ref_report)
  {
    plHybridArray<plString, 16> compatible;
    bool bSameNameOtherCase = false;

    for (const KeyInfo& key : scope.m_Keys)
    {
      if (key.m_sName == sKey)
      {
        if (IsCompatible(sType, key.m_sType))
          return;

        // a key of another type: no fix adds a second one
        plStringBuilder sMessage;
        sMessage.SetFormat("The input reads {} as {}; the template {} declares {}", sKey, sType, scope.m_sTemplate, key.m_sType);
        plAiValidationIssue& issue = AddIssue(ref_report, context.m_Source, plAiValidationSeverity::Error, element, sElement, sKey, sType, key.m_sType, sMessage);
        AddChooseKeyFix(issue, context, sType, {});
        return;
      }

      bSameNameOtherCase |= key.m_sName.GetView().IsEqual_NoCase(sKey);

      if (IsCompatible(sType, key.m_sType))
        compatible.PushBack(key.m_sName);
    }

    const plString sSuggestion = FindClosest(sKey, compatible);

    plStringBuilder sMessage;
    sMessage.SetFormat("Key not in the archetype template ({} of {})", scope.m_sTemplate, scope.m_sArchetype);
    if (!sSuggestion.IsEmpty())
      sMessage.AppendFormat("; did you mean {}?", sSuggestion);

    plAiValidationIssue& issue = AddIssue(ref_report, context.m_Source, plAiValidationSeverity::Error, element, sElement, sKey, sType, "missing", sMessage);
    AddChooseKeyFix(issue, context, sType, sSuggestion);

    if (!bSameNameOtherCase)
      AddKeyToTemplateFix(issue, scope, sKey, sType);
  }

  void ValidateInputKey(const plAiInput* pInput, const plAiValidationElement& element, plStringView sElement, const BehaviorContext& context, plAiValidationReport& ref_report)
  {
    plString sKey;
    plStringView sType;
    if (!GetInputKey(pInput, sKey, sType))
      return;

    if (sKey.IsEmpty())
    {
      plAiValidationIssue& issue = AddIssue(ref_report, context.m_Source, plAiValidationSeverity::Error, element, sElement, "-", sType, "none", "No blackboard key; the input never finds a value");
      AddChooseKeyFix(issue, context, sType, {});
      return;
    }

    if (const KnownKey* pFramework = FindFrameworkKey(sKey))
    {
      ValidateFrameworkKey(*pFramework, sType, element, sElement, context, ref_report);
      return;
    }

    if (IsSystemKey(sKey))
      return;

    for (const KeyScope& scope : context.m_Scopes)
      ValidateTemplateKey(scope, sKey, sType, element, sElement, context, ref_report);
  }

  void ValidateConsideration(const plAiConsiderationObject& consideration, plUInt32 uiIndex, const BehaviorContext& context, plAiValidationReport& ref_report)
  {
    const plAiValidationElement element = MakeElement(plAiValidationElement::Consideration, uiIndex);
    const plAiInput* pInput = consideration.m_pInput;

    plStringBuilder sInput;
    plAiAssetUi::GetPrettyInputName(pInput != nullptr ? pInput->GetDynamicRTTI() : nullptr, sInput);

    plStringBuilder sElement;
    sElement.SetFormat("Consideration {}: {}", uiIndex + 1, sInput);

    if (pInput == nullptr)
    {
      AddIssue(ref_report, context.m_Source, plAiValidationSeverity::Error, element, sElement, "Input", "input type", "none", "No input; the runtime skips this consideration");
      return;
    }

    if (consideration.m_fInputMin == consideration.m_fInputMax)
    {
      plStringBuilder sActual("both ", FormatValue(consideration.m_fInputMin));
      AddIssue(ref_report, context.m_Source, plAiValidationSeverity::Warning, element, sElement, "Input range", "Min and Max differ", sActual, "Input Min equals Input Max; every value normalizes to 0");
    }

    ValidateInputKey(pInput, element, sElement, context, ref_report);
  }

  void ValidateWeight(const plAiBehaviorAssetObject& props, const BehaviorContext& context, plAiValidationReport& ref_report)
  {
    if (props.m_fWeight > 0.0f && props.m_fWeight <= 1.0f)
      return;

    // a weight multiplies the utility, which is capped at 1: above 1, every score that reaches the cap ties
    const bool bZero = props.m_fWeight <= 0.0f;
    plAiValidationIssue& issue = AddIssue(ref_report, context.m_Source, plAiValidationSeverity::Warning, MakeElement(plAiValidationElement::Root), "Behavior", "Weight", bZero ? "above 0" : "0 to 1", FormatValue(props.m_fWeight),
      bZero ? "Weight 0; the behavior always scores 0 and never runs" : "Weight above 1; utility is capped at 1, so every score that reaches the cap ties with the others there");

    plAiValidationFix& fix = AddFix(issue, plAiValidationFix::SetWeight, "Set weight to 1", "Sets the behavior's weight to 1");
    fix.m_sValue = "1";
    SetAffected(fix, context.m_UsedBy);
  }

  void ValidateCompletion(const AssetRef& stateMachine, const BehaviorContext& context, plAiValidationReport& ref_report)
  {
    if (GetEndsBehavior(stateMachine) != 0)
      return;

    // the state machine is a dependency; its row selects the behavior's logic (or the archetype row)
    IssueSource source = MakeSource(stateMachine, true);
    source.m_bRevealElement = false;
    source.m_Reveal = context.m_Source.m_bRevealElement ? MakeElement(plAiValidationElement::Logic) : context.m_Source.m_Reveal;

    plAiValidationIssue& issue = AddIssue(ref_report, source, plAiValidationSeverity::Warning, MakeElement(plAiValidationElement::Root), "State machine", "Ai_BehaviorDone", "bool", "never written", "No state ends the behavior (Ai_BehaviorDone or Ai_BehaviorFailed); it only ends when another behavior wins");

    plAiValidationFix& fix = AddFix(issue, plAiValidationFix::OpenAsset, "Open state machine", "Opens the state machine asset");
    fix.m_sTargetAsset = stateMachine.m_sGuid;
    fix.m_sTargetName = stateMachine.m_sName;
  }

  void ValidateLogic(const plAiBehaviorAssetObject& props, const BehaviorContext& context, plAiValidationReport& ref_report)
  {
    const plAiValidationElement logic = MakeElement(plAiValidationElement::Logic);

    if (props.m_pLogic == nullptr)
    {
      plAiValidationIssue& issue = AddIssue(ref_report, context.m_Source, plAiValidationSeverity::Error, MakeElement(plAiValidationElement::Root), "Logic", "Logic", "state machine or action queue", "none", "No logic; the behavior can win but does nothing");
      AddFix(issue, plAiValidationFix::AddStateMachineLogic, "Add state machine", "Gives the behavior a state machine logic");
      return;
    }

    const auto* pStateMachine = plDynamicCast<const plAiBehaviorLogic_StateMachine*>(props.m_pLogic);
    if (pStateMachine == nullptr)
      return;

    const plStringView sFile = pStateMachine->GetStateMachineFile();
    if (sFile.IsEmpty())
    {
      plAiValidationIssue& issue = AddIssue(ref_report, context.m_Source, plAiValidationSeverity::Error, logic, "Logic: State Machine", "State machine", "asset", "none", "No state machine asset; the behavior does nothing while it runs");
      AddFix(issue, plAiValidationFix::ChooseStateMachine, "Choose state machine", "Picks the state machine asset");
      return;
    }

    const AssetRef stateMachine = Resolve(sFile);
    if (!stateMachine.m_bFound)
    {
      plAiValidationIssue& issue = AddIssue(ref_report, context.m_Source, plAiValidationSeverity::Error, logic, "Logic: State Machine", sFile, "asset", "missing", "The state machine asset does not exist");
      AddFix(issue, plAiValidationFix::ChooseStateMachine, "Choose state machine", "Picks the state machine asset");
      return;
    }

    ValidateCompletion(stateMachine, context, ref_report);
  }

  void ValidateBehavior(const plAiBehaviorAssetObject& props, const BehaviorContext& context, plAiValidationReport& ref_report)
  {
    for (plUInt32 i = 0; i < props.m_Considerations.GetCount(); ++i)
    {
      if (props.m_Considerations[i] != nullptr)
        ValidateConsideration(*props.m_Considerations[i], i, context, ref_report);
    }

    ValidateWeight(props, context, ref_report);
    ValidateLogic(props, context, ref_report);
  }

  constexpr const char* s_szOwnerTooltip = "The brain, tactical, squads and EQS read the blackboard of the first blackboard component on the agent or its parents. Without one, the brain uses a private board the other systems do not see. The archetype's template fills the board when the agent starts.";

  void SetBehaviorOwner(plArrayPtr<const KeyScope> scopes, plArrayPtr<const plString> usedBy, plAiValidationReport& ref_report)
  {
    plStringBuilder sOwner("Blackboard owner: blackboard component on the agent");

    if (usedBy.IsEmpty())
      sOwner.Append(" \xC2\xB7 no archetype uses this behavior; keys are not checked");
    else if (scopes.IsEmpty())
      sOwner.Append(" \xC2\xB7 no template on its archetypes; keys are not checked");
    else if (scopes.GetCount() == 1)
      sOwner.AppendFormat(" \xC2\xB7 template {} via {}", scopes[0].m_sTemplate, scopes[0].m_sArchetype);
    else
      sOwner.AppendFormat(" \xC2\xB7 templates of {} archetypes", scopes.GetCount());

    ref_report.m_sOwner = sOwner;
    ref_report.m_sOwnerTooltip = s_szOwnerTooltip;
  }

  void ValidateBehaviorDocument(const plAiBehaviorAssetDocument& document, plAiValidationReport& out_report)
  {
    const AssetRef self = ResolveDocument(document);

    plDynamicArray<ArchetypeUse> archetypes;
    ScanArchetypes(archetypes);

    plHybridArray<plString, 4> usedBy;
    GetBehaviorUsers(self.m_Guid, archetypes, usedBy);

    plDynamicArray<KeyScope> scopes;
    for (const ArchetypeUse& archetype : archetypes)
    {
      if (!archetype.m_Behaviors.Contains(self.m_Guid))
        continue;

      KeyScope scope;
      if (BuildScope(archetype, archetypes, scope))
        scopes.PushBack(std::move(scope));
    }

    BehaviorContext context;
    context.m_Source = MakeSource(self, false);
    context.m_Scopes = scopes;
    context.m_UsedBy = usedBy;

    ValidateBehavior(*document.GetProperties(), context, out_report);
    SetBehaviorOwner(scopes, usedBy, out_report);
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// Archetype rules

namespace
{
  plString GetBehaviorName(const plAiBehaviorAssetObject& props, const AssetRef& asset)
  {
    return props.m_sName.IsEmpty() ? asset.m_sName : props.m_sName;
  }

  /// A template that does not resolve; the scope of the archetype otherwise.
  void ValidateTemplate(const plAiArchetypeAssetObject& props, const ArchetypeUse& self, plArrayPtr<const ArchetypeUse> archetypes, const IssueSource& source, plDynamicArray<KeyScope>& out_scopes, plAiValidationReport& ref_report)
  {
    if (props.m_sBlackboardTemplate.IsEmpty())
      return;

    if (!self.m_Template.m_bFound)
    {
      plAiValidationIssue& issue = AddIssue(ref_report, source, plAiValidationSeverity::Error, MakeElement(plAiValidationElement::Root), "Blackboard template", props.m_sBlackboardTemplate, "template asset", "missing", "The blackboard template asset does not exist");
      AddFix(issue, plAiValidationFix::ChooseTemplate, "Choose template", "Picks the blackboard template asset");
      return;
    }

    KeyScope scope;
    if (BuildScope(self, archetypes, scope))
      out_scopes.PushBack(std::move(scope));
  }

  struct ArchetypeRow
  {
    AssetRef m_Asset;
    const plAiBehaviorAssetObject* m_pBehavior = nullptr;
    float m_fWeightScale = 1.0f;
  };

  void AddChooseBehaviorFix(plAiValidationIssue& ref_issue)
  {
    AddFix(ref_issue, plAiValidationFix::ChooseBehavior, "Choose behavior", "Picks the behavior asset of this row");
  }

  /// Resolves the row; false (with an issue) when it has no readable behavior.
  bool ValidateRowReference(const plAiArchetypeBehaviorObject& row, plUInt32 uiIndex, const IssueSource& source, ArchetypeRow& out_row, plAiValidationReport& ref_report)
  {
    const plAiValidationElement element = MakeElement(plAiValidationElement::BehaviorRow, uiIndex);

    plStringBuilder sElement;
    sElement.SetFormat("Behavior {}", uiIndex + 1);

    if (row.m_sBehavior.IsEmpty())
    {
      plAiValidationIssue& issue = AddIssue(ref_report, source, plAiValidationSeverity::Error, element, sElement, "-", "behavior asset", "none", "The row has no behavior asset");
      AddChooseBehaviorFix(issue);
      return false;
    }

    out_row.m_Asset = Resolve(row.m_sBehavior);
    out_row.m_fWeightScale = row.m_fWeightScale;

    if (!out_row.m_Asset.m_bFound)
    {
      plAiValidationIssue& issue = AddIssue(ref_report, source, plAiValidationSeverity::Error, element, sElement, row.m_sBehavior, "behavior asset", "missing", "The behavior asset does not exist");
      AddChooseBehaviorFix(issue);
      return false;
    }

    out_row.m_pBehavior = GetBehavior(out_row.m_Asset);
    if (out_row.m_pBehavior == nullptr)
    {
      plAiValidationIssue& issue = AddIssue(ref_report, source, plAiValidationSeverity::Error, element, sElement, out_row.m_Asset.m_sName, "behavior asset", "unreadable", "The behavior asset could not be read");
      AddChooseBehaviorFix(issue);
      return false;
    }

    return true;
  }

  void ValidateDuplicate(plArrayPtr<const ArchetypeRow> rows, plUInt32 uiIndex, const IssueSource& source, plAiValidationReport& ref_report)
  {
    const ArchetypeRow& row = rows[uiIndex];

    for (plUInt32 j = 0; j < uiIndex; ++j)
    {
      if (rows[j].m_pBehavior == nullptr || rows[j].m_Asset.m_Guid != row.m_Asset.m_Guid)
        continue;

      const plString sName = GetBehaviorName(*row.m_pBehavior, row.m_Asset);

      plStringBuilder sElement;
      sElement.SetFormat("Behavior {}: {}", uiIndex + 1, sName);

      plStringBuilder sActual;
      sActual.SetFormat("also row {}", j + 1);

      AddIssue(ref_report, source, plAiValidationSeverity::Warning, MakeElement(plAiValidationElement::BehaviorRow, uiIndex), sElement, sName, "listed once", sActual, "The behavior is listed twice; both rows are scored");
      return;
    }
  }

  bool SameCurve(const plSingleCurveData& a, const plSingleCurveData& b)
  {
    if (a.m_ControlPoints.GetCount() != b.m_ControlPoints.GetCount())
      return false;

    for (plUInt32 i = 0; i < a.m_ControlPoints.GetCount(); ++i)
    {
      if (a.m_ControlPoints[i].m_iTick != b.m_ControlPoints[i].m_iTick || a.m_ControlPoints[i].m_fValue != b.m_ControlPoints[i].m_fValue)
        return false;
    }

    return true;
  }

  bool SameConsideration(const plAiConsiderationObject* pA, const plAiConsiderationObject* pB)
  {
    if (pA == nullptr || pB == nullptr)
      return pA == pB;

    if (pA->m_pInput == nullptr || pB->m_pInput == nullptr)
      return pA->m_pInput == pB->m_pInput;

    if (pA->m_pInput->GetDynamicRTTI() != pB->m_pInput->GetDynamicRTTI())
      return false;

    plStringBuilder sA;
    plStringBuilder sB;
    plAiAssetUi::GetInputParamSummary(pA->m_pInput, sA);
    plAiAssetUi::GetInputParamSummary(pB->m_pInput, sB);

    return sA == sB && pA->m_fInputMin == pB->m_fInputMin && pA->m_fInputMax == pB->m_fInputMax && pA->m_bLegacyCurveDomain == pB->m_bLegacyCurveDomain && SameCurve(pA->m_ResponseCurve, pB->m_ResponseCurve);
  }

  /// Same category, effective weight and considerations: the two always score the same.
  bool ScoreAlike(const ArchetypeRow& a, const ArchetypeRow& b)
  {
    const plAiBehaviorAssetObject& propsA = *a.m_pBehavior;
    const plAiBehaviorAssetObject& propsB = *b.m_pBehavior;

    if (propsA.m_Category != propsB.m_Category || propsA.m_fWeight * a.m_fWeightScale != propsB.m_fWeight * b.m_fWeightScale)
      return false;

    if (propsA.m_Considerations.GetCount() != propsB.m_Considerations.GetCount())
      return false;

    for (plUInt32 i = 0; i < propsA.m_Considerations.GetCount(); ++i)
    {
      if (!SameConsideration(propsA.m_Considerations[i], propsB.m_Considerations[i]))
        return false;
    }

    return true;
  }

  void ValidateAlike(plArrayPtr<const ArchetypeRow> rows, const IssueSource& source, plAiValidationReport& ref_report)
  {
    for (plUInt32 b = 1; b < rows.GetCount(); ++b)
    {
      for (plUInt32 a = 0; a < b; ++a)
      {
        if (rows[a].m_pBehavior == nullptr || rows[b].m_pBehavior == nullptr || rows[a].m_Asset.m_Guid == rows[b].m_Asset.m_Guid)
          continue;

        if (!ScoreAlike(rows[a], rows[b]))
          continue;

        const plString sA = GetBehaviorName(*rows[a].m_pBehavior, rows[a].m_Asset);
        const plString sB = GetBehaviorName(*rows[b].m_pBehavior, rows[b].m_Asset);

        plStringBuilder sMessage;
        sMessage.SetFormat("{} and {} have the same category, weight and considerations; one never wins", sA, sB);

        AddIssue(ref_report, source, plAiValidationSeverity::Info, MakeElement(plAiValidationElement::BehaviorRow, b), "Behaviors", sB, "-", "-", sMessage);
        break;
      }
    }
  }

  void ValidateWeightScale(const ArchetypeRow& row, plUInt32 uiIndex, const IssueSource& source, plAiValidationReport& ref_report)
  {
    if (row.m_fWeightScale <= 1.0f)
      return;

    plStringBuilder sElement;
    sElement.SetFormat("Behavior {}: {}", uiIndex + 1, GetBehaviorName(*row.m_pBehavior, row.m_Asset));

    plAiValidationIssue& issue = AddIssue(ref_report, source, plAiValidationSeverity::Warning, MakeElement(plAiValidationElement::BehaviorRow, uiIndex), sElement, "Weight Scale", "0 to 1", FormatValue(row.m_fWeightScale),
      "Weight scale above 1; utility is capped at 1, so every score that reaches the cap ties with the others there");

    plAiValidationFix& fix = AddFix(issue, plAiValidationFix::SetWeightScale, "Set scale to 1", "Sets this row's weight scale to 1");
    fix.m_sValue = "1";
  }

  void ValidateRowBehavior(const ArchetypeRow& row, plUInt32 uiIndex, plArrayPtr<const KeyScope> scopes, plArrayPtr<const ArchetypeUse> archetypes, plAiValidationReport& ref_report)
  {
    plHybridArray<plString, 4> usedBy;
    GetBehaviorUsers(row.m_Asset.m_Guid, archetypes, usedBy);

    // the behavior is a dependency; its rows select the archetype row
    BehaviorContext context;
    context.m_Source = MakeSource(row.m_Asset, true);
    context.m_Source.m_bRevealElement = false;
    context.m_Source.m_Reveal = MakeElement(plAiValidationElement::BehaviorRow, uiIndex);
    context.m_Scopes = scopes;
    context.m_UsedBy = usedBy;

    ValidateBehavior(*row.m_pBehavior, context, ref_report);
  }

  void SetArchetypeOwner(const plAiArchetypeAssetObject& props, plArrayPtr<const KeyScope> scopes, plAiValidationReport& ref_report)
  {
    plStringBuilder sOwner("Blackboard owner: blackboard component on the agent");

    if (!scopes.IsEmpty())
      sOwner.AppendFormat(" \xC2\xB7 template {}", scopes[0].m_sTemplate);
    else if (props.m_sBlackboardTemplate.IsEmpty())
      sOwner.Append(" \xC2\xB7 no template; keys are not checked");

    ref_report.m_sOwner = sOwner;
    ref_report.m_sOwnerTooltip = s_szOwnerTooltip;
  }

  void ValidateArchetypeDocument(const plAiArchetypeAssetDocument& document, plAiValidationReport& out_report)
  {
    const plAiArchetypeAssetObject& props = *document.GetProperties();
    const AssetRef self = ResolveDocument(document);
    const IssueSource source = MakeSource(self, false);
    const ArchetypeUse selfUse = MakeArchetypeUse(self, props);

    plDynamicArray<ArchetypeUse> archetypes;
    ScanArchetypes(archetypes);

    plDynamicArray<KeyScope> scopes;
    ValidateTemplate(props, selfUse, archetypes, source, scopes, out_report);
    SetArchetypeOwner(props, scopes, out_report);

    if (props.m_Behaviors.IsEmpty())
    {
      plAiValidationIssue& issue = AddIssue(out_report, source, plAiValidationSeverity::Warning, MakeElement(plAiValidationElement::Root), "Behaviors", "-", "at least one", "none", "The archetype has no behaviors; its agents do nothing");
      AddFix(issue, plAiValidationFix::AddBehavior, "Add behavior", "Picks a behavior asset and adds it to the archetype");
      return;
    }

    plDynamicArray<ArchetypeRow> rows;
    rows.SetCount(props.m_Behaviors.GetCount());

    for (plUInt32 i = 0; i < props.m_Behaviors.GetCount(); ++i)
    {
      if (!ValidateRowReference(props.m_Behaviors[i], i, source, rows[i], out_report))
      {
        rows[i].m_pBehavior = nullptr;
        continue;
      }

      ValidateDuplicate(rows, i, source, out_report);
      ValidateWeightScale(rows[i], i, source, out_report);
      ValidateRowBehavior(rows[i], i, scopes, archetypes, out_report);
    }

    ValidateAlike(rows, source, out_report);
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// EQS query rules

namespace
{
  plString PrettyEqsName(const plRTTI* pRtti)
  {
    plStringBuilder sName;
    plAiEqsUi::GetPrettyName(pRtti, sName);
    return sName;
  }

  /// "Querier" and the authored slot names.
  void CollectContextChoices(const plAiEqsQueryAssetObject& props, plHybridArray<plString, 8>& out_choices)
  {
    out_choices.PushBack("Querier");

    for (const plAiEqsContextSlotObject* pSlot : props.m_ContextSlots)
    {
      if (pSlot != nullptr && pSlot->m_pContext != nullptr && !pSlot->m_sName.IsEmpty())
        AppendUnique(out_choices, pSlot->m_sName);
    }
  }

  /// The single authored slot, else the slot whose name is closest.
  plString SuggestContext(const plAiEqsQueryAssetObject& props, plStringView sContext)
  {
    plHybridArray<plString, 8> slots;
    CollectContextChoices(props, slots);
    slots.RemoveAtAndCopy(0);

    if (slots.GetCount() == 1)
      return slots[0];

    return FindClosest(sContext, slots);
  }

  void AddChooseContextFix(plAiValidationIssue& ref_issue, const plAiEqsQueryAssetObject& props, plStringView sContext)
  {
    const plString sSuggestion = SuggestContext(props, sContext);

    if (!sSuggestion.IsEmpty())
    {
      plStringBuilder sLabel;
      sLabel.SetFormat("Use slot \"{}\"", sSuggestion);

      plAiValidationFix& fix = AddFix(ref_issue, plAiValidationFix::ChooseContext, sLabel, "Points the element at this context slot");
      fix.m_sValue = sSuggestion;
      return;
    }

    plAiValidationFix& fix = AddFix(ref_issue, plAiValidationFix::ChooseContext, "Choose context", "Points the element at an existing context slot");
    CollectContextChoices(props, fix.m_Choices);
  }

  plString ContextKey(plStringView sContext)
  {
    plStringBuilder sKey;
    sKey.SetFormat("context \"{}\"", sContext);
    return sKey;
  }

  void ValidateEqsSlots(const plAiEqsQueryAssetObject& props, const IssueSource& source, plStringView sMessage, plAiValidationReport& ref_report)
  {
    for (plUInt32 i = 0; i < props.m_ContextSlots.GetCount(); ++i)
    {
      const plAiEqsContextSlotObject* pSlot = props.m_ContextSlots[i];
      const plString sName = pSlot != nullptr ? pSlot->m_sName : plString();

      plStringView sActual;
      if (pSlot == nullptr || pSlot->m_pContext == nullptr)
        sActual = "no provider";
      else if (sName.IsEmpty())
        sActual = "no name";
      else if (sName == "Querier")
        sActual = "reserved name";

      for (plUInt32 j = 0; j < i && sActual.IsEmpty(); ++j)
      {
        if (props.m_ContextSlots[j] != nullptr && props.m_ContextSlots[j]->m_sName == sName)
          sActual = "duplicate name";
      }

      if (sActual.IsEmpty())
        continue;

      plStringBuilder sElement;
      sElement.SetFormat("Context {}: {}", i + 1, sName.IsEmpty() ? plStringView("<unnamed>") : sName.GetView());

      AddIssue(ref_report, source, plAiValidationSeverity::Error, MakeElement(plAiValidationElement::ContextSlot, i), sElement, sName.IsEmpty() ? plStringView("-") : sName.GetView(), "unique name and provider", sActual, sMessage);
      return;
    }
  }

  /// The asset's root validation (the transform fails on it), with its element.
  void ValidateEqsRoot(const plAiEqsQueryAssetObject& props, const IssueSource& source, plAiValidationReport& ref_report)
  {
    const plString sMessage = props.ValidateRoot();
    if (sMessage.IsEmpty())
      return;

    const plAiEqsGenerator* pGenerator = props.m_pGenerator;
    const plAiValidationElement generator = MakeElement(plAiValidationElement::Generator);

    if (pGenerator == nullptr)
    {
      AddIssue(ref_report, source, plAiValidationSeverity::Error, MakeElement(plAiValidationElement::Root), "Generator", "Generator", "generator", "none", sMessage);
      return;
    }

    plStringBuilder sElement("Generator: ", PrettyEqsName(pGenerator->GetDynamicRTTI()));

    if (!props.HasContext(pGenerator->GetCenterContext()))
    {
      plAiValidationIssue& issue = AddIssue(ref_report, source, plAiValidationSeverity::Error, generator, sElement, ContextKey(pGenerator->GetCenterContext()), "context slot", "missing", sMessage);
      AddChooseContextFix(issue, props, pGenerator->GetCenterContext());
      return;
    }

    const bool bRadiusValid = plMath::IsFinite(pGenerator->m_fRadiusMin) && plMath::IsFinite(pGenerator->m_fRadiusMax) && pGenerator->m_fRadiusMin >= 0 && pGenerator->m_fRadiusMin <= pGenerator->m_fRadiusMax;
    if (!bRadiusValid)
    {
      plStringBuilder sActual;
      sActual.SetFormat("{} \xE2\x86\x92 {}", pGenerator->m_fRadiusMin, pGenerator->m_fRadiusMax);
      AddIssue(ref_report, source, plAiValidationSeverity::Error, generator, sElement, "Radius", "0 \xE2\x89\xA4 Min \xE2\x89\xA4 Max", sActual, sMessage);
      return;
    }

    ValidateEqsSlots(props, source, sMessage, ref_report);
  }

  struct TestFacts
  {
    plString m_sKey;
    plString m_sExpected;
    plString m_sActual;
    bool m_bContext = false;
  };

  plString Range(float fMin, float fMax)
  {
    plStringBuilder sRange;
    sRange.SetFormat("{} \xE2\x86\x92 {}", fMin, fMax);
    return sRange;
  }

  /// The filter part of plAiEqsQueryAssetObject::ValidateTest; false when the filter is valid.
  bool DescribeFilterIssue(const plAiEqsTest& test, TestFacts& out_facts)
  {
    const bool bFinite = plMath::IsFinite(test.m_fFilterMin) && plMath::IsFinite(test.m_fFilterMax);
    const bool bBetween = test.m_FilterCondition == plAiEqsFilterCondition::Between;

    if (!bFinite || (bBetween && test.m_fFilterMin > test.m_fFilterMax))
    {
      out_facts.m_sKey = "Filter";
      out_facts.m_sExpected = "finite, Min \xE2\x89\xA4 Max";
      out_facts.m_sActual = Range(test.m_fFilterMin, test.m_fFilterMax);
      return true;
    }

    if (test.m_FilterCondition == plAiEqsFilterCondition::Reachable && !plDynamicCast<const plAiEqsTest_PathLength*>(&test))
    {
      out_facts.m_sKey = "Filter";
      out_facts.m_sExpected = "Path Length test";
      out_facts.m_sActual = PrettyEqsName(test.GetDynamicRTTI());
      return true;
    }

    if (test.m_FilterCondition == plAiEqsFilterCondition::DirectPath && !plDynamicCast<const plAiEqsTest_ReachableApprox*>(&test))
    {
      out_facts.m_sKey = "Filter";
      out_facts.m_sExpected = "Reachable Approx test";
      out_facts.m_sActual = PrettyEqsName(test.GetDynamicRTTI());
      return true;
    }

    return false;
  }

  /// The key, expected and actual value of a test's issue, in the order plAiEqsQueryAssetObject::ValidateTest checks.
  void DescribeTestIssue(const plAiEqsQueryAssetObject& props, const plAiEqsTest& test, TestFacts& out_facts)
  {
    const char* szContext = test.GetContextProperty();
    if (szContext != nullptr && !props.HasContext(szContext))
    {
      out_facts.m_sKey = ContextKey(szContext);
      out_facts.m_sExpected = "context slot";
      out_facts.m_sActual = "missing";
      out_facts.m_bContext = true;
      return;
    }

    if (!plMath::IsFinite(test.m_fWeight) || test.m_fWeight < 0)
    {
      out_facts.m_sKey = "Weight";
      out_facts.m_sExpected = "0 or more";
      out_facts.m_sActual = FormatValue(test.m_fWeight);
      return;
    }

    if (test.m_Purpose != plAiEqsTestPurpose::ScoreOnly && DescribeFilterIssue(test, out_facts))
      return;

    const auto* pDistance = plDynamicCast<const plAiEqsTest_Distance*>(&test);
    const auto* pPath = plDynamicCast<const plAiEqsTest_PathLength*>(&test);
    const float fMin = pDistance ? pDistance->m_fBandMin : (pPath ? pPath->m_fBandMin : 0.0f);
    const float fMax = pDistance ? pDistance->m_fBandMax : (pPath ? pPath->m_fBandMax : 0.0f);

    if (!plMath::IsFinite(fMin) || !plMath::IsFinite(fMax) || fMin < 0 || fMin > fMax)
    {
      out_facts.m_sKey = "Band";
      out_facts.m_sExpected = "0 \xE2\x89\xA4 Min \xE2\x89\xA4 Max";
      out_facts.m_sActual = Range(fMin, fMax);
      return;
    }

    // the remaining checks are about the generator's candidates
    const char* szPayload = props.m_pGenerator != nullptr ? plAiEqsUi::GetPayloadLabel(props.m_pGenerator) : nullptr;

    out_facts.m_sKey = "Candidates";
    out_facts.m_sExpected = plDynamicCast<const plAiEqsTest_Unclaimed*>(&test) ? "cover or smart objects" : "cover points";
    out_facts.m_sActual = plStringUtils::IsNullOrEmpty(szPayload) ? "-" : szPayload;
  }

  void ValidateEqsTest(const plAiEqsQueryAssetObject& props, plUInt32 uiIndex, const IssueSource& source, plAiValidationReport& ref_report)
  {
    const plString sMessage = props.ValidateTest(uiIndex);
    if (sMessage.IsEmpty())
      return;

    const plAiValidationElement element = MakeElement(plAiValidationElement::Test, uiIndex);
    const plAiEqsTestObject* pObject = props.m_Tests[uiIndex];
    const plAiEqsTest* pTest = pObject != nullptr ? pObject->m_pTest : nullptr;

    plStringBuilder sElement;
    sElement.SetFormat("Test {}: {}", uiIndex + 1, PrettyEqsName(pTest != nullptr ? pTest->GetDynamicRTTI() : nullptr));

    if (pTest == nullptr)
    {
      AddIssue(ref_report, source, plAiValidationSeverity::Warning, element, sElement, "Test", "test type", "none", sMessage);
      return;
    }

    TestFacts facts;
    DescribeTestIssue(props, *pTest, facts);

    plAiValidationIssue& issue = AddIssue(ref_report, source, plAiValidationSeverity::Warning, element, sElement, facts.m_sKey, facts.m_sExpected, facts.m_sActual, sMessage);

    if (facts.m_bContext)
      AddChooseContextFix(issue, props, pTest->GetContextProperty());
  }

  void ValidateConfigName(plStringView sName, plArrayPtr<const plString> names, plStringView sProperty, plStringView sKind, const IssueSource& source, plAiValidationReport& ref_report)
  {
    if (sName.IsEmpty() || names.Contains(sName))
      return;

    plStringBuilder sKey;
    sKey.SetFormat("config \"{}\"", sName);

    plStringBuilder sMessage;
    sMessage.SetFormat("The project's AI navigation settings have no {} with this name", sKind);

    plAiValidationIssue& issue = AddIssue(ref_report, source, plAiValidationSeverity::Warning, MakeElement(plAiValidationElement::Root), "Query", sKey, sKind, "not in project settings", sMessage);

    plStringBuilder sLabel("Assign config");
    if (names.GetCount() == 1)
      sLabel.SetFormat("Assign \"{}\"", names[0]);

    plStringBuilder sTooltip;
    sTooltip.SetFormat("Sets the query's {} to one the project settings have", sKind);

    plAiValidationFix& fix = AddFix(issue, plAiValidationFix::AssignConfig, sLabel, sTooltip);
    fix.m_sProperty = sProperty;

    if (names.GetCount() == 1)
    {
      fix.m_sValue = names[0];
      return;
    }

    // the empty choice is the default config
    fix.m_Choices.PushBack(plString());
    for (const plString& sChoice : names)
      fix.m_Choices.PushBack(sChoice);
  }

  void ValidateEqsConfigs(const plAiEqsQueryAssetObject& props, const IssueSource& source, plAiValidationReport& ref_report)
  {
    plAiNavigationConfig config;
    if (config.Load().Failed())
      return;

    plHybridArray<plString, 8> navmeshes;
    for (const plAiNavmeshConfig& navmesh : config.m_NavmeshConfigs)
      navmeshes.PushBack(navmesh.m_sName);

    plHybridArray<plString, 8> searches;
    for (const plAiPathSearchConfig& search : config.m_PathSearchConfigs)
      searches.PushBack(search.m_sName);

    ValidateConfigName(props.m_sNavmeshConfig, navmeshes, "NavmeshConfig", "navmesh config", source, ref_report);
    ValidateConfigName(props.m_sPathSearchConfig, searches, "PathSearchConfig", "path search config", source, ref_report);
  }

  void ValidateEqsDocument(const plAiEqsQueryAssetDocument& document, plAiValidationReport& out_report)
  {
    const plAiEqsQueryAssetObject& props = *document.GetProperties();
    const IssueSource source = MakeSource(ResolveDocument(document), false);

    ValidateEqsRoot(props, source, out_report);

    for (plUInt32 i = 0; i < props.m_Tests.GetCount(); ++i)
      ValidateEqsTest(props, i, source, out_report);

    ValidateEqsConfigs(props, source, out_report);

    out_report.m_sOwner = "Blackboard owner: blackboard component on the querier";
    out_report.m_sOwnerTooltip = "Contexts that read the blackboard use the first blackboard component on the querying agent or its parents.";
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plAiValidation

namespace plAiValidation
{
  void Validate(const plDocument* pDocument, plAiValidationReport& out_report)
  {
    out_report = plAiValidationReport();

    if (pDocument == nullptr)
      return;

    if (auto pBehavior = plDynamicCast<const plAiBehaviorAssetDocument*>(pDocument))
      ValidateBehaviorDocument(*pBehavior, out_report);
    else if (auto pArchetype = plDynamicCast<const plAiArchetypeAssetDocument*>(pDocument))
      ValidateArchetypeDocument(*pArchetype, out_report);
    else if (auto pQuery = plDynamicCast<const plAiEqsQueryAssetDocument*>(pDocument))
      ValidateEqsDocument(*pQuery, out_report);
  }

  void ClearCache()
  {
    s_Files.Clear();
  }
} // namespace plAiValidation
