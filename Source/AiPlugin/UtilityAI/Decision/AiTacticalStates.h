#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <GameEngine/StateMachine/StateMachine.h>

/// \brief Where plStateMachineState_AiPickTacticalPoint takes the threat position from.
struct PL_AIPLUGIN_DLL plAiTacticalThreatSource
{
  using StorageType = plUInt8;

  enum Enum
  {
    PerceivedTarget, ///< the brain-published 'Ai_TargetPosition' / 'Ai_TargetConfidence'
    BlackboardEntry, ///< a plVec3 blackboard entry named by ThreatEntry
    None,            ///< no threat (self-centered queries like RandomNearSelf)

    Default = PerceivedTarget
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiTacticalThreatSource);

/// \brief Runs a tactical position query (take cover, flank, retreat, random point) and writes the
/// best position to the blackboard.
///
/// Writes the picked position into TargetEntry (default 'Ai_TacticalTarget', a plVec3) and a result
/// code into ResultEntry (default 'Ai_TacticalResult'): 0 = still querying, 1 = success, 2 = failed.
/// Chain a plStateMachineState_AiNavigateTo with the same entry to move there, and transition on the
/// result code - exactly like the patrol-point state.
///
/// With ClaimCover enabled (default), a picked cover point is claimed for this agent so other agents
/// avoid it. The claim persists across states (so navigation and shooting keep it) and releases
/// automatically when the agent dies, leaves it behind, or its sector rebuilds.
class PL_AIPLUGIN_DLL plStateMachineState_AiPickTacticalPoint : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiPickTacticalPoint, plStateMachineState);

public:
  plStateMachineState_AiPickTacticalPoint(plStringView sName = plStringView());
  ~plStateMachineState_AiPickTacticalPoint();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;
  virtual void OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const override;
  virtual void Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  virtual bool GetInstanceDataDesc(plInstanceDataDesc& out_desc) override;

  void SetThreatEntry(const char* szName) { m_sThreatEntry.Assign(szName); } // [ property ]
  const char* GetThreatEntry() const { return m_sThreatEntry.GetData(); }    // [ property ]
  void SetTargetEntry(const char* szName) { m_sTargetEntry.Assign(szName); } // [ property ]
  const char* GetTargetEntry() const { return m_sTargetEntry.GetData(); }    // [ property ]
  void SetResultEntry(const char* szName) { m_sResultEntry.Assign(szName); } // [ property ]
  const char* GetResultEntry() const { return m_sResultEntry.GetData(); }    // [ property ]

  plEnum<plAiTacticalQueryPreset> m_Preset;
  plEnum<plAiTacticalThreatSource> m_ThreatSource;
  plHashedString m_sThreatEntry;
  float m_fRadiusMin = 2.0f;
  float m_fRadiusMax = 12.0f;
  plAngle m_FlankAngle = plAngle::MakeFromDegree(100);
  plEnum<plAiCoverQuality> m_MinQuality;
  plHashedString m_sTargetEntry;
  plHashedString m_sResultEntry;
  bool m_bClaimCover = true;
  plTime m_ClaimTimeout = plTime::Seconds(20.0);
  bool m_bReleaseClaimOnExit = false;

private:
  struct InstanceData
  {
    plAiTacticalWorldModule::QueryID m_QueryId = plAiTacticalWorldModule::InvalidQueryID;
    plUInt8 m_uiRetries = 0;
    bool m_bDone = false;
    plTime m_RetryAt;
  };

  bool SubmitQuery(plStateMachineInstance& ref_instance, InstanceData* pData) const;
};

/// \brief Finds the position to fire from, relative to the agent's claimed cover, and writes it to
/// the blackboard.
///
/// Low cover (crates): standing up at the cover position already gives line of sight - the cover
/// position itself is written. High/solid cover (walls): side-steps along the wall (up to
/// MaxSideStep) until standing line of sight to the threat opens up. Chain into AiNavigateTo on
/// the target entry to step out, fire, then navigate back to the cover position (which is still
/// in 'Ai_TacticalTarget' from the take-cover pick).
///
/// Result codes in ResultEntry: 1 = position written, 2 = no claimed cover / no firing position.
/// This state completes instantly in OnEnter (a few raycasts).
class PL_AIPLUGIN_DLL plStateMachineState_AiPickPeekPosition : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiPickPeekPosition, plStateMachineState);

public:
  plStateMachineState_AiPickPeekPosition(plStringView sName = plStringView());
  ~plStateMachineState_AiPickPeekPosition();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  void SetThreatEntry(const char* szName) { m_sThreatEntry.Assign(szName); } // [ property ]
  const char* GetThreatEntry() const { return m_sThreatEntry.GetData(); }    // [ property ]
  void SetTargetEntry(const char* szName) { m_sTargetEntry.Assign(szName); } // [ property ]
  const char* GetTargetEntry() const { return m_sTargetEntry.GetData(); }    // [ property ]
  void SetResultEntry(const char* szName) { m_sResultEntry.Assign(szName); } // [ property ]
  const char* GetResultEntry() const { return m_sResultEntry.GetData(); }    // [ property ]

  plEnum<plAiTacticalThreatSource> m_ThreatSource;
  plHashedString m_sThreatEntry;
  float m_fMaxSideStep = 2.5f;
  plHashedString m_sTargetEntry; ///< default "Ai_PeekTarget"
  plHashedString m_sResultEntry; ///< default "Ai_PeekResult"
};

/// \brief Releases the cover point claimed by this agent (if any). Use in states where the agent
/// abandons its cover for good, e.g. when starting a melee charge.
class PL_AIPLUGIN_DLL plStateMachineState_AiReleaseCover : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiReleaseCover, plStateMachineState);

public:
  plStateMachineState_AiReleaseCover(plStringView sName = plStringView());
  ~plStateMachineState_AiReleaseCover();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;
};
