#pragma once

#include <AiPlugin/UtilityAI/Decision/AiArchetypeResource.h>
#include <AiPlugin/UtilityAI/Perception/AiPerception.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

using plAiAgentComponentManager = plComponentManager<class plAiAgentComponent, plBlockStorageType::FreeList>;

/// \brief Makes the owner object a utility AI agent.
///
/// The component itself is only a thin configuration shell: it references an 'AI Archetype' asset
/// (behavior set, blackboard template, perception tuning) and registers with the
/// plAiBrainWorldModule, which drives all agent updates centrally.
///
/// The agent uses the blackboard found via plBlackboardComponent::FindBlackboard() on the owner,
/// or creates a private one from the archetype's blackboard template.
class PL_AIPLUGIN_DLL plAiAgentComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiAgentComponent, plComponent, plAiAgentComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiAgentComponent

public:
  plAiAgentComponent();
  ~plAiAgentComponent();

  void SetArchetypeFile(const char* szFile); // [ property ]
  const char* GetArchetypeFile() const;      // [ property ]

  plAiArchetypeResourceHandle m_hArchetype;

  /// [ property ] Overrides the team from the archetype, if not empty.
  plHashedString m_sTeam;

  /// [ property ] Agents sharing a non-empty squad id coordinate: shared targets, attack tokens,
  /// squad maneuvers (see plAiSquadWorldModule). Empty = no squad.
  plHashedString m_sSquadId;

  bool m_bDebugInfo = false; ///< [ property ] Always show score/history debug info for this agent.

  /// \brief The team this agent belongs to (component override or archetype team).
  plHashedString GetResolvedTeam() const;

protected:
  void OnMsgAiStimulus(plMsgAiStimulus& ref_msg);

private:
  friend class plAiBrainWorldModule;

  plUInt32 m_uiAgentIndex = plInvalidIndex;
  plUInt32 m_uiTacticalAgentID = plInvalidIndex; ///< registration with plAiTacticalWorldModule (exposure/cover probes)
  plUInt32 m_uiSquadMemberID = plInvalidIndex;   ///< registration with plAiSquadWorldModule (when SquadId is set)
};
