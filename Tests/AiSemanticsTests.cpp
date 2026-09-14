#include <AiPlugin/Eqs/EqsQueryResource.h>
#include <AiPlugin/UtilityAI/Decision/AiBehaviorResource.h>
#include <AiPlugin/Utils/AiResponseCurve.h>
#include <Foundation/Configuration/Startup.h>
#include <Foundation/IO/MemoryStream.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <Foundation/Serialization/GraphVersioning.h>
#include <Foundation/Serialization/ReflectionSerializer.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <cstdio>

static int s_Failures = 0;
static int s_Checks = 0;
static void Check(bool value, const char* message)
{
  ++s_Checks;
  if (!value)
  {
    std::printf("FAIL: %s\n", message);
    ++s_Failures;
  }
}

static void TestFilters()
{
  plAiEqsTest_Distance test;
  plAiEqsTestDesc processor;
  plAiEqsItem sample;
  sample.m_fRaw = 0.0f;
  sample.m_bHasMeasurement = true;
  sample.m_fMeasurement = 12.0f;
  test.m_FilterCondition = plAiEqsFilterCondition::Between;
  test.m_fFilterMin = 10;
  test.m_fFilterMax = 15;
  Check(test.PassesFilter(sample), "numeric filter uses meters rather than score");
  test.m_bInvertFilter = true;
  Check(!test.PassesFilter(sample), "invert range rejects inside");
  sample.m_fMeasurement = 20;
  Check(test.PassesFilter(sample), "invert range accepts outside");
  test.m_bInvertFilter = false;
  test.m_FilterCondition = plAiEqsFilterCondition::AtLeast;
  Check(test.PassesFilter(sample), "minimum condition");
  test.m_FilterCondition = plAiEqsFilterCondition::AtMost;
  Check(!test.PassesFilter(sample), "maximum condition");
  test.m_FilterCondition = plAiEqsFilterCondition::IsTrue;
  Check(!test.PassesFilter(sample), "boolean condition uses raw result");
  sample.m_fRaw = 1;
  Check(test.PassesFilter(sample), "boolean true");
  test.m_FilterCondition = plAiEqsFilterCondition::LegacyPositiveScore;
  sample.m_fRaw = 0.25f;
  Check(test.PassesFilter(sample), "legacy positive threshold retained");

  test.m_Purpose = plAiEqsTestPurpose::ScoreOnly;
  test.m_bInvertScore = true;
  test.m_fWeight = 2;
  plAiEqsItem scored;
  scored.m_fRaw = 0.25f;
  Check(processor.ProcessItem(test, scored, 2), "process score");
  Check(plMath::IsEqual(scored.m_fScoreSum, 1.5f, 0.001f), "score inversion before weighting");
  Check(scored.m_fWeightSum == 2, "score denominator");
  Check(scored.m_TestTrace[2].m_bEvaluated && scored.m_TestTrace[2].m_bPassed, "trace recorded");
  Check(!scored.m_TestTrace[0].m_bEvaluated, "unexecuted trace distinct from zero");

  test.m_Purpose = plAiEqsTestPurpose::FilterOnly;
  plAiEqsItem rejected;
  rejected.m_fRaw = 0;
  Check(processor.ProcessItem(test, rejected, 3), "filter failure is not query error");
  Check(rejected.m_bDiscarded && rejected.m_uiRejectedBy == 3, "rejecting test retained");
  Check(rejected.m_fWeightSum == 0, "filter-only contributes no weight");
  test.m_bInvertFilter = true;
  rejected = plAiEqsItem();
  Check(processor.ProcessItem(test, rejected, 3) && !rejected.m_bDiscarded, "inverted zero filter passes");

  // A zero curve output must not determine the hard filter result.
  test.m_bInvertFilter = false;
  test.m_Purpose = plAiEqsTestPurpose::FilterAndScore;
  test.m_bInvertScore = false;
  processor.m_ScoreCurve.AddControlPoint(0.5).m_Position.y = 0;
  processor.PrepareCurve();
  plAiEqsItem zeroScore;
  zeroScore.m_fRaw = 1;
  processor.ProcessItem(test, zeroScore, 0);
  Check(!zeroScore.m_bDiscarded && zeroScore.m_fScoreSum == 0, "filter and curve decision independent");
}

