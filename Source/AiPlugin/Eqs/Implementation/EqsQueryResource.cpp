#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/EqsQueryResource.h>
#include <AiPlugin/Utils/AiResponseCurve.h>
#include <Foundation/Serialization/ReflectionSerializer.h>
#include <Foundation/Utilities/AssetFileHeader.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiEqsPayloadType, 1)
  PL_ENUM_CONSTANTS(plAiEqsPayloadType::None, plAiEqsPayloadType::CoverPoint, plAiEqsPayloadType::SmartObjectSlot, plAiEqsPayloadType::GameObject)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiEqsRunMode, 1)
  PL_ENUM_CONSTANTS(plAiEqsRunMode::SingleBest, plAiEqsRunMode::RandomOfTopPercent, plAiEqsRunMode::AllMatching)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsQueryResource, 1, plRTTIDefaultAllocator<plAiEqsQueryResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_RESOURCE_IMPLEMENT_COMMON_CODE(plAiEqsQueryResource);
// clang-format on

namespace
{
  void WriteReflectedObject(plStreamWriter& inout_stream, const plReflectedClass* pObject)
  {
    const bool bHasObject = pObject != nullptr;
    inout_stream << bHasObject;

    if (bHasObject)
    {
      inout_stream << pObject->GetDynamicRTTI()->GetTypeName();
      plReflectionSerializer::WriteObjectToBinary(inout_stream, pObject->GetDynamicRTTI(), pObject);
    }
  }

  template <typename T>
  plResult ReadReflectedObject(plStreamReader& inout_stream, plUniquePtr<T>& out_pObject)
  {
    out_pObject.Clear();

    bool bHasObject = false;
    inout_stream >> bHasObject;

    if (!bHasObject)
      return PL_SUCCESS;

    plStringBuilder sTypeName;
    inout_stream >> sTypeName;

    const plRTTI* pRtti = plRTTI::FindTypeByName(sTypeName);
    if (pRtti == nullptr || !pRtti->IsDerivedFrom<T>() || pRtti->GetAllocator() == nullptr || !pRtti->GetAllocator()->CanAllocate())
    {
      plLog::Error("Unknown or invalid EQS object type '{}'", sTypeName);
      return PL_FAILURE;
    }

    out_pObject = pRtti->GetAllocator()->Allocate<T>();
    plReflectionSerializer::ReadObjectPropertiesFromBinary(inout_stream, *pRtti, out_pObject.Borrow());
    return PL_SUCCESS;
  }
} // namespace

plAiEqsContextSlotDesc::plAiEqsContextSlotDesc() = default;
plAiEqsContextSlotDesc::~plAiEqsContextSlotDesc() = default;

plAiEqsTestDesc::plAiEqsTestDesc() = default;
plAiEqsTestDesc::~plAiEqsTestDesc() = default;

void plAiEqsTestDesc::PrepareCurve()
{
  if (m_ScoreCurve.IsEmpty())
    return;

  m_ScoreCurve.SortControlPoints();
  m_ScoreCurve.ApplyTangentModes();
  m_ScoreCurve.ClampTangents();
  m_ScoreCurve.CreateLinearApproximation();
}

float plAiEqsTestDesc::ApplyCurve(float fRaw) const
{
  fRaw = plMath::Clamp(fRaw, 0.0f, 1.0f);

  if (m_ScoreCurve.IsEmpty())
    return fRaw;

  return plAiEvaluateResponseCurve(m_ScoreCurve, fRaw, m_bLegacyCurveDomain);
}

//////////////////////////////////////////////////////////////////////////

bool plAiEqsTestDesc::ProcessItem(const plAiEqsTest& test, plAiEqsItem& item, plUInt32 uiTestIndex) const
{
  plAiEqsTestTrace trace;
  const bool nonFinite = !plMath::IsFinite(item.m_fRaw) || (item.m_bHasMeasurement && !plMath::IsFinite(item.m_fMeasurement));
  if (nonFinite)
    item.m_TestData = plAiEqsTestData::InvalidMeasurement;
  trace.m_bEvaluated = true;
  trace.m_Data = item.m_TestData;
  trace.m_fMeasurement = item.m_bHasMeasurement ? item.m_fMeasurement : item.m_fRaw;
  trace.m_fRaw = item.m_fRaw;
  const bool valid = item.m_TestData == plAiEqsTestData::Valid;
  const bool legacy = test.m_MissingDataPolicy == plAiEqsMissingDataPolicy::Legacy && !nonFinite;
  const bool failQuery = !valid && test.m_MissingDataPolicy == plAiEqsMissingDataPolicy::FailQuery;
  trace.m_bSkipped = !valid && test.m_MissingDataPolicy == plAiEqsMissingDataPolicy::SkipTest;

  if (!trace.m_bSkipped)
  {
    const auto purpose = static_cast<plAiEqsTestPurpose::Enum>(test.m_Purpose.GetValue());
    // Compatibility uses the old raw decision for unavailable data. Never invert unavailable data.
    trace.m_bPassed = valid ? (!plAiEqsTestPurpose::Filters(purpose) || test.PassesFilter(item))
                            : (legacy && (!plAiEqsTestPurpose::Filters(purpose) || item.m_fRaw > 0.0f));
    trace.m_fCurved = (valid || legacy) ? ApplyCurve(item.m_fRaw) : 0.0f;
    if (valid && test.m_bInvertScore)
      trace.m_fCurved = 1.0f - trace.m_fCurved;
    if (!trace.m_bPassed)
    {
      item.m_bDiscarded = true;
      item.m_uiRejectedBy = static_cast<plUInt16>(uiTestIndex);
    }
    else if (plAiEqsTestPurpose::Scores(purpose))
    {
      trace.m_fContribution = test.m_fWeight * trace.m_fCurved;
      item.m_fScoreSum += trace.m_fContribution;
      item.m_fWeightSum += test.m_fWeight;
    }
  }
  if (uiTestIndex < plAiEqsItem::MaxRecordedTests)
  {
    item.m_TestTrace[uiTestIndex] = trace;
    item.m_TestScores[uiTestIndex] = trace.m_fCurved;
  }
  return !failQuery;
}

