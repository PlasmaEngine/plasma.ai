#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Tactical/Components/SmartObjectComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiSmartObjectStates.h>
#include <Core/Utils/Blackboard.h>
#include <Core/World/World.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiPickSmartObject, 1, plRTTIDefaultAllocator<plStateMachineState_AiPickSmartObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Type", m_sType),
    PL_MEMBER_PROPERTY("SearchRadius", m_fSearchRadius)->AddAttributes(new plDefaultValueAttribute(12.0f), new plClampValueAttribute(0.5f, 200.0f)),
    PL_ACCESSOR_PROPERTY("TargetEntry", GetTargetEntry, SetTargetEntry)->AddAttributes(new plDefaultValueAttribute(plStringView("Ai_SmartObjectPos"))),
    PL_ACCESSOR_PROPERTY("ResultEntry", GetResultEntry, SetResultEntry)->AddAttributes(new plDefaultValueAttribute(plStringView("Ai_SmartObjectResult"))),
    PL_MEMBER_PROPERTY("ClaimTimeout", m_ClaimTimeout)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(20.0))),
    PL_MEMBER_PROPERTY("ReleaseClaimOnExit", m_bReleaseClaimOnExit),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

namespace
{
  plGameObject* GetOwnerObjectSo(plStateMachineInstance& ref_instance)
  {
    if (plComponent* pComponent = plDynamicCast<plComponent*>(&ref_instance.GetOwner()))
    {
      return pComponent->GetOwner();
    }

    return nullptr;
  }

  void SetBlackboardIntSo(plStateMachineInstance& ref_instance, const plHashedString& sEntry, plInt32 iValue)
  {
    if (sEntry.IsEmpty())
      return;

    if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
    {
      pBlackboard->SetEntryValue(sEntry, iValue);
    }
  }

  // reset a user-intent entry to the zero-value of its authored type (Crouch=1 -> 0, flags -> false, ...)
  plVariant MakeZeroValue(const plVariant& authoredValue)
  {
    switch (authoredValue.GetType())
    {
      case plVariantType::Bool:
        return plVariant(false);
      case plVariantType::Float:
        return plVariant(0.0f);
      case plVariantType::Double:
        return plVariant(0.0);
      case plVariantType::Vector3:
        return plVariant(plVec3::MakeZero());
      default:
        break;
    }

    if (authoredValue.CanConvertTo<plInt32>())
      return plVariant(plInt32(0));

    return plVariant(0.0f);
  }
} // namespace

plStateMachineState_AiPickSmartObject::plStateMachineState_AiPickSmartObject(plStringView sName)
  : plStateMachineState(sName)
{
  m_sTargetEntry.Assign("Ai_SmartObjectPos");
  m_sResultEntry.Assign("Ai_SmartObjectResult");
}

plStateMachineState_AiPickSmartObject::~plStateMachineState_AiPickSmartObject() = default;

void plStateMachineState_AiPickSmartObject::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObjectSo(ref_instance);
  auto* pTactical = (pWorld != nullptr) ? pWorld->GetModule<plAiTacticalWorldModule>() : nullptr;

  if (pOwner == nullptr || pTactical == nullptr || m_sType.IsEmpty())
  {
    SetBlackboardIntSo(ref_instance, m_sResultEntry, 2);
    return;
  }

  plAiSmartObjectHandle hSlot;
  plVec3 vSlotPos;

  if (!pTactical->FindSmartObject(pOwner->GetGlobalPosition(), m_fSearchRadius, plTempHashedString(m_sType.GetHash()), pOwner->GetHandle(), hSlot, vSlotPos) ||
      !pTactical->ClaimSmartObject(hSlot, pOwner->GetHandle(), m_ClaimTimeout))
  {
    SetBlackboardIntSo(ref_instance, m_sResultEntry, 2);
    return;
  }

  if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
  {
    pBlackboard->SetEntryValue(m_sTargetEntry, vSlotPos);
  }

  SetBlackboardIntSo(ref_instance, m_sResultEntry, 1);
}

void plStateMachineState_AiPickSmartObject::OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const
{
  if (!m_bReleaseClaimOnExit)
    return;

  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObjectSo(ref_instance);

  if (pWorld == nullptr || pOwner == nullptr)
    return;

  if (auto* pTactical = pWorld->GetModule<plAiTacticalWorldModule>())
  {
    pTactical->ReleaseSmartObjectClaim(pOwner->GetHandle());
  }
}

