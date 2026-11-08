#pragma once

#include <AiPlugin/Encounter/EncounterTypes.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

class plAiEncounterWaveComponent;

using plAiEncounterDirectorComponentManager = plComponentManagerSimple<class plAiEncounterDirectorComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Paces an encounter: decides when each of its waves may start, from how intense the fight is right now.
///
/// The waves are the plAiEncounterWaveComponents below the director's object, in hierarchy order (W1, W2, ...). Each wave
/// names a spawner that does the spawning; the director only releases it. A wave whose trigger fired waits ("held") while
/// the measured intensity is above the pacing curve plus HoldMargin, at most the wave's MaxHold.
///
/// Intensity (0 to 1) is measured once per tick from:
/// - agents at alert level Alert whose target belongs to PlayerTeam (objects without an AI agent have no team, so with
///   PlayerTeam empty, every target that isn't an AI agent counts);
/// - sound and damage stimuli posted by PlayerTeam;
/// - damage the players took, reported by the game through ReportPlayerDamage().
///
/// Run it where the spawners run (the host, or offline).
class PL_AIPLUGIN_DLL plAiEncounterDirectorComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiEncounterDirectorComponent, plComponent, plAiEncounterDirectorComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiEncounterDirectorComponent

public:
  plAiEncounterDirectorComponent();
  ~plAiEncounterDirectorComponent();

  enum class EncounterState : plUInt8
  {
    NotStarted,
    Running,
    Finished, ///< every wave is cleared
  };

  void StartEncounter();                   // [ scriptable ] starts (or restarts) the encounter
  void ResetEncounter();                   // [ scriptable ] back to not started; spawned agents stay
  void TriggerWave(const char* szWave);    // [ scriptable ] fires a waiting wave's trigger, whatever its type
  void ReportPlayerDamage(float fDamage);  // [ scriptable ] damage a player took, for the intensity
  float GetIntensity() const;              // [ scriptable ]
  float GetPacingTarget() const;           // [ scriptable ]
  bool IsFinished() const;                 // [ scriptable ]

  void Update();

  /// \brief The phases of the pacing curve.
  plDynamicArray<plAiEncounterPhase> m_Pacing; ///< [ property ]

  bool m_bStartOnSimulation = true;               ///< [ property ]
  plEnum<plAiEncounterPacingBy> m_PacingBy;       ///< [ property ]
  float m_fHoldMargin = 0.1f;                     ///< [ property ]
  plHashedString m_sPlayerTeam;                   ///< [ property ]
  plAiEncounterIntensitySettings m_Intensity;     ///< [ property ] the weights and scales

  /// \name Read by the editor's Encounter panel
  ///@{

  struct WaveStatus
  {
    plComponentHandle m_hWave;
    plGameObjectHandle m_hSpawner;
    plHashedString m_sSquad;                 ///< resolved squad id, empty when unknown
    plEnum<plAiEncounterWaveState> m_State;
    plInt32 m_iAfterWave = -1;               ///< index of the wave its trigger refers to, -1 = none
    plInt32 m_iAlive = 0;
    plInt32 m_iPeakAlive = 0;
    plTime m_DueSince;                       ///< encounter time the trigger fired
    plTime m_ReleasedAt;                     ///< encounter time it was released
    plTime m_ClearedAt;
    plTime m_EmptySince;                     ///< encounter time its agents were last seen gone; zero while some live
    plTime m_CalmSince;                      ///< Calm trigger: encounter time intensity went below the threshold, negative while above
    bool m_bScriptTriggered = false;
    const char* m_szReleasedBy = "";         ///< how it was released: "message", "function" or "plSpawnComponent"
  };

  EncounterState GetEncounterState() const { return m_State; }
  plTime GetEncounterTime() const { return m_Elapsed; }
  float GetPacingPosition() const;
  plUInt32 GetCurrentPhase() const; ///< index into m_Pacing, plInvalidIndex without phases
  const plAiEncounterIntensity& GetIntensityMeasurement() const { return m_Measured; }
  plUInt32 GetEngagedAgents() const { return m_uiEngaged; }

  /// \brief The waves and their states. Before the encounter starts (and in the editor), the waves found below the director.
  void GetWaves(plDynamicArray<WaveStatus>& out_waves) const;

  /// \brief The component of a spawner object that a release reaches, and how: "plMsgAiEncounterWave", the release
  /// function ("Activate()") or "plSpawnComponent". nullptr when nothing on the object can be released.
  static const plComponent* FindReleaseTarget(const plGameObject& spawner, plStringView sReleaseFunction, plStringBuilder& out_sPath);

  /// \brief Whether a component of the object has a reflected function of that name.
  static bool HasFunction(const plGameObject& object, plStringView sName);

  ///@}

private:
  void SetEngagedWeight(float f) { m_Intensity.m_fEngagedWeight = f; }
  float GetEngagedWeight() const { return m_Intensity.m_fEngagedWeight; }
  void SetStimulusWeight(float f) { m_Intensity.m_fStimulusWeight = f; }
  float GetStimulusWeight() const { return m_Intensity.m_fStimulusWeight; }
  void SetDamageWeight(float f) { m_Intensity.m_fDamageWeight = f; }
  float GetDamageWeight() const { return m_Intensity.m_fDamageWeight; }
  void SetEngagedForFull(float f) { m_Intensity.m_fEngagedForFull = f; }
  float GetEngagedForFull() const { return m_Intensity.m_fEngagedForFull; }
  void SetStimuliForFull(float f) { m_Intensity.m_fStimuliForFull = f; }
  float GetStimuliForFull() const { return m_Intensity.m_fStimuliForFull; }
  void SetDamageForFull(float f) { m_Intensity.m_fDamageForFull = f; }
  float GetDamageForFull() const { return m_Intensity.m_fDamageForFull; }
  void SetWindow(float f) { m_Intensity.m_fWindowSeconds = f; }
  float GetWindow() const { return m_Intensity.m_fWindowSeconds; }
  void SetSmoothing(float f) { m_Intensity.m_fSmoothingSeconds = f; }
  float GetSmoothing() const { return m_Intensity.m_fSmoothingSeconds; }

  void CollectWaves(plDynamicArray<WaveStatus>& out_waves) const;
  void Measure(float fDeltaSeconds);
  plUInt32 CountEngagedAgents() const;
  float CollectPlayerStimuli();
  void UpdateWave(WaveStatus& ref_wave, const plAiEncounterWaveComponent& wave, float fTarget);
  bool HasTriggered(WaveStatus& ref_wave, const plAiEncounterWaveComponent& wave) const;
  void UpdateDueWave(WaveStatus& ref_wave, const plAiEncounterWaveComponent& wave, float fTarget);
  void UpdateReleasedWave(WaveStatus& ref_wave);
  void Release(WaveStatus& ref_wave, const plAiEncounterWaveComponent& wave);
  plInt32 CountAlive(const WaveStatus& wave);

  /// Gives the wave's squad the order the wave carries, if it carries one.
  void IssueWaveOrder(const WaveStatus& wave_status, const plAiEncounterWaveComponent& wave);
  plUInt32 CountClearedWaves() const;

  EncounterState m_State = EncounterState::NotStarted;
  plTime m_Elapsed;
  float m_fTickAccumulator = 0.0f;
  plDynamicArray<WaveStatus> m_Waves;
  plAiEncounterIntensity m_Measured;
  plUInt32 m_uiEngaged = 0;
  float m_fPendingDamage = 0.0f;
  plUInt64 m_uiLastStimulusSequence = 0;
};
