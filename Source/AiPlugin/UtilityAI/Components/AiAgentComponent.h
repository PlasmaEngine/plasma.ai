#pragma once

#include <AiPlugin/Tactical/SquadTypes.h>
#include <AiPlugin/UtilityAI/Decision/AiArchetypeResource.h>
#include <AiPlugin/UtilityAI/Perception/AiPerception.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

using plAiAgentComponentManager = plComponentManager<class plAiAgentComponent, plBlockStorageType::FreeList>;

class plAiBrainWorldModule;

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

  /// \name Script API
  ///
  /// Functions for scripts, state machines and game code. All of them only take effect while the
  /// component simulates.
  ///@{

  /// \brief Makes the agent decide as soon as the archetype's wake spacing allows.
  void RequestDecision(); // [ scriptable ]

  /// \brief Ends the active behavior now, even while it is locked, and decides again. The behavior that wins again
  /// starts from the beginning: a take-cover picks its cover again.
  void InterruptBehavior(); // [ scriptable ]

  /// \brief A disabled behavior doesn't compete. Disabling the active behavior ends it, even while locked.
  void SetBehaviorEnabled(const char* szBehavior, bool bEnabled); // [ scriptable ]

  /// \brief Scales a behavior's weight. A duration of 0 keeps the scale until it is changed again; 1 restores the authored weight.
  void SetBehaviorWeight(const char* szBehavior, float fScale, float fDurationSeconds); // [ scriptable ]

  /// \brief Gives the agent a command through its blackboard: Ai_CommandActive, Ai_Command, Ai_CommandPosition and
  /// Ai_CommandTarget. Behaviors that score on Ai_CommandActive carry it out. An expiry of 0 keeps it until cancelled.
  void IssueCommand(const char* szCommand, const plVec3& vPosition, plGameObjectHandle hTarget, float fExpirySeconds); // [ scriptable ]

  /// \brief Clears the current command (Ai_CommandActive becomes false).
  void CancelCommand(); // [ scriptable ]

  /// \brief Orders the agent's squad (its SquadId), replacing the order it has. The squad's members follow the intent
  /// until the duration is up (0 = until CancelSquadOrder). A radius above zero adds a region: a square of that
  /// half-size around the position, which the members hold, advance into or fall back to. NoRetreat keeps losses from
  /// breaking the order.
  void IssueSquadOrder(plEnum<plAiSquadIntent> intent, const plVec3& vRegionCenter, float fRegionRadius, float fDurationSeconds, bool bNoRetreat); // [ scriptable ]

  /// \brief Ends the order of the agent's squad.
  void CancelSquadOrder(); // [ scriptable ]

  /// \brief The name of the active behavior, empty when idle.
  const char* GetActiveBehavior() const; // [ scriptable ]

  /// \brief The object the active behavior targets, if any.
  plGameObjectHandle GetActiveTarget() const; // [ scriptable ]

  ///@}

protected:
  void OnMsgAiStimulus(plMsgAiStimulus& ref_msg);

private:
  friend class plAiBrainWorldModule;

  plAiBrainWorldModule* GetBrain() const; ///< nullptr while not registered

  plUInt32 m_uiAgentIndex = plInvalidIndex;
  plUInt32 m_uiTacticalAgentID = plInvalidIndex; ///< registration with plAiTacticalWorldModule (exposure/cover probes)
  plUInt32 m_uiSquadMemberID = plInvalidIndex;   ///< registration with plAiSquadWorldModule (when SquadId is set)
};