plResult plStateMachineState_AiPickSmartObject::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));

  inout_stream << m_sType;
  inout_stream << m_fSearchRadius;
  inout_stream << m_sTargetEntry;
  inout_stream << m_sResultEntry;
  inout_stream << m_ClaimTimeout;
  inout_stream << m_bReleaseClaimOnExit;

  return PL_SUCCESS;
}

plResult plStateMachineState_AiPickSmartObject::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));

  inout_stream >> m_sType;
  inout_stream >> m_fSearchRadius;
  inout_stream >> m_sTargetEntry;
  inout_stream >> m_sResultEntry;
  inout_stream >> m_ClaimTimeout;
  inout_stream >> m_bReleaseClaimOnExit;

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiUseSmartObject, 1, plRTTIDefaultAllocator<plStateMachineState_AiUseSmartObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("ResultEntry", GetResultEntry, SetResultEntry)->AddAttributes(new plDefaultValueAttribute(plStringView("Ai_UseResult"))),
    PL_MEMBER_PROPERTY("ReleaseClaimWhenDone", m_bReleaseClaimWhenDone)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("RequireInRange", m_bRequireInRange)->AddAttributes(new plDefaultValueAttribute(true)),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plStateMachineState_AiUseSmartObject::plStateMachineState_AiUseSmartObject(plStringView sName)
  : plStateMachineState(sName)
{
  m_sResultEntry.Assign("Ai_UseResult");
}

plStateMachineState_AiUseSmartObject::~plStateMachineState_AiUseSmartObject() = default;

void plStateMachineState_AiUseSmartObject::ApplyUserEntries(plStateMachineInstance& ref_instance, const plComponentHandle& hComponent, bool bApply) const
{
  const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard();

  if (pBlackboard == nullptr)
    return;

  plAiSmartObjectComponent* pComponent = nullptr;

  if (!ref_instance.GetOwnerWorld()->TryGetComponent(hComponent, pComponent))
    return;

  for (const plBlackboardEntry& entry : pComponent->m_UserEntries)
  {
    pBlackboard->SetEntryValue(entry.m_sName, bApply ? entry.m_InitialValue : MakeZeroValue(entry.m_InitialValue));
  }
}

void plStateMachineState_AiUseSmartObject::AbortUse(plStateMachineInstance& ref_instance, InstanceData* pData, plAiTacticalWorldModule* pTactical, plGameObject* pOwner) const
{
  if (pData->m_bStarted && pTactical != nullptr && pOwner != nullptr)
  {
    plAiSmartObjectHandle hSlot;
    plVec3 vSlotPos;
    plComponentHandle hComponent;

    if (pTactical->GetClaimedSmartObject(pOwner->GetHandle(), hSlot) && pTactical->ResolveSmartObject(hSlot, vSlotPos, hComponent))
    {
      ApplyUserEntries(ref_instance, hComponent, false);
    }

    pTactical->EndUse(pOwner->GetHandle());
  }

  pData->m_bStarted = false;
  pData->m_bDone = true;
}

void plStateMachineState_AiUseSmartObject::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);
  pData->m_bStarted = false;
  pData->m_bBuiltinWait = false;
  pData->m_bDone = false;

  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObjectSo(ref_instance);
  auto* pTactical = (pWorld != nullptr) ? pWorld->GetModule<plAiTacticalWorldModule>() : nullptr;

  plAiSmartObjectHandle hSlot;
  plVec3 vSlotPos;
  plComponentHandle hComponent;

  if (pOwner == nullptr || pTactical == nullptr ||
      !pTactical->GetClaimedSmartObject(pOwner->GetHandle(), hSlot) ||
      !pTactical->ResolveSmartObject(hSlot, vSlotPos, hComponent))
  {
    SetBlackboardIntSo(ref_instance, m_sResultEntry, 2);
    pData->m_bDone = true;
    return;
  }

  plAiSmartObjectComponent* pComponent = nullptr;
  pWorld->TryGetComponent(hComponent, pComponent);

  if (m_bRequireInRange && pComponent != nullptr)
  {
    const float fRange = pComponent->m_fUseRange + 0.5f;

    if ((pOwner->GetGlobalPosition().GetAsVec2() - vSlotPos.GetAsVec2()).GetLengthSquared() > plMath::Square(fRange))
    {
      SetBlackboardIntSo(ref_instance, m_sResultEntry, 2);
      pData->m_bDone = true;
      return;
    }
  }

  const bool bHandled = pTactical->BeginUse(pOwner->GetHandle());
  pData->m_bStarted = true;
  pData->m_bBuiltinWait = !bHandled;

  if (pData->m_bBuiltinWait && pComponent != nullptr)
  {
    pData->m_FinishAt = pWorld->GetClock().GetAccumulatedTime() + pComponent->m_UseDuration;
  }

  ApplyUserEntries(ref_instance, hComponent, true);
  SetBlackboardIntSo(ref_instance, m_sResultEntry, 0);
}

