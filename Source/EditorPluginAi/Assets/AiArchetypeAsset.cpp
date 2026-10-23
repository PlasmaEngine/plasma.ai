#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiArchetypeAsset.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_TYPE(plAiArchetypeBehaviorObject, plNoBase, 1, plRTTIDefaultAllocator<plAiArchetypeBehaviorObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Behavior", m_sBehavior)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_AiBehavior", plDependencyFlags::Package)),
    PL_MEMBER_PROPERTY("WeightScale", m_fWeightScale)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 1.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;

PL_BEGIN_STATIC_REFLECTED_TYPE(plAiArchetypeSenseObject, plNoBase, 1, plRTTIDefaultAllocator<plAiArchetypeSenseObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Enabled", m_bEnabled)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("UseSightSettings", m_bUseSightSettings)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("RangeScale", m_fRangeScale)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10.0f)),
    PL_MEMBER_PROPERTY("StrengthScale", m_fStrengthScale)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10.0f)),
    PL_MEMBER_PROPERTY("DecayPerSec", m_fDecayPerSecond)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.0f, 100.0f)),
    PL_MEMBER_PROPERTY("MemoryDuration", m_MemoryDuration)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(10.0))),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiArchetypeAssetObject, 1, plRTTIDefaultAllocator<plAiArchetypeAssetObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ARRAY_MEMBER_PROPERTY("Behaviors", m_Behaviors),
    PL_MEMBER_PROPERTY("BlackboardTemplate", m_sBlackboardTemplate)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_BlackboardTemplate", plDependencyFlags::Package)),
    PL_MEMBER_PROPERTY("Team", m_sTeam),
    PL_MEMBER_PROPERTY("SensorObjectName", m_sSensorObjectName),
    PL_MEMBER_PROPERTY("ConfidenceGainPerSec", m_fConfidenceGainPerSecond)->AddAttributes(new plDefaultValueAttribute(4.0f), new plClampValueAttribute(0.0f, 100.0f)),
    PL_MEMBER_PROPERTY("ConfidenceDecayPerSec", m_fConfidenceDecayPerSecond)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.0f, 100.0f)),
    PL_MEMBER_PROPERTY("TargetMemoryDuration", m_TargetMemoryDuration)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(10.0))),
    PL_MEMBER_PROPERTY("Hearing", m_Hearing),
    PL_MEMBER_PROPERTY("Damage", m_Damage),
    PL_MEMBER_PROPERTY("Custom", m_Custom),
    PL_MEMBER_PROPERTY("MaxTargets", m_uiMaxTargets)->AddAttributes(new plDefaultValueAttribute(8), new plClampValueAttribute(1, 64)),
    PL_MEMBER_PROPERTY("WakeOnStimulus", m_bWakeOnStimulus)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("WakeOnBehaviorEnd", m_bWakeOnBehaviorEnd)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("MinWakeSpacing", m_MinWakeSpacing)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(0.1))),
    PL_MEMBER_PROPERTY("TargetSwitchMargin", m_fTargetSwitchMargin)->AddAttributes(new plDefaultValueAttribute(0.1f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("TargetMinHold", m_TargetMinHold)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(0.5))),
    PL_MEMBER_PROPERTY("MaxLockDuration", m_MaxLockDuration),
    PL_MEMBER_PROPERTY("ScoringVersion", m_uiScoringVersion)->AddAttributes(new plDefaultValueAttribute(1), new plClampValueAttribute(1, 2)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiArchetypeAssetDocument, 2, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

namespace
{
  void CopySense(const plAiArchetypeSenseObject& sense, plAiSenseProfile& out_profile)
  {
    out_profile.m_bEnabled = sense.m_bEnabled;
    out_profile.m_bUseSightSettings = sense.m_bUseSightSettings;
    out_profile.m_fRangeScale = sense.m_fRangeScale;
    out_profile.m_fStrengthScale = sense.m_fStrengthScale;
    out_profile.m_fDecayPerSecond = sense.m_fDecayPerSecond;
    out_profile.m_MemoryDuration = sense.m_MemoryDuration;
  }
} // namespace

plAiArchetypeAssetDocument::plAiArchetypeAssetDocument(plStringView sDocumentPath)
  : plSimpleAssetDocument<plAiArchetypeAssetObject>(sDocumentPath, plAssetDocEngineConnection::None)
{
}

plStatus plAiArchetypeAssetDocument::WriteAsset(plStreamWriter& inout_stream, const plPlatformProfile* pAssetProfile) const
{
  const plAiArchetypeAssetObject* pProp = GetProperties();

  plAiArchetypeResourceDescriptor desc;

  for (const auto& behavior : pProp->m_Behaviors)
  {
    if (behavior.m_sBehavior.IsEmpty())
      continue;

    auto& entry = desc.m_Behaviors.ExpandAndGetRef();
    entry.m_hBehavior = plResourceManager::LoadResource<plAiBehaviorResource>(behavior.m_sBehavior);
    entry.m_fWeightScale = behavior.m_fWeightScale;
  }

  if (!pProp->m_sBlackboardTemplate.IsEmpty())
  {
    desc.m_hBlackboardTemplate = plResourceManager::LoadResource<plBlackboardTemplateResource>(pProp->m_sBlackboardTemplate);
  }

  desc.m_sTeam.Assign(pProp->m_sTeam);
  GetSettings(desc);

  PL_SUCCEED_OR_RETURN(desc.Serialize(inout_stream));

  return plStatus(PL_SUCCESS);
}

void plAiArchetypeAssetDocument::GetSettings(plAiArchetypeResourceDescriptor& out_desc) const
{
  const plAiArchetypeAssetObject* pProp = GetProperties();

  out_desc.m_sSensorObjectName = pProp->m_sSensorObjectName;
  out_desc.m_fConfidenceGainPerSecond = pProp->m_fConfidenceGainPerSecond;
  out_desc.m_fConfidenceDecayPerSecond = pProp->m_fConfidenceDecayPerSecond;
  out_desc.m_TargetMemoryDuration = pProp->m_TargetMemoryDuration;

  CopySense(pProp->m_Hearing, out_desc.m_Hearing);
  CopySense(pProp->m_Damage, out_desc.m_Damage);
  CopySense(pProp->m_Custom, out_desc.m_Custom);
  out_desc.m_uiMaxTargets = pProp->m_uiMaxTargets;

  out_desc.m_bWakeOnStimulus = pProp->m_bWakeOnStimulus;
  out_desc.m_bWakeOnBehaviorEnd = pProp->m_bWakeOnBehaviorEnd;
  out_desc.m_MinWakeSpacing = pProp->m_MinWakeSpacing;
  out_desc.m_fTargetSwitchMargin = pProp->m_fTargetSwitchMargin;
  out_desc.m_TargetMinHold = pProp->m_TargetMinHold;
  out_desc.m_MaxLockDuration = pProp->m_MaxLockDuration;

  out_desc.m_uiScoringVersion = pProp->m_uiScoringVersion;
}

plTransformStatus plAiArchetypeAssetDocument::InternalTransformAsset(plStreamWriter& inout_stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  return WriteAsset(inout_stream, pAssetProfile);
}
