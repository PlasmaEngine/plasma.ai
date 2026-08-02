#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <AiPlugin/Utils/RcMath.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/World/World.h>
#include <DetourNavMesh.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/Math/Random.h>

extern plCVarInt cvar_TacticalMaxRaycastsPerQuery;

//////////////////////////////////////////////////////////////////////////
// plAiTacticalQueryDesc::MakeFromPreset

plAiTacticalQueryDesc plAiTacticalQueryDesc::MakeFromPreset(plAiTacticalQueryPreset::Enum preset, const plVec3& vQuerier, const plVec3& vThreat, bool bHasThreat, float fRadiusMin, float fRadiusMax, plAngle flankAngle /*= 100 deg*/, plAiCoverQuality::Enum minQuality /*= Low*/)
{
  plAiTacticalQueryDesc desc;
  desc.m_vQuerier = vQuerier;
  desc.m_vThreat = vThreat;
  desc.m_bHasThreat = bHasThreat;
  desc.m_fRadiusMin = fRadiusMin;
  desc.m_fRadiusMax = fRadiusMax;

  auto addScorer = [&](plAiTacticalScorerDesc::Type type, bool bHard, float fParam0, float fParam1, float fWeight) {
    auto& s = desc.m_Scorers.ExpandAndGetRef();
    s.m_Type = type;
    s.m_bHardFilter = bHard;
    s.m_fParam0 = fParam0;
    s.m_fParam1 = fParam1;
    s.m_fWeight = fWeight;
  };

  switch (preset)
  {
    case plAiTacticalQueryPreset::TakeCoverFromTarget:
      desc.m_Generator = Generator::CoverPointsNear;
      desc.m_vCenter = vQuerier;
      addScorer(plAiTacticalScorerDesc::Type::UnclaimedOnly, true, 0, 0, 1);
      addScorer(plAiTacticalScorerDesc::Type::CoverQualityMin, true, static_cast<float>(minQuality), 0, 1);
      // cover must actually protect against the threat: the wall has to face them AND break their
      // line of sight - both are HARD requirements. No protecting cover in range => the query
      // fails and the utility layer picks a different behavior, instead of cowering in the open.
      addScorer(plAiTacticalScorerDesc::Type::CoverFacingThreat, true, 0.2f /*min facing dot*/, 0, 1);
      addScorer(plAiTacticalScorerDesc::Type::LosToThreat, true, 0 /*prefer blocked*/, 0, 1.5f);
      addScorer(plAiTacticalScorerDesc::Type::DistanceBandToQuerier, false, 0, fRadiusMax * 0.4f, 1.0f);
      break;

    case plAiTacticalQueryPreset::FlankTarget:
      desc.m_Generator = Generator::RingAroundPosition;
      desc.m_vCenter = vThreat;
      addScorer(plAiTacticalScorerDesc::Type::LosToThreat, false, 1 /*prefer visible*/, 0, 1.0f);
      addScorer(plAiTacticalScorerDesc::Type::DirectionFromThreat, false, flankAngle.GetRadian(), 0, 1.5f);
      addScorer(plAiTacticalScorerDesc::Type::DistanceBandToQuerier, false, 0, fRadiusMax * 1.5f, 0.5f);
      addScorer(plAiTacticalScorerDesc::Type::ReachableApprox, false, 0, 0, 1.0f);
      break;

    case plAiTacticalQueryPreset::RetreatFromTarget:
    {
      desc.m_Generator = Generator::RingAroundPosition;
      desc.m_vCenter = vQuerier;
      const float fCurThreatDist = bHasThreat ? (vThreat - vQuerier).GetAsVec2().GetLength() : 0.0f;
      addScorer(plAiTacticalScorerDesc::Type::DistanceBandToThreat, false, fCurThreatDist + fRadiusMax * 0.5f, fCurThreatDist + fRadiusMax * 4.0f, 1.5f);
      addScorer(plAiTacticalScorerDesc::Type::LosToThreat, false, 0 /*prefer blocked*/, 0, 0.75f);
      addScorer(plAiTacticalScorerDesc::Type::ReachableApprox, false, 0, 0, 1.0f);
      break;
    }

    case plAiTacticalQueryPreset::RandomNearSelf:
      desc.m_Generator = Generator::NavmeshRandom;
      desc.m_vCenter = vQuerier;
      addScorer(plAiTacticalScorerDesc::Type::ReachableApprox, false, 0, 0, 1.0f);
      break;

    case plAiTacticalQueryPreset::HideFromTarget:
      desc.m_Generator = Generator::CoverPointsNear;
      desc.m_vCenter = vQuerier;
      addScorer(plAiTacticalScorerDesc::Type::UnclaimedOnly, true, 0, 0, 1);
      addScorer(plAiTacticalScorerDesc::Type::CoverQualityMin, true, static_cast<float>(minQuality), 0, 1);
      // deliberately NO CoverFacingThreat: hiding behind a wall that faces away still hides.
      // Its absence also keeps the collection-time facing prefilter off (see ExecuteQueryJob),
      // so concealed-but-not-facing points (corners, far sides of blocks) survive to the LOS check.
      addScorer(plAiTacticalScorerDesc::Type::LosToThreat, true, 0 /*prefer blocked*/, 0, 1.5f);
      addScorer(plAiTacticalScorerDesc::Type::DistanceBandToQuerier, false, 0, fRadiusMax * 0.4f, 1.0f);
      break;
  }

  return desc;
}

