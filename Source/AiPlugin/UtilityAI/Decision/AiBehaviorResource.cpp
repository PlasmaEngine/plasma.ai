#include <AiPlugin/AiPluginPCH.h>
#include <AiPlugin/Utils/AiResponseCurve.h>

#include <AiPlugin/UtilityAI/Decision/AiBehaviorResource.h>
#include <Foundation/Serialization/ReflectionSerializer.h>
#include <Foundation/Utilities/AssetFileHeader.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiBehaviorResource, 1, plRTTIDefaultAllocator<plAiBehaviorResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_RESOURCE_IMPLEMENT_COMMON_CODE(plAiBehaviorResource);
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
      plLog::Error("Unknown or invalid AI object type '{}'", sTypeName);
      return PL_FAILURE;
    }

    out_pObject = pRtti->GetAllocator()->Allocate<T>();
    plReflectionSerializer::ReadObjectPropertiesFromBinary(inout_stream, *pRtti, out_pObject.Borrow());
    return PL_SUCCESS;
  }
} // namespace

void plAiConsiderationDesc::PrepareCurve()
{
  if (m_ResponseCurve.IsEmpty())
    return;

  m_ResponseCurve.SortControlPoints();
  m_ResponseCurve.ApplyTangentModes();
  m_ResponseCurve.ClampTangents();
  m_ResponseCurve.CreateLinearApproximation();
}

float plAiConsiderationDesc::Evaluate(const plAiScoringContext& context) const
{
  if (m_pInput == nullptr)
    return 1.0f; // an unconfigured consideration doesn't influence the score

  const float fRaw = m_pInput->Evaluate(context);

  const float fRange = m_fInputMax - m_fInputMin;
  float fNormalized = (plMath::Abs(fRange) < plMath::SmallEpsilon<float>()) ? 0.0f : (fRaw - m_fInputMin) / fRange;
  fNormalized = plMath::Clamp(fNormalized, 0.0f, 1.0f);

  if (m_ResponseCurve.IsEmpty())
    return fNormalized;

  return plAiEvaluateResponseCurve(m_ResponseCurve, fNormalized, m_bLegacyCurveDomain);
}

//////////////////////////////////////////////////////////////////////////

plAiBehaviorResourceDescriptor::plAiBehaviorResourceDescriptor() = default;
plAiBehaviorResourceDescriptor::~plAiBehaviorResourceDescriptor() = default;

bool plAiBehaviorResourceDescriptor::NeedsTarget() const
{
  for (const auto& consideration : m_Considerations)
  {
    if (consideration.m_pInput != nullptr && consideration.m_pInput->NeedsTarget())
      return true;
  }

  return false;
}

void plAiBehaviorResourceDescriptor::CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const
{
  for (const auto& consideration : m_Considerations)
  {
    if (consideration.m_pInput != nullptr)
    {
      consideration.m_pInput->CollectBlackboardEntries(out_entries);
    }
  }
}

plResult plAiBehaviorResourceDescriptor::Serialize(plStreamWriter& inout_stream) const
{
  inout_stream.WriteVersion(2);

  inout_stream << m_sName;
  inout_stream << m_Category;
  inout_stream << m_fWeight;
  inout_stream << m_fCommitBonus;
  inout_stream << m_CooldownDuration;

  inout_stream << m_Considerations.GetCount();

  for (const auto& consideration : m_Considerations)
  {
    WriteReflectedObject(inout_stream, consideration.m_pInput.Borrow());
    inout_stream << consideration.m_fInputMin;
    inout_stream << consideration.m_fInputMax;
    consideration.m_ResponseCurve.Save(inout_stream);
    inout_stream << consideration.m_bLegacyCurveDomain;
  }

  WriteReflectedObject(inout_stream, m_pLogic.Borrow());

  return PL_SUCCESS;
}

plResult plAiBehaviorResourceDescriptor::Deserialize(plStreamReader& inout_stream)
{
  const auto version = inout_stream.ReadVersion(2);

  inout_stream >> m_sName;
  inout_stream >> m_Category;
  inout_stream >> m_fWeight;
  inout_stream >> m_fCommitBonus;
  inout_stream >> m_CooldownDuration;

  plUInt32 uiNumConsiderations = 0;
  inout_stream >> uiNumConsiderations;

  m_Considerations.Clear();
  m_Considerations.Reserve(uiNumConsiderations);

  for (plUInt32 i = 0; i < uiNumConsiderations; ++i)
  {
    auto& consideration = m_Considerations.ExpandAndGetRef();

    PL_SUCCEED_OR_RETURN(ReadReflectedObject(inout_stream, consideration.m_pInput));
    inout_stream >> consideration.m_fInputMin;
    inout_stream >> consideration.m_fInputMax;
    consideration.m_ResponseCurve.Load(inout_stream);
    consideration.m_bLegacyCurveDomain = true;
    if (version >= 2)
      inout_stream >> consideration.m_bLegacyCurveDomain;
    consideration.PrepareCurve();
  }

  PL_SUCCEED_OR_RETURN(ReadReflectedObject(inout_stream, m_pLogic));

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

plAiBehaviorResource::plAiBehaviorResource()
  : plResource(DoUpdate::OnAnyThread, 1)
{
}

plAiBehaviorResource::~plAiBehaviorResource() = default;

plResourceLoadDesc plAiBehaviorResource::UnloadData(Unload WhatToUnload)
{
  m_Descriptor = plAiBehaviorResourceDescriptor();

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Unloaded;

  return res;
}

plResourceLoadDesc plAiBehaviorResource::UpdateContent(plStreamReader* Stream)
{
  PL_LOG_BLOCK("plAiBehaviorResource::UpdateContent", GetResourceIdOrDescription());

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

  plAiBehaviorResourceDescriptor desc;
  if (desc.Deserialize(*Stream).Failed())
  {
    res.m_State = plResourceState::LoadedResourceMissing;
    return res;
  }

  CreateResource(std::move(desc));

  res.m_State = plResourceState::Loaded;
  return res;
}

void plAiBehaviorResource::UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage)
{
  out_NewMemoryUsage.m_uiMemoryGPU = 0;
  out_NewMemoryUsage.m_uiMemoryCPU = sizeof(plAiBehaviorResource);
  out_NewMemoryUsage.m_uiMemoryCPU += m_Descriptor.m_Considerations.GetHeapMemoryUsage();
}

PL_RESOURCE_IMPLEMENT_CREATEABLE(plAiBehaviorResource, plAiBehaviorResourceDescriptor)
{
  m_Descriptor = std::move(descriptor);

  for (auto& consideration : m_Descriptor.m_Considerations)
  {
    consideration.PrepareCurve();
  }

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Loaded;

  return res;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiBehaviorResource);