void plStateMachineState_AiUseSmartObject::Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  if (pData->m_bDone || !pData->m_bStarted)
    return;

  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObjectSo(ref_instance);
  auto* pTactical = (pWorld != nullptr) ? pWorld->GetModule<plAiTacticalWorldModule>() : nullptr;

  if (pOwner == nullptr || pTactical == nullptr)
  {
    pData->m_bDone = true;
    return;
  }

  plAiSmartObjectHandle hSlot;

  if (!pTactical->GetClaimedSmartObject(pOwner->GetHandle(), hSlot))
  {
    // the object vanished or the claim was swept away mid-use
    AbortUse(ref_instance, pData, pTactical, pOwner);
    SetBlackboardIntSo(ref_instance, m_sResultEntry, 2);
    return;
  }

  bool bFinished = false;

  if (pData->m_bBuiltinWait)
  {
    bFinished = pWorld->GetClock().GetAccumulatedTime() >= pData->m_FinishAt;
  }
  else
  {
    bFinished = pTactical->GetUsePhase(pOwner->GetHandle()) == plAiSmartObjectUsePhase::Finished;
  }

  if (!bFinished)
    return;

  plVec3 vSlotPos;
  plComponentHandle hComponent;

  if (pTactical->ResolveSmartObject(hSlot, vSlotPos, hComponent))
  {
    ApplyUserEntries(ref_instance, hComponent, false);
  }

  pTactical->EndUse(pOwner->GetHandle());

  if (m_bReleaseClaimWhenDone)
  {
    pTactical->ReleaseSmartObjectClaim(pOwner->GetHandle());
  }

  pData->m_bStarted = false;
  pData->m_bDone = true;
  SetBlackboardIntSo(ref_instance, m_sResultEntry, 1);
}

void plStateMachineState_AiUseSmartObject::OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  if (!pData->m_bStarted)
    return;

  // interrupted mid-use (behavior switch): revert entries, notify the object, keep the claim
  // (the sweep drops it when the agent walks away)
  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObjectSo(ref_instance);
  auto* pTactical = (pWorld != nullptr) ? pWorld->GetModule<plAiTacticalWorldModule>() : nullptr;

  AbortUse(ref_instance, pData, pTactical, pOwner);
}

plResult plStateMachineState_AiUseSmartObject::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));

  inout_stream << m_sResultEntry;
  inout_stream << m_bReleaseClaimWhenDone;
  inout_stream << m_bRequireInRange;

  return PL_SUCCESS;
}

plResult plStateMachineState_AiUseSmartObject::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));

  inout_stream >> m_sResultEntry;
  inout_stream >> m_bReleaseClaimWhenDone;
  inout_stream >> m_bRequireInRange;

  return PL_SUCCESS;
}

bool plStateMachineState_AiUseSmartObject::GetInstanceDataDesc(plInstanceDataDesc& out_desc)
{
  out_desc.FillFromType<InstanceData>();
  return true;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiReleaseSmartObject, 1, plRTTIDefaultAllocator<plStateMachineState_AiReleaseSmartObject>)
{
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plStateMachineState_AiReleaseSmartObject::plStateMachineState_AiReleaseSmartObject(plStringView sName)
  : plStateMachineState(sName)
{
}

plStateMachineState_AiReleaseSmartObject::~plStateMachineState_AiReleaseSmartObject() = default;

void plStateMachineState_AiReleaseSmartObject::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObjectSo(ref_instance);

  if (pWorld == nullptr || pOwner == nullptr)
    return;

  if (auto* pTactical = pWorld->GetModule<plAiTacticalWorldModule>())
  {
    pTactical->ReleaseSmartObjectClaim(pOwner->GetHandle());
  }
}

plResult plStateMachineState_AiReleaseSmartObject::Serialize(plStreamWriter& inout_stream) const
{
  return SUPER::Serialize(inout_stream);
}

plResult plStateMachineState_AiReleaseSmartObject::Deserialize(plStreamReader& inout_stream)
{
  return SUPER::Deserialize(inout_stream);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiSmartObjectStates);
