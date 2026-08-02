#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Tactical/Components/SmartObjectComponent.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <Core/World/World.h>
#include <Foundation/Configuration/CVar.h>
#include <RendererCore/Debug/DebugRenderer.h>

plCVarBool cvar_SmartObjectsShowDebug("AI.SmartObjects.ShowDebug", false, plCVarFlags::Default, "Visualize smart objects: slots, types and who claims them.");

// clang-format off
PL_BEGIN_STATIC_REFLECTED_TYPE(plAiSmartObjectSlot, plNoBase, 1, plRTTIDefaultAllocator<plAiSmartObjectSlot>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("LocalPosition", m_vLocalPosition),
    PL_MEMBER_PROPERTY("LocalRotation", m_qLocalRotation),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;

PL_IMPLEMENT_MESSAGE_TYPE(plMsgAiSmartObjectUse);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgAiSmartObjectUse, 1, plRTTIDefaultAllocator<plMsgAiSmartObjectUse>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiTacticalWorldModule::SmartObjectID plAiTacticalWorldModule::RegisterSmartObject(plComponentHandle hComponent)
{
  SmartObjectID id = InvalidSmartObjectID;

  if (!m_FreeSmartObjects.IsEmpty())
  {
    id = m_FreeSmartObjects.PeekBack();
    m_FreeSmartObjects.PopBack();
  }
  else
  {
    id = m_SmartObjects.GetCount();
    m_SmartObjects.ExpandAndGetRef();
  }

  SmartObjectEntry& entry = m_SmartObjects[id];
  entry.m_hComponent = hComponent;
  entry.m_bInUse = true;
  entry.m_SlotClaims.Clear();

  plAiSmartObjectComponent* pComponent = nullptr;

  if (GetWorld()->TryGetComponent(hComponent, pComponent))
  {
    entry.m_SlotClaims.SetCount(plMath::Max<plUInt32>(1, pComponent->m_Slots.GetCount()), 0xFFFF);
  }
  else
  {
    entry.m_SlotClaims.SetCount(1, 0xFFFF);
  }

  return id;
}

void plAiTacticalWorldModule::UnregisterSmartObject(SmartObjectID id)
{
  if (id >= m_SmartObjects.GetCount() || !m_SmartObjects[id].m_bInUse)
    return;

  SmartObjectEntry& entry = m_SmartObjects[id];

  for (plUInt16 uiClaim : entry.m_SlotClaims)
  {
    if (uiClaim != 0xFFFF)
    {
      ReleaseSoClaimAtIndex(uiClaim);
    }
  }

  entry.m_bInUse = false;
  entry.m_hComponent.Invalidate();
  ++entry.m_uiGeneration; // stale handles now mismatch

  m_FreeSmartObjects.PushBack(id);
}

bool plAiTacticalWorldModule::FindSmartObject(const plVec3& vPosition, float fRadius, const plTempHashedString& sType, plGameObjectHandle hClaimant, plAiSmartObjectHandle& out_hSlot, plVec3& out_vSlotPosition) const
{
  float fBestDistSqr = plMath::Square(fRadius);
  bool bFound = false;

  for (plUInt32 uiEntry = 0; uiEntry < m_SmartObjects.GetCount(); ++uiEntry)
  {
    const SmartObjectEntry& entry = m_SmartObjects[uiEntry];

    if (!entry.m_bInUse)
      continue;

    const plAiSmartObjectComponent* pComponent = nullptr;

    if (!GetWorld()->TryGetComponent(entry.m_hComponent, pComponent) || !pComponent->IsActiveAndSimulating())
      continue;

    if (pComponent->m_sType != sType)
      continue;

    for (plUInt32 uiSlot = 0; uiSlot < entry.m_SlotClaims.GetCount(); ++uiSlot)
    {
      // skip slots claimed by someone else (own claim counts as available)
      if (entry.m_SlotClaims[uiSlot] != 0xFFFF)
      {
        const SoClaim& claim = m_SoClaims[entry.m_SlotClaims[uiSlot]];

        if (claim.m_bInUse && claim.m_hClaimant != hClaimant)
          continue;
      }

      const plVec3 vSlotPos = pComponent->GetSlotGlobalTransform(static_cast<plUInt8>(uiSlot)).m_vPosition;
      const float fDistSqr = (vSlotPos.GetAsVec2() - vPosition.GetAsVec2()).GetLengthSquared();

      if (fDistSqr < fBestDistSqr)
      {
        fBestDistSqr = fDistSqr;
        bFound = true;
        out_hSlot.m_uiEntry = uiEntry;
        out_hSlot.m_uiGeneration = entry.m_uiGeneration;
        out_hSlot.m_uiSlot = static_cast<plUInt8>(uiSlot);
        out_vSlotPosition = vSlotPos;
      }
    }
  }

  return bFound;
}

bool plAiTacticalWorldModule::ResolveSmartObject(const plAiSmartObjectHandle& hSlot, plVec3& out_vSlotPosition, plComponentHandle& out_hComponent) const
{
  if (!hSlot.IsValid() || hSlot.m_uiEntry >= m_SmartObjects.GetCount())
    return false;

  const SmartObjectEntry& entry = m_SmartObjects[hSlot.m_uiEntry];

  if (!entry.m_bInUse || entry.m_uiGeneration != hSlot.m_uiGeneration || hSlot.m_uiSlot >= entry.m_SlotClaims.GetCount())
    return false;

  const plAiSmartObjectComponent* pComponent = nullptr;

  if (!GetWorld()->TryGetComponent(entry.m_hComponent, pComponent))
    return false;

  out_vSlotPosition = pComponent->GetSlotGlobalTransform(hSlot.m_uiSlot).m_vPosition;
  out_hComponent = entry.m_hComponent;
  return true;
}

bool plAiTacticalWorldModule::ClaimSmartObject(const plAiSmartObjectHandle& hSlot, plGameObjectHandle hClaimant, plTime timeout)
{
  if (hClaimant.IsInvalidated() || !hSlot.IsValid() || hSlot.m_uiEntry >= m_SmartObjects.GetCount())
    return false;

  SmartObjectEntry& entry = m_SmartObjects[hSlot.m_uiEntry];

  if (!entry.m_bInUse || entry.m_uiGeneration != hSlot.m_uiGeneration || hSlot.m_uiSlot >= entry.m_SlotClaims.GetCount())
    return false;

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  if (entry.m_SlotClaims[hSlot.m_uiSlot] != 0xFFFF)
  {
    SoClaim& existing = m_SoClaims[entry.m_SlotClaims[hSlot.m_uiSlot]];

    if (existing.m_bInUse && existing.m_hClaimant == hClaimant)
    {
      existing.m_Expiry = now + timeout;
      existing.m_Timeout = timeout;
      return true;
    }

    return false; // taken by someone else
  }

  // one tactical claim of either kind per agent
  ReleaseClaim(hClaimant);
  ReleaseSmartObjectClaim(hClaimant);

  plUInt32 uiClaim = plInvalidIndex;

  if (!m_FreeSoClaims.IsEmpty())
  {
    uiClaim = m_FreeSoClaims.PeekBack();
    m_FreeSoClaims.PopBack();
  }
  else
  {
    uiClaim = m_SoClaims.GetCount();
    m_SoClaims.ExpandAndGetRef();
  }

  SoClaim& claim = m_SoClaims[uiClaim];
  claim.m_hClaimant = hClaimant;
  claim.m_hSlot = hSlot;
  claim.m_Expiry = now + timeout;
  claim.m_Timeout = timeout;
  claim.m_uiUsePhase = plAiSmartObjectUsePhase::NotUsing;
  claim.m_bInUse = true;

  entry.m_SlotClaims[hSlot.m_uiSlot] = static_cast<plUInt16>(uiClaim);
  m_SoClaimByOwner[hClaimant] = uiClaim;

  return true;
}

void plAiTacticalWorldModule::ReleaseSoClaimAtIndex(plUInt32 uiClaimIndex)
{
  if (uiClaimIndex >= m_SoClaims.GetCount() || !m_SoClaims[uiClaimIndex].m_bInUse)
    return;

  SoClaim& claim = m_SoClaims[uiClaimIndex];

  if (claim.m_hSlot.IsValid() && claim.m_hSlot.m_uiEntry < m_SmartObjects.GetCount())
  {
    SmartObjectEntry& entry = m_SmartObjects[claim.m_hSlot.m_uiEntry];

    if (entry.m_bInUse && entry.m_uiGeneration == claim.m_hSlot.m_uiGeneration && claim.m_hSlot.m_uiSlot < entry.m_SlotClaims.GetCount())
    {
      entry.m_SlotClaims[claim.m_hSlot.m_uiSlot] = 0xFFFF;
    }
  }

  m_SoClaimByOwner.Remove(claim.m_hClaimant);
  claim.m_bInUse = false;
  claim.m_hClaimant.Invalidate();
  claim.m_hSlot.Invalidate();

  m_FreeSoClaims.PushBack(uiClaimIndex);
}

void plAiTacticalWorldModule::ReleaseSmartObjectClaim(plGameObjectHandle hClaimant)
{
  plUInt32 uiClaim = plInvalidIndex;

  if (m_SoClaimByOwner.TryGetValue(hClaimant, uiClaim))
  {
    ReleaseSoClaimAtIndex(uiClaim);
  }
}

bool plAiTacticalWorldModule::GetClaimedSmartObject(plGameObjectHandle hClaimant, plAiSmartObjectHandle& out_hSlot) const
{
  plUInt32 uiClaim = plInvalidIndex;

  if (!m_SoClaimByOwner.TryGetValue(hClaimant, uiClaim) || !m_SoClaims[uiClaim].m_bInUse)
    return false;

  out_hSlot = m_SoClaims[uiClaim].m_hSlot;
  return true;
}

bool plAiTacticalWorldModule::BeginUse(plGameObjectHandle hUser)
{
  plUInt32 uiClaim = plInvalidIndex;

  if (!m_SoClaimByOwner.TryGetValue(hUser, uiClaim) || !m_SoClaims[uiClaim].m_bInUse)
    return false;

  SoClaim& claim = m_SoClaims[uiClaim];

  plVec3 vSlotPos;
  plComponentHandle hComponent;

  if (!ResolveSmartObject(claim.m_hSlot, vSlotPos, hComponent))
    return false;

  claim.m_uiUsePhase = plAiSmartObjectUsePhase::InUse;

  plAiSmartObjectComponent* pComponent = nullptr;
  GetWorld()->TryGetComponent(hComponent, pComponent);

  plMsgAiSmartObjectUse msg;
  msg.m_hUser = hUser;
  msg.m_hSmartObject = pComponent->GetOwner()->GetHandle();
  msg.m_uiSlot = claim.m_hSlot.m_uiSlot;
  msg.m_sType = pComponent->m_sType;
  msg.m_bStart = true;

  return pComponent->GetOwner()->SendMessage(msg);
}

void plAiTacticalWorldModule::FinishUse(plGameObjectHandle hUser)
{
  plUInt32 uiClaim = plInvalidIndex;

  if (m_SoClaimByOwner.TryGetValue(hUser, uiClaim) && m_SoClaims[uiClaim].m_bInUse && m_SoClaims[uiClaim].m_uiUsePhase == plAiSmartObjectUsePhase::InUse)
  {
    m_SoClaims[uiClaim].m_uiUsePhase = plAiSmartObjectUsePhase::Finished;
  }
}

void plAiTacticalWorldModule::EndUse(plGameObjectHandle hUser)
{
  plUInt32 uiClaim = plInvalidIndex;

  if (!m_SoClaimByOwner.TryGetValue(hUser, uiClaim) || !m_SoClaims[uiClaim].m_bInUse)
    return;

  SoClaim& claim = m_SoClaims[uiClaim];

  if (claim.m_uiUsePhase == plAiSmartObjectUsePhase::NotUsing)
    return;

  claim.m_uiUsePhase = plAiSmartObjectUsePhase::NotUsing;

  plVec3 vSlotPos;
  plComponentHandle hComponent;

  if (ResolveSmartObject(claim.m_hSlot, vSlotPos, hComponent))
  {
    plAiSmartObjectComponent* pComponent = nullptr;

    if (GetWorld()->TryGetComponent(hComponent, pComponent))
    {
      plMsgAiSmartObjectUse msg;
      msg.m_hUser = hUser;
      msg.m_hSmartObject = pComponent->GetOwner()->GetHandle();
      msg.m_uiSlot = claim.m_hSlot.m_uiSlot;
      msg.m_sType = pComponent->m_sType;
      msg.m_bStart = false;

      pComponent->GetOwner()->SendMessage(msg);
    }
  }
}

plAiSmartObjectUsePhase::Enum plAiTacticalWorldModule::GetUsePhase(plGameObjectHandle hUser) const
{
  plUInt32 uiClaim = plInvalidIndex;

  if (m_SoClaimByOwner.TryGetValue(hUser, uiClaim) && m_SoClaims[uiClaim].m_bInUse)
  {
    return static_cast<plAiSmartObjectUsePhase::Enum>(m_SoClaims[uiClaim].m_uiUsePhase);
  }

  return plAiSmartObjectUsePhase::NotUsing;
}

void plAiTacticalWorldModule::SweepSmartObjectClaims()
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  for (plUInt32 uiClaim = 0; uiClaim < m_SoClaims.GetCount(); ++uiClaim)
  {
    SoClaim& claim = m_SoClaims[uiClaim];

    if (!claim.m_bInUse)
      continue;

    plGameObject* pClaimant = nullptr;

    if (!GetWorld()->TryGetObject(claim.m_hClaimant, pClaimant))
    {
      ReleaseSoClaimAtIndex(uiClaim); // claimant died
      continue;
    }

    plVec3 vSlotPos;
    plComponentHandle hComponent;

    if (!ResolveSmartObject(claim.m_hSlot, vSlotPos, hComponent))
    {
      ReleaseSoClaimAtIndex(uiClaim); // object unregistered
      continue;
    }

    if (claim.m_uiUsePhase != plAiSmartObjectUsePhase::NotUsing)
    {
      claim.m_Expiry = now + claim.m_Timeout; // objects in use never expire
      continue;
    }

    plAiSmartObjectComponent* pComponent = nullptr;
    GetWorld()->TryGetComponent(hComponent, pComponent);

    const float fRange = (pComponent != nullptr) ? plMath::Max(1.0f, pComponent->m_fUseRange * 2.0f) : 2.0f;

    if ((pClaimant->GetGlobalPosition().GetAsVec2() - vSlotPos.GetAsVec2()).GetLengthSquared() < plMath::Square(fRange))
    {
      claim.m_Expiry = now + claim.m_Timeout; // standing at it - keep the reservation
    }
    else if (now > claim.m_Expiry)
    {
      ReleaseSoClaimAtIndex(uiClaim);
    }
  }
}

