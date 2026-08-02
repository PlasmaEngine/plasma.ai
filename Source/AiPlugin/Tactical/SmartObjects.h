#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/Declarations.h>
#include <Foundation/Communication/Message.h>
#include <Foundation/Math/Quat.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Strings/HashedString.h>

/// \brief One usable position on a smart object (local to the owner object).
struct PL_AIPLUGIN_DLL plAiSmartObjectSlot
{
  plVec3 m_vLocalPosition = plVec3::MakeZero();
  plQuat m_qLocalRotation = plQuat::MakeIdentity();
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiSmartObjectSlot);

/// \brief Identifies one slot of a registered smart object. Stale after the object unregisters.
struct plAiSmartObjectHandle
{
  PL_DECLARE_POD_TYPE();

  plUInt32 m_uiEntry = plInvalidIndex; ///< index into the smart object registry
  plUInt16 m_uiGeneration = 0;         ///< must match the entry's generation
  plUInt8 m_uiSlot = 0;                ///< which slot of the object

  bool IsValid() const { return m_uiEntry != plInvalidIndex; }
  void Invalidate() { m_uiEntry = plInvalidIndex; }

  bool operator==(const plAiSmartObjectHandle& rhs) const
  {
    return m_uiEntry == rhs.m_uiEntry && m_uiGeneration == rhs.m_uiGeneration && m_uiSlot == rhs.m_uiSlot;
  }
};

/// \brief How far along a smart object use is (per claimant, tracked by the tactical module).
struct plAiSmartObjectUsePhase
{
  enum Enum : plUInt8
  {
    NotUsing, ///< claimed (or nothing), but not using yet
    InUse,    ///< between use start and finish
    Finished, ///< game code signaled completion (FinishUse); consumed by the use state
  };
};

/// \brief Sent to the smart object's owner when an agent starts (m_bStart) or ends using it.
///
/// Handle the start message to run custom logic (mount the turret, play an interaction) and call
/// plAiSmartObjectComponent::FinishUse() when done. When the start message is NOT handled, the
/// using agent simply waits the component's UseDuration - smart objects work without any game code.
struct PL_AIPLUGIN_DLL plMsgAiSmartObjectUse : public plMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgAiSmartObjectUse, plMessage);

  plGameObjectHandle m_hUser;        ///< the agent using the object
  plGameObjectHandle m_hSmartObject; ///< the object carrying the plAiSmartObjectComponent
  plUInt8 m_uiSlot = 0;
  plHashedString m_sType;
  bool m_bStart = true; ///< true on use start, false on use end/abort
};
