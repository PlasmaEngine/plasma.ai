#pragma once

#include <AiPlugin/Encounter/EncounterTypes.h>
#include <AiPlugin/Tactical/SquadTypes.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

using plAiEncounterWaveComponentManager = plComponentManager<class plAiEncounterWaveComponent, plBlockStorageType::Compact>;

/// \brief One wave of an encounter: which spawner it starts, and when.
///
/// Waves belong to the plAiEncounterDirectorComponent on an object above them, numbered in hierarchy order. The wave's
/// name is its object's name. The wave doesn't spawn anything: when it is released, the director tells its spawner to
/// start, in this order:
/// - it sends plMsgAiEncounterWave to the spawner object; a component that handles it starts spawning;
/// - otherwise it calls the function named ReleaseFunction on the spawner object's components (arguments get default values);
/// - otherwise a plSpawnComponent on the spawner object spawns once.
///
/// The director counts the wave's living agents through its squad id: SquadId, or the spawner's own "SquadId" property.
/// Without one, it asks the spawner's "GetAliveCount" function. A spawner with an "IsCleared" function says when the wave
/// is cleared; otherwise the wave is cleared when its agents are gone.
///
/// A wave can give its squad an order when it is released ("hold the bridge for 30 s"): Order is the intent, and a radius
/// above zero adds a region around the wave's object. The squad takes the order when its first member appears.
class PL_AIPLUGIN_DLL plAiEncounterWaveComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiEncounterWaveComponent, plComponent, plAiEncounterWaveComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  //////////////////////////////////////////////////////////////////////////
  // plAiEncounterWaveComponent

public:
  plAiEncounterWaveComponent();
  ~plAiEncounterWaveComponent();

  void SetSpawnerReference(const char* szReference); // [ property ]
  void SetAfterWaveReference(const char* szReference); // [ property ]
  const char* DummyGetter() const { return nullptr; }

  /// \brief The spawner object: the referenced one, or this wave's own object.
  plGameObjectHandle GetSpawner() const;

  /// \brief The object of the wave that AfterWave names; invalid when not set.
  plGameObjectHandle GetAfterWave() const { return m_hAfterWave; }

  plHashedString m_sSquadId;                         ///< [ property ] empty = the spawner's own SquadId property
  plEnum<plAiEncounterTrigger> m_Trigger;            ///< [ property ]
  plUInt32 m_uiAliveAtMost = 1;                      ///< [ property ] WaveDown: released at this many living agents of AfterWave
  float m_fTime = 60.0f;                             ///< [ property ] Time: seconds after the encounter started
  float m_fCalmBelow = 0.4f;                         ///< [ property ] Calm: intensity threshold
  float m_fCalmFor = 10.0f;                          ///< [ property ] Calm: seconds below the threshold
  bool m_bHoldWhenIntense = true;                    ///< [ property ] a due wave waits while intensity is above the pacing plus the margin
  float m_fMaxHold = 45.0f;                          ///< [ property ] seconds a wave is held at most; 0 = no limit
  plString m_sReleaseFunction = "Activate";          ///< [ property ] called on the spawner when no component handles the release message

  plEnum<plAiSquadIntent> m_OrderIntent;             ///< [ property ] None = the squad decides for itself; otherwise the order it gets when the wave is released
  float m_fOrderDuration = 30.0f;                    ///< [ property ] seconds the order lasts; 0 = until the wave is cleared or the order is cancelled
  float m_fOrderRadius = 0.0f;                       ///< [ property ] a region around this wave's object, half the side of its square; 0 = no region
  bool m_bOrderNoRetreat = false;                    ///< [ property ] losses don't break the order

private:
  plGameObjectHandle m_hSpawner;
  plGameObjectHandle m_hAfterWave;
};
