#pragma once

#include <AiPlugin/Eqs/EqsTypes.h>

#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Math/Vec2.h>
#include <Foundation/Strings/String.h>

class plAiEqsQueryAssetObject;
class plAiEqsTest;
class plAiEqsGenerator;
class plRTTI;
class plStringBuilder;

/// \brief Names, summaries and curve presets shared by the Qt and the engine-drawn EQS editors.
namespace plAiEqsUi
{
  /// "plAiEqsTest_LineOfSight" -> "Line Of Sight" (also generators and contexts); "<none>" for null.
  void GetPrettyName(const plRTTI* pRtti, plStringBuilder& out_sName);

  /// The Add Test menu group of a test type: "Distance & Direction", "Cover", "Visibility", "Pathfinding", "Blackboard" or "Other".
  const char* GetTestGroup(const plRTTI* pRtti);

  /// One-line summary of a test's configuration ("band 4.0 -> 18.0 m, vs Threat", dot-separated).
  void GetTestSummary(const plAiEqsTest* pTest, plStringBuilder& out_sSummary);

  /// The filter rule a test applies, as the cards print it after "Require ": "value >= 2", "raw score > 0".
  void GetFilterRule(const plAiEqsTest* pTest, plStringBuilder& out_sRule);

  /// One-line summary of the generator ("golden-angle rings 2.0 -> 14.0 m around Querier, navmesh-projected").
  void GetGeneratorSummary(const plAiEqsGenerator* pGenerator, plStringBuilder& out_sSummary);

  /// The context a test is evaluated against, or nullptr when the test has none.
  const char* GetTestContext(const plAiEqsTest* pTest);

  /// "PAYLOAD: COVER", "PAYLOAD: SMART OBJECT", "PAYLOAD: OBJECT" or empty.
  const char* GetPayloadLabel(const plAiEqsGenerator* pGenerator);

  /// "CHEAP", "RAYCAST" or "NAVMESH".
  const char* GetCostLabel(const plAiEqsTest* pTest);

  /// "FILTER", "SCORE" or "FILTER + SCORE".
  const char* GetPurposeLabel(const plAiEqsTest* pTest);

  /// The response curve presets of the test cards; the last one clears the curve.
  plUInt32 GetCurvePresetCount();
  const char* GetCurvePresetName(plUInt32 uiPreset);

  /// The control points of a preset (x and value in 0..1) and whether its tangents are smooth.
  plUInt32 GetCurvePresetPoints(plUInt32 uiPreset, plVec2d* out_pPoints, plUInt32 uiMaxPoints, bool& out_bSmooth);

  /// "pass", "rejected", "skipped" or "not run".
  const char* GetTraceState(const plAiEqsTestTrace& trace);

  /// The breakdown tooltip of one test: state and data, measurement, raw score, curve and contribution.
  void GetTraceDetail(const plAiEqsTestTrace& trace, plStringBuilder& out_sDetail);
} // namespace plAiEqsUi

/// \brief One locally simulated candidate of the query preview.
struct plAiEqsPreviewCandidate
{
  plVec2 m_vPosition = plVec2::MakeZero(); ///< meters, querier-relative plane
  plVec2 m_vWallDir = plVec2::MakeZero();  ///< synthetic cover wall direction
  plUInt8 m_uiPayload = 0;                 ///< plAiEqsPayloadType
  plUInt8 m_uiCoverQuality = 0;            ///< synthetic plAiCoverQuality
  plHybridArray<float, 12> m_TestScores;   ///< curved per-test scores
  plHybridArray<plAiEqsTestTrace, 12> m_TestTrace;
  float m_fFinal = 0.0f;
  bool m_bDiscarded = false;
  bool m_bWinner = false;
};

/// \brief Runs an EQS query against a simulated top-down situation inside the editor process.
///
/// Candidates come from the generator type (seeded, so they stay put between runs). Tests run cheap first, as at
/// runtime, through the real plAiEqsTestDesc::ProcessItem. Line of sight uses the occluder segment, navmesh tests
/// are approximated and every authored context slot resolves to the threat.
class plAiEqsPreviewSimulation
{
public:
  plVec2 m_vQuerier = plVec2(-4.0f, -3.0f);
  plVec2 m_vThreat = plVec2(5.0f, 4.0f);
  plVec2 m_vOccluderA = plVec2(0.0f, 1.5f);
  plVec2 m_vOccluderB = plVec2(2.5f, -1.0f);

  void Run(const plAiEqsQueryAssetObject& props);

  /// The stats lines: counts, the simulation note, the stage counts and the root validation. A root
  /// validation issue replaces them.
  void GetStats(plStringBuilder& out_sText) const;

  plHybridArray<plAiEqsPreviewCandidate, 64> m_Candidates;
  plInt32 m_iWinner = -1;
  plUInt32 m_uiGenerated = 0;
  plUInt32 m_uiSurvivors = 0;
  plUInt32 m_uiResults = 0;
  plString m_sStageCounts; ///< "32 generated -> T1: 20 -> T2: 9"
  plString m_sRootIssue;   ///< not empty: the query cannot run

private:
  void Generate(const plAiEqsQueryAssetObject& props);
  void RunTests(const plAiEqsQueryAssetObject& props);
  void Finalize(const plAiEqsQueryAssetObject& props);
  plVec2 ResolveContext(plStringView sName) const;

  plHybridArray<float, 64> m_ScoreSums;
  plHybridArray<float, 64> m_WeightSums;
};
