#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Decision/AiArchetypeResource.h>
#include <AiPlugin/UtilityAI/Perception/AiPerception.h>
#include <Foundation/Utilities/AssetFileHeader.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiArchetypeResource, 1, plRTTIDefaultAllocator<plAiArchetypeResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_RESOURCE_IMPLEMENT_COMMON_CODE(plAiArchetypeResource);
// clang-format on

namespace
{
  template <typename T>
  void WriteResourceHandle(plStreamWriter& inout_stream, const plTypedResourceHandle<T>& hResource)
  {
    inout_stream << (hResource.IsValid() ? plStringView(hResource.GetResourceID()) : plStringView());
  }

  template <typename T>
  void ReadResourceHandle(plStreamReader& inout_stream, plTypedResourceHandle<T>& out_hResource)
  {
    plStringBuilder sResourceId;
    inout_stream >> sResourceId;

    out_hResource.Invalidate();

    if (!sResourceId.IsEmpty())
    {
      out_hResource = plResourceManager::LoadResource<T>(sResourceId);
    }
  }

  void WriteSense(plStreamWriter& inout_stream, const plAiSenseProfile& sense)
  {
    inout_stream << sense.m_bEnabled;
    inout_stream << sense.m_bUseSightSettings;
    inout_stream << sense.m_fRangeScale;
    inout_stream << sense.m_fStrengthScale;
    inout_stream << sense.m_fDecayPerSecond;
    inout_stream << sense.m_MemoryDuration;
  }

  void ReadSense(plStreamReader& inout_stream, plAiSenseProfile& out_sense)
  {
    inout_stream >> out_sense.m_bEnabled;
    inout_stream >> out_sense.m_bUseSightSettings;
    inout_stream >> out_sense.m_fRangeScale;
    inout_stream >> out_sense.m_fStrengthScale;
    inout_stream >> out_sense.m_fDecayPerSecond;
    inout_stream >> out_sense.m_MemoryDuration;
  }
} // namespace

const plAiSenseProfile* plAiArchetypeResourceDescriptor::GetSenseProfile(plUInt8 uiStimulusType) const
{
  switch (uiStimulusType)
  {
    case plAiStimulusType::Sound:
      return &m_Hearing;
    case plAiStimulusType::Damage:
      return &m_Damage;
    case plAiStimulusType::Custom:
      return &m_Custom;
  }

  return nullptr;
}

plResult plAiArchetypeResourceDescriptor::Serialize(plStreamWriter& inout_stream) const
{
  inout_stream.WriteVersion(2);

  inout_stream << m_Behaviors.GetCount();

  for (const auto& entry : m_Behaviors)
  {
    WriteResourceHandle(inout_stream, entry.m_hBehavior);
    inout_stream << entry.m_fWeightScale;
  }

  WriteResourceHandle(inout_stream, m_hBlackboardTemplate);

  inout_stream << m_sTeam;
  inout_stream << m_sSensorObjectName;
  inout_stream << m_fConfidenceGainPerSecond;
  inout_stream << m_fConfidenceDecayPerSecond;
  inout_stream << m_TargetMemoryDuration;

  // version 2
  WriteSense(inout_stream, m_Hearing);
  WriteSense(inout_stream, m_Damage);
  WriteSense(inout_stream, m_Custom);
  inout_stream << m_uiMaxTargets;
  inout_stream << m_bWakeOnStimulus;
  inout_stream << m_bWakeOnBehaviorEnd;
  inout_stream << m_MinWakeSpacing;
  inout_stream << m_fTargetSwitchMargin;
  inout_stream << m_TargetMinHold;
  inout_stream << m_MaxLockDuration;
  inout_stream << m_uiScoringVersion;

  return PL_SUCCESS;
}

plResult plAiArchetypeResourceDescriptor::Deserialize(plStreamReader& inout_stream)
{
  const plTypeVersion uiVersion = inout_stream.ReadVersion(2);

  plUInt32 uiNumBehaviors = 0;
  inout_stream >> uiNumBehaviors;

  m_Behaviors.Clear();
  m_Behaviors.Reserve(uiNumBehaviors);

  for (plUInt32 i = 0; i < uiNumBehaviors; ++i)
  {
    auto& entry = m_Behaviors.ExpandAndGetRef();
    ReadResourceHandle(inout_stream, entry.m_hBehavior);
    inout_stream >> entry.m_fWeightScale;
  }

  ReadResourceHandle(inout_stream, m_hBlackboardTemplate);

  inout_stream >> m_sTeam;
  inout_stream >> m_sSensorObjectName;
  inout_stream >> m_fConfidenceGainPerSecond;
  inout_stream >> m_fConfidenceDecayPerSecond;
  inout_stream >> m_TargetMemoryDuration;

  if (uiVersion >= 2)
  {
    ReadSense(inout_stream, m_Hearing);
    ReadSense(inout_stream, m_Damage);
    ReadSense(inout_stream, m_Custom);
    inout_stream >> m_uiMaxTargets;
    inout_stream >> m_bWakeOnStimulus;
    inout_stream >> m_bWakeOnBehaviorEnd;
    inout_stream >> m_MinWakeSpacing;
    inout_stream >> m_fTargetSwitchMargin;
    inout_stream >> m_TargetMinHold;
    inout_stream >> m_MaxLockDuration;
    inout_stream >> m_uiScoringVersion;
  }

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

plAiArchetypeResource::plAiArchetypeResource()
  : plResource(DoUpdate::OnAnyThread, 1)
{
}

plAiArchetypeResource::~plAiArchetypeResource() = default;

plResourceLoadDesc plAiArchetypeResource::UnloadData(Unload WhatToUnload)
{
  m_Descriptor = plAiArchetypeResourceDescriptor();

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Unloaded;

  return res;
}

plResourceLoadDesc plAiArchetypeResource::UpdateContent(plStreamReader* Stream)
{
  PL_LOG_BLOCK("plAiArchetypeResource::UpdateContent", GetResourceIdOrDescription());

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

  plAiArchetypeResourceDescriptor desc;
  if (desc.Deserialize(*Stream).Failed())
  {
    res.m_State = plResourceState::LoadedResourceMissing;
    return res;
  }

  CreateResource(std::move(desc));

  res.m_State = plResourceState::Loaded;
  return res;
}

void plAiArchetypeResource::UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage)
{
  out_NewMemoryUsage.m_uiMemoryGPU = 0;
  out_NewMemoryUsage.m_uiMemoryCPU = sizeof(plAiArchetypeResource);
  out_NewMemoryUsage.m_uiMemoryCPU += m_Descriptor.m_Behaviors.GetHeapMemoryUsage();
}

PL_RESOURCE_IMPLEMENT_CREATEABLE(plAiArchetypeResource, plAiArchetypeResourceDescriptor)
{
  m_Descriptor = std::move(descriptor);

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Loaded;

  return res;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiArchetypeResource);
