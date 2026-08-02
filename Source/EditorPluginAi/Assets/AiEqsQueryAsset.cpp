#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEqsQueryAsset.h>
#include <Foundation/Serialization/ReflectionSerializer.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsContextSlotObject, 1, plRTTIDefaultAllocator<plAiEqsContextSlotObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_MEMBER_PROPERTY("Context", m_pContext)->AddFlags(plPropertyFlags::PointerOwner),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTestObject, 1, plRTTIDefaultAllocator<plAiEqsTestObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Test", m_pTest)->AddFlags(plPropertyFlags::PointerOwner),
    PL_MEMBER_PROPERTY("ScoreCurve", m_ScoreCurve)->AddAttributes(new plCurveExtentsAttribute(0.0f, true, 1.0f, true), new plClampValueAttribute(0.0, 1.0), new plDefaultValueAttribute(1.0)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsQueryAssetObject, 1, plRTTIDefaultAllocator<plAiEqsQueryAssetObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_ENUM_MEMBER_PROPERTY("RunMode", plAiEqsRunMode, m_RunMode),
    PL_MEMBER_PROPERTY("TopPercent", m_fTopPercent)->AddAttributes(new plDefaultValueAttribute(25.0f), new plClampValueAttribute(1.0f, 100.0f)),
    PL_MEMBER_PROPERTY("Candidates", m_uiCandidates)->AddAttributes(new plDefaultValueAttribute(32), new plClampValueAttribute(4, 64)),
    PL_MEMBER_PROPERTY("MaxResults", m_uiMaxResults)->AddAttributes(new plDefaultValueAttribute(8), new plClampValueAttribute(1, 16)),
    PL_MEMBER_PROPERTY("NavmeshConfig", m_sNavmeshConfig)->AddAttributes(new plDynamicStringEnumAttribute("AiNavmeshConfig")),
    PL_MEMBER_PROPERTY("PathSearchConfig", m_sPathSearchConfig)->AddAttributes(new plDynamicStringEnumAttribute("AiPathSearchConfig")),
    PL_ARRAY_MEMBER_PROPERTY("ContextSlots", m_ContextSlots)->AddFlags(plPropertyFlags::PointerOwner),
    PL_MEMBER_PROPERTY("Generator", m_pGenerator)->AddFlags(plPropertyFlags::PointerOwner),
    PL_ARRAY_MEMBER_PROPERTY("Tests", m_Tests)->AddFlags(plPropertyFlags::PointerOwner),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsQueryAssetDocument, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiEqsContextSlotObject::plAiEqsContextSlotObject() = default;

plAiEqsContextSlotObject::~plAiEqsContextSlotObject()
{
  if (m_pContext != nullptr)
  {
    m_pContext->GetDynamicRTTI()->GetAllocator()->Deallocate(m_pContext);
    m_pContext = nullptr;
  }
}

plAiEqsTestObject::plAiEqsTestObject() = default;

plAiEqsTestObject::~plAiEqsTestObject()
{
  if (m_pTest != nullptr)
  {
    m_pTest->GetDynamicRTTI()->GetAllocator()->Deallocate(m_pTest);
    m_pTest = nullptr;
  }
}

plAiEqsQueryAssetObject::plAiEqsQueryAssetObject() = default;

plAiEqsQueryAssetObject::~plAiEqsQueryAssetObject()
{
  for (auto pSlot : m_ContextSlots)
  {
    pSlot->GetDynamicRTTI()->GetAllocator()->Deallocate(pSlot);
  }

  m_ContextSlots.Clear();

  for (auto pTest : m_Tests)
  {
    pTest->GetDynamicRTTI()->GetAllocator()->Deallocate(pTest);
  }

  m_Tests.Clear();

  if (m_pGenerator != nullptr)
  {
    m_pGenerator->GetDynamicRTTI()->GetAllocator()->Deallocate(m_pGenerator);
    m_pGenerator = nullptr;
  }
}

plAiEqsQueryAssetDocument::plAiEqsQueryAssetDocument(plStringView sDocumentPath)
  : plSimpleAssetDocument<plAiEqsQueryAssetObject>(sDocumentPath, plAssetDocEngineConnection::None)
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

plStatus plAiEqsQueryAssetDocument::WriteAsset(plStreamWriter& inout_stream, const plPlatformProfile* pAssetProfile) const
{
  const plAiEqsQueryAssetObject* pProp = GetProperties();

  plAiEqsQueryDesc desc;

  if (!pProp->m_sName.IsEmpty())
  {
    desc.m_sName.Assign(pProp->m_sName);
  }
  else
  {
    plStringBuilder sAssetName = plPathUtils::GetFileName(GetDocumentPath());
    desc.m_sName.Assign(sAssetName);
  }

  desc.m_RunMode = pProp->m_RunMode;
  desc.m_fTopPercent = pProp->m_fTopPercent;
  desc.m_uiCandidates = pProp->m_uiCandidates;
  desc.m_uiMaxResults = pProp->m_uiMaxResults;
  desc.m_sNavmeshConfig.Assign(pProp->m_sNavmeshConfig);
  desc.m_sPathSearchConfig.Assign(pProp->m_sPathSearchConfig);

  for (const plAiEqsContextSlotObject* pSlot : pProp->m_ContextSlots)
  {
    if (pSlot == nullptr || pSlot->m_pContext == nullptr || pSlot->m_sName.IsEmpty())
      continue;

    auto& slotDesc = desc.m_ContextSlots.ExpandAndGetRef();
    slotDesc.m_sName.Assign(pSlot->m_sName);
    slotDesc.m_pContext = CloneReflected(pSlot->m_pContext);
  }

  desc.m_pGenerator = CloneReflected(pProp->m_pGenerator);

  if (desc.m_pGenerator == nullptr)
  {
    return plStatus("EQS query asset has no Generator configured.");
  }

  for (const plAiEqsTestObject* pTest : pProp->m_Tests)
  {
    if (pTest == nullptr || pTest->m_pTest == nullptr)
      continue;

    auto& testDesc = desc.m_Tests.ExpandAndGetRef();
    testDesc.m_pTest = CloneReflected(pTest->m_pTest);
    pTest->m_ScoreCurve.ConvertToRuntimeData(testDesc.m_ScoreCurve);
  }

  PL_SUCCEED_OR_RETURN(desc.Serialize(inout_stream));

  return plStatus(PL_SUCCESS);
}

plTransformStatus plAiEqsQueryAssetDocument::InternalTransformAsset(plStreamWriter& inout_stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  return WriteAsset(inout_stream, pAssetProfile);
}
