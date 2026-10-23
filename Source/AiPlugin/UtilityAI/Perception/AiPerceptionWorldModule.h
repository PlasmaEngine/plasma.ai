#pragma once

#include <AiPlugin/UtilityAI/Perception/AiPerception.h>
#include <Core/World/WorldModule.h>
#include <Foundation/Threading/Mutex.h>

/// \brief Collects AI stimuli (sounds, damage events, custom events) and owns the faction setup.
///
/// PostStimulus() may be called from any thread at any time. Posted stimuli are published to the
/// AI agents in the NEXT frame's decision updates: the queue is drained once per frame in the
/// PreAsync phase, and plAiBrainWorldModule matches the frame's stimuli against all agents
/// (radius + faction attitude) during its decision pass.
class PL_AIPLUGIN_DLL plAiPerceptionWorldModule final : public plWorldModule
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiPerceptionWorldModule, plWorldModule);

public:
  plAiPerceptionWorldModule(plWorld* pWorld);
  ~plAiPerceptionWorldModule();

  virtual void Initialize() override;

  /// \brief Emits a stimulus to all AI agents in range. Thread-safe.
  void PostStimulus(const plAiStimulus& stimulus);

  struct TimedStimulus
  {
    plAiStimulus m_Stimulus;
    plTime m_Timestamp;        ///< world time at which the stimulus was published
    plUInt64 m_uiSequence = 0; ///< publication order, starting at 1
  };

  /// \brief Recently published stimuli: kept until every agent has processed them (see SetProcessedSequence), so agents
  /// with slow decision rates, or held back by the decision budget, don't miss any.
  ///
  /// Consumers remember the sequence number up to which they already processed stimuli and only handle
  /// newer ones. Sequence numbers don't depend on which module updates first within a frame.
  plArrayPtr<const TimedStimulus> GetRecentStimuli() const { return m_RecentStimuli; }

  /// \brief The sequence number of the newest published stimulus, 0 if none was published yet.
  plUInt64 GetLatestStimulusSequence() const { return m_uiLatestSequence; }

  /// \brief Every agent has processed the stimuli up to this sequence number, so they can be dropped.
  ///
  /// The brain reports it once per frame. Stimuli stay at least s_MinKeep (for other readers) and at most s_MaxKeep (an
  /// agent that never decides doesn't keep them forever).
  void SetProcessedSequence(plUInt64 uiSequence) { m_uiProcessedSequence = uiSequence; }

  static constexpr plTime s_MinKeep = plTime::MakeFromSeconds(0.5);
  static constexpr plTime s_MaxKeep = plTime::MakeFromSeconds(30.0);

  const plAiFactionConfig& GetFactionConfig() const { return m_FactionConfig; }

  plAiAttitude::Enum GetAttitude(const plTempHashedString& sTeamA, const plTempHashedString& sTeamB) const
  {
    return m_FactionConfig.GetAttitude(sTeamA, sTeamB);
  }

  /// \brief Reloads the faction config file (e.g. after the project settings changed it).
  void ReloadFactionConfig();

private:
  void UpdateDrainStimuli(const UpdateContext& context);

  plMutex m_QueueMutex;
  plDynamicArray<plAiStimulus> m_Queue;
  plDynamicArray<TimedStimulus> m_RecentStimuli;
  plUInt64 m_uiLatestSequence = 0;
  plUInt64 m_uiProcessedSequence = 0;

  plAiFactionConfig m_FactionConfig;
};
