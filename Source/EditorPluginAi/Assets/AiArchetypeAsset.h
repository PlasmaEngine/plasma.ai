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

/// \brief How the archetype perceives one kind of stimulus (hearing, damage, custom). See plAiSenseProfile.
struct plAiArchetypeSenseObject
{
  bool m_bEnabled = true;
  bool m_bUseSightSettings = true;
  float m_fRangeScale = 1.0f;
  float m_fStrengthScale = 1.0f;
  float m_fDecayPerSecond = 0.5f;
  plTime m_MemoryDuration = plTime::Seconds(10.0);
};

PL_DECLARE_REFLECTABLE_TYPE(PL_NO_LINKAGE, plAiArchetypeSenseObject);

/// \brief Editor-side object model of the 'AI Archetype' asset.
class plAiArchetypeAssetObject : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiArchetypeAssetObject, plReflectedClass);

public:
  plDynamicArray<plAiArchetypeBehaviorObject> m_Behaviors;
  plString m_sBlackboardTemplate;
  plString m_sTeam;
  plString m_sSensorObjectName;

  // sight
  float m_fConfidenceGainPerSecond = 4.0f;
  float m_fConfidenceDecayPerSecond = 0.5f;
  plTime m_TargetMemoryDuration = plTime::Seconds(10.0);

  // stimulus senses
  plAiArchetypeSenseObject m_Hearing;
  plAiArchetypeSenseObject m_Damage;
  plAiArchetypeSenseObject m_Custom;
  plUInt8 m_uiMaxTargets = 8;

  // reactivity
  bool m_bWakeOnStimulus = true;
  bool m_bWakeOnBehaviorEnd = true;
  plTime m_MinWakeSpacing = plTime::Seconds(0.1);
  float m_fTargetSwitchMargin = 0.1f;
  plTime m_TargetMinHold = plTime::Seconds(0.5);
  plTime m_MaxLockDuration;

  // scoring
  plUInt8 m_uiScoringVersion = 1;
};

class plAiArchetypeAssetDocument : public plSimpleAssetDocument<plAiArchetypeAssetObject>
{
  PL_ADD_DYNAMIC_REFLECTION(plAiArchetypeAssetDocument, plSimpleAssetDocument<plAiArchetypeAssetObject>);

public:
  plAiArchetypeAssetDocument(plStringView sDocumentPath);

  plStatus WriteAsset(plStreamWriter& inout_stream, const plPlatformProfile* pAssetProfile) const;

  /// \brief Writes the sight, sense, reactivity and scoring settings into a descriptor. Leaves the behaviors and the
  /// blackboard template alone, so previews can call it without loading resources.
  void GetSettings(plAiArchetypeResourceDescriptor& out_desc) const;

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& inout_stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;
};