plAiEqsQueryDesc::plAiEqsQueryDesc() = default;
plAiEqsQueryDesc::~plAiEqsQueryDesc() = default;

plResult plAiEqsQueryDesc::Serialize(plStreamWriter& inout_stream) const
{
  inout_stream.WriteVersion(2);

  inout_stream << m_sName;
  inout_stream << m_RunMode;
  inout_stream << m_fTopPercent;
  inout_stream << m_uiCandidates;
  inout_stream << m_uiMaxResults;
  inout_stream << m_sNavmeshConfig;
  inout_stream << m_sPathSearchConfig;

  inout_stream << m_ContextSlots.GetCount();

  for (const auto& slot : m_ContextSlots)
  {
    inout_stream << slot.m_sName;
    WriteReflectedObject(inout_stream, slot.m_pContext.Borrow());
  }

  WriteReflectedObject(inout_stream, m_pGenerator.Borrow());

  inout_stream << m_Tests.GetCount();

  for (const auto& test : m_Tests)
  {
    WriteReflectedObject(inout_stream, test.m_pTest.Borrow());
    test.m_ScoreCurve.Save(inout_stream);
    inout_stream << test.m_bLegacyCurveDomain;
  }

  return PL_SUCCESS;
}

plResult plAiEqsQueryDesc::Deserialize(plStreamReader& inout_stream)
{
  const auto version = inout_stream.ReadVersion(2);

  inout_stream >> m_sName;
  inout_stream >> m_RunMode;
  inout_stream >> m_fTopPercent;
  inout_stream >> m_uiCandidates;
  inout_stream >> m_uiMaxResults;
  inout_stream >> m_sNavmeshConfig;
  inout_stream >> m_sPathSearchConfig;

  plUInt32 uiNumSlots = 0;
  inout_stream >> uiNumSlots;

  m_ContextSlots.Clear();
  m_ContextSlots.Reserve(uiNumSlots);

  for (plUInt32 i = 0; i < uiNumSlots; ++i)
  {
    auto& slot = m_ContextSlots.ExpandAndGetRef();
    inout_stream >> slot.m_sName;
    PL_SUCCEED_OR_RETURN(ReadReflectedObject(inout_stream, slot.m_pContext));
  }

  PL_SUCCEED_OR_RETURN(ReadReflectedObject(inout_stream, m_pGenerator));

  plUInt32 uiNumTests = 0;
  inout_stream >> uiNumTests;

  m_Tests.Clear();
  m_Tests.Reserve(uiNumTests);

  for (plUInt32 i = 0; i < uiNumTests; ++i)
  {
    auto& test = m_Tests.ExpandAndGetRef();
    PL_SUCCEED_OR_RETURN(ReadReflectedObject(inout_stream, test.m_pTest));
    if (version == 1 && test.m_pTest != nullptr)
      test.m_pTest->m_MissingDataPolicy = plAiEqsMissingDataPolicy::Legacy;
    test.m_ScoreCurve.Load(inout_stream);
    test.m_bLegacyCurveDomain = true;
    if (version >= 2)
      inout_stream >> test.m_bLegacyCurveDomain;
    test.PrepareCurve();
  }

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

plAiEqsQueryResource::plAiEqsQueryResource()
  : plResource(DoUpdate::OnAnyThread, 1)
{
}

plAiEqsQueryResource::~plAiEqsQueryResource() = default;

plResourceLoadDesc plAiEqsQueryResource::UnloadData(Unload WhatToUnload)
{
  m_Descriptor = plAiEqsQueryDesc();

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Unloaded;

  return res;
}

plResourceLoadDesc plAiEqsQueryResource::UpdateContent(plStreamReader* Stream)
{
  PL_LOG_BLOCK("plAiEqsQueryResource::UpdateContent", GetResourceIdOrDescription());

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;

  if (Stream == nullptr)
  {
    res.m_State = plResourceState::LoadedResourceMissing;
    return res;
  }

  // skip the absolute file path data that the standard file reader writes into the stream
  plStringBuilder sAbsFilePath;
  (*Stream) >> sAbsFilePath;

  // skip the asset file header at the start of the file
  plAssetFileHeader AssetHash;
  AssetHash.Read(*Stream).IgnoreResult();

  plAiEqsQueryDesc desc;
  if (desc.Deserialize(*Stream).Failed())
  {
    res.m_State = plResourceState::LoadedResourceMissing;
    return res;
  }

  CreateResource(std::move(desc));

  res.m_State = plResourceState::Loaded;
  return res;
}

void plAiEqsQueryResource::UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage)
{
  out_NewMemoryUsage.m_uiMemoryGPU = 0;
  out_NewMemoryUsage.m_uiMemoryCPU = sizeof(plAiEqsQueryResource);
  out_NewMemoryUsage.m_uiMemoryCPU += m_Descriptor.m_Tests.GetHeapMemoryUsage();
  out_NewMemoryUsage.m_uiMemoryCPU += m_Descriptor.m_ContextSlots.GetHeapMemoryUsage();
}

PL_RESOURCE_IMPLEMENT_CREATEABLE(plAiEqsQueryResource, plAiEqsQueryDesc)
{
  m_Descriptor = std::move(descriptor);

  for (auto& test : m_Descriptor.m_Tests)
  {
    test.PrepareCurve();
  }

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Loaded;

  return res;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Eqs_Implementation_EqsQueryResource);
