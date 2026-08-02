#pragma once

#include <AiPlugin/UtilityAI/Decision/AiArchetypeResource.h>
#include <EditorFramework/Assets/SimpleAssetDocument.h>

/// \brief One behavior reference in the AI archetype asset.
struct plAiArchetypeBehaviorObject
{
  plString m_sBehavior; ///< AI Behavior asset reference
  float m_fWeightScale = 1.0f;
};

PL_DECLARE_REFLECTABLE_TYPE(PL_NO_LINKAGE, plAiArchetypeBehaviorObject);

/// \brief Editor-side object model of the 'AI Archetype' asset.
class plAiArchetypeAssetObject : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiArchetypeAssetObject, plReflectedClass);

public:
  plDynamicArray<plAiArchetypeBehaviorObject> m_Behaviors;
  plString m_sBlackboardTemplate;
  plString m_sTeam;
  plString m_sSensorObjectName;

  float m_fConfidenceGainPerSecond = 4.0f;
  float m_fConfidenceDecayPerSecond = 0.5f;
  plTime m_TargetMemoryDuration = plTime::Seconds(10.0);
};

class plAiArchetypeAssetDocument : public plSimpleAssetDocument<plAiArchetypeAssetObject>
{
  PL_ADD_DYNAMIC_REFLECTION(plAiArchetypeAssetDocument, plSimpleAssetDocument<plAiArchetypeAssetObject>);

public:
  plAiArchetypeAssetDocument(plStringView sDocumentPath);

  plStatus WriteAsset(plStreamWriter& inout_stream, const plPlatformProfile* pAssetProfile) const;

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& inout_stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;
};
