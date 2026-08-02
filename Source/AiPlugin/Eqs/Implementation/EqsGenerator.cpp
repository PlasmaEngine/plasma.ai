#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/EqsGenerator.h>
#include <AiPlugin/Eqs/EqsQueryResource.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <AiPlugin/Utils/RcMath.h>
#include <DetourNavMeshQuery.h>
#include <Foundation/Math/Random.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsGenerator, 1, plRTTINoAllocator)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("CenterContext", GetCenterContext, SetCenterContext)->AddAttributes(new plDefaultValueAttribute(plStringView("Querier"))),
    PL_MEMBER_PROPERTY("RadiusMin", m_fRadiusMin)->AddAttributes(new plDefaultValueAttribute(2.0f), new plClampValueAttribute(0.0f, 200.0f)),
    PL_MEMBER_PROPERTY("RadiusMax", m_fRadiusMax)->AddAttributes(new plDefaultValueAttribute(12.0f), new plClampValueAttribute(0.5f, 500.0f)),
    PL_MEMBER_PROPERTY("VerticalRange", m_fVerticalRange)->AddAttributes(new plDefaultValueAttribute(3.0f), new plClampValueAttribute(0.25f, 100.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsGenerator_Ring, 1, plRTTIDefaultAllocator<plAiEqsGenerator_Ring>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsGenerator_Grid, 1, plRTTIDefaultAllocator<plAiEqsGenerator_Grid>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Spacing", m_fSpacing)->AddAttributes(new plDefaultValueAttribute(1.5f), new plClampValueAttribute(0.25f, 20.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsGenerator_NavmeshRandom, 1, plRTTIDefaultAllocator<plAiEqsGenerator_NavmeshRandom>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsGenerator_CoverPoints, 1, plRTTIDefaultAllocator<plAiEqsGenerator_CoverPoints>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsGenerator_SmartObjects, 1, plRTTIDefaultAllocator<plAiEqsGenerator_SmartObjects>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Type", GetType, SetType),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plVec3 plAiEqsGenerator::GetCenter(const plAiEqsEvalContext& ctx, bool* out_pValid /*= nullptr*/) const
{
  if (out_pValid != nullptr)
  {
    *out_pValid = true;
  }

  if (!m_sCenterContext.IsEmpty())
  {
    if (const plAiEqsResolvedSlot* pSlot = ctx.FindSlot(plTempHashedString(m_sCenterContext.GetData())))
    {
      if (!pSlot->m_Positions.IsEmpty())
        return pSlot->m_Positions[0];

      if (out_pValid != nullptr)
      {
        *out_pValid = false;
      }
    }
  }

  return ctx.m_vQuerier;
}

namespace
{
  /// Projects a sample onto the navmesh; returns false when no polygon is within fVerticalRange.
  bool ProjectToNavmesh(plAiEqsEvalContext& ctx, const plVec3& vSample, float fVerticalRange, plVec3& out_vProjected)
  {
    if (ctx.m_pNavQuery == nullptr)
      return false;

    const float fExtents[3] = {plAiNavProjectionExtentXY, fVerticalRange, plAiNavProjectionExtentXY};
    dtPolyRef polyRef = 0;
    plRcPos nearest;

    if (dtStatusFailed(ctx.m_pNavQuery->findNearestPoly(plRcPos(vSample), fExtents, ctx.m_pFilter, &polyRef, nearest)) || polyRef == 0)
      return false;

    out_vProjected = nearest;
    return true;
  }
} // namespace

void plAiEqsGenerator_Ring::Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const
{
  const plVec3 vCenter = GetCenter(ref_ctx);
  const float fGoldenAngle = 2.39996323f;

  for (plUInt32 i = 0; i < ref_ctx.m_uiMaxCandidates; ++i)
  {
    const float fAngle = i * fGoldenAngle;
    const float fRadiusT = plMath::Sqrt((i + 0.5f) / ref_ctx.m_uiMaxCandidates);
    const float fRadius = plMath::Lerp(m_fRadiusMin, m_fRadiusMax, fRadiusT);
    const plVec3 vSample = vCenter + plVec3(plMath::Cos(plAngle::MakeFromRadian(fAngle)), plMath::Sin(plAngle::MakeFromRadian(fAngle)), 0) * fRadius;

    plVec3 vProjected;

    if (ProjectToNavmesh(ref_ctx, vSample, m_fVerticalRange, vProjected))
    {
      auto& item = out_items.ExpandAndGetRef();
      item.m_vPosition = vProjected;
    }
  }
}

void plAiEqsGenerator_Grid::Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const
{
  const plVec3 vCenter = GetCenter(ref_ctx);

  // grow the spacing when the extent needs more cells than the candidate budget allows
  float fSpacing = plMath::Max(0.25f, m_fSpacing);
  const float fExtent = plMath::Max(fSpacing, m_fRadiusMax);

  {
    const plUInt32 uiPerAxis = static_cast<plUInt32>(plMath::Floor(plMath::Sqrt(static_cast<float>(ref_ctx.m_uiMaxCandidates))));
    const float fMinSpacing = (2.0f * fExtent) / plMath::Max(1u, uiPerAxis - 1);
    fSpacing = plMath::Max(fSpacing, fMinSpacing);
  }

  const float fRadiusMinSqr = plMath::Square(m_fRadiusMin);
  const float fRadiusMaxSqr = plMath::Square(fExtent);

  for (float y = -fExtent; y <= fExtent + 0.01f; y += fSpacing)
  {
    for (float x = -fExtent; x <= fExtent + 0.01f; x += fSpacing)
    {
      if (out_items.GetCount() >= ref_ctx.m_uiMaxCandidates)
        return;

      const float fDistSqr = x * x + y * y;

      if (fDistSqr < fRadiusMinSqr || fDistSqr > fRadiusMaxSqr)
        continue;

      plVec3 vProjected;

      if (ProjectToNavmesh(ref_ctx, vCenter + plVec3(x, y, 0), m_fVerticalRange, vProjected))
      {
        auto& item = out_items.ExpandAndGetRef();
        item.m_vPosition = vProjected;
      }
    }
  }
}

void plAiEqsGenerator_NavmeshRandom::Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const
{
  const plVec3 vCenter = GetCenter(ref_ctx);

  plRandom rng;
  rng.Initialize((static_cast<plUInt64>(ref_ctx.m_uiRngSeed) + 1) * 0x9E3779B97F4A7C15ull);

  for (plUInt32 i = 0; i < ref_ctx.m_uiMaxCandidates; ++i)
  {
    const float fAngle = static_cast<float>(rng.DoubleZeroToOneExclusive()) * 2.0f * plMath::Pi<float>();
    const float fRadiusT = plMath::Sqrt(static_cast<float>(rng.DoubleZeroToOneExclusive()));
    const float fRadius = plMath::Lerp(m_fRadiusMin, m_fRadiusMax, fRadiusT);
    const plVec3 vSample = vCenter + plVec3(plMath::Cos(plAngle::MakeFromRadian(fAngle)), plMath::Sin(plAngle::MakeFromRadian(fAngle)), 0) * fRadius;

    plVec3 vProjected;

    if (ProjectToNavmesh(ref_ctx, vSample, m_fVerticalRange, vProjected))
    {
      auto& item = out_items.ExpandAndGetRef();
      item.m_vPosition = vProjected;
    }
  }
}

void plAiEqsGenerator_CoverPoints::Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const
{
  if (ref_ctx.m_pTactical == nullptr)
    return;

  const plVec3 vCenter = GetCenter(ref_ctx);

  plHybridArray<plAiCoverPointHandle, 64> points;
  plHybridArray<plVec3, 64> positions;
  ref_ctx.m_pTactical->CollectCoverPoints(vCenter, m_fRadiusMax, ref_ctx.m_uiMaxCandidates, points, positions, m_fVerticalRange);

  for (plUInt32 i = 0; i < points.GetCount(); ++i)
  {
    auto& item = out_items.ExpandAndGetRef();
    item.m_vPosition = positions[i];
    item.m_hCover = points[i];
    item.m_Payload = plAiEqsPayloadType::CoverPoint;
  }
}

void plAiEqsGenerator_SmartObjects::Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const
{
  // slots were pre-collected on the main thread at submit time
  for (const plAiEqsItem& item : ref_ctx.m_PreCollectedItems)
  {
    if (out_items.GetCount() >= ref_ctx.m_uiMaxCandidates)
      return;

    out_items.PushBack(item);
  }
}

void plAiEqsGenerator_SmartObjects::CollectOnMainThread(const plVec3& vCenter, plGameObjectHandle hClaimant, const plAiTacticalWorldModule& tactical, plDynamicArray<plAiEqsItem>& out_items) const
{
  plHybridArray<plAiSmartObjectHandle, 32> slots;
  plHybridArray<plVec3, 32> positions;

  tactical.CollectFreeSmartObjectSlots(vCenter, m_fRadiusMax, plTempHashedString(m_sType.GetData()), hClaimant, 64, slots, positions);

  for (plUInt32 i = 0; i < slots.GetCount(); ++i)
  {
    auto& item = out_items.ExpandAndGetRef();
    item.m_vPosition = positions[i];
    item.m_hSmartObject = slots[i];
    item.m_Payload = plAiEqsPayloadType::SmartObjectSlot;
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Eqs_Implementation_EqsGenerator);