//////////////////////////////////////////////////////////////////////////
// query execution (worker thread)

struct plAiTacticalCandidateSet
{
  struct Cand
  {
    PL_DECLARE_POD_TYPE();

    plVec3 m_vPosition = plVec3::MakeZero();
    plAiCoverPointHandle m_hCover;
    float m_fScoreSum = 0.0f;
    float m_fWeightSum = 0.0f;
    float m_fFinal = 0.0f;
    bool m_bDiscarded = false;
  };

  plHybridArray<Cand, 32> m_Candidates;
};

void plAiTacticalWorldModule::CollectCoverCandidates(const plVec3& vCenter, float fRadius, plUInt32 uiMaxCandidates, plAiTacticalCandidateSet& ref_candidates, const plVec3* pFacingThreat /*= nullptr*/, float fMinFacingDot /*= 0.0f*/, float fMaxZDelta /*= 2.5f*/) const
{
  if (m_pCoverNavMesh == nullptr)
    return;

  struct Found
  {
    PL_DECLARE_POD_TYPE();

    float m_fDistSqr;
    plUInt32 m_uiEntry;
    plUInt32 m_uiPoint;
  };

  plHybridArray<Found, 64> found;
  const float fRadiusSqr = plMath::Square(fRadius);

  for (plUInt32 uiEntry = 0; uiEntry < m_CoverSectors.GetCount(); ++uiEntry)
  {
    const CoverSector& entry = m_CoverSectors[uiEntry];

    if (!entry.m_bInUse || entry.m_Points.IsEmpty())
      continue;

    // authored entries hold few points and no real sector - skip the sector-bounds culling
    if (entry.m_SectorID != AuthoredCoverSectorID)
    {
      const plBoundingBox sectorBounds = m_pCoverNavMesh->GetSectorBounds(m_pCoverNavMesh->CalculateSectorCoord(entry.m_SectorID));
      const plVec2 vClamped(plMath::Clamp(vCenter.x, sectorBounds.m_vMin.x, sectorBounds.m_vMax.x), plMath::Clamp(vCenter.y, sectorBounds.m_vMin.y, sectorBounds.m_vMax.y));

      if ((vClamped - vCenter.GetAsVec2()).GetLengthSquared() > fRadiusSqr)
        continue;
    }

    for (plUInt32 uiPoint = 0; uiPoint < entry.m_Points.GetCount(); ++uiPoint)
    {
      const plAiCoverPoint& point = entry.m_Points[uiPoint];

      if (fMaxZDelta > 0.0f && plMath::Abs(point.m_vPosition.z - vCenter.z) > fMaxZDelta)
        continue; // different floor

      const float fDistSqrXY = (point.m_vPosition.GetAsVec2() - vCenter.GetAsVec2()).GetLengthSquared();

      if (fDistSqrXY > fRadiusSqr)
        continue;

      // pre-filter by wall facing, so wrong-side points don't consume candidate slots:
      // a point only protects against threats its wall is oriented towards
      if (pFacingThreat != nullptr)
      {
        plVec2 vToThreat = (*pFacingThreat - point.m_vPosition).GetAsVec2();

        if (vToThreat.NormalizeIfNotZero(plVec2(1, 0)).Succeeded() && point.m_vWallDir.GetAsVec2().Dot(vToThreat) < fMinFacingDot)
          continue;
      }

      // rank by 3D distance, so same-floor points come first in the truncated candidate set
      found.PushBack({(point.m_vPosition - vCenter).GetLengthSquared(), uiEntry, uiPoint});
    }
  }

  found.Sort([](const Found& lhs, const Found& rhs) { return lhs.m_fDistSqr < rhs.m_fDistSqr; });

  const plUInt32 uiCount = plMath::Min(uiMaxCandidates, found.GetCount());

  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    const CoverSector& entry = m_CoverSectors[found[i].m_uiEntry];

    auto& cand = ref_candidates.m_Candidates.ExpandAndGetRef();
    cand.m_vPosition = entry.m_Points[found[i].m_uiPoint].m_vPosition;
    cand.m_hCover.m_uiSectorEntry = found[i].m_uiEntry;
    cand.m_hCover.m_uiPoint = static_cast<plUInt16>(found[i].m_uiPoint);
    cand.m_hCover.m_uiBakeGeneration = entry.m_uiBakeGeneration;
  }
}

