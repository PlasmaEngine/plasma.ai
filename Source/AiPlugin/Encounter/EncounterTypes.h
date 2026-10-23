#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Communication/Message.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Strings/HashedString.h>

/// \brief When an encounter wave becomes due.
struct PL_AIPLUGIN_DLL plAiEncounterTrigger
{
  using StorageType = plUInt8;

  enum Enum
  {
    EncounterStart, ///< as soon as the encounter starts
    WaveCleared,    ///< the wave named in AfterWave is cleared
    WaveDown,       ///< the wave named in AfterWave has at most AliveAtMost agents left (after it was released)
    Time,           ///< Time seconds after the encounter started
    Calm,           ///< measured intensity stayed below CalmBelow for CalmFor seconds
    Script,         ///< only when the director's TriggerWave() names this wave

    Default = EncounterStart
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiEncounterTrigger);

/// \brief Where a wave is in its lifetime.
struct PL_AIPLUGIN_DLL plAiEncounterWaveState
{
  using StorageType = plUInt8;

  enum Enum
  {
    Waiting,  ///< its trigger hasn't fired
    Held,     ///< due, but held back while the fight is more intense than the pacing allows
    Released, ///< its spawner was told to start
    Cleared,  ///< everything it spawned is gone

    Default = Waiting
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiEncounterWaveState);

/// \brief What the pacing curve's position measures.
struct PL_AIPLUGIN_DLL plAiEncounterPacingBy
{
  using StorageType = plUInt8;

  enum Enum
  {
    Time,         ///< seconds since the encounter started; phase durations are seconds
    WavesCleared, ///< the number of cleared waves; phase durations are waves

    Default = Time
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiEncounterPacingBy);

/// \brief One named section of the pacing curve. The target intensity goes in a straight line from the previous phase's
/// target (0 for the first phase) to this phase's target over its duration.
struct PL_AIPLUGIN_DLL plAiEncounterPhase
{
  plString m_sName;
  float m_fDuration = 30.0f; ///< seconds, or waves with plAiEncounterPacingBy::WavesCleared
  float m_fTarget = 0.5f;    ///< target intensity at the end of the phase, 0 to 1
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiEncounterPhase);

PL_AIPLUGIN_DLL void operator<<(plStreamWriter& inout_stream, const plAiEncounterPhase& phase);
PL_AIPLUGIN_DLL void operator>>(plStreamReader& inout_stream, plAiEncounterPhase& ref_phase);

/// \brief The pacing curve's target intensity at a position. Without phases the target is 1, so nothing is held. Past the
/// last phase the last target stays.
///
/// out_pPhase receives the index of the phase the position is in (the last one past the end), plInvalidIndex without phases.
PL_AIPLUGIN_DLL float plAiEvaluatePacing(plArrayPtr<const plAiEncounterPhase> phases, float fPosition, plUInt32* out_pPhase = nullptr);

/// \brief The settings that turn the measured fight into one intensity value.
struct PL_AIPLUGIN_DLL plAiEncounterIntensitySettings
{
  float m_fEngagedWeight = 0.5f;  ///< agents in combat with a player
  float m_fStimulusWeight = 0.3f; ///< player-team gunshot and damage stimuli
  float m_fDamageWeight = 0.2f;   ///< damage the players took (ReportPlayerDamage)

  float m_fEngagedForFull = 6.0f;   ///< this many engaged agents count as 1
  float m_fStimuliForFull = 12.0f;  ///< this much stimulus strength within the window counts as 1
  float m_fDamageForFull = 50.0f;   ///< this much damage within the window counts as 1
  float m_fWindowSeconds = 4.0f;    ///< how long stimuli and damage keep counting (an exponential falloff)
  float m_fSmoothingSeconds = 4.0f; ///< how quickly the reported intensity follows the measurement
};

/// \brief Measures how intense a fight is, 0 to 1, from three parts: engaged agents, recent player stimuli and recent
/// damage to the players. Each part is 0 to 1; the intensity is their weighted sum, smoothed over time.
class PL_AIPLUGIN_DLL plAiEncounterIntensity
{
public:
  enum Part
  {
    Engaged,
    Stimuli,
    Damage,
    PartCount
  };

  void Reset();

  /// \brief Advances the measurement by fDeltaSeconds. fStimulusStrength and fDamage are what arrived since the last call.
  void Update(const plAiEncounterIntensitySettings& settings, float fDeltaSeconds, plUInt32 uiEngagedAgents, float fStimulusStrength, float fDamage);

  float GetIntensity() const { return m_fSmoothed; }
  float GetPart(Part part) const { return m_fParts[part]; }

private:
  float m_fStimulusSum = 0.0f; ///< stimulus strength, falling off over the window
  float m_fDamageSum = 0.0f;   ///< damage, falling off over the window
  float m_fParts[PartCount] = {};
  float m_fSmoothed = 0.0f;
};

/// \brief Sent to a wave's spawner object when the encounter director releases the wave, or starts holding it.
///
/// A component that handles it with Event Released starts spawning. When no component of the spawner object handles the
/// message, the director calls the wave's release function instead (see plAiEncounterWaveComponent).
struct PL_AIPLUGIN_DLL plMsgAiEncounterWave : public plMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgAiEncounterWave, plMessage);

  enum class Event : plUInt8
  {
    Released, ///< start spawning
    Held,     ///< the wave is due but held back; nothing to do, for information
  };

  Event m_Event = Event::Released;
  plHashedString m_sWave; ///< the wave's object name
};
