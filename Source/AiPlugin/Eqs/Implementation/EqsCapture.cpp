#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/EqsCapture.h>
#include <Foundation/IO/Stream.h>

plResult plAiEqsCapture::Serialize(plStreamWriter& inout_stream) const
{
  inout_stream.WriteVersion(1);

  inout_stream << m_sQueryName;
  inout_stream << m_sResourceID;
  inout_stream << m_uiResourceChangeCounter;
  inout_stream << m_Time;
  inout_stream << m_uiSeed;
  inout_stream << m_vQuerier;
  inout_stream << static_cast<plUInt8>(m_Status);
  inout_stream << m_iSelected;

  inout_stream << m_Contexts.GetCount();
  for (const Context& context : m_Contexts)
  {
    inout_stream << context.m_sName;
    PL_SUCCEED_OR_RETURN(inout_stream.WriteArray(context.m_Positions));
  }

  inout_stream << m_Candidates.GetCount();
  for (const Candidate& candidate : m_Candidates)
  {
    inout_stream << candidate.m_vPosition;
    inout_stream << candidate.m_Payload;
    inout_stream << candidate.m_fFinal;
    inout_stream << candidate.m_bDiscarded;
    inout_stream << candidate.m_uiRejectedBy;

    inout_stream << candidate.m_Samples.GetCount();
    for (const Sample& sample : candidate.m_Samples)
    {
      inout_stream << sample.m_fRaw;
      inout_stream << sample.m_fMeasurement;
      inout_stream << sample.m_bHasMeasurement;
      inout_stream << sample.m_bMeasured;
      inout_stream << static_cast<plUInt8>(sample.m_Data);
    }
  }

  return PL_SUCCESS;
}

plResult plAiEqsCapture::Deserialize(plStreamReader& inout_stream)
{
  inout_stream.ReadVersion(1);

  plUInt8 uiStatus = 0;

  inout_stream >> m_sQueryName;
  inout_stream >> m_sResourceID;
  inout_stream >> m_uiResourceChangeCounter;
  inout_stream >> m_Time;
  inout_stream >> m_uiSeed;
  inout_stream >> m_vQuerier;
  inout_stream >> uiStatus;
  inout_stream >> m_iSelected;
  m_Status = static_cast<plAiEqsQueryResult::Status>(uiStatus);

  plUInt32 uiContexts = 0;
  inout_stream >> uiContexts;
  m_Contexts.SetCount(uiContexts);
  for (Context& context : m_Contexts)
  {
    inout_stream >> context.m_sName;
    PL_SUCCEED_OR_RETURN(inout_stream.ReadArray(context.m_Positions));
  }

  plUInt32 uiCandidates = 0;
  inout_stream >> uiCandidates;
  m_Candidates.SetCount(uiCandidates);
  for (Candidate& candidate : m_Candidates)
  {
    inout_stream >> candidate.m_vPosition;
    inout_stream >> candidate.m_Payload;
    inout_stream >> candidate.m_fFinal;
    inout_stream >> candidate.m_bDiscarded;
    inout_stream >> candidate.m_uiRejectedBy;

    plUInt32 uiSamples = 0;
    inout_stream >> uiSamples;
    candidate.m_Samples.SetCount(uiSamples);
    for (Sample& sample : candidate.m_Samples)
    {
      plUInt8 uiData = 0;
      inout_stream >> sample.m_fRaw;
      inout_stream >> sample.m_fMeasurement;
      inout_stream >> sample.m_bHasMeasurement;
      inout_stream >> sample.m_bMeasured;
      inout_stream >> uiData;
      sample.m_Data = static_cast<plAiEqsTestData>(uiData);
    }
  }

  return PL_SUCCESS;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Eqs_Implementation_EqsCapture);