static void TestMissingData()
{
  plAiEqsTest_LineOfSight test;
  plAiEqsTestDesc processor;
  plAiEqsItem sample;
  plAiEqsEvalContext context;
  test.Run(context, plMakeArrayPtr(&sample, 1), 0);
  Check(sample.m_TestData == plAiEqsTestData::MissingContext, "LOS identifies missing context");
  test.m_bInvertFilter = true;
  test.m_bInvertScore = true;
  Check(processor.ProcessItem(test, sample, 0) && sample.m_bDiscarded, "inversion cannot rescue missing data");

  for (auto policy : {plAiEqsMissingDataPolicy::SkipTest, plAiEqsMissingDataPolicy::FailQuery, plAiEqsMissingDataPolicy::Legacy})
  {
    test.m_MissingDataPolicy = policy;
    plAiEqsItem item;
    item.m_TestData = plAiEqsTestData::MissingContext;
    item.m_fRaw = 1;
    const bool success = processor.ProcessItem(test, item, 0);
    if (policy == plAiEqsMissingDataPolicy::SkipTest)
      Check(success && !item.m_bDiscarded && item.m_fWeightSum == 0 && item.m_TestTrace[0].m_bSkipped, "skip omits score and denominator");
    else if (policy == plAiEqsMissingDataPolicy::FailQuery)
      Check(!success && item.m_bDiscarded, "fail-query propagates failure");
    else
      Check(success && !item.m_bDiscarded && item.m_fScoreSum == 1, "legacy missing data is not inverted");
  }
  plAiEqsResolvedSlot slot;
  slot.m_sName.Assign("Threat");
  slot.m_Positions.PushBack(plVec3(1, 0, 0));
  context.m_ResolvedSlots = plMakeArrayPtr(&slot, 1);
  sample = plAiEqsItem();
  test.Run(context, plMakeArrayPtr(&sample, 1), 0);
  Check(sample.m_TestData == plAiEqsTestData::MissingPhysics, "LOS identifies missing physics");
  plAiEqsTest_PathLength path;
  path.Run(context, plMakeArrayPtr(&sample, 1), 0);
  Check(sample.m_TestData == plAiEqsTestData::MissingNavigation, "path identifies missing navigation");
  plAiEqsTest_CoverQuality cover;
  sample = plAiEqsItem();
  cover.Run(context, plMakeArrayPtr(&sample, 1), 0);
  Check(sample.m_TestData == plAiEqsTestData::MissingPayload, "cover identifies incompatible payload");
}

static void TestMeasurements()
{
  plAiEqsTest_Distance test;
  plAiEqsResolvedSlot slot;
  slot.m_sName.Assign("Querier");
  slot.m_Positions.PushBack(plVec3::MakeZero());
  plAiEqsEvalContext context;
  context.m_ResolvedSlots = plMakeArrayPtr(&slot, 1);
  plAiEqsItem sample;
  sample.m_vPosition = plVec3(3, 4, 99);
  test.Run(context, plMakeArrayPtr(&sample, 1), 0);
  Check(sample.m_bHasMeasurement && sample.m_fMeasurement == 5, "distance measured in 2D meters");
  test.m_FilterCondition = plAiEqsFilterCondition::Between;
  test.m_fFilterMin = 5;
  test.m_fFilterMax = 5;
  Check(test.PassesFilter(sample), "numeric bounds are inclusive");
  plAiEqsTest_ReachableApprox approx;
  approx.m_FilterCondition = plAiEqsFilterCondition::DirectPath;
  sample.m_bDirectPath = false;
  sample.m_fRaw = 0.25f;
  Check(!approx.PassesFilter(sample), "detour fails explicit direct path");
  sample.m_bDirectPath = true;
  Check(approx.PassesFilter(sample), "direct path succeeds");
  plAiEqsTest_PathLength path;
  path.m_FilterCondition = plAiEqsFilterCondition::Reachable;
  sample.m_bReachable = true;
  sample.m_fRaw = 0.001f;
  Check(path.PassesFilter(sample), "reachable is independent of scoring band");
}

