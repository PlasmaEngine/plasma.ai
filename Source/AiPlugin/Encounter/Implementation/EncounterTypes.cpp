#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Encounter/EncounterTypes.h>
#include <Foundation/IO/Stream.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiEncounterTrigger, 1)
  PL_ENUM_CONSTANTS(plAiEncounterTrigger::EncounterStart, plAiEncounterTrigger::WaveCleared, plAiEncounterTrigger::WaveDown, plAiEncounterTrigger::Time, plAiEncounterTrigger::Calm, plAiEncounterTrigger::Script)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiEncounterWaveState, 1)
  PL_ENUM_CONSTANTS(plAiEncounterWaveState::Waiting, plAiEncounterWaveState::Held, plAiEncounterWaveState::Released, plAiEncounterWaveState::Cleared)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiEncounterPacingBy, 1)
  PL_ENUM_CONSTANTS(plAiEncounterPacingBy::Time, plAiEncounterPacingBy::WavesCleared)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_TYPE(plAiEncounterPhase, plNoBase, 1, plRTTIDefaultAllocator<plAiEncounterPhase>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_MEMBER_PROPERTY("Duration", m_fDuration)->AddAttributes(new plDefaultValueAttribute(30.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_MEMBER_PROPERTY("Target", m_fTarget)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.0f, 1.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;

PL_IMPLEMENT_MESSAGE_TYPE(plMsgAiEncounterWave);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgAiEncounterWave, 1, plRTTIDefaultAllocator<plMsgAiEncounterWave>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Wave", m_sWave),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

void operator<<(plStreamWriter& inout_stream, const plAiEncounterPhase& phase)
{
  inout_stream << phase.m_sName;
  inout_stream << phase.m_fDuration;
  inout_stream << phase.m_fTarget;
}

void operator>>(plStreamReader& inout_stream, plAiEncounterPhase& ref_phase)
{
  inout_stream >> ref_phase.m_sName;
  inout_stream >> ref_phase.m_fDuration;
  inout_stream >> ref_phase.m_fTarget;
}

float plAiEvaluatePacing(plArrayPtr<const plAiEncounterPhase> phases, float fPosition, plUInt32* out_pPhase)
{
  if (out_pPhase != nullptr)
    *out_pPhase = plInvalidIndex;

  if (phases.IsEmpty())
    return 1.0f;

  float fPhaseStart = 0.0f;
  float fPreviousTarget = 0.0f;

  for (plUInt32 i = 0; i < phases.GetCount(); ++i)
  {
    const plAiEncounterPhase& phase = phases[i];
    const float fPhaseEnd = fPhaseStart + plMath::Max(0.0f, phase.m_fDuration);

    if (fPosition < fPhaseEnd)
    {
      if (out_pPhase != nullptr)
        *out_pPhase = i;

      const float fAlong = (fPosition - fPhaseStart) / (fPhaseEnd - fPhaseStart);
      return plMath::Lerp(fPreviousTarget, phase.m_fTarget, plMath::Clamp(fAlong, 0.0f, 1.0f));
    }

    fPhaseStart = fPhaseEnd;
    fPreviousTarget = phase.m_fTarget;
  }

  if (out_pPhase != nullptr)
    *out_pPhase = phases.GetCount() - 1;

  return phases[phases.GetCount() - 1].m_fTarget;
}

void plAiEncounterIntensity::Reset()
{
  *this = plAiEncounterIntensity();
}

void plAiEncounterIntensity::Update(const plAiEncounterIntensitySettings& settings, float fDeltaSeconds, plUInt32 uiEngagedAgents, float fStimulusStrength, float fDamage)
{
  const float fDelta = plMath::Max(0.0f, fDeltaSeconds);

  // the sums fall off with the window as their time constant, so a burst counts fully at first and fades out
  const float fKeep = plMath::Exp(-fDelta / plMath::Max(0.1f, settings.m_fWindowSeconds));
  m_fStimulusSum = m_fStimulusSum * fKeep + plMath::Max(0.0f, fStimulusStrength);
  m_fDamageSum = m_fDamageSum * fKeep + plMath::Max(0.0f, fDamage);

  m_fParts[Engaged] = plMath::Clamp(static_cast<float>(uiEngagedAgents) / plMath::Max(1.0f, settings.m_fEngagedForFull), 0.0f, 1.0f);
  m_fParts[Stimuli] = plMath::Clamp(m_fStimulusSum / plMath::Max(0.01f, settings.m_fStimuliForFull), 0.0f, 1.0f);
  m_fParts[Damage] = plMath::Clamp(m_fDamageSum / plMath::Max(0.01f, settings.m_fDamageForFull), 0.0f, 1.0f);

  const float fMeasured = plMath::Clamp(m_fParts[Engaged] * settings.m_fEngagedWeight + m_fParts[Stimuli] * settings.m_fStimulusWeight + m_fParts[Damage] * settings.m_fDamageWeight, 0.0f, 1.0f);

  if (settings.m_fSmoothingSeconds <= 0.0f)
  {
    m_fSmoothed = fMeasured;
    return;
  }

  const float fFollow = 1.0f - plMath::Exp(-fDelta / settings.m_fSmoothingSeconds);
  m_fSmoothed += (fMeasured - m_fSmoothed) * fFollow;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Encounter_Implementation_EncounterTypes);
