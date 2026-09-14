#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEqsQueryAsset.h>
#include <Foundation/Serialization/GraphPatch.h>
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

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTestObject, 2, plRTTIDefaultAllocator<plAiEqsTestObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Test", m_pTest)->AddFlags(plPropertyFlags::PointerOwner),
    PL_MEMBER_PROPERTY("LegacyCurveDomain", m_bLegacyCurveDomain),
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

// Preserve authored version-1 curves without modifying their control points.
class plAiEqsTestObjectPatch_1_2 : public plGraphPatch
{
public:
  plAiEqsTestObjectPatch_1_2()
    : plGraphPatch("plAiEqsTestObject", 2)
  {
  }
  void Patch(plGraphPatchContext&, plAbstractObjectGraph*, plAbstractObjectNode* pNode) const override
  {
    pNode->AddProperty("LegacyCurveDomain", true);
  }
};
static plAiEqsTestObjectPatch_1_2 s_plAiEqsTestObjectPatch;

plAiEqsTestObject::~plAiEqsTestObject()
{
  if (m_pTest != nullptr)
  {
    m_pTest->GetDynamicRTTI()->GetAllocator()->Deallocate(m_pTest);
    m_pTest = nullptr;
  }
}

plAiEqsQueryAssetObject::plAiEqsQueryAssetObject() = default;

bool plAiEqsQueryAssetObject::HasContext(const char* szName) const
{
  if (plStringUtils::IsEqual(szName, "Querier"))
    return true;
  for (const auto* slot : m_ContextSlots)
  {
    if (slot != nullptr && slot->m_pContext != nullptr && slot->m_sName == szName)
      return true;
  }
  return false;
}

plString plAiEqsQueryAssetObject::ValidateRoot() const
{
  if (m_pGenerator == nullptr)
    return "Choose a generator.";
  if (!HasContext(m_pGenerator->GetCenterContext()))
    return "Generator center references an unknown context. Add the slot or select Querier.";
  if (!plMath::IsFinite(m_pGenerator->m_fRadiusMin) || !plMath::IsFinite(m_pGenerator->m_fRadiusMax) ||
      m_pGenerator->m_fRadiusMin < 0 || m_pGenerator->m_fRadiusMin > m_pGenerator->m_fRadiusMax)
    return "Generator radius must satisfy 0 <= Min <= Max.";
  for (plUInt32 i = 0; i < m_ContextSlots.GetCount(); ++i)
  {
    const auto* slot = m_ContextSlots[i];
    if (slot == nullptr || slot->m_pContext == nullptr || slot->m_sName.IsEmpty())
      return "Every context slot needs a name and provider.";
    if (slot->m_sName == "Querier")
      return "Querier is a reserved context name.";
    for (plUInt32 j = 0; j < i; ++j)
    {
      if (m_ContextSlots[j] != nullptr && m_ContextSlots[j]->m_sName == slot->m_sName)
        return "Context names must be unique.";
    }
  }
  return {};
}

plString plAiEqsQueryAssetObject::ValidateTest(plUInt32 uiIndex) const
{
  const auto* object = m_Tests[uiIndex];
  if (object == nullptr || object->m_pTest == nullptr)
    return "Choose a test type.";
  const auto* test = object->m_pTest;
  if (const char* context = test->GetContextProperty())
  {
    if (!HasContext(context))
      return "Unknown context. Add a matching context slot (names are case sensitive).";
  }
  if (!plMath::IsFinite(test->m_fWeight) || test->m_fWeight < 0)
    return "Weight must be finite and non-negative.";
  if (test->m_Purpose != plAiEqsTestPurpose::ScoreOnly)
  {
    if (!plMath::IsFinite(test->m_fFilterMin) || !plMath::IsFinite(test->m_fFilterMax))
      return "Filter limits must be finite.";
    if (test->m_FilterCondition == plAiEqsFilterCondition::Between && test->m_fFilterMin > test->m_fFilterMax)
      return "Filter Min must not exceed Max.";
    if (test->m_FilterCondition == plAiEqsFilterCondition::Reachable && !plDynamicCast<const plAiEqsTest_PathLength*>(test))
      return "Reachable requires a Path Length test. Approximate reachability only proves a direct path.";
    if (test->m_FilterCondition == plAiEqsFilterCondition::DirectPath && !plDynamicCast<const plAiEqsTest_ReachableApprox*>(test))
      return "Direct path requires a Reachable Approx test.";
  }
  const auto* distance = plDynamicCast<const plAiEqsTest_Distance*>(test);
  const auto* path = plDynamicCast<const plAiEqsTest_PathLength*>(test);
  const float min = distance ? distance->m_fBandMin : (path ? path->m_fBandMin : 0);
  const float max = distance ? distance->m_fBandMax : (path ? path->m_fBandMax : 0);
  if (!plMath::IsFinite(min) || !plMath::IsFinite(max) || min < 0 || min > max)
    return "Scoring band must satisfy 0 <= BandMin <= BandMax.";
  if (m_pGenerator != nullptr)
  {
    const auto payload = m_pGenerator->GetPayloadType();
    if ((plDynamicCast<const plAiEqsTest_CoverQuality*>(test) || plDynamicCast<const plAiEqsTest_CoverFacing*>(test)) &&
        payload != plAiEqsPayloadType::CoverPoint)
      return "This test requires a Cover Points generator; otherwise its missing-data policy applies.";
    if (plDynamicCast<const plAiEqsTest_Unclaimed*>(test) && payload != plAiEqsPayloadType::CoverPoint && payload != plAiEqsPayloadType::SmartObjectSlot)
      return "Unclaimed requires cover or smart-object candidates; otherwise its missing-data policy applies.";
  }
  return {};
}

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
  const plString rootError = pProp->ValidateRoot();
  if (!rootError.IsEmpty())
    return plStatus(rootError.GetView());
  for (plUInt32 i = 0; i < pProp->m_Tests.GetCount(); ++i)
  {
    const plString issue = pProp->ValidateTest(i);
    if (!issue.IsEmpty())
      plLog::Warning("EQS test {}: {}", i + 1, issue);
  }

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
    testDesc.m_bLegacyCurveDomain = pTest->m_bLegacyCurveDomain;
  }

  PL_SUCCEED_OR_RETURN(desc.Serialize(inout_stream));

  return plStatus(PL_SUCCESS);
}

plTransformStatus plAiEqsQueryAssetDocument::InternalTransformAsset(plStreamWriter& inout_stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  return WriteAsset(inout_stream, pAssetProfile);
}