static void TestCompatibility()
{
  plAiEqsQueryDesc query;
  auto& desc = query.m_Tests.ExpandAndGetRef();
  desc.m_pTest = PL_DEFAULT_NEW(plAiEqsTest_Distance);
  desc.m_pTest->m_bInvertFilter = true;
  desc.m_pTest->m_bInvertScore = true;
  desc.m_pTest->m_FilterCondition = plAiEqsFilterCondition::Between;
  desc.m_pTest->m_fFilterMin = 5;
  desc.m_pTest->m_fFilterMax = 15;
  desc.m_pTest->m_MissingDataPolicy = plAiEqsMissingDataPolicy::SkipTest;
  plDefaultMemoryStreamStorage storage;
  plMemoryStreamWriter writer(&storage);
  query.Serialize(writer).IgnoreResult();
  plMemoryStreamReader reader(&storage);
  plAiEqsQueryDesc restored;
  Check(restored.Deserialize(reader).Succeeded(), "new EQS descriptor round trip");
  const auto& test = *restored.m_Tests[0].m_pTest;
  Check(test.m_bInvertFilter && test.m_bInvertScore && test.m_fFilterMin == 5 && test.m_fFilterMax == 15, "filter controls round trip");
  Check(test.m_MissingDataPolicy == plAiEqsMissingDataPolicy::SkipTest, "missing data policy round trip");

  // Construct the previous binary layout explicitly: no curve-domain flag.
  plDefaultMemoryStreamStorage oldStorage;
  plMemoryStreamWriter old(&oldStorage);
  old.WriteVersion(1);
  old << query.m_sName;
  old << query.m_RunMode;
  old << query.m_fTopPercent;
  old << query.m_uiCandidates;
  old << query.m_uiMaxResults;
  old << query.m_sNavmeshConfig;
  old << query.m_sPathSearchConfig;
  old << plUInt32(0) << false << plUInt32(1); // slots, generator, test count
  old << true << desc.m_pTest->GetDynamicRTTI()->GetTypeName();
  plReflectionSerializer::WriteObjectToBinary(old, desc.m_pTest->GetDynamicRTTI(), desc.m_pTest.Borrow());
  desc.m_ScoreCurve.Save(old);
  plMemoryStreamReader oldReader(&oldStorage);
  plAiEqsQueryDesc legacy;
  Check(legacy.Deserialize(oldReader).Succeeded(), "read old binary layout");
  Check(legacy.m_Tests[0].m_bLegacyCurveDomain, "old binary keeps curve domain");
  Check(legacy.m_Tests[0].m_pTest->m_MissingDataPolicy == plAiEqsMissingDataPolicy::Legacy, "old binary keeps missing-data semantics");

  plAbstractObjectGraph graph;
  plAbstractObjectGraph types;
  auto* type = types.AddNode(plUuid::MakeUuid(), "plReflectedTypeDescriptor", 1);
  type->AddProperty("TypeName", "plAiEqsTest");
  type->AddProperty("ParentTypeName", "plReflectedClass");
  type->AddProperty("TypeVersion", plUInt32(1));
  auto* node = graph.AddNode(plUuid::MakeUuid(), "plAiEqsTest", 1);
  plGraphVersioning::GetSingleton()->PatchGraph(&graph, &types);
  Check(node->FindProperty("MissingDataPolicy") != nullptr, "source graph compatibility patch registered");
}

