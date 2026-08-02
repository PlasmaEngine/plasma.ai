#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/Messages/EventMessage.h>
#include <Core/World/Declarations.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Strings/HashedString.h>

/// \brief What kind of event a stimulus represents.
struct PL_AIPLUGIN_DLL plAiStimulusType
{
  using StorageType = plUInt8;

  enum Enum
  {
    Sight,  ///< generated from sensor components seeing an object
    Sound,  ///< a noise: footsteps, gunshots, explosions
    Damage, ///< the agent (or something nearby) took damage
    Custom, ///< game-specific, differentiated by m_sCustomType

    Default = Sound
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiStimulusType);

/// \brief One perception event that AI agents may react to.
///
/// Stimuli are broadcast: all agents within m_fRadius around m_vGlobalPosition receive the
/// stimulus (subject to faction filtering) and update their perceived-target records from it.
struct PL_AIPLUGIN_DLL plAiStimulus
{
  plEnum<plAiStimulusType> m_Type;
  plHashedString m_sCustomType;    ///< only used when m_Type == Custom
  plVec3 m_vGlobalPosition = plVec3::MakeZero();
  float m_fRadius = 20.0f;         ///< how far the stimulus carries
  float m_fStrength = 1.0f;        ///< [0,1] how strongly it drives target confidence
  plGameObjectHandle m_hSource;    ///< who caused the stimulus (becomes the perceived target)
  plHashedString m_sSourceTeam;    ///< team of the source, for faction filtering
};

/// \brief Send this message to any object (or broadcast it) to emit an AI stimulus.
///
/// plAiAgentComponent forwards it to the plAiPerceptionWorldModule. Gameplay code, visual scripts
/// and state machines (via SendMsg states) can use this to make noise, report damage, etc.
struct PL_AIPLUGIN_DLL plMsgAiStimulus : public plEventMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgAiStimulus, plEventMessage);

  plEnum<plAiStimulusType> m_Type;
  plHashedString m_sCustomType;
  float m_fRadius = 20.0f;
  float m_fStrength = 1.0f;
};

/// \brief How one team feels about another.
struct PL_AIPLUGIN_DLL plAiAttitude
{
  using StorageType = plUInt8;

  enum Enum
  {
    Hostile,
    Neutral,
    Friendly,

    Default = Hostile
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiAttitude);

/// \brief Project-wide faction setup: which teams exist and how they relate.
///
/// Unknown team pairs default to Hostile, so agents react to everything until factions are
/// explicitly configured.
struct PL_AIPLUGIN_DLL plAiFactionConfig
{
  struct Relation
  {
    plHashedString m_sTeamA;
    plHashedString m_sTeamB;
    plEnum<plAiAttitude> m_Attitude;
  };

  plDynamicArray<plString> m_Teams;
  plDynamicArray<Relation> m_Relations;

  plAiAttitude::Enum GetAttitude(const plTempHashedString& sTeamA, const plTempHashedString& sTeamB) const;

  static constexpr const plStringView s_sConfigFile = ":project/RuntimeConfigs/AiFactionConfig.cfg"_plsv;

  plResult Save(plStringView sFile = s_sConfigFile) const;
  plResult Load(plStringView sFile = s_sConfigFile);

  void Save(plStreamWriter& inout_stream) const;
  void Load(plStreamReader& inout_stream);
};