static float TrapezoidScore(float fValue, float fFullMin, float fFullMax)
{
  if (fValue >= fFullMin && fValue <= fFullMax)
    return 1.0f;

  const float fFalloff = plMath::Max(0.5f, (fFullMax - fFullMin) * 0.25f);

  if (fValue < fFullMin)
    return plMath::Max(0.0f, 1.0f - (fFullMin - fValue) / fFalloff);

  return plMath::Max(0.0f, 1.0f - (fValue - fFullMax) / fFalloff);
}

void plAiTacticalWorldModule::ExecuteQueryJob(plUInt32 uiSlotIndex, plUInt32 uiScratchIndex)
{
  QuerySlot& slot = m_Queries[uiSlotIndex];
  const plAiTacticalQueryDesc& desc = slot.m_Desc;

  Scratch& scratch = m_Scratch[uiScratchIndex];

  if (scratch.m_pInitializedFor != slot.m_pNavMesh)
  {
    if (dtStatusFailed(scratch.m_NavQuery.init(slot.m_pNavMesh->GetDetourNavMesh(), 512)))
    {
      slot.m_Result.m_Status = plAiTacticalQueryResult::Status::NoResult;
      return;
    }

    scratch.m_pInitializedFor = slot.m_pNavMesh;
  }

  plAiTacticalCandidateSet set;

  // a facing constraint is applied during collection already, so wrong-side points
  // don't occupy candidate slots or eat the raycast budget
  const plVec3* pFacingThreat = nullptr;
  float fMinFacingDot = 0.0f;

  for (const plAiTacticalScorerDesc& scorer : desc.m_Scorers)
  {
    if (scorer.m_Type == plAiTacticalScorerDesc::Type::CoverFacingThreat && desc.m_bHasThreat)
    {
      pFacingThreat = &desc.m_vThreat;
      fMinFacingDot = scorer.m_fParam0;
      break;
    }
  }

  // ---- generate ----
  switch (desc.m_Generator)
  {
    case plAiTacticalQueryDesc::Generator::CoverPointsNear:
      CollectCoverCandidates(desc.m_vCenter, desc.m_fRadiusMax, desc.m_uiCandidates, set, pFacingThreat, fMinFacingDot, desc.m_fMaxCoverZDelta);
      break;

    case plAiTacticalQueryDesc::Generator::RingAroundPosition:
    case plAiTacticalQueryDesc::Generator::NavmeshRandom:
    {
      plRandom rng;
      rng.Initialize((static_cast<plUInt64>(uiSlotIndex) + 1) * 0x9E3779B97F4A7C15ull + slot.m_Result.m_uiFrameStamp);

      const bool bRandom = desc.m_Generator == plAiTacticalQueryDesc::Generator::NavmeshRandom;
      const float fGoldenAngle = 2.39996323f;
      const float fExtents[3] = {plAiNavProjectionExtentXY, plAiNavProjectionExtentZ, plAiNavProjectionExtentXY};

      for (plUInt32 i = 0; i < desc.m_uiCandidates; ++i)
      {
        float fAngle, fRadiusT;

        if (bRandom)
        {
          fAngle = static_cast<float>(rng.DoubleZeroToOneExclusive()) * 2.0f * plMath::Pi<float>();
          fRadiusT = plMath::Sqrt(static_cast<float>(rng.DoubleZeroToOneExclusive()));
        }
        else
        {
          fAngle = i * fGoldenAngle;
          fRadiusT = plMath::Sqrt((i + 0.5f) / desc.m_uiCandidates);
        }

        const float fRadius = plMath::Lerp(desc.m_fRadiusMin, desc.m_fRadiusMax, fRadiusT);
        const plVec3 vSample = desc.m_vCenter + plVec3(plMath::Cos(plAngle::MakeFromRadian(fAngle)), plMath::Sin(plAngle::MakeFromRadian(fAngle)), 0) * fRadius;

        dtPolyRef polyRef = 0;
        plRcPos nearest;

        if (dtStatusFailed(scratch.m_NavQuery.findNearestPoly(plRcPos(vSample), fExtents, slot.m_pFilter, &polyRef, nearest)) || polyRef == 0)
          continue;

        auto& cand = set.m_Candidates.ExpandAndGetRef();
        cand.m_vPosition = nearest;
      }

      break;
    }
  }

  if (set.m_Candidates.IsEmpty())
  {
    slot.m_Result.m_Status = plAiTacticalQueryResult::Status::NoResult;
    slot.m_CompletedAt = GetWorld()->GetClock().GetAccumulatedTime();
    return;
  }

  // ---- cheap scorers and hard filters ----
  const plAiTacticalScorerDesc* pLosScorer = nullptr;
  const plAiTacticalScorerDesc* pReachScorer = nullptr;

  for (const plAiTacticalScorerDesc& scorer : desc.m_Scorers)
  {
    if (scorer.m_Type == plAiTacticalScorerDesc::Type::LosToThreat)
    {
      pLosScorer = &scorer;
      continue; // expensive, handled best-first below
    }

    if (scorer.m_Type == plAiTacticalScorerDesc::Type::ReachableApprox)
    {
      pReachScorer = &scorer;
      continue; // expensive, handled on the survivors below
    }

    for (auto& cand : set.m_Candidates)
    {
      if (cand.m_bDiscarded)
        continue;

      float fScore = 1.0f;

      switch (scorer.m_Type)
      {
        case plAiTacticalScorerDesc::Type::CoverQualityMin:
        {
          plAiCoverPoint point;
          fScore = (cand.m_hCover.IsValid() && ResolveCover(cand.m_hCover, point) && static_cast<float>(point.m_Quality.GetValue()) >= scorer.m_fParam0) ? 1.0f : 0.0f;
          break;
        }

        case plAiTacticalScorerDesc::Type::UnclaimedOnly:
        {
          if (cand.m_hCover.IsValid())
          {
            plAiCoverPoint point;

            if (ResolveCover(cand.m_hCover, point) && point.m_uiClaimIndex != 0xFFFF)
            {
              const bool bOwnClaim = !desc.m_hClaimant.IsInvalidated() && m_Claims[point.m_uiClaimIndex].m_bInUse && m_Claims[point.m_uiClaimIndex].m_hClaimant == desc.m_hClaimant;
              fScore = bOwnClaim ? 1.0f : 0.0f;
            }
          }

          break;
        }

        case plAiTacticalScorerDesc::Type::CoverFacingThreat:
        {
          // collection usually pre-filters this; kept as a real check for ring/random candidates
          // (no cover handle = no facing = pass) and non-prefiltered paths
          if (!desc.m_bHasThreat || !cand.m_hCover.IsValid())
            break;

          plAiCoverPoint point;

          if (!ResolveCover(cand.m_hCover, point))
          {
            fScore = 0.0f;
            break;
          }

          plVec2 vToThreat = (desc.m_vThreat - cand.m_vPosition).GetAsVec2();

          if (vToThreat.NormalizeIfNotZero(plVec2(1, 0)).Succeeded())
          {
            fScore = (point.m_vWallDir.GetAsVec2().Dot(vToThreat) >= scorer.m_fParam0) ? 1.0f : 0.0f;
          }

          break;
        }

        case plAiTacticalScorerDesc::Type::DistanceBandToQuerier:
          fScore = TrapezoidScore((cand.m_vPosition - desc.m_vQuerier).GetAsVec2().GetLength(), scorer.m_fParam0, scorer.m_fParam1);
          break;

        case plAiTacticalScorerDesc::Type::DistanceBandToThreat:
          fScore = desc.m_bHasThreat ? TrapezoidScore((cand.m_vPosition - desc.m_vThreat).GetAsVec2().GetLength(), scorer.m_fParam0, scorer.m_fParam1) : 1.0f;
          break;

        case plAiTacticalScorerDesc::Type::DirectionFromThreat:
        {
          if (!desc.m_bHasThreat)
            break;

          plVec2 vBase = (desc.m_vQuerier - desc.m_vThreat).GetAsVec2();
          plVec2 vCand = (cand.m_vPosition - desc.m_vThreat).GetAsVec2();

          if (vBase.NormalizeIfNotZero(plVec2(1, 0)).Failed() | vCand.NormalizeIfNotZero(plVec2(1, 0)).Failed())
          {
            break;
          }

          const float fAngle = plMath::ACos(plMath::Clamp(vBase.Dot(vCand), -1.0f, 1.0f)).GetRadian();
          fScore = plMath::Max(0.0f, 1.0f - plMath::Abs(fAngle - scorer.m_fParam0) / plMath::Pi<float>());
          break;
        }

        default:
          break;
      }

      if (scorer.m_bHardFilter && fScore <= 0.0f)
      {
        cand.m_bDiscarded = true;
        continue;
      }

      cand.m_fScoreSum += scorer.m_fWeight * fScore;
      cand.m_fWeightSum += scorer.m_fWeight;
    }
  }

  // compact + partial score
  for (plUInt32 i = set.m_Candidates.GetCount(); i-- > 0;)
  {
    if (set.m_Candidates[i].m_bDiscarded)
    {
      set.m_Candidates.RemoveAtAndSwap(i);
    }
    else
    {
      auto& cand = set.m_Candidates[i];
      cand.m_fFinal = (cand.m_fWeightSum > 0.0f) ? (cand.m_fScoreSum / cand.m_fWeightSum) : 1.0f;
    }
  }

  set.m_Candidates.Sort([](const plAiTacticalCandidateSet::Cand& lhs, const plAiTacticalCandidateSet::Cand& rhs) { return lhs.m_fFinal > rhs.m_fFinal; });

  // ---- line of sight, best-first within the ray budget ----
  if (pLosScorer != nullptr && desc.m_bHasThreat && m_pPhysicsForQueries != nullptr && !set.m_Candidates.IsEmpty())
  {
    const plUInt32 uiBudget = static_cast<plUInt32>(plMath::Clamp<plInt32>(cvar_TacticalMaxRaycastsPerQuery, 1, 256));
    const plUInt32 uiChecked = plMath::Min(uiBudget, set.m_Candidates.GetCount());
    const plPhysicsQueryParameters params(slot.m_pNavMesh->GetConfig().m_uiCollisionLayer, plPhysicsShapeType::Static);
    const plVec3 vThreatEye = desc.m_vThreat + plVec3(0, 0, desc.m_fThreatEyeHeight);

    for (plUInt32 i = 0; i < uiChecked; ++i)
    {
      auto& cand = set.m_Candidates[i];

      // cover candidates are evaluated crouching (that is how they will be used)
      const float fEyeHeight = cand.m_hCover.IsValid() ? 0.6f : desc.m_fQuerierEyeHeight;
      const plVec3 vCandEye = cand.m_vPosition + plVec3(0, 0, fEyeHeight);

      plVec3 vDir = vCandEye - vThreatEye;
      const float fDist = vDir.GetLength();

      bool bBlocked = false;

      if (fDist > 0.01f)
      {
        vDir /= fDist;
        plPhysicsCastResult hit;
        bBlocked = m_pPhysicsForQueries->Raycast(hit, vThreatEye, vDir, fDist, params);
        m_uiStatRaycastsThisFrame.Increment();
      }

      const bool bPreferVisible = pLosScorer->m_fParam0 > 0.5f;
      const float fScore = (bBlocked != bPreferVisible) ? 1.0f : 0.0f;

      if (pLosScorer->m_bHardFilter && fScore <= 0.0f)
      {
        cand.m_bDiscarded = true; // e.g. take-cover: a point the threat can see is NOT cover
        continue;
      }

      cand.m_fScoreSum += pLosScorer->m_fWeight * fScore;
      cand.m_fWeightSum += pLosScorer->m_fWeight;
      cand.m_fFinal = cand.m_fScoreSum / cand.m_fWeightSum;
    }

    // only LOS-checked candidates that passed stay eligible
    set.m_Candidates.SetCount(uiChecked);

    for (plUInt32 i = set.m_Candidates.GetCount(); i-- > 0;)
    {
      if (set.m_Candidates[i].m_bDiscarded)
      {
        set.m_Candidates.RemoveAtAndSwap(i);
      }
    }

    set.m_Candidates.Sort([](const plAiTacticalCandidateSet::Cand& lhs, const plAiTacticalCandidateSet::Cand& rhs) { return lhs.m_fFinal > rhs.m_fFinal; });
  }

  // ---- approximate reachability on the survivors ----
  if (pReachScorer != nullptr && !set.m_Candidates.IsEmpty())
  {
    dtPolyRef startRef = 0;
    plRcPos startPos;
    const float fExtents[3] = {plAiNavProjectionExtentXY, plAiNavProjectionExtentZ, plAiNavProjectionExtentXY};

    if (!dtStatusFailed(scratch.m_NavQuery.findNearestPoly(plRcPos(desc.m_vQuerier), fExtents, slot.m_pFilter, &startRef, startPos)) && startRef != 0)
    {
      const plUInt32 uiChecked = plMath::Min<plUInt32>(desc.m_uiMaxResults * 2, set.m_Candidates.GetCount());

      for (plUInt32 i = 0; i < uiChecked; ++i)
      {
        auto& cand = set.m_Candidates[i];

        float fT = 0.0f;
        plVec3 vHitNormal;
        dtPolyRef path[32];
        int iPathCount = 0;

        if (!dtStatusFailed(scratch.m_NavQuery.raycast(startRef, startPos, plRcPos(cand.m_vPosition), slot.m_pFilter, &fT, &vHitNormal.x, path, &iPathCount, 32)))
        {
          // t == FLT_MAX means the target was reached in a straight walk; a hit means detour needed
          const float fScore = (fT > 1.0f) ? 1.0f : 0.25f;
          cand.m_fScoreSum += pReachScorer->m_fWeight * fScore;
          cand.m_fWeightSum += pReachScorer->m_fWeight;
          cand.m_fFinal = cand.m_fScoreSum / cand.m_fWeightSum;
        }
      }

      set.m_Candidates.Sort([](const plAiTacticalCandidateSet::Cand& lhs, const plAiTacticalCandidateSet::Cand& rhs) { return lhs.m_fFinal > rhs.m_fFinal; });
    }
  }

  // ---- fill the result ----
  slot.m_Result.m_TopN.Clear();

  const plUInt32 uiResults = plMath::Min<plUInt32>(desc.m_uiMaxResults, set.m_Candidates.GetCount());

  for (plUInt32 i = 0; i < uiResults; ++i)
  {
    const auto& cand = set.m_Candidates[i];

    if (cand.m_fFinal <= 0.0f)
      break;

    auto& result = slot.m_Result.m_TopN.ExpandAndGetRef();
    result.m_vPosition = cand.m_vPosition;
    result.m_fScore = cand.m_fFinal;
    result.m_CoverHandle = cand.m_hCover;
  }

  slot.m_Result.m_Status = slot.m_Result.m_TopN.IsEmpty() ? plAiTacticalQueryResult::Status::NoResult : plAiTacticalQueryResult::Status::Ready;
  slot.m_CompletedAt = GetWorld()->GetClock().GetAccumulatedTime();
}

