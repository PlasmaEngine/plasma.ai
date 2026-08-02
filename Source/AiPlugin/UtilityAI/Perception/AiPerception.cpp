#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Perception/AiPerception.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/FileSystem/FileWriter.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiStimulusType, 1)
  PL_ENUM_CONSTANTS(plAiStimulusType::Sight, plAiStimulusType::Sound, plAiStimulusType::Damage, plAiStimulusType::Custom)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiAttitude, 1)
  PL_ENUM_CONSTANTS(plAiAttitude::Hostile, plAiAttitude::Neutral, plAiAttitude::Friendly)
PL_END_STATIC_REFLECTED_ENUM;

PL_IMPLEMENT_MESSAGE_TYPE(plMsgAiStimulus);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgAiStimulus, 1, plRTTIDefaultAllocator<plMsgAiStimulus>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Type", plAiStimulusType, m_Type),
    PL_MEMBER_PROPERTY("CustomType", m_sCustomType),
    PL_MEMBER_PROPERTY("Radius", m_fRadius)->AddAttributes(new plDefaultValueAttribute(20.0f)),
    PL_MEMBER_PROPERTY("Strength", m_fStrength)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 1.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiAttitude::Enum plAiFactionConfig::GetAttitude(const plTempHashedString& sTeamA, const plTempHashedString& sTeamB) const
{
  if (sTeamA == sTeamB && sTeamA != plTempHashedString())
  {
    // same (non-empty) team: friendly unless explicitly configured otherwise
    for (const auto& relation : m_Relations)
    {
      if (relation.m_sTeamA == sTeamA && relation.m_sTeamB == sTeamB)
        return static_cast<plAiAttitude::Enum>(relation.m_Attitude.GetValue());
    }

    return plAiAttitude::Friendly;
  }

  for (const auto& relation : m_Relations)
  {
    if ((relation.m_sTeamA == sTeamA && relation.m_sTeamB == sTeamB) ||
        (relation.m_sTeamA == sTeamB && relation.m_sTeamB == sTeamA))
    {
      return static_cast<plAiAttitude::Enum>(relation.m_Attitude.GetValue());
    }
  }

  // unknown pairs are hostile, so that agents react to everything until factions are configured
  return plAiAttitude::Hostile;
}

plResult plAiFactionConfig::Save(plStringView sFile) const
{
  plFileWriter file;
  if (file.Open(sFile).Failed())
    return PL_FAILURE;

  Save(file);
  return PL_SUCCESS;
}

plResult plAiFactionConfig::Load(plStringView sFile)
{
  plFileReader file;
  if (file.Open(sFile).Failed())
    return PL_FAILURE;

  Load(file);
  return PL_SUCCESS;
}

void plAiFactionConfig::Save(plStreamWriter& inout_stream) const
{
  const plUInt8 uiVersion = 1;
  inout_stream << uiVersion;

  inout_stream << m_Teams.GetCount();
  for (const auto& sTeam : m_Teams)
  {
    inout_stream << sTeam;
  }

  inout_stream << m_Relations.GetCount();
  for (const auto& relation : m_Relations)
  {
    inout_stream << relation.m_sTeamA;
    inout_stream << relation.m_sTeamB;
    inout_stream << relation.m_Attitude;
  }
}

void plAiFactionConfig::Load(plStreamReader& inout_stream)
{
  plUInt8 uiVersion = 0;
  inout_stream >> uiVersion;

  if (uiVersion != 1)
    return;

  plUInt32 uiNumTeams = 0;
  inout_stream >> uiNumTeams;

  m_Teams.Clear();
  m_Teams.Reserve(uiNumTeams);

  for (plUInt32 i = 0; i < uiNumTeams; ++i)
  {
    inout_stream >> m_Teams.ExpandAndGetRef();
  }

  plUInt32 uiNumRelations = 0;
  inout_stream >> uiNumRelations;

  m_Relations.Clear();
  m_Relations.Reserve(uiNumRelations);

  for (plUInt32 i = 0; i < uiNumRelations; ++i)
  {
    auto& relation = m_Relations.ExpandAndGetRef();
    inout_stream >> relation.m_sTeamA;
    inout_stream >> relation.m_sTeamB;
    inout_stream >> relation.m_Attitude;
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Perception_AiPerception);
