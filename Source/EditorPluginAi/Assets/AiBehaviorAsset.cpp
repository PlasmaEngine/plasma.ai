#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiBehaviorAsset.h>
#include <Foundation/Serialization/GraphPatch.h>
#include <Foundation/Serialization/ReflectionSerializer.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiConsiderationObject, 2, plRTTIDefaultAllocator<plAiConsiderationObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Input", m_pInput)->AddFlags(plPropertyFlags::PointerOwner),
    PL_MEMBER_PROPERTY("LegacyCurveDomain", m_bLegacyCurveDomain),
    PL_MEMBER_PROPERTY("InputMin", m_fInputMin),
    PL_MEMBER_PROPERTY("InputMax", m_fInputMax)->AddAttributes(new plDefaultValueAttribute(1.0f)),
    PL_MEMBER_PROPERTY("ResponseCurve", m_ResponseCurve)->AddAttributes(new plCurveExtentsAttribute(0.0f, true, 1.0f, true), new plClampValueAttribute(0.0, 1.0), new plDefaultValueAttribute(1.0)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiBehaviorAssetObject, 1, plRTTIDefaultAllocator<plAiBehaviorAssetObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_ENUM_MEMBER_PROPERTY("Category", plAiBehaviorCategory, m_Category),
    PL_MEMBER_PROPERTY("Weight", m_fWeight)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10.0f)),
    PL_MEMBER_PROPERTY("CommitBonus", m_fCommitBonus)->AddAttributes(new plDefaultValueAttribute(0.2f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("Cooldown", m_CooldownDuration),
    PL_ARRAY_MEMBER_PROPERTY("Considerations", m_Considerations)->AddFlags(plPropertyFlags::PointerOwner),
    PL_MEMBER_PROPERTY("Logic", m_pLogic)->AddFlags(plPropertyFlags::PointerOwner),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiBehaviorAssetDocument, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiConsiderationObject::plAiConsiderationObject() = default;

// Preserve authored version-1 curves without modifying their control points.
class plAiConsiderationObjectPatch_1_2 : public plGraphPatch
{
public:
  plAiConsiderationObjectPatch_1_2()
    : plGraphPatch("plAiConsiderationObject", 2)
  {
  }
  void Patch(plGraphPatchContext&, plAbstractObjectGraph*, plAbstractObjectNode* pNode) const override
  {
    pNode->AddProperty("LegacyCurveDomain", true);
  }
};
static plAiConsiderationObjectPatch_1_2 s_plAiConsiderationObjectPatch;

plAiConsiderationObject::~plAiConsiderationObject()
{
  if (m_pInput != nullptr)
  {
    m_pInput->GetDynamicRTTI()->GetAllocator()->Deallocate(m_pInput);
    m_pInput = nullptr;
  }
}

plAiBehaviorAssetObject::plAiBehaviorAssetObject() = default;

plAiBehaviorAssetObject::~plAiBehaviorAssetObject()
{
  for (auto pConsideration : m_Considerations)
  {
    pConsideration->GetDynamicRTTI()->GetAllocator()->Deallocate(pConsideration);
  }

  m_Considerations.Clear();

  if (m_pLogic != nullptr)
  {
    m_pLogic->GetDynamicRTTI()->GetAllocator()->Deallocate(m_pLogic);
    m_pLogic = nullptr;
  }
}

plAiBehaviorAssetDocument::plAiBehaviorAssetDocument(plStringView sDocumentPath)
  : plSimpleAssetDocument<plAiBehaviorAssetObject>(sDocumentPath, plAssetDocEngineConnection::None)
{
}

namespace
{
  template <typename T>
  plUniquePtr<T> CloneReflected(const T* pSource)
  {
    if (pSource == nullptr)
      return nullptr;

    const plRTTI* pRtti = pSource->GetDynamicRTTI();
    plUniquePtr<T> pClone = pRtti->GetAllocator()->template Allocate<T>();
    plReflectionSerializer::Clone(pSource, pClone.Borrow(), pRtti);
    return pClone;
  }
} // namespace

plStatus plAiBehaviorAssetDocument::WriteAsset(plStreamWriter& inout_stream, const plPlatformProfile* pAssetProfile) const
{
  const plAiBehaviorAssetObject* pProp = GetProperties();

  plAiBehaviorResourceDescriptor desc;

  if (!pProp->m_sName.IsEmpty())
  {
    desc.m_sName.Assign(pProp->m_sName);
  }
  else
  {
    plStringBuilder sAssetName = plPathUtils::GetFileName(GetDocumentPath());
    desc.m_sName.Assign(sAssetName);
  }

  desc.m_Category = pProp->m_Category;
  desc.m_fWeight = pProp->m_fWeight;
  desc.m_fCommitBonus = pProp->m_fCommitBonus;
  desc.m_CooldownDuration = pProp->m_CooldownDuration;

  for (const plAiConsiderationObject* pConsideration : pProp->m_Considerations)
  {
    if (pConsideration == nullptr)
      continue;

    auto& considerationDesc = desc.m_Considerations.ExpandAndGetRef();
    considerationDesc.m_pInput = CloneReflected(pConsideration->m_pInput);
    considerationDesc.m_fInputMin = pConsideration->m_fInputMin;
    considerationDesc.m_fInputMax = pConsideration->m_fInputMax;
    pConsideration->m_ResponseCurve.ConvertToRuntimeData(considerationDesc.m_ResponseCurve);
    considerationDesc.m_bLegacyCurveDomain = pConsideration->m_bLegacyCurveDomain;
  }

  desc.m_pLogic = CloneReflected(pProp->m_pLogic);

  if (desc.m_pLogic == nullptr)
  {
    return plStatus("AI Behavior asset has no execution 'Logic' configured.");
  }

  PL_SUCCEED_OR_RETURN(desc.Serialize(inout_stream));

  return plStatus(PL_SUCCESS);
}

plTransformStatus plAiBehaviorAssetDocument::InternalTransformAsset(plStreamWriter& inout_stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  return WriteAsset(inout_stream, pAssetProfile);
}