//////////////////////////////////////////////////////////////////////////
// EQS data provider

void plAiTacticalWorldModule::CollectCoverPoints(const plVec3& vCenter, float fRadius, plUInt32 uiMaxPoints, plDynamicArray<plAiCoverPointHandle>& out_points, plDynamicArray<plVec3>& out_positions, float fMaxZDelta /*= 2.5f*/) const
{
  if (m_pCoverNavMesh == nullptr)
    return;

  struct Found
  {
    PL_DECLARE_POD_TYPE();

    float m_fDistSqr;
    plUInt32 m_uiEntry;
    plUInt32 m_uiPoint;
  };

  plHybridArray<Found, 64> found;
  const float fRadiusSqr = plMath::Square(fRadius);

  for (plUInt32 uiEntry = 0; uiEntry < m_CoverSectors.GetCount(); ++uiEntry)
  {
    const CoverSector& entry = m_CoverSectors[uiEntry];

    if (!entry.m_bInUse || entry.m_Points.IsEmpty())
      continue;

    // authored entries hold few points and no real sector - skip the sector-bounds culling
    if (entry.m_SectorID != AuthoredCoverSectorID)
    {
      const plBoundingBox sectorBounds = m_pCoverNavMesh->GetSectorBounds(m_pCoverNavMesh->CalculateSectorCoord(entry.m_SectorID));
      const plVec2 vClamped(plMath::Clamp(vCenter.x, sectorBounds.m_vMin.x, sectorBounds.m_vMax.x), plMath::Clamp(vCenter.y, sectorBounds.m_vMin.y, sectorBounds.m_vMax.y));

      if ((vClamped - vCenter.GetAsVec2()).GetLengthSquared() > fRadiusSqr)
        continue;
    }

    for (plUInt32 uiPoint = 0; uiPoint < entry.m_Points.GetCount(); ++uiPoint)
    {
      const plAiCoverPoint& point = entry.m_Points[uiPoint];

      if (fMaxZDelta > 0.0f && plMath::Abs(point.m_vPosition.z - vCenter.z) > fMaxZDelta)
        continue; // different floor

      if ((point.m_vPosition.GetAsVec2() - vCenter.GetAsVec2()).GetLengthSquared() <= fRadiusSqr)
      {
        // rank by 3D distance, so same-floor points come first in the truncated set
        found.PushBack({(point.m_vPosition - vCenter).GetLengthSquared(), uiEntry, uiPoint});
      }
    }
  }

  found.Sort([](const Found& lhs, const Found& rhs) { return lhs.m_fDistSqr < rhs.m_fDistSqr; });

  const plUInt32 uiCount = plMath::Min(uiMaxPoints, found.GetCount());

  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    const CoverSector& entry = m_CoverSectors[found[i].m_uiEntry];

    auto& hPoint = out_points.ExpandAndGetRef();
    hPoint.m_uiSectorEntry = found[i].m_uiEntry;
    hPoint.m_uiPoint = static_cast<plUInt16>(found[i].m_uiPoint);
    hPoint.m_uiBakeGeneration = entry.m_uiBakeGeneration;

    out_positions.PushBack(entry.m_Points[found[i].m_uiPoint].m_vPosition);
  }
}

bool plAiTacticalWorldModule::IsCoverClaimedByOther(const plAiCoverPointHandle& hPoint, plGameObjectHandle hSelf) const
{
  plAiCoverPoint point;

  if (!ResolveCover(hPoint, point))
    return false; // stale handles are handled elsewhere; not a claim conflict

  if (point.m_uiClaimIndex == 0xFFFF)
    return false;

  const Claim& claim = m_Claims[point.m_uiClaimIndex];
  return claim.m_bInUse && claim.m_hClaimant != hSelf;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Implementation_TacticalQuery);
