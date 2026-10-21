#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEditorHelpers.h>
#include <EditorPluginAi/Assets/AiEqsPreviewSimulation.h>
#include <EditorPluginAi/Assets/AiEqsQueryAsset.h>

#include <AiPlugin/Eqs/EqsContext.h>
#include <AiPlugin/Eqs/EqsGenerator.h>
#include <AiPlugin/Eqs/EqsTest.h>

#include <Foundation/Math/Random.h>

namespace
{
  struct CurvePresetPoint
  {
    double x;
    double v;
  };

  struct CurvePreset
  {
    const char* m_szName;
    bool m_bSmooth;
    plUInt32 m_uiCount;
    CurvePresetPoint m_Points[4];
  };

  static const CurvePreset s_CurvePresets[] = {
    {"Linear Rise", false, 2, {{0.0, 0.0}, {1.0, 1.0}}},
    {"Linear Fall", false, 2, {{0.0, 1.0}, {1.0, 0.0}}},
    {"Smooth Rise", true, 2, {{0.0, 0.0}, {1.0, 1.0}}},
    {"Smooth Fall", true, 2, {{0.0, 1.0}, {1.0, 0.0}}},
    {"Square (emphasize best)", true, 3, {{0.0, 0.0}, {0.5, 0.25}, {1.0, 1.0}}},
    {"Square Root (lenient)", true, 3, {{0.0, 0.0}, {0.5, 0.7}, {1.0, 1.0}}},
    {"Bell", true, 3, {{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}}},
    {"Constant 1", false, 2, {{0.0, 1.0}, {1.0, 1.0}}},
    {"Clear (use raw score)", false, 0, {}},
  };

  bool SegmentsIntersect(const plVec2& p1, const plVec2& p2, const plVec2& q1, const plVec2& q2)
  {
    auto cross = [](const plVec2& a, const plVec2& b)
    { return a.x * b.y - a.y * b.x; };

    const plVec2 r = p2 - p1;
    const plVec2 s = q2 - q1;
    const float rxs = cross(r, s);

    if (plMath::Abs(rxs) < 0.00001f)
      return false; // parallel

    const float t = cross(q1 - p1, s) / rxs;
    const float u = cross(q1 - p1, r) / rxs;

    return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
  }