void plAiTacticalWorldModule::DrawSmartObjectDebug()
{
  if (!cvar_SmartObjectsShowDebug)
    return;

  for (const SmartObjectEntry& entry : m_SmartObjects)
  {
    if (!entry.m_bInUse)
      continue;

    plAiSmartObjectComponent* pComponent = nullptr;

    if (!GetWorld()->TryGetComponent(entry.m_hComponent, pComponent))
      continue;

    for (plUInt32 uiSlot = 0; uiSlot < entry.m_SlotClaims.GetCount(); ++uiSlot)
    {
      const plVec3 vSlotPos = pComponent->GetSlotGlobalTransform(static_cast<plUInt8>(uiSlot)).m_vPosition;
      const bool bClaimed = entry.m_SlotClaims[uiSlot] != 0xFFFF;

      plDebugRenderer::DrawLineSphere(GetWorld(), plBoundingSphere::MakeFromCenterAndRadius(vSlotPos + plVec3(0, 0, 0.2f), 0.2f), bClaimed ? plColor::Red : plColor::CornflowerBlue);

      if (bClaimed)
      {
        const SoClaim& claim = m_SoClaims[entry.m_SlotClaims[uiSlot]];
        plGameObject* pClaimant = nullptr;

        if (GetWorld()->TryGetObject(claim.m_hClaimant, pClaimant))
        {
          plDebugRenderer::Line line(vSlotPos + plVec3(0, 0, 0.3f), pClaimant->GetGlobalPosition() + plVec3(0, 0, 0.3f));
          line.m_startColor = line.m_endColor = plColor::Red;
          plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(&line, 1), plColor::White);
        }
      }
    }

    plDebugRenderer::Draw3DText(GetWorld(), plFmt("{}", pComponent->m_sType), pComponent->GetOwner()->GetGlobalPosition() + plVec3(0, 0, 1.2f), plColor::CornflowerBlue);
  }
}

