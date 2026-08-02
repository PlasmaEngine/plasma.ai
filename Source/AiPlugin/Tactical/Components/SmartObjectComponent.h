#pragma once

#include <AiPlugin/Tactical/SmartObjects.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>
#include <GameEngine/Gameplay/BlackboardComponent.h> // plBlackboardEntry

using plAiSmartObjectComponentManager = plComponentManager<class plAiSmartObjectComponent, plBlockStorageType::Compact>;

/// \brief Marks the owner object as an AI smart object: an authored interaction point agents can
/// find, claim and use (a mounted turret, an ambush spot, a lean/idle point...).
///
/// Agents discover it by Type via the AiPickSmartObject state machine state and use it via
/// AiUseSmartObject. On use start the owner receives plMsgAiSmartObjectUse - handle it to run
/// custom logic and call FinishUse() when done, or leave it unhandled and the agent simply waits
/// UseDuration. UserEntries are written to the USING AGENT's blackboard while the use lasts
/// (e.g. Crouch = 1 for an ambush spot) and reset to zero-values afterwards.
class PL_AIPLUGIN_DLL plAiSmartObjectComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiSmartObjectComponent, plComponent, plAiSmartObjectComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiSmartObjectComponent

public:
  plAiSmartObjectComponent();
  ~plAiSmartObjectComponent();

  plHashedString m_sType;                          ///< [ property ] What kind of interaction this is ("Turret", "AmbushSpot", ...). AiPickSmartObject filters by it.
  float m_fUseRange = 1.5f;                        ///< [ property ] How close an agent must stand (to a slot) to use the object.
  plTime m_UseDuration = plTime::Seconds(3.0);     ///< [ property ] Built-in use time when no game code handles the use message.
  plDynamicArray<plAiSmartObjectSlot> m_Slots;     ///< [ property ] Usable positions (local). Empty = one implicit slot at the owner.
  plDynamicArray<plBlackboardEntry> m_UserEntries; ///< [ property ] Set on the USER's blackboard while used, reset to zero-values afterwards.

  /// \brief The world transform of the given slot (the owner's transform for the implicit slot).
  plTransform GetSlotGlobalTransform(plUInt8 uiSlot) const;

  /// \brief Game code calls this when its handling of the use message is complete. [ scriptable ]
  void FinishUse(plGameObjectHandle hUser);

private:
  plAiTacticalWorldModule* m_pTacticalModule = nullptr;
  plAiTacticalWorldModule::SmartObjectID m_SmartObjectID = plAiTacticalWorldModule::InvalidSmartObjectID;
};
