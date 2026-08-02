#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Eqs/EqsWorldModule.h>
#include <GameEngine/StateMachine/StateMachine.h>

/// \brief Runs an EQS query asset and writes the best position to the blackboard.
///
/// The drop-in sibling of plStateMachineState_AiPickTacticalPoint for authored query assets:
/// writes the picked position into TargetEntry (default 'Ai_TacticalTarget', a plVec3) and a
/// result code into ResultEntry (default 'Ai_TacticalResult'): 0 = still querying, 1 = success,
/// 2 = failed. Chain a plStateMachineState_AiNavigateTo with the same entry to move there.
///
/// Contexts resolve from the agent itself (querier position, blackboard entries like the
/// perceived target); AreaNotReady results are retried a few times, exactly like the tactical
/// pick state.
///
/// With ClaimCover / ClaimSmartObject enabled (default), a winning item that carries a cover or
/// smart-object-slot payload is claimed for this agent so other agents avoid it - candidates
/// whose payload got snatched between execution and polling fall through to the next candidate.
/// A claimed smart object feeds directly into plStateMachineState_AiUseSmartObject.
class PL_AIPLUGIN_DLL plStateMachineState_AiRunEqsQuery : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiRunEqsQuery, plStateMachineState);

public:
  plStateMachineState_AiRunEqsQuery(plStringView sName = plStringView());
  ~plStateMachineState_AiRunEqsQuery();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;
  virtual void OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const override;
  virtual void Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  virtual bool GetInstanceDataDesc(plInstanceDataDesc& out_desc) override;

  void SetQueryFile(const char* szFile);                                     // [ property ]
  const char* GetQueryFile() const;                                          // [ property ]
  void SetTargetEntry(const char* szName) { m_sTargetEntry.Assign(szName); } // [ property ]
  const char* GetTargetEntry() const { return m_sTargetEntry.GetData(); }    // [ property ]
  void SetResultEntry(const char* szName) { m_sResultEntry.Assign(szName); } // [ property ]
  const char* GetResultEntry() const { return m_sResultEntry.GetData(); }    // [ property ]

  plAiEqsQueryResourceHandle m_hQuery;
  plHashedString m_sTargetEntry;
  plHashedString m_sResultEntry;
  bool m_bClaimCover = true;
  bool m_bClaimSmartObject = true;
  plTime m_ClaimTimeout = plTime::Seconds(20.0);
  bool m_bReleaseClaimOnExit = false;

private:
  struct InstanceData
  {
    plAiEqsWorldModule::QueryID m_QueryId = plAiEqsWorldModule::InvalidQueryID;
    plUInt8 m_uiRetries = 0;
    bool m_bDone = false;
    plTime m_RetryAt;
  };

  bool SubmitQuery(plStateMachineInstance& ref_instance, InstanceData* pData) const;
};