void plAiTacticalWorldModule::CollectFreeSmartObjectSlots(const plVec3& vCenter, float fRadius, const plTempHashedString& sType, plGameObjectHandle hClaimant, plUInt32 uiMaxSlots, plDynamicArray<plAiSmartObjectHandle>& out_slots, plDynamicArray<plVec3>& out_positions) const
{
  struct Found
  {
    PL_DECLARE_POD_TYPE();

    float m_fDistSqr;
    plVec3 m_vPosition;
    plUInt32 m_uiEntry;
    plUInt16 m_uiGeneration;
    plUInt8 m_uiSlot;
  };

  plHybridArray<Found, 32> found;
  const float fRadiusSqr = plMath::Square(fRadius);
  const bool bAnyType = sType == plTempHashedString("");

  for (plUInt32 uiEntry = 0; uiEntry < m_SmartObjects.GetCount(); ++uiEntry)
  {
    const SmartObjectEntry& entry = m_SmartObjects[uiEntry];

    if (!entry.m_bInUse)
      continue;

    const plAiSmartObjectComponent* pComponent = nullptr;

    if (!GetWorld()->TryGetComponent(entry.m_hComponent, pComponent) || !pComponent->IsActiveAndSimulating())
      continue;

    if (!bAnyType && pComponent->m_sType != sType)
      continue;

    for (plUInt32 uiSlot = 0; uiSlot < entry.m_SlotClaims.GetCount(); ++uiSlot)
    {
      // skip slots claimed by someone else (own claim counts as available)
      if (entry.m_SlotClaims[uiSlot] != 0xFFFF)
      {
        const SoClaim& claim = m_SoClaims[entry.m_SlotClaims[uiSlot]];

        if (claim.m_bInUse && claim.m_hClaimant != hClaimant)
          continue;
      }

      const plVec3 vSlotPos = pComponent->GetSlotGlobalTransform(static_cast<plUInt8>(uiSlot)).m_vPosition;
      const float fDistSqr = (vSlotPos.GetAsVec2() - vCenter.GetAsVec2()).GetLengthSquared();

      if (fDistSqr <= fRadiusSqr)
      {
        found.PushBack({fDistSqr, vSlotPos, uiEntry, entry.m_uiGeneration, static_cast<plUInt8>(uiSlot)});
      }
    }
  }

  found.Sort([](const Found& lhs, const Found& rhs) { return lhs.m_fDistSqr < rhs.m_fDistSqr; });

  const plUInt32 uiCount = plMath::Min(uiMaxSlots, found.GetCount());

  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    auto& hSlot = out_slots.ExpandAndGetRef();
    hSlot.m_uiEntry = found[i].m_uiEntry;
    hSlot.m_uiGeneration = found[i].m_uiGeneration;
    hSlot.m_uiSlot = found[i].m_uiSlot;

    out_positions.PushBack(found[i].m_vPosition);
  }
}

bool plAiTacticalWorldModule::IsSmartObjectSlotClaimedByOther(const plAiSmartObjectHandle& hSlot, plGameObjectHandle hSelf) const
{
  if (!hSlot.IsValid() || hSlot.m_uiEntry >= m_SmartObjects.GetCount())
    return false;

  const SmartObjectEntry& entry = m_SmartObjects[hSlot.m_uiEntry];

  if (!entry.m_bInUse || entry.m_uiGeneration != hSlot.m_uiGeneration || hSlot.m_uiSlot >= entry.m_SlotClaims.GetCount())
    return false;

  if (entry.m_SlotClaims[hSlot.m_uiSlot] == 0xFFFF)
    return false;

  const SoClaim& claim = m_SoClaims[entry.m_SlotClaims[hSlot.m_uiSlot]];
  return claim.m_bInUse && claim.m_hClaimant != hSelf;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Implementation_SmartObjects);
