#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiArchetypeAsset.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_TYPE(plAiArchetypeBehaviorObject, plNoBase, 1, plRTTIDefaultAllocator<plAiArchetypeBehaviorObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Behavior", m_sBehavior)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_AiBehavior", plDependencyFlags::Package)),
    PL_MEMBER_PROPERTY("WeightScale", m_fWeightScale)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10.0f)),
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
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiArchetypeAssetDocument, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

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
  desc.m_sSensorObjectName = pProp->m_sSensorObjectName;
  desc.m_fConfidenceGainPerSecond = pProp->m_fConfidenceGainPerSecond;
  desc.m_fConfidenceDecayPerSecond = pProp->m_fConfidenceDecayPerSecond;
  desc.m_TargetMemoryDuration = pProp->m_TargetMemoryDuration;

  PL_SUCCEED_OR_RETURN(desc.Serialize(inout_stream));

  return plStatus(PL_SUCCESS);
}

plTransformStatus plAiArchetypeAssetDocument::InternalTransformAsset(plStreamWriter& inout_stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  return WriteAsset(inout_stream, pAssetProfile);
}
