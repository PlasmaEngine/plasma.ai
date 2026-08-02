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
    plTime m_Timestamp; ///< world time at which the stimulus was published
  };

  /// \brief Recently published stimuli (rolling window, so that agents with slow decision rates don't miss any).
  ///
  /// Consumers remember the timestamp up to which they already processed stimuli and only handle newer ones.
  /// Only valid to read during the PostAsync phase.
  plArrayPtr<const TimedStimulus> GetRecentStimuli() const { return m_RecentStimuli; }

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

  plAiFactionConfig m_FactionConfig;
};