static void TestCurves()
{
  plAiEqsTestDesc test;
  Check(test.ApplyCurve(0.25f) == 0.25f, "empty curve is identity");
  auto& first = test.m_ScoreCurve.AddControlPoint(0.25);
  first.m_Position.y = 0.2;
  first.m_TangentModeLeft = plCurveTangentMode::Linear;
  first.m_TangentModeRight = plCurveTangentMode::Linear;
  test.PrepareCurve();
  Check(plMath::IsEqual(test.ApplyCurve(0.9f), 0.2f, 0.001f), "single point is constant");
  auto& last = test.m_ScoreCurve.AddControlPoint(0.75);
  last.m_Position.y = 0.8;
  last.m_TangentModeLeft = plCurveTangentMode::Linear;
  last.m_TangentModeRight = plCurveTangentMode::Linear;
  test.PrepareCurve();
  Check(plMath::IsEqual(test.ApplyCurve(0.0f), 0.2f, 0.001f), "hold left endpoint");
  Check(plMath::IsEqual(test.ApplyCurve(1.0f), 0.8f, 0.001f), "hold right endpoint");
  Check(plMath::IsEqual(test.ApplyCurve(0.25f), 0.2f, 0.001f), "authored X is sampled directly");
  Check(plMath::IsEqual(test.ApplyCurve(0.5f), 0.5f, 0.001f), "interior interpolation");
  test.m_bLegacyCurveDomain = true;
  Check(plMath::IsEqual(test.ApplyCurve(0.25f), 0.35f, 0.001f), "legacy domain is preserved");

  plAiEqsQueryDesc query;
  query.m_Tests.PushBack(std::move(test));
  plDefaultMemoryStreamStorage storage;
  plMemoryStreamWriter writer(&storage);
  Check(query.Serialize(writer).Succeeded(), "serialize curve mode");
  plMemoryStreamReader reader(&storage);
  plAiEqsQueryDesc copy;
  Check(copy.Deserialize(reader).Succeeded(), "deserialize curve mode");
  Check(copy.m_Tests[0].m_bLegacyCurveDomain, "curve mode round trip");
  Check(plMath::IsEqual(copy.m_Tests[0].ApplyCurve(0.25f), 0.35f, 0.001f), "curve result round trip");

  plAiConsiderationDesc consideration;
  auto input = PL_DEFAULT_NEW(plAiInput_Constant);
  input->m_fValue = 25;
  consideration.m_pInput = std::move(input);
  consideration.m_fInputMin = 0;
  consideration.m_fInputMax = 100;
  consideration.m_ResponseCurve = copy.m_Tests[0].m_ScoreCurve;
  consideration.PrepareCurve();
  plAiScoringContext context;
  Check(plMath::IsEqual(consideration.Evaluate(context), 0.2f, 0.001f), "utility uses authored X after normalization");
  consideration.m_bLegacyCurveDomain = true;
  Check(plMath::IsEqual(consideration.Evaluate(context), 0.35f, 0.001f), "utility retains legacy domain");
  plAiBehaviorResourceDescriptor behavior;
  behavior.m_Considerations.PushBack(std::move(consideration));
  plDefaultMemoryStreamStorage behaviorStorage;
  plMemoryStreamWriter behaviorWriter(&behaviorStorage);
  Check(behavior.Serialize(behaviorWriter).Succeeded(), "behavior serialization");
  plMemoryStreamReader behaviorReader(&behaviorStorage);
  plAiBehaviorResourceDescriptor behaviorCopy;
  Check(behaviorCopy.Deserialize(behaviorReader).Succeeded(), "behavior deserialization");
  Check(plMath::IsEqual(behaviorCopy.m_Considerations[0].Evaluate(context), 0.35f, 0.001f), "behavior curve mode round trip");

  plAiEqsTestDesc full;
  full.m_ScoreCurve.AddControlPoint(0).m_Position.y = 0;
  full.m_ScoreCurve.AddControlPoint(1).m_Position.y = 1;
  full.PrepareCurve();
  for (float x : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
  {
    const float modern = full.ApplyCurve(x);
    full.m_bLegacyCurveDomain = true;
    Check(plMath::IsEqual(modern, full.ApplyCurve(x), 0.0001f), "full-range curve unchanged");
    full.m_bLegacyCurveDomain = false;
  }
}

static void CheckMigratedEqsResource(const char* path)
{
  plOSFile file;
  if (file.Open(path, plFileOpenMode::Read).Failed())
  {
    Check(false, "open transformed EQS resource");
    return;
  }
  plDynamicArray<plUInt8> bytes;
  file.ReadAll(bytes);
  plRawMemoryStreamReader reader(bytes.GetData(), bytes.GetCount());
  plAssetFileHeader header;
  Check(header.Read(reader).Succeeded(), "read transformed asset header");
  plAiEqsQueryDesc desc;
  if (desc.Deserialize(reader).Failed())
  {
    Check(false, "read transformed EQS descriptor");
    return;
  }
  Check(!desc.m_Tests.IsEmpty(), "transformed EQS has tests");
  for (const auto& test : desc.m_Tests)
  {
    Check(test.m_bLegacyCurveDomain, "migrated source preserves curve domain");
    Check(test.m_pTest && test.m_pTest->m_MissingDataPolicy == plAiEqsMissingDataPolicy::Legacy, "migrated source preserves missing-data handling");
  }
  std::printf("Checked migrated resource: %s (%u tests)\n", path, desc.m_Tests.GetCount());
}

int main(int argc, char** argv)
{
  plStartup::StartupCoreSystems();
  TestCurves();
  TestFilters();
  TestMissingData();
  TestMeasurements();
  TestCompatibility();
  for (int i = 1; i < argc; ++i)
    CheckMigratedEqsResource(argv[i]);
  plStartup::ShutdownCoreSystems();
  std::printf("AI semantics: %d checks, %d failure(s)\n", s_Checks, s_Failures);
  return s_Failures == 0 ? 0 : 1;
}
