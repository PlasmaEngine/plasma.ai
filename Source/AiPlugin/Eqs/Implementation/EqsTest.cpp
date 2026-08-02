#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/EqsQueryResource.h>
#include <AiPlugin/Eqs/EqsTest.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <AiPlugin/Utils/RcMath.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <DetourNavMeshQuery.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiEqsTestPurpose, 1)
  PL_ENUM_CONSTANTS(plAiEqsTestPurpose::FilterOnly, plAiEqsTestPurpose::ScoreOnly, plAiEqsTestPurpose::FilterAndScore)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiEqsContextCombine, 1)
  PL_ENUM_CONSTANTS(plAiEqsContextCombine::Min, plAiEqsContextCombine::Max, plAiEqsContextCombine::Average)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTest, 1, plRTTINoAllocator)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Purpose", plAiEqsTestPurpose, m_Purpose),
    PL_MEMBER_PROPERTY("Weight", m_fWeight)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTest_Distance, 1, plRTTIDefaultAllocator<plAiEqsTest_Distance>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Context", GetContext, SetContext)->AddAttributes(new plDefaultValueAttribute(plStringView("Querier"))),
    PL_MEMBER_PROPERTY("BandMin", m_fBandMin)->AddAttributes(new plClampValueAttribute(0.0f, 500.0f)),
    PL_MEMBER_PROPERTY("BandMax", m_fBandMax)->AddAttributes(new plDefaultValueAttribute(10.0f), new plClampValueAttribute(0.0f, 500.0f)),
    PL_ENUM_MEMBER_PROPERTY("Combine", plAiEqsContextCombine, m_Combine),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTest_Direction, 1, plRTTIDefaultAllocator<plAiEqsTest_Direction>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Context", GetContext, SetContext)->AddAttributes(new plDefaultValueAttribute(plStringView("Threat"))),
    PL_MEMBER_PROPERTY("DesiredAngle", m_DesiredAngle)->AddAttributes(new plDefaultValueAttribute(plAngle::MakeFromDegree(100))),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTest_CoverQuality, 1, plRTTIDefaultAllocator<plAiEqsTest_CoverQuality>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("MinQuality", plAiCoverQuality, m_MinQuality),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTest_CoverFacing, 1, plRTTIDefaultAllocator<plAiEqsTest_CoverFacing>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Context", GetContext, SetContext)->AddAttributes(new plDefaultValueAttribute(plStringView("Threat"))),
    PL_MEMBER_PROPERTY("MinDot", m_fMinDot)->AddAttributes(new plDefaultValueAttribute(0.2f), new plClampValueAttribute(-1.0f, 1.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTest_Unclaimed, 1, plRTTIDefaultAllocator<plAiEqsTest_Unclaimed>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTest_LineOfSight, 1, plRTTIDefaultAllocator<plAiEqsTest_LineOfSight>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Context", GetContext, SetContext)->AddAttributes(new plDefaultValueAttribute(plStringView("Threat"))),
    PL_MEMBER_PROPERTY("PreferVisible", m_bPreferVisible),
    PL_MEMBER_PROPERTY("ItemEyeHeight", m_fItemEyeHeight)->AddAttributes(new plDefaultValueAttribute(1.55f), new plClampValueAttribute(0.0f, 5.0f)),
    PL_MEMBER_PROPERTY("CoverEyeHeight", m_fCoverEyeHeight)->AddAttributes(new plDefaultValueAttribute(0.6f), new plClampValueAttribute(0.0f, 5.0f)),
    PL_MEMBER_PROPERTY("ContextEyeHeight", m_fContextEyeHeight)->AddAttributes(new plDefaultValueAttribute(1.55f), new plClampValueAttribute(0.0f, 5.0f)),
    PL_ENUM_MEMBER_PROPERTY("Combine", plAiEqsContextCombine, m_Combine),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTest_ReachableApprox, 1, plRTTIDefaultAllocator<plAiEqsTest_ReachableApprox>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsTest_PathLength, 1, plRTTIDefaultAllocator<plAiEqsTest_PathLength>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("BandMin", m_fBandMin)->AddAttributes(new plClampValueAttribute(0.0f, 1000.0f)),
    PL_MEMBER_PROPERTY("BandMax", m_fBandMax)->AddAttributes(new plDefaultValueAttribute(20.0f), new plClampValueAttribute(0.0f, 1000.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

float plAiEqsTrapezoidScore(float fValue, float fFullMin, float fFullMax)
{
  if (fValue >= fFullMin && fValue <= fFullMax)
    return 1.0f;

  const float fFalloff = plMath::Max(0.5f, (fFullMax - fFullMin) * 0.25f);

  if (fValue < fFullMin)
    return plMath::Max(0.0f, 1.0f - (fFullMin - fValue) / fFalloff);

  return plMath::Max(0.0f, 1.0f - (fValue - fFullMax) / fFalloff);
}

namespace
{
  float Combine(plAiEqsContextCombine::Enum mode, plArrayPtr<const float> values)
  {
    if (values.IsEmpty())
      return 1.0f;

    float fResult = values[0];

    switch (mode)
    {
      case plAiEqsContextCombine::Min:
        for (float f : values)
          fResult = plMath::Min(fResult, f);
        break;

      case plAiEqsContextCombine::Max:
        for (float f : values)
          fResult = plMath::Max(fResult, f);
        break;

      case plAiEqsContextCombine::Average:
      {
        fResult = 0.0f;
        for (float f : values)
          fResult += f;
        fResult /= values.GetCount();
        break;
      }
    }

    return fResult;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// Distance

plAiEqsTest_Distance::plAiEqsTest_Distance()
{
  m_sContext.Assign("Querier");
}

plUInt32 plAiEqsTest_Distance::Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const
{
  const plAiEqsResolvedSlot* pSlot = ref_ctx.FindSlot(plTempHashedString(m_sContext.GetData()));

  for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
  {
    plAiEqsItem& item = items[i];

    if (pSlot == nullptr || pSlot->m_Positions.IsEmpty())
    {
      item.m_fRaw = 1.0f; // context unavailable - the test does not discriminate
      continue;
    }

    plHybridArray<float, 4> scores;

    for (const plVec3& vPos : pSlot->m_Positions)
    {
      scores.PushBack(plAiEqsTrapezoidScore((item.m_vPosition - vPos).GetAsVec2().GetLength(), m_fBandMin, m_fBandMax));
    }

    item.m_fRaw = Combine(static_cast<plAiEqsContextCombine::Enum>(m_Combine.GetValue()), scores);
  }

  return items.GetCount() - uiFirstItem;
}

//////////////////////////////////////////////////////////////////////////
// Direction

plAiEqsTest_Direction::plAiEqsTest_Direction()
{
  m_sContext.Assign("Threat");
}

plUInt32 plAiEqsTest_Direction::Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const
{
  const plAiEqsResolvedSlot* pSlot = ref_ctx.FindSlot(plTempHashedString(m_sContext.GetData()));
  const plVec3 vFrom = (pSlot != nullptr && !pSlot->m_Positions.IsEmpty()) ? pSlot->m_Positions[0] : plVec3::MakeZero();
  const bool bValid = pSlot != nullptr && !pSlot->m_Positions.IsEmpty();

  plVec2 vBase = (ref_ctx.m_vQuerier - vFrom).GetAsVec2();
  const bool bBaseValid = bValid && vBase.NormalizeIfNotZero(plVec2(1, 0)).Succeeded();

  for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
  {
    plAiEqsItem& item = items[i];
    item.m_fRaw = 1.0f;

    if (!bBaseValid)
      continue;

    plVec2 vCand = (item.m_vPosition - vFrom).GetAsVec2();

    if (vCand.NormalizeIfNotZero(plVec2(1, 0)).Failed())
      continue;

    const float fAngle = plMath::ACos(plMath::Clamp(vBase.Dot(vCand), -1.0f, 1.0f)).GetRadian();
    item.m_fRaw = plMath::Max(0.0f, 1.0f - plMath::Abs(fAngle - m_DesiredAngle.GetRadian()) / plMath::Pi<float>());
  }

  return items.GetCount() - uiFirstItem;
}

//////////////////////////////////////////////////////////////////////////
// CoverQuality

plAiEqsTest_CoverQuality::plAiEqsTest_CoverQuality()
{
  m_Purpose = plAiEqsTestPurpose::FilterOnly;
  m_MinQuality = plAiCoverQuality::Low;
}

plUInt32 plAiEqsTest_CoverQuality::Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const
{
  for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
  {
    plAiEqsItem& item = items[i];
    item.m_fRaw = 1.0f;

    if (!item.m_hCover.IsValid() || ref_ctx.m_pTactical == nullptr)
      continue; // non-cover items pass

    plAiCoverPoint point;

    if (!ref_ctx.m_pTactical->ResolveCover(item.m_hCover, point) || point.m_Quality.GetValue() < m_MinQuality.GetValue())
    {
      item.m_fRaw = 0.0f;
    }
    else
    {
      item.m_fRaw = (point.m_Quality == plAiCoverQuality::High) ? 1.0f : 0.5f;
    }
  }

  return items.GetCount() - uiFirstItem;
}

//////////////////////////////////////////////////////////////////////////
// CoverFacing

plAiEqsTest_CoverFacing::plAiEqsTest_CoverFacing()
{
  m_Purpose = plAiEqsTestPurpose::FilterOnly;
  m_sContext.Assign("Threat");
}

plUInt32 plAiEqsTest_CoverFacing::Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const
{
  const plAiEqsResolvedSlot* pSlot = ref_ctx.FindSlot(plTempHashedString(m_sContext.GetData()));
  const bool bValid = pSlot != nullptr && !pSlot->m_Positions.IsEmpty();

  for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
  {
    plAiEqsItem& item = items[i];
    item.m_fRaw = 1.0f;

    if (!bValid || !item.m_hCover.IsValid() || ref_ctx.m_pTactical == nullptr)
      continue; // no context / non-cover items pass

    plAiCoverPoint point;

    if (!ref_ctx.m_pTactical->ResolveCover(item.m_hCover, point))
    {
      item.m_fRaw = 0.0f; // stale handle - never pick it
      continue;
    }

    // the wall must face towards at least one context position (pessimistic: the FIRST one,
    // matching the tactical CoverFacingThreat scorer; multi-threat facing is a curve concern)
    plVec2 vToThreat = (pSlot->m_Positions[0] - item.m_vPosition).GetAsVec2();

    if (vToThreat.NormalizeIfNotZero(plVec2(1, 0)).Succeeded())
    {
      item.m_fRaw = (point.m_vWallDir.GetAsVec2().Dot(vToThreat) >= m_fMinDot) ? 1.0f : 0.0f;
    }
  }

  return items.GetCount() - uiFirstItem;
}

//////////////////////////////////////////////////////////////////////////
// Unclaimed

plAiEqsTest_Unclaimed::plAiEqsTest_Unclaimed()
{
  m_Purpose = plAiEqsTestPurpose::FilterOnly;
}

plUInt32 plAiEqsTest_Unclaimed::Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const
{
  for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
  {
    plAiEqsItem& item = items[i];
    item.m_fRaw = 1.0f;

    if (ref_ctx.m_pTactical == nullptr)
      continue;

    if (item.m_hCover.IsValid() && ref_ctx.m_pTactical->IsCoverClaimedByOther(item.m_hCover, ref_ctx.m_hQuerier))
    {
      item.m_fRaw = 0.0f;
    }
    else if (item.m_hSmartObject.IsValid() && ref_ctx.m_pTactical->IsSmartObjectSlotClaimedByOther(item.m_hSmartObject, ref_ctx.m_hQuerier))
    {
      item.m_fRaw = 0.0f;
    }
  }

  return items.GetCount() - uiFirstItem;
}

//////////////////////////////////////////////////////////////////////////
// LineOfSight

plAiEqsTest_LineOfSight::plAiEqsTest_LineOfSight()
{
  m_sContext.Assign("Threat");
}

plUInt32 plAiEqsTest_LineOfSight::Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const
{
  const plAiEqsResolvedSlot* pSlot = ref_ctx.FindSlot(plTempHashedString(m_sContext.GetData()));
  const bool bValid = pSlot != nullptr && !pSlot->m_Positions.IsEmpty() && ref_ctx.m_pPhysics != nullptr;

  const plPhysicsQueryParameters params(ref_ctx.m_uiCollisionLayer, plPhysicsShapeType::Static);

  for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
  {
    plAiEqsItem& item = items[i];

    if (!bValid)
    {
      item.m_fRaw = 1.0f;
      continue;
    }

    // all rays of ONE item must fit the remaining budget, so a suspended query resumes cleanly at
    // this item without re-casting half of its rays
    if (ref_ctx.m_iRaycastBudget < static_cast<plInt32>(pSlot->m_Positions.GetCount()))
      return i - uiFirstItem;

    const float fEyeHeight = item.m_hCover.IsValid() ? m_fCoverEyeHeight : m_fItemEyeHeight;
    const plVec3 vItemEye = item.m_vPosition + plVec3(0, 0, fEyeHeight);

    plHybridArray<float, 4> scores;

    for (const plVec3& vContextPos : pSlot->m_Positions)
    {
      const plVec3 vContextEye = vContextPos + plVec3(0, 0, m_fContextEyeHeight);

      plVec3 vDir = vItemEye - vContextEye;
      const float fDist = vDir.GetLength();

      bool bBlocked = false;

      if (fDist > 0.01f && ref_ctx.TryConsumeRaycast())
      {
        vDir /= fDist;
        plPhysicsCastResult hit;
        bBlocked = ref_ctx.m_pPhysics->Raycast(hit, vContextEye, vDir, fDist, params);
      }

      scores.PushBack((bBlocked != m_bPreferVisible) ? 1.0f : 0.0f);
    }

    item.m_fRaw = Combine(static_cast<plAiEqsContextCombine::Enum>(m_Combine.GetValue()), scores);
  }

  return items.GetCount() - uiFirstItem;
}

//////////////////////////////////////////////////////////////////////////
// ReachableApprox

plAiEqsTest_ReachableApprox::plAiEqsTest_ReachableApprox() = default;

plUInt32 plAiEqsTest_ReachableApprox::Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const
{
  dtPolyRef startRef = 0;
  plRcPos startPos;
  const float fExtents[3] = {plAiNavProjectionExtentXY, plAiNavProjectionExtentZ, plAiNavProjectionExtentXY};

  if (ref_ctx.m_pNavQuery == nullptr || dtStatusFailed(ref_ctx.m_pNavQuery->findNearestPoly(plRcPos(ref_ctx.m_vQuerier), fExtents, ref_ctx.m_pFilter, &startRef, startPos)) || startRef == 0)
  {
    for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
    {
      items[i].m_fRaw = 1.0f; // querier off the navmesh - do not discriminate
    }

    return items.GetCount() - uiFirstItem;
  }

  for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
  {
    plAiEqsItem& item = items[i];
    item.m_fRaw = 1.0f;

    if (!ref_ctx.TryConsumePathQuery())
      return i - uiFirstItem;

    float fT = 0.0f;
    plVec3 vHitNormal;
    dtPolyRef path[32];
    int iPathCount = 0;

    if (!dtStatusFailed(ref_ctx.m_pNavQuery->raycast(startRef, startPos, plRcPos(item.m_vPosition), ref_ctx.m_pFilter, &fT, &vHitNormal.x, path, &iPathCount, 32)))
    {
      // t > 1 means the target was reached in a straight walk; a hit means a detour is needed
      item.m_fRaw = (fT > 1.0f) ? 1.0f : 0.25f;
    }
  }

  return items.GetCount() - uiFirstItem;
}

//////////////////////////////////////////////////////////////////////////
// PathLength

plAiEqsTest_PathLength::plAiEqsTest_PathLength() = default;

plUInt32 plAiEqsTest_PathLength::Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const
{
  dtPolyRef startRef = 0;
  plRcPos startPos;
  const float fExtents[3] = {plAiNavProjectionExtentXY, plAiNavProjectionExtentZ, plAiNavProjectionExtentXY};

  if (ref_ctx.m_pNavQuery == nullptr || dtStatusFailed(ref_ctx.m_pNavQuery->findNearestPoly(plRcPos(ref_ctx.m_vQuerier), fExtents, ref_ctx.m_pFilter, &startRef, startPos)) || startRef == 0)
  {
    for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
    {
      items[i].m_fRaw = 1.0f; // querier off the navmesh - do not discriminate
    }

    return items.GetCount() - uiFirstItem;
  }

  for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
  {
    plAiEqsItem& item = items[i];

    if (!ref_ctx.TryConsumePathQuery())
      return i - uiFirstItem;

    item.m_fRaw = 0.0f; // unreachable until proven otherwise

    dtPolyRef endRef = 0;
    plRcPos endPos;

    if (dtStatusFailed(ref_ctx.m_pNavQuery->findNearestPoly(plRcPos(item.m_vPosition), fExtents, ref_ctx.m_pFilter, &endRef, endPos)) || endRef == 0)
      continue;

    dtPolyRef path[64];
    int iPathCount = 0;

    const dtStatus status = ref_ctx.m_pNavQuery->findPath(startRef, endRef, startPos, endPos, ref_ctx.m_pFilter, path, &iPathCount, 64);

    if (dtStatusFailed(status) || iPathCount == 0 || path[iPathCount - 1] != endRef)
      continue; // no full path - unreachable (or truncated by the poly limit: treat as unreachable)

    // walk the string-pulled corridor to measure the real path distance
    plRcPos straight[32];
    int iStraightCount = 0;

    if (dtStatusFailed(ref_ctx.m_pNavQuery->findStraightPath(startPos, endPos, path, iPathCount, &straight[0].m_Pos[0], nullptr, nullptr, &iStraightCount, 32)) || iStraightCount == 0)
      continue;

    float fLength = 0.0f;

    for (int c = 1; c < iStraightCount; ++c)
    {
      fLength += (plVec3(straight[c]) - plVec3(straight[c - 1])).GetLength();
    }

    item.m_fRaw = plMath::Max(0.001f, plAiEqsTrapezoidScore(fLength, m_fBandMin, m_fBandMax)); // reachable never hard-fails on distance alone... unless banded to 0
  }

  return items.GetCount() - uiFirstItem;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Eqs_Implementation_EqsTest);
