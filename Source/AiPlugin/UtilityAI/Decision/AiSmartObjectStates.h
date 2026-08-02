#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <GameEngine/StateMachine/StateMachine.h>

/// \brief Finds the nearest free smart object of the given type, claims a slot on it and writes
/// the slot position to the blackboard.
///
/// Result codes in ResultEntry (default 'Ai_SmartObjectResult'): 1 = found + claimed (position in
/// TargetEntry, default 'Ai_SmartObjectPos'), 2 = none available. Chain into AiNavigateTo on the
/// target entry, then AiUseSmartObject. Completes instantly in OnEnter.
class PL_AIPLUGIN_DLL plStateMachineState_AiPickSmartObject : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiPickSmartObject, plStateMachineState);

public:
  plStateMachineState_AiPickSmartObject(plStringView sName = plStringView());
  ~plStateMachineState_AiPickSmartObject();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;
  virtual void OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  void SetTargetEntry(const char* szName) { m_sTargetEntry.Assign(szName); } // [ property ]
  const char* GetTargetEntry() const { return m_sTargetEntry.GetData(); }    // [ property ]
  void SetResultEntry(const char* szName) { m_sResultEntry.Assign(szName); } // [ property ]
  const char* GetResultEntry() const { return m_sResultEntry.GetData(); }    // [ property ]

  plHashedString m_sType;         ///< which smart object type to look for
  float m_fSearchRadius = 12.0f;
  plHashedString m_sTargetEntry;  ///< default "Ai_SmartObjectPos"
  plHashedString m_sResultEntry;  ///< default "Ai_SmartObjectResult"
  plTime m_ClaimTimeout = plTime::Seconds(20.0);
  bool m_bReleaseClaimOnExit = false;
};

/// \brief Uses the smart object this agent claimed (via AiPickSmartObject): sends the use-start
/// message to the object; when game code handles it, waits for FinishUse(), otherwise waits the
/// object's built-in UseDuration. The object's UserEntries are applied to the agent's blackboard
/// for the duration.
///
/// Result codes in ResultEntry (default 'Ai_UseResult'): 0 = using, 1 = done, 2 = failed (no
/// claim, out of range, object vanished).
class PL_AIPLUGIN_DLL plStateMachineState_AiUseSmartObject : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiUseSmartObject, plStateMachineState);

public:
  plStateMachineState_AiUseSmartObject(plStringView sName = plStringView());
  ~plStateMachineState_AiUseSmartObject();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;
  virtual void OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const override;
  virtual void Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  virtual bool GetInstanceDataDesc(plInstanceDataDesc& out_desc) override;

  void SetResultEntry(const char* szName) { m_sResultEntry.Assign(szName); } // [ property ]
  const char* GetResultEntry() const { return m_sResultEntry.GetData(); }    // [ property ]

  plHashedString m_sResultEntry;       ///< default "Ai_UseResult"
  bool m_bReleaseClaimWhenDone = true; ///< release the slot when the use completes
  bool m_bRequireInRange = true;       ///< fail when the agent isn't within the object's UseRange

private:
  struct InstanceData
  {
    plTime m_FinishAt;
    bool m_bStarted = false;
    bool m_bBuiltinWait = false;
    bool m_bDone = false;
  };

  void ApplyUserEntries(plStateMachineInstance& ref_instance, const plComponentHandle& hComponent, bool bApply) const;
  void AbortUse(plStateMachineInstance& ref_instance, InstanceData* pData, plAiTacticalWorldModule* pTactical, plGameObject* pOwner) const;
};

/// \brief Releases the smart object slot claimed by this agent (if any).
class PL_AIPLUGIN_DLL plStateMachineState_AiReleaseSmartObject : public plStateMachineState
{
  PL_ADD_DYNAMIC_REFLECTION(plStateMachineState_AiReleaseSmartObject, plStateMachineState);

public:
  plStateMachineState_AiReleaseSmartObject(plStringView sName = plStringView());
  ~plStateMachineState_AiReleaseSmartObject();

  virtual void OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;
};
