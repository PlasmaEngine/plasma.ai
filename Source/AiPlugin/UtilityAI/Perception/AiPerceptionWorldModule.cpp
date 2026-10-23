#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Perception/AiPerceptionWorldModule.h>

// clang-format off
PL_IMPLEMENT_WORLD_MODULE(plAiPerceptionWorldModule);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiPerceptionWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiPerceptionWorldModule::plAiPerceptionWorldModule(plWorld* pWorld)
  : plWorldModule(pWorld)
{
}

plAiPerceptionWorldModule::~plAiPerceptionWorldModule() = default;

void plAiPerceptionWorldModule::Initialize()
{
  SUPER::Initialize();

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiPerceptionWorldModule::UpdateDrainStimuli, this);
    updateDesc.m_Phase = plWorldUpdatePhase::PreAsync;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;
    RegisterUpdateFunction(updateDesc);
  }

  ReloadFactionConfig();
}

void plAiPerceptionWorldModule::ReloadFactionConfig()
{
  m_FactionConfig = plAiFactionConfig();
  m_FactionConfig.Load().IgnoreResult(); // missing config file is fine, defaults apply
}

void plAiPerceptionWorldModule::PostStimulus(const plAiStimulus& stimulus)
{
  PL_LOCK(m_QueueMutex);
  m_Queue.PushBack(stimulus);
}

void plAiPerceptionWorldModule::UpdateDrainStimuli(const UpdateContext& context)
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  // drop the oldest stimuli that every agent has processed, or that are too old to matter; the array is in publication
  // order, so they form its front
  plUInt32 uiDrop = 0;
  while (uiDrop < m_RecentStimuli.GetCount())
  {
    const TimedStimulus& stimulus = m_RecentStimuli[uiDrop];
    const plTime age = now - stimulus.m_Timestamp;
    const bool bProcessed = stimulus.m_uiSequence <= m_uiProcessedSequence && age > s_MinKeep;

    if (!bProcessed && age <= s_MaxKeep)
      break;

    ++uiDrop;
  }

  if (uiDrop > 0)
  {
    m_RecentStimuli.RemoveAtAndCopy(0, uiDrop);
  }

  PL_LOCK(m_QueueMutex);

  for (const plAiStimulus& stimulus : m_Queue)
  {
    auto& entry = m_RecentStimuli.ExpandAndGetRef();
    entry.m_Stimulus = stimulus;
    entry.m_Timestamp = now;
    entry.m_uiSequence = ++m_uiLatestSequence;
  }

  m_Queue.Clear();
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Perception_AiPerceptionWorldModule);
