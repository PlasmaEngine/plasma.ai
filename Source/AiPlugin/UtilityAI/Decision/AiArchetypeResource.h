#pragma once

#include <AiPlugin/UtilityAI/Decision/AiBehaviorResource.h>
#include <GameEngine/Utils/BlackboardTemplateResource.h>

using plAiArchetypeResourceHandle = plTypedResourceHandle<class plAiArchetypeResource>;

/// \brief One behavior of an archetype, with an optional archetype-specific weight scale.
struct PL_AIPLUGIN_DLL plAiArchetypeBehaviorEntry
{
  plAiBehaviorResourceHandle m_hBehavior;
  float m_fWeightScale = 1.0f;
};

/// \brief Defines a type of AI agent: its behavior set, blackboard template, team and perception tuning.
struct PL_AIPLUGIN_DLL plAiArchetypeResourceDescriptor
{
  plDynamicArray<plAiArchetypeBehaviorEntry> m_Behaviors;
  plBlackboardTemplateResourceHandle m_hBlackboardTemplate;
  plHashedString m_sTeam;

  /// Name of the child object that carries the plSensorComponents used for sight. Empty = search all children.
  plString m_sSensorObjectName;

  float m_fConfidenceGainPerSecond = 4.0f;
  float m_fConfidenceDecayPerSecond = 0.5f;
  plTime m_TargetMemoryDuration = plTime::Seconds(10.0);

  plResult Serialize(plStreamWriter& inout_stream) const;
  plResult Deserialize(plStreamReader& inout_stream);
};

/// \brief Per-agent-type configuration, authored through the 'AI Archetype' asset and referenced by plAiAgentComponent.
class PL_AIPLUGIN_DLL plAiArchetypeResource : public plResource
{
  PL_ADD_DYNAMIC_REFLECTION(plAiArchetypeResource, plResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plAiArchetypeResource);
  PL_RESOURCE_DECLARE_CREATEABLE(plAiArchetypeResource, plAiArchetypeResourceDescriptor);

public:
  plAiArchetypeResource();
  ~plAiArchetypeResource();

  const plAiArchetypeResourceDescriptor& GetDescriptor() const { return m_Descriptor; }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* Stream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage) override;

  plAiArchetypeResourceDescriptor m_Descriptor;
};
