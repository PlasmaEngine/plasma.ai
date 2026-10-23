#pragma once

#include <AiPlugin/Eqs/EqsTypes.h>

class plAiEqsQueryDesc;
class plStreamReader;
class plStreamWriter;

/// \brief A recorded EQS query execution: every candidate with what each test measured.
///
/// Recorded with plAiEqsWorldModule::CaptureNextQuery. Captures can be saved, loaded and replayed
/// against an edited query (plAiEqsReplayCapture), so filters, curves and weights can be tuned on real data.
/// Samples cover the first plAiEqsItem::MaxRecordedTests tests.
struct PL_AIPLUGIN_DLL plAiEqsCapture
{
  /// What one test measured for one candidate.
  struct Sample
  {
    float m_fRaw = 0.0f;
    float m_fMeasurement = 0.0f;
    bool m_bHasMeasurement = false;
    bool m_bMeasured = false; ///< false when the test never ran for this candidate (rejected earlier)
    plAiEqsTestData m_Data = plAiEqsTestData::Valid;
  };

  struct Candidate
  {
    plVec3 m_vPosition = plVec3::MakeZero();
    plEnum<plAiEqsPayloadType> m_Payload;
    float m_fFinal = 0.0f;
    bool m_bDiscarded = false;
    plUInt16 m_uiRejectedBy = 0xFFFF;   ///< test index, 0xFFFF = not rejected
    plHybridArray<Sample, 8> m_Samples; ///< per test, in descriptor order
  };

  struct Context
  {
    plHashedString m_sName;
    plHybridArray<plVec3, 2> m_Positions;
  };

  plString m_sQueryName;
  plString m_sResourceID;
  plUInt32 m_uiResourceChangeCounter = 0; ///< the asset revision the capture was made with
  plTime m_Time;                          ///< world time of the capture
  plUInt32 m_uiSeed = 0;                  ///< seed of the result selection
  plVec3 m_vQuerier = plVec3::MakeZero();
  plGameObjectHandle m_hQuerier; ///< not serialized; only valid in the world that recorded the capture
  plAiEqsQueryResult::Status m_Status = plAiEqsQueryResult::Status::Invalid;
  plInt32 m_iSelected = -1; ///< index into m_Candidates of the best result, -1 = none

  plHybridArray<Context, 4> m_Contexts;
  plDynamicArray<Candidate> m_Candidates; ///< survivors best first, then rejected candidates

  plResult Serialize(plStreamWriter& inout_stream) const;
  plResult Deserialize(plStreamReader& inout_stream);
};

/// \brief The outcome of replaying a capture against a query descriptor.
struct PL_AIPLUGIN_DLL plAiEqsReplayResult
{
  struct Candidate
  {
    float m_fFinal = 0.0f;
    bool m_bDiscarded = false;
    plUInt16 m_uiRejectedBy = 0xFFFF;
    bool m_bMissingSample = false; ///< the edited query reaches a test the capture has no sample for
  };

  plDynamicArray<Candidate> m_Candidates; ///< parallel to the capture's candidates
  plInt32 m_iSelected = -1;               ///< index into the capture's candidates
  plUInt32 m_uiMissingSamples = 0;        ///< candidates scored without one of their tests
};

/// \brief Re-scores a capture with the descriptor's filters, curves, weights and run mode, using the captured measurements.
///
/// Tests run in the runtime's order (cheap first). A descriptor with a different number of tests than the
/// capture replays the tests it has samples for.
PL_AIPLUGIN_DLL void plAiEqsReplayCapture(const plAiEqsQueryDesc& desc, const plAiEqsCapture& capture, plAiEqsReplayResult& out_result);

/// \brief Which of uiCount candidates, sorted best first, a query with the given run mode returns.
///
/// The runtime, replays and previews share this, so they select the same candidate for the same seed.
PL_AIPLUGIN_DLL plUInt32 plAiEqsSelectCandidate(plUInt32 uiCount, plAiEqsRunMode::Enum runMode, float fTopPercent, plUInt32 uiSeed);