  /// 1 inside the band, falling off linearly over a quarter of its width (at least 0.5 m) outside it.
  float PreviewTrapezoid(float fValue, float fFullMin, float fFullMax)
  {
    if (fValue >= fFullMin && fValue <= fFullMax)
      return 1.0f;

    const float fFalloff = plMath::Max(0.5f, (fFullMax - fFullMin) * 0.25f);

    if (fValue < fFullMin)
      return plMath::Max(0.0f, 1.0f - (fFullMin - fValue) / fFalloff);

    return plMath::Max(0.0f, 1.0f - (fValue - fFullMax) / fFalloff);
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plAiEqsUi

namespace plAiEqsUi
{
  void GetPrettyName(const plRTTI* pRtti, plStringBuilder& out_sName)
  {
    out_sName.Clear();

    if (pRtti == nullptr)
    {
      out_sName = "<none>";
      return;
    }

    plStringBuilder sName = pRtti->GetTypeName();
    sName.TrimWordStart("plAiEqsTest_");
    sName.TrimWordStart("plAiEqsGenerator_");
    sName.TrimWordStart("plAiEqsContext_");

    // a space before each capital that follows a lower-case letter
    bool bPreviousUpper = true;
    for (auto it = sName.GetIteratorFront(); it.IsValid(); ++it)
    {
      const plUInt32 uiChar = it.GetCharacter();
      const bool bUpper = uiChar >= 'A' && uiChar <= 'Z';

      if (bUpper && !bPreviousUpper)
        out_sName.Append(' ');

      out_sName.Append(uiChar);
      bPreviousUpper = bUpper;
    }
  }

  const char* GetTestGroup(const plRTTI* pRtti)
  {
    const plStringView sName = pRtti->GetTypeName();

    if (sName.FindSubString("Distance") || sName.FindSubString("Direction"))
      return "Distance & Direction";
    if (sName.FindSubString("Cover") || sName.FindSubString("Unclaimed"))
      return "Cover";
    if (sName.FindSubString("LineOfSight"))
      return "Visibility";
    if (sName.FindSubString("Reachable") || sName.FindSubString("PathLength"))
      return "Pathfinding";
    if (sName.FindSubString("Blackboard"))
      return "Blackboard";
    return "Other";
  }

  void GetTestSummary(const plAiEqsTest* pTest, plStringBuilder& out_sSummary)
  {
    out_sSummary.Clear();

    if (pTest == nullptr)
    {
      out_sSummary = "select a test type";
      return;
    }

    if (auto pDist = plDynamicCast<const plAiEqsTest_Distance*>(pTest))
    {
      out_sSummary.SetFormat("band {} \xE2\x86\x92 {} m \xC2\xB7 vs {}", plArgF(pDist->m_fBandMin, 1), plArgF(pDist->m_fBandMax, 1), pDist->GetContext());
    }
    else if (auto pDir = plDynamicCast<const plAiEqsTest_Direction*>(pTest))
    {
      out_sSummary.SetFormat("desired angle {}\xC2\xB0 around {}", plArgF(pDir->m_DesiredAngle.GetDegree(), 0), pDir->GetContext());
    }
    else if (auto pQuality = plDynamicCast<const plAiEqsTest_CoverQuality*>(pTest))
    {
      const char* szQuality = pQuality->m_MinQuality == plAiCoverQuality::High ? "High" : (pQuality->m_MinQuality == plAiCoverQuality::Low ? "Low" : "None");
      out_sSummary.SetFormat("min quality: {} \xC2\xB7 scored by tier", szQuality);
    }
    else if (auto pFacing = plDynamicCast<const plAiEqsTest_CoverFacing*>(pTest))
    {
      out_sSummary.SetFormat("wall must face context \xC2\xB7 min dot {}", plArgF(pFacing->m_fMinDot, 2));
    }
    else if (plDynamicCast<const plAiEqsTest_Unclaimed*>(pTest) != nullptr)
    {
      out_sSummary = "cover/slot payload not claimed by others";
    }
    else if (auto pLos = plDynamicCast<const plAiEqsTest_LineOfSight*>(pTest))
    {
      const char* szCombine = pLos->m_Combine == plAiEqsContextCombine::Max ? "Max" : (pLos->m_Combine == plAiEqsContextCombine::Average ? "Average" : "Min");
      out_sSummary.SetFormat("prefer {} \xC2\xB7 eye heights {} / {} \xC2\xB7 combine: {}", pLos->m_bPreferVisible ? "VISIBLE" : "BLOCKED", plArgF(pLos->m_fItemEyeHeight, 2), plArgF(pLos->m_fContextEyeHeight, 2), szCombine);
    }
    else if (plDynamicCast<const plAiEqsTest_ReachableApprox*>(pTest) != nullptr)
    {
      out_sSummary = "navmesh raycast from the querier \xC2\xB7 detour scores 0.25";
    }
    else if (auto pPath = plDynamicCast<const plAiEqsTest_PathLength*>(pTest))
    {
      out_sSummary.SetFormat("path length band {} \xE2\x86\x92 {} m \xC2\xB7 unreachable fails", plArgF(pPath->m_fBandMin, 1), plArgF(pPath->m_fBandMax, 1));
    }
  }

  void GetFilterRule(const plAiEqsTest* pTest, plStringBuilder& out_sRule)
  {
    switch (pTest->m_FilterCondition.GetValue())
    {
      case plAiEqsFilterCondition::IsTrue:
        out_sRule = "true";
        break;
      case plAiEqsFilterCondition::AtLeast:
        out_sRule.SetFormat("value >= {}", pTest->m_fFilterMin);
        break;
      case plAiEqsFilterCondition::AtMost:
        out_sRule.SetFormat("value <= {}", pTest->m_fFilterMax);
        break;
      case plAiEqsFilterCondition::Between:
        out_sRule.SetFormat("value in [{}, {}]", pTest->m_fFilterMin, pTest->m_fFilterMax);
        break;
      case plAiEqsFilterCondition::Reachable:
        out_sRule = "reachable";
        break;
      case plAiEqsFilterCondition::DirectPath:
        out_sRule = "direct path";
        break;
      default:
        out_sRule = "raw score > 0";
        break;
    }
  }

  void GetGeneratorSummary(const plAiEqsGenerator* pGenerator, plStringBuilder& out_sSummary)
  {
    out_sSummary.Clear();

    if (pGenerator == nullptr)
    {
      out_sSummary = "choose a generator - it produces the candidate set";
      return;
    }

    const char* szCenter = pGenerator->GetCenterContext();

    if (plDynamicCast<const plAiEqsGenerator_Ring*>(pGenerator) != nullptr)
    {
      out_sSummary.SetFormat("golden-angle rings {} \xE2\x86\x92 {} m around {}, navmesh-projected", plArgF(pGenerator->m_fRadiusMin, 1), plArgF(pGenerator->m_fRadiusMax, 1), szCenter);
    }
    else if (auto pGrid = plDynamicCast<const plAiEqsGenerator_Grid*>(pGenerator))
    {
      out_sSummary.SetFormat("grid, spacing {} m, extent {} m around {}, navmesh-projected", plArgF(pGrid->m_fSpacing, 1), plArgF(pGenerator->m_fRadiusMax, 1), szCenter);
    }
    else if (plDynamicCast<const plAiEqsGenerator_NavmeshRandom*>(pGenerator) != nullptr)
    {
      out_sSummary.SetFormat("random reachable points {} \xE2\x86\x92 {} m around {}", plArgF(pGenerator->m_fRadiusMin, 1), plArgF(pGenerator->m_fRadiusMax, 1), szCenter);
    }
    else if (plDynamicCast<const plAiEqsGenerator_CoverPoints*>(pGenerator) != nullptr)
    {
      out_sSummary.SetFormat("baked cover points within {} m of {} \xC2\xB7 AreaNotReady while sectors stream", plArgF(pGenerator->m_fRadiusMax, 1), szCenter);
    }
    else if (auto pSmartObjects = plDynamicCast<const plAiEqsGenerator_SmartObjects*>(pGenerator))
    {
      const char* szType = plStringUtils::IsNullOrEmpty(pSmartObjects->GetType()) ? "any" : pSmartObjects->GetType();
      out_sSummary.SetFormat("free '{}' smart object slots within {} m of {}", szType, plArgF(pGenerator->m_fRadiusMax, 1), szCenter);
    }
  }

  const char* GetTestContext(const plAiEqsTest* pTest)
  {
    if (pTest == nullptr)
      return nullptr;

    const char* szContext = pTest->GetContextProperty();
    return plStringUtils::IsNullOrEmpty(szContext) ? nullptr : szContext;
  }

  const char* GetPayloadLabel(const plAiEqsGenerator* pGenerator)
  {
    if (pGenerator == nullptr)
      return "";

    switch (pGenerator->GetPayloadType())
    {
      case plAiEqsPayloadType::CoverPoint:
        return "PAYLOAD: COVER";
      case plAiEqsPayloadType::SmartObjectSlot:
        return "PAYLOAD: SMART OBJECT";
      case plAiEqsPayloadType::GameObject:
        return "PAYLOAD: OBJECT";
      default:
        return "";
    }
  }

  const char* GetCostLabel(const plAiEqsTest* pTest)
  {
    if (pTest->GetCost() != plAiEqsTest::Cost::Expensive)
      return "CHEAP";

    const bool bNavmesh = plDynamicCast<const plAiEqsTest_ReachableApprox*>(pTest) != nullptr || plDynamicCast<const plAiEqsTest_PathLength*>(pTest) != nullptr;
    return bNavmesh ? "NAVMESH" : "RAYCAST";
  }

  const char* GetPurposeLabel(const plAiEqsTest* pTest)
  {
    switch (pTest->m_Purpose.GetValue())
    {
      case plAiEqsTestPurpose::FilterOnly:
        return "FILTER";
      case plAiEqsTestPurpose::ScoreOnly:
        return "SCORE";
      default:
        return "FILTER + SCORE";
    }
  }

  plUInt32 GetCurvePresetCount()
  {
    return PL_ARRAY_SIZE(s_CurvePresets);
  }

  const char* GetCurvePresetName(plUInt32 uiPreset)
  {
    return uiPreset < PL_ARRAY_SIZE(s_CurvePresets) ? s_CurvePresets[uiPreset].m_szName : "";
  }

  plUInt32 GetCurvePresetPoints(plUInt32 uiPreset, plVec2d* out_pPoints, plUInt32 uiMaxPoints, bool& out_bSmooth)
  {
    if (uiPreset >= PL_ARRAY_SIZE(s_CurvePresets))
      return 0;

    const CurvePreset& preset = s_CurvePresets[uiPreset];
    out_bSmooth = preset.m_bSmooth;

    const plUInt32 uiCount = plMath::Min(preset.m_uiCount, uiMaxPoints);
    for (plUInt32 i = 0; i < uiCount; ++i)
      out_pPoints[i] = plVec2d(preset.m_Points[i].x, preset.m_Points[i].v);

    return uiCount;
  }

  const char* GetTraceState(const plAiEqsTestTrace& trace)
  {
    if (!trace.m_bEvaluated)
      return "not run";
    if (trace.m_bSkipped)
      return "skipped";
    return trace.m_bPassed ? "pass" : "rejected";
  }

  void GetTraceDetail(const plAiEqsTestTrace& trace, plStringBuilder& out_sDetail)
  {
    out_sDetail.SetFormat("{}: {}\nMeasurement: {}\nRaw score: {}\nCurve/inversion: {}\nWeighted contribution: {}", GetTraceState(trace), plAiEqsTestDataName(trace.m_Data), trace.m_fMeasurement, trace.m_fRaw, trace.m_fCurved, trace.m_fContribution);
  }
} // namespace plAiEqsUi

//////////////////////////////////////////////////////////////////////////
// plAiEqsPreviewSimulation

void plAiEqsPreviewSimulation::Run(const plAiEqsQueryAssetObject& props)
{
  m_Candidates.Clear();
  m_iWinner = -1;
  m_uiGenerated = 0;
  m_uiSurvivors = 0;
  m_uiResults = 0;
  m_sStageCounts.Clear();
  m_sRootIssue = props.ValidateRoot();

  if (!m_sRootIssue.IsEmpty())
    return;

  Generate(props);
  RunTests(props);
  Finalize(props);
}

void plAiEqsPreviewSimulation::GetStats(plStringBuilder& out_sText) const
{
  if (!m_sRootIssue.IsEmpty())
  {
    out_sText = m_sRootIssue;
    return;
  }

  out_sText.SetFormat("generated {} \xC2\xB7 after filters {} \xC2\xB7 results {}\nlocal simulation \xC2\xB7 LOS via draggable occluder \xC2\xB7 navmesh tests approximated\n{}", m_uiGenerated, m_uiSurvivors, m_uiResults, m_sStageCounts);
}

plVec2 plAiEqsPreviewSimulation::ResolveContext(plStringView sName) const
{
  // any authored slot resolves to the threat marker in the local simulation
  return sName.IsEqual_NoCase("Querier") ? m_vQuerier : m_vThreat;
}

void plAiEqsPreviewSimulation::Generate(const plAiEqsQueryAssetObject& props)
{
  const plAiEqsGenerator* pGen = props.m_pGenerator;
  if (pGen == nullptr)
    return;

  const plUInt32 uiMaxCandidates = plMath::Clamp<plUInt32>(props.m_uiCandidates, 4, 64);

  // seeded: the candidates stay put while the situation changes
  plRandom rng;
  rng.Initialize(0xEC5D47u);

  const plVec2 vCenter = ResolveContext(pGen->GetCenterContext());
  const float fRadiusMin = pGen->m_fRadiusMin;
  const float fRadiusMax = plMath::Max(pGen->m_fRadiusMax, fRadiusMin + 0.1f);

  const bool bCover = plDynamicCast<const plAiEqsGenerator_CoverPoints*>(pGen) != nullptr;
  const bool bSmartObjects = plDynamicCast<const plAiEqsGenerator_SmartObjects*>(pGen) != nullptr;
  const bool bRandom = plDynamicCast<const plAiEqsGenerator_NavmeshRandom*>(pGen) != nullptr;

  if (auto pGrid = plDynamicCast<const plAiEqsGenerator_Grid*>(pGen))
  {
    float fSpacing = plMath::Max(0.25f, pGrid->m_fSpacing);

    const plUInt32 uiPerAxis = static_cast<plUInt32>(plMath::Floor(plMath::Sqrt(static_cast<float>(uiMaxCandidates))));
    fSpacing = plMath::Max(fSpacing, (2.0f * fRadiusMax) / plMath::Max(1u, uiPerAxis - 1));

    for (float y = -fRadiusMax; y <= fRadiusMax + 0.01f && m_Candidates.GetCount() < uiMaxCandidates; y += fSpacing)
    {
      for (float x = -fRadiusMax; x <= fRadiusMax + 0.01f && m_Candidates.GetCount() < uiMaxCandidates; x += fSpacing)
      {
        const float fDistSqr = x * x + y * y;

        if (fDistSqr < plMath::Square(fRadiusMin) || fDistSqr > plMath::Square(fRadiusMax))
          continue;

        m_Candidates.ExpandAndGetRef().m_vPosition = vCenter + plVec2(x, y);
      }
    }

    return;
  }

  const plUInt32 uiCount = bSmartObjects ? plMath::Min<plUInt32>(6, uiMaxCandidates) : uiMaxCandidates;
  const float fGoldenAngle = 2.39996323f;

  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    float fAngle, fRadiusT;

    if (bRandom || bSmartObjects)
    {
      fAngle = static_cast<float>(rng.DoubleZeroToOneExclusive()) * 2.0f * plMath::Pi<float>();
      fRadiusT = plMath::Sqrt(static_cast<float>(rng.DoubleZeroToOneExclusive()));
    }
    else
    {
      fAngle = i * fGoldenAngle;
      fRadiusT = plMath::Sqrt((i + 0.5f) / uiCount);
    }

    const float fRadius = plMath::Lerp(fRadiusMin, fRadiusMax, fRadiusT);

    auto& cand = m_Candidates.ExpandAndGetRef();
    cand.m_vPosition = vCenter + plVec2(plMath::Cos(plAngle::MakeFromRadian(fAngle)), plMath::Sin(plAngle::MakeFromRadian(fAngle))) * fRadius;

    if (bCover)
    {
      cand.m_uiPayload = 1; // CoverPoint

      // synthetic wall: points away from the generator center, jittered
      plVec2 vWall = cand.m_vPosition - vCenter;
      vWall.NormalizeIfNotZero(plVec2(1, 0)).IgnoreResult();
      const plAngle jitter = plAngle::MakeFromRadian((static_cast<float>(rng.DoubleZeroToOneExclusive()) - 0.5f) * 2.5f);
      const float fSin = plMath::Sin(jitter);
      const float fCos = plMath::Cos(jitter);
      cand.m_vWallDir = plVec2(vWall.x * fCos - vWall.y * fSin, vWall.x * fSin + vWall.y * fCos);
      cand.m_uiCoverQuality = (rng.DoubleZeroToOneExclusive() < 0.4) ? 2 : 1; // High : Low
    }
    else if (bSmartObjects)
    {
      cand.m_uiPayload = 2; // SmartObjectSlot
    }
  }
}

void plAiEqsPreviewSimulation::RunTests(const plAiEqsQueryAssetObject& props)
{
  struct OrderedTest
  {
    PL_DECLARE_POD_TYPE();

    const plAiEqsTestObject* m_pObj;
    plUInt32 m_uiRowIndex;
  };

  // cheap tests first, as the runtime pipeline orders them; rows keep the authored order
  plHybridArray<OrderedTest, 12> order;
  {
    plHybridArray<OrderedTest, 12> all;
    plUInt32 uiRow = 0;

    for (const plAiEqsTestObject* pTestObj : props.m_Tests)
    {
      if (pTestObj == nullptr || pTestObj->m_pTest == nullptr)
        continue;

      all.PushBack({pTestObj, uiRow});
      ++uiRow;
    }

    for (const auto& t : all)
    {
      if (t.m_pObj->m_pTest->GetCost() == plAiEqsTest::Cost::Cheap)
        order.PushBack(t);
    }

    for (const auto& t : all)
    {
      if (t.m_pObj->m_pTest->GetCost() == plAiEqsTest::Cost::Expensive)
        order.PushBack(t);
    }
  }

  m_uiGenerated = m_Candidates.GetCount();

  for (auto& cand : m_Candidates)
  {
    cand.m_TestScores.SetCount(props.m_Tests.GetCount(), 0.0f);
    cand.m_TestTrace.SetCount(props.m_Tests.GetCount());
  }

  m_ScoreSums.Clear();
  m_WeightSums.Clear();
  m_ScoreSums.SetCount(m_Candidates.GetCount(), 0.0f);
  m_WeightSums.SetCount(m_Candidates.GetCount(), 0.0f);

  plStringBuilder sStages;
  sStages.SetFormat("{} generated", m_Candidates.GetCount());

  bool bQueryFailed = false;
  for (const OrderedTest& ordered : order)
  {
    const plAiEqsTest* pTest = ordered.m_pObj->m_pTest;

    plAiEqsTestDesc processor;
    plAiAssetUi::BuildRuntimeCurve(ordered.m_pObj->m_ScoreCurve, processor.m_ScoreCurve);
    processor.m_bLegacyCurveDomain = ordered.m_pObj->m_bLegacyCurveDomain;

    for (plUInt32 c = 0; c < m_Candidates.GetCount(); ++c)
    {
      plAiEqsPreviewCandidate& cand = m_Candidates[c];

      if (cand.m_bDiscarded)
        continue;

      float fRaw = 1.0f;
      plAiEqsItem item;

      if (auto pDist = plDynamicCast<const plAiEqsTest_Distance*>(pTest))
      {
        item.m_fMeasurement = (cand.m_vPosition - ResolveContext(pDist->GetContext())).GetLength();
        item.m_bHasMeasurement = true;
        fRaw = PreviewTrapezoid(item.m_fMeasurement, pDist->m_fBandMin, pDist->m_fBandMax);
      }
      else if (auto pDir = plDynamicCast<const plAiEqsTest_Direction*>(pTest))
      {
        const plVec2 vFrom = ResolveContext(pDir->GetContext());
        plVec2 vBase = m_vQuerier - vFrom;
        plVec2 vCand = cand.m_vPosition - vFrom;

        if (vBase.NormalizeIfNotZero(plVec2(1, 0)).Succeeded() && vCand.NormalizeIfNotZero(plVec2(1, 0)).Succeeded())
        {
          const float fAngle = plMath::ACos(plMath::Clamp(vBase.Dot(vCand), -1.0f, 1.0f)).GetRadian();
          item.m_fMeasurement = plAngle::MakeFromRadian(fAngle).GetDegree();
          item.m_bHasMeasurement = true;
          fRaw = plMath::Max(0.0f, 1.0f - plMath::Abs(fAngle - pDir->m_DesiredAngle.GetRadian()) / plMath::Pi<float>());
        }
        else
          item.m_TestData = plAiEqsTestData::InvalidMeasurement;
      }
      else if (auto pQuality = plDynamicCast<const plAiEqsTest_CoverQuality*>(pTest))
      {
        if (cand.m_uiPayload == 1)
        {
          item.m_fMeasurement = cand.m_uiCoverQuality >= 2 ? 1.0f : 0.5f;
          item.m_bHasMeasurement = true;
          if (cand.m_uiCoverQuality < pQuality->m_MinQuality.GetValue())
            fRaw = 0.0f;
          else
            fRaw = (cand.m_uiCoverQuality >= 2) ? 1.0f : 0.5f;
        }
        else
          item.m_TestData = plAiEqsTestData::MissingPayload;
      }
      else if (auto pFacing = plDynamicCast<const plAiEqsTest_CoverFacing*>(pTest))
      {
        if (cand.m_uiPayload == 1)
        {
          plVec2 vToThreat = ResolveContext(pFacing->GetContext()) - cand.m_vPosition;

          if (vToThreat.NormalizeIfNotZero(plVec2(1, 0)).Succeeded())
          {
            item.m_fMeasurement = cand.m_vWallDir.Dot(vToThreat);
            item.m_bHasMeasurement = true;
            fRaw = (item.m_fMeasurement >= pFacing->m_fMinDot) ? 1.0f : 0.0f;
          }
          else
            item.m_TestData = plAiEqsTestData::InvalidMeasurement;
        }
        else
          item.m_TestData = plAiEqsTestData::MissingPayload;
      }
      else if (plDynamicCast<const plAiEqsTest_Unclaimed*>(pTest) != nullptr)
      {
        fRaw = 1.0f; // everything is unclaimed in the local simulation
        if (cand.m_uiPayload != 1 && cand.m_uiPayload != 2)
          item.m_TestData = plAiEqsTestData::MissingPayload;
      }
      else if (auto pLos = plDynamicCast<const plAiEqsTest_LineOfSight*>(pTest))
      {
        const plVec2 vEye = ResolveContext(pLos->GetContext());
        const bool bBlocked = SegmentsIntersect(vEye, cand.m_vPosition, m_vOccluderA, m_vOccluderB);
        fRaw = (bBlocked != pLos->m_bPreferVisible) ? 1.0f : 0.0f;
      }
      else if (plDynamicCast<const plAiEqsTest_ReachableApprox*>(pTest) != nullptr)
      {
        fRaw = 1.0f; // no navmesh locally
        item.m_bDirectPath = true;
        item.m_bReachable = true;
      }
      else if (auto pPath = plDynamicCast<const plAiEqsTest_PathLength*>(pTest))
      {
        // approximate: euclidean distance with a detour factor
        item.m_fMeasurement = (cand.m_vPosition - m_vQuerier).GetLength() * 1.15f;
        item.m_bHasMeasurement = true;
        item.m_bReachable = true;
        fRaw = plMath::Max(0.001f, PreviewTrapezoid(item.m_fMeasurement, pPath->m_fBandMin, pPath->m_fBandMax));
      }
      else
        item.m_TestData = plAiEqsTestData::InvalidMeasurement; // custom tests need a runtime preview

      if (const char* szContext = pTest->GetContextProperty())
      {
        if (!props.HasContext(szContext))
          item.m_TestData = plAiEqsTestData::MissingContext;
      }

      item.m_fRaw = fRaw;

      // slot zero holds the temporary item's trace; editor rows have no 12-test limit
      const bool bSuccess = processor.ProcessItem(*pTest, item, 0);
      cand.m_TestScores[ordered.m_uiRowIndex] = item.m_TestScores[0];
      cand.m_TestTrace[ordered.m_uiRowIndex] = item.m_TestTrace[0];
      cand.m_bDiscarded = item.m_bDiscarded;
      m_ScoreSums[c] += item.m_fScoreSum;
      m_WeightSums[c] += item.m_fWeightSum;

      if (!bSuccess)
      {
        bQueryFailed = true;
        break;
      }
    }

    plUInt32 uiSurviving = 0;
    for (const auto& candidate : m_Candidates)
      uiSurviving += candidate.m_bDiscarded ? 0 : 1;

    sStages.AppendFormat(" -> T{}: {}", ordered.m_uiRowIndex + 1, uiSurviving);

    if (bQueryFailed)
    {
      sStages.Append(" (query failed: missing data)");
      for (auto& candidate : m_Candidates)
        candidate.m_bDiscarded = true;
      break;
    }
  }

  m_sStageCounts = sStages;
}

void plAiEqsPreviewSimulation::Finalize(const plAiEqsQueryAssetObject& props)
{
  plUInt32 uiPositiveResults = 0;
  float fBest = -1.0f;

  for (plUInt32 c = 0; c < m_Candidates.GetCount(); ++c)
  {
    plAiEqsPreviewCandidate& cand = m_Candidates[c];

    if (cand.m_bDiscarded)
      continue;

    ++m_uiSurvivors;
    cand.m_fFinal = (m_WeightSums[c] > 0.0f) ? (m_ScoreSums[c] / m_WeightSums[c]) : 1.0f;

    // the runtime omits zero-score results even when all filters passed
    if (cand.m_fFinal <= 0.0f)
      continue;

    ++uiPositiveResults;

    if (cand.m_fFinal > fBest)
    {
      fBest = cand.m_fFinal;
      m_iWinner = static_cast<plInt32>(c);
    }
  }

  if (m_iWinner >= 0)
    m_Candidates[m_iWinner].m_bWinner = true;

  const plUInt32 uiResultLimit = plMath::Clamp<plUInt32>(props.m_uiMaxResults, 1, props.m_RunMode == plAiEqsRunMode::SingleBest ? 8 : 16);
  m_uiResults = plMath::Min<plUInt32>(uiPositiveResults, uiResultLimit);
}
