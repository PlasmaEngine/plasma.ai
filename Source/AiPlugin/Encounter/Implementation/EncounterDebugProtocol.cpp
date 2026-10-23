#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Encounter/EncounterDebugProtocol.h>
#include <Foundation/IO/MemoryStream.h>

// The plUuid stream operators return void, so GUIDs are written in statements of their own.

namespace
{
  constexpr plUInt8 s_uiProtocolVersion = 1;

  template <typename T>
  void WriteToBuffer(const T& msg, plDataBuffer& out_buffer)
  {
    plContiguousMemoryStreamStorage storage;
    plMemoryStreamWriter writer(&storage);
    msg.Write(writer);

    out_buffer.SetCountUninitialized(storage.GetStorageSize32());
    if (storage.GetStorageSize32() > 0)
      plMemoryUtils::Copy(out_buffer.GetData(), storage.GetData(), storage.GetStorageSize32());
  }

  template <typename T>
  plResult ReadFromBuffer(T& msg, const plDataBuffer& buffer)
  {
    if (buffer.IsEmpty())
      return PL_FAILURE;

    plRawMemoryStreamReader reader(buffer.GetData(), buffer.GetCount());
    return msg.Read(reader);
  }

  template <typename Array, typename WriteItem>
  void WriteItems(plStreamWriter& out, const Array& items, WriteItem writeItem)
  {
    out << items.GetCount();
    for (const auto& item : items)
      writeItem(item);
  }

  template <typename Array, typename ReadItem>
  plResult ReadItems(plStreamReader& in, Array& out_items, ReadItem readItem)
  {
    plUInt32 uiCount = 0;
    in >> uiCount;

    if (uiCount > 64 * 1024)
      return PL_FAILURE; // a corrupt count, don't allocate it

    out_items.SetCount(uiCount);
    for (auto& item : out_items)
      readItem(item);

    return PL_SUCCESS;
  }
} // namespace

void plAiEncounterDebugRequest::Write(plStreamWriter& inout_stream) const
{
  inout_stream << s_uiProtocolVersion;
  inout_stream << m_uiRequestId;
  inout_stream << m_Director;
  inout_stream << m_bRestart;
}

plResult plAiEncounterDebugRequest::Read(plStreamReader& inout_stream)
{
  plUInt8 uiVersion = 0;
  inout_stream >> uiVersion;
  if (uiVersion != s_uiProtocolVersion)
    return PL_FAILURE;

  inout_stream >> m_uiRequestId;
  inout_stream >> m_Director;
  inout_stream >> m_bRestart;
  return PL_SUCCESS;
}

void plAiEncounterDebugRequest::ToBuffer(plDataBuffer& out_buffer) const
{
  WriteToBuffer(*this, out_buffer);
}

plResult plAiEncounterDebugRequest::FromBuffer(const plDataBuffer& buffer)
{
  return ReadFromBuffer(*this, buffer);
}

void plAiEncounterDebugFrame::Write(plStreamWriter& inout_stream) const
{
  plStreamWriter& out = inout_stream;

  out << s_uiProtocolVersion;
  out << m_uiRequestId << m_bSimulating;

  WriteItems(out, m_Directors, [&](const Director& d) {
    out << d.m_Object;
    out << d.m_sName;
  });

  out << m_bHasDirector;
  out << m_Director;
  out << m_sDirectorName << m_uiState << m_fEncounterTime << m_fPacingPosition << m_uiPacingBy << m_uiPhase;
  out << m_fIntensity << m_fTarget << m_fHoldMargin << m_fSmoothing << m_uiEngaged;

  for (plUInt32 i = 0; i < 3; ++i)
    out << m_fParts[i] << m_fWeights[i];

  WriteItems(out, m_Phases, [&](const Phase& p) { out << p.m_sName << p.m_fDuration << p.m_fTarget; });

  WriteItems(out, m_Waves, [&](const Wave& w) {
    out << w.m_Object;
    out << w.m_Spawner;
    out << w.m_sName << w.m_sSpawner << w.m_sSpawnerType << w.m_sSquad << w.m_sTrigger << w.m_sReleasePath << w.m_sProblem;
    out << w.m_uiState << w.m_iAlive << w.m_iPeakAlive << w.m_fDueSince << w.m_fReleasedAt << w.m_fClearedAt << w.m_bHoldWhenIntense << w.m_fMaxHold;
  });
}

plResult plAiEncounterDebugFrame::Read(plStreamReader& inout_stream)
{
  plStreamReader& in = inout_stream;

  plUInt8 uiVersion = 0;
  in >> uiVersion;
  if (uiVersion != s_uiProtocolVersion)
    return PL_FAILURE;

  in >> m_uiRequestId >> m_bSimulating;

  PL_SUCCEED_OR_RETURN(ReadItems(in, m_Directors, [&](Director& d) {
    in >> d.m_Object;
    in >> d.m_sName;
  }));

  in >> m_bHasDirector;
  in >> m_Director;
  in >> m_sDirectorName >> m_uiState >> m_fEncounterTime >> m_fPacingPosition >> m_uiPacingBy >> m_uiPhase;
  in >> m_fIntensity >> m_fTarget >> m_fHoldMargin >> m_fSmoothing >> m_uiEngaged;

  for (plUInt32 i = 0; i < 3; ++i)
    in >> m_fParts[i] >> m_fWeights[i];

  PL_SUCCEED_OR_RETURN(ReadItems(in, m_Phases, [&](Phase& p) { in >> p.m_sName >> p.m_fDuration >> p.m_fTarget; }));

  return ReadItems(in, m_Waves, [&](Wave& w) {
    in >> w.m_Object;
    in >> w.m_Spawner;
    in >> w.m_sName >> w.m_sSpawner >> w.m_sSpawnerType >> w.m_sSquad >> w.m_sTrigger >> w.m_sReleasePath >> w.m_sProblem;
    in >> w.m_uiState >> w.m_iAlive >> w.m_iPeakAlive >> w.m_fDueSince >> w.m_fReleasedAt >> w.m_fClearedAt >> w.m_bHoldWhenIntense >> w.m_fMaxHold;
  });
}

void plAiEncounterDebugFrame::ToBuffer(plDataBuffer& out_buffer) const
{
  WriteToBuffer(*this, out_buffer);
}

plResult plAiEncounterDebugFrame::FromBuffer(const plDataBuffer& buffer)
{
  return ReadFromBuffer(*this, buffer);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Encounter_Implementation_EncounterDebugProtocol);
