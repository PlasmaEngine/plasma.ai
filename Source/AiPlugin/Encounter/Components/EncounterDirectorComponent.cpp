#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Encounter/Components/EncounterDirectorComponent.h>
#include <AiPlugin/Encounter/Components/EncounterWaveComponent.h>
#include <AiPlugin/Tactical/SquadWorldModule.h>
#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>
#include <AiPlugin/UtilityAI/Decision/AiScoring.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Reflection/ReflectionUtils.h>
#include <GameEngine/Gameplay/SpawnComponent.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiEncounterDirectorComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("StartOnSimulation", m_bStartOnSimulation)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_ENUM_MEMBER_PROPERTY("PacingBy", plAiEncounterPacingBy, m_PacingBy),
    PL_ARRAY_MEMBER_PROPERTY("Pacing", m_Pacing),
    PL_MEMBER_PROPERTY("HoldMargin", m_fHoldMargin)->AddAttributes(new plDefaultValueAttribute(0.1f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("PlayerTeam", m_sPlayerTeam),
    PL_ACCESSOR_PROPERTY("EngagedWeight", GetEngagedWeight, SetEngagedWeight)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.0f, 1.0f), new plGroupAttribute("Intensity")),
    PL_ACCESSOR_PROPERTY("StimulusWeight", GetStimulusWeight, SetStimulusWeight)->AddAttributes(new plDefaultValueAttribute(0.3f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_ACCESSOR_PROPERTY("DamageWeight", GetDamageWeight, SetDamageWeight)->AddAttributes(new plDefaultValueAttribute(0.2f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_ACCESSOR_PROPERTY("EngagedForFull", GetEngagedForFull, SetEngagedForFull)->AddAttributes(new plDefaultValueAttribute(6.0f), new plClampValueAttribute(1.0f, plVariant())),
    PL_ACCESSOR_PROPERTY("StimuliForFull", GetStimuliForFull, SetStimuliForFull)->AddAttributes(new plDefaultValueAttribute(12.0f), new plClampValueAttribute(0.01f, plVariant())),
    PL_ACCESSOR_PROPERTY("DamageForFull", GetDamageForFull, SetDamageForFull)->AddAttributes(new plDefaultValueAttribute(50.0f), new plClampValueAttribute(0.01f, plVariant())),
    PL_ACCESSOR_PROPERTY("Window", GetWindow, SetWindow)->AddAttributes(new plDefaultValueAttribute(4.0f), new plClampValueAttribute(0.1f, plVariant()), new plSuffixAttribute(" s")),
    PL_ACCESSOR_PROPERTY("Smoothing", GetSmoothing, SetSmoothing)->AddAttributes(new plDefaultValueAttribute(4.0f), new plClampValueAttribute(0.0f, plVariant()), new plSuffixAttribute(" s")),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(StartEncounter),
    PL_SCRIPT_FUNCTION_PROPERTY(ResetEncounter),
    PL_SCRIPT_FUNCTION_PROPERTY(TriggerWave, In, "Wave"),
    PL_SCRIPT_FUNCTION_PROPERTY(ReportPlayerDamage, In, "Damage"),
    PL_SCRIPT_FUNCTION_PROPERTY(GetIntensity),
    PL_SCRIPT_FUNCTION_PROPERTY(GetPacingTarget),
    PL_SCRIPT_FUNCTION_PROPERTY(IsFinished),
  }
  PL_END_FUNCTIONS;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Encounter"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

namespace
{
  constexpr float s_fTickSeconds = 0.1f;

  /// A wave whose agents have all been gone this long is cleared (a spawner can be between two spawns).
  constexpr plTime s_ClearedGrace = plTime::MakeFromSeconds(2.0);

  /// A wave that shows no living agent this long after its release is treated as cleared.
  constexpr plTime s_EmptyWaveTimeout = plTime::MakeFromSeconds(30.0);

  const plAbstractFunctionProperty* FindFunction(const plRTTI* pType, plStringView sName)
  {
    for (; pType != nullptr; pType = pType->GetParentType())
    {
      for (const plAbstractFunctionProperty* pFunction : pType->GetFunctions())
      {
        if (sName == pFunction->GetPropertyName())
          return pFunction;
      }
    }

    return nullptr;
  }

  /// Calls the named function on the first component of the object that has one whose arguments all have standard types.
  /// Arguments get their type's default value. False when no component has such a function.
  bool CallFunction(plGameObject* pObject, plStringView sName, plVariant& out_result)
  {
    if (sName.IsEmpty())
      return false;

    for (plComponent* pComponent : pObject->GetComponents())
    {
      const plAbstractFunctionProperty* pFunction = FindFunction(pComponent->GetDynamicRTTI(), sName);
      if (pFunction == nullptr)
        continue;

      plHybridArray<plVariant, 4> arguments;
      bool bSupported = true;

      for (plUInt32 i = 0; i < pFunction->GetArgumentCount(); ++i)
      {
        plVariant& argument = arguments.ExpandAndGetRef();
        argument = plReflectionUtils::GetDefaultVariantFromType(pFunction->GetArgumentType(i));
        bSupported = bSupported && argument.IsValid();
      }

      if (!bSupported)
        continue;

      pFunction->Execute(pComponent, arguments, out_result);
      return true;
    }

    return false;
  }

  /// The first non-empty "SquadId" member property on the object's components (a wave on the spawner object has one too).
  plHashedString ReadSpawnerSquadId(const plGameObject* pObject)
  {
    for (const plComponent* pComponent : pObject->GetComponents())
    {
      const plAbstractProperty* pProperty = pComponent->GetDynamicRTTI()->FindPropertyByName("SquadId");
      if (pProperty == nullptr || pProperty->GetCategory() != plPropertyCategory::Member)
        continue;

      const plVariant value = plReflectionUtils::GetMemberPropertyValue(static_cast<const plAbstractMemberProperty*>(pProperty), pComponent);

      plHashedString sSquad;
      if (value.IsA<plHashedString>())
        sSquad = value.Get<plHashedString>();
      else if (value.IsA<plString>())
        sSquad.Assign(value.Get<plString>());

      if (!sSquad.IsEmpty())
        return sSquad;
    }

    return {};
  }

  void CollectWavesInSubtree(const plGameObject* pObject, plDynamicArray<const plAiEncounterWaveComponent*>& out_waves)
  {
    for (auto it = pObject->GetChildren(); it.IsValid(); ++it)
    {
      const plAiEncounterWaveComponent* pWave = nullptr;
      if (it->TryGetComponentOfBaseType(pWave) && pWave->IsActive())
        out_waves.PushBack(pWave);

      CollectWavesInSubtree(&(*it), out_waves);
    }
  }

  plHashedString GetTeamOf(const plGameObject* pObject)
  {
    const plAiAgentComponent* pAgent = nullptr;
    if (pObject->TryGetComponentOfBaseType(pAgent))
      return pAgent->GetResolvedTeam();

    return {};
  }
} // namespace

plAiEncounterDirectorComponent::plAiEncounterDirectorComponent() = default;
plAiEncounterDirectorComponent::~plAiEncounterDirectorComponent() = default;

void plAiEncounterDirectorComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s.WriteArray(m_Pacing).AssertSuccess();
  s << m_bStartOnSimulation;
  s << m_PacingBy;
  s << m_fHoldMargin;
  s << m_sPlayerTeam;
  s << m_Intensity.m_fEngagedWeight;
  s << m_Intensity.m_fStimulusWeight;
  s << m_Intensity.m_fDamageWeight;
  s << m_Intensity.m_fEngagedForFull;
  s << m_Intensity.m_fStimuliForFull;
  s << m_Intensity.m_fDamageForFull;
  s << m_Intensity.m_fWindowSeconds;
  s << m_Intensity.m_fSmoothingSeconds;
}

void plAiEncounterDirectorComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s.ReadArray(m_Pacing).AssertSuccess();
  s >> m_bStartOnSimulation;
  s >> m_PacingBy;
  s >> m_fHoldMargin;
  s >> m_sPlayerTeam;
  s >> m_Intensity.m_fEngagedWeight;
  s >> m_Intensity.m_fStimulusWeight;
  s >> m_Intensity.m_fDamageWeight;
  s >> m_Intensity.m_fEngagedForFull;
  s >> m_Intensity.m_fStimuliForFull;
  s >> m_Intensity.m_fDamageForFull;
  s >> m_Intensity.m_fWindowSeconds;
  s >> m_Intensity.m_fSmoothingSeconds;
}

void plAiEncounterDirectorComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  // created here, where creating modules is safe; the director reads stimuli and squads every tick
  GetWorld()->GetOrCreateModule<plAiPerceptionWorldModule>();
  GetWorld()->GetOrCreateModule<plAiSquadWorldModule>();

  ResetEncounter();

  if (m_bStartOnSimulation)
    StartEncounter();
}

void plAiEncounterDirectorComponent::StartEncounter()
{
  ResetEncounter();
  CollectWaves(m_Waves);

  if (const plAiPerceptionWorldModule* pPerception = GetWorld()->GetModule<plAiPerceptionWorldModule>())
    m_uiLastStimulusSequence = pPerception->GetLatestStimulusSequence();

  m_State = EncounterState::Running;

  // the first tick runs with the next update, once every spawner has started simulating (starting resets their state)
  m_fTickAccumulator = s_fTickSeconds;
}

void plAiEncounterDirectorComponent::ResetEncounter()
{
  m_State = EncounterState::NotStarted;
  m_Elapsed = plTime::MakeZero();
  m_fTickAccumulator = 0.0f;
  m_Waves.Clear();
  m_Measured.Reset();
  m_uiEngaged = 0;
  m_fPendingDamage = 0.0f;
}

void plAiEncounterDirectorComponent::TriggerWave(const char* szWave)
{
  const plTempHashedString sWave(szWave);

  for (WaveStatus& wave : m_Waves)
  {
    const plAiEncounterWaveComponent* pWave = nullptr;
    if (GetWorld()->TryGetComponent(wave.m_hWave, pWave) && pWave->GetOwner()->GetNameHashed() == sWave)
      wave.m_bScriptTriggered = true;
  }
}

void plAiEncounterDirectorComponent::ReportPlayerDamage(float fDamage)
{
  if (fDamage > 0.0f && plMath::IsFinite(fDamage))
    m_fPendingDamage += fDamage;
}

float plAiEncounterDirectorComponent::GetIntensity() const
{
  return m_Measured.GetIntensity();
}

float plAiEncounterDirectorComponent::GetPacingPosition() const
{
  if (m_PacingBy == plAiEncounterPacingBy::WavesCleared)
    return static_cast<float>(CountClearedWaves());

  return m_Elapsed.AsFloatInSeconds();
}

float plAiEncounterDirectorComponent::GetPacingTarget() const
{
  return plAiEvaluatePacing(m_Pacing, GetPacingPosition());
}

plUInt32 plAiEncounterDirectorComponent::GetCurrentPhase() const
{
  plUInt32 uiPhase = plInvalidIndex;
  plAiEvaluatePacing(m_Pacing, GetPacingPosition(), &uiPhase);
  return uiPhase;
}

bool plAiEncounterDirectorComponent::IsFinished() const
{
  return m_State == EncounterState::Finished;
}

void plAiEncounterDirectorComponent::GetWaves(plDynamicArray<WaveStatus>& out_waves) const
{
  if (m_State == EncounterState::NotStarted)
  {
    CollectWaves(out_waves);
    return;
  }

  out_waves = m_Waves;
}

const plComponent* plAiEncounterDirectorComponent::FindReleaseTarget(const plGameObject& spawner, plStringView sReleaseFunction, plStringBuilder& out_sPath)
{
  for (const plComponent* pComponent : spawner.GetComponents())
  {
    if (pComponent->GetDynamicRTTI()->CanHandleMessage<plMsgAiEncounterWave>())
    {
      out_sPath = "plMsgAiEncounterWave";
      return pComponent;
    }
  }

  for (const plComponent* pComponent : spawner.GetComponents())
  {
    if (!sReleaseFunction.IsEmpty() && FindFunction(pComponent->GetDynamicRTTI(), sReleaseFunction) != nullptr)
    {
      out_sPath.SetFormat("{}()", sReleaseFunction);
      return pComponent;
    }
  }

  const plSpawnComponent* pSpawn = nullptr;
  if (spawner.TryGetComponentOfBaseType(pSpawn))
  {
    out_sPath = "plSpawnComponent";
    return pSpawn;
  }

  out_sPath.Clear();
  return nullptr;
}

bool plAiEncounterDirectorComponent::HasFunction(const plGameObject& object, plStringView sName)
{
  for (const plComponent* pComponent : object.GetComponents())
  {
    if (FindFunction(pComponent->GetDynamicRTTI(), sName) != nullptr)
      return true;
  }

  return false;
}

void plAiEncounterDirectorComponent::Update()
{
  if (m_State != EncounterState::Running)
    return;

  m_fTickAccumulator += GetWorld()->GetClock().GetTimeDiff().AsFloatInSeconds();
  if (m_fTickAccumulator < s_fTickSeconds)
    return;

  const float fDeltaSeconds = m_fTickAccumulator;
  m_fTickAccumulator = 0.0f;
  m_Elapsed += plTime::MakeFromSeconds(fDeltaSeconds);

  Measure(fDeltaSeconds);

  const float fTarget = GetPacingTarget();
  bool bAllCleared = true;

  for (WaveStatus& wave : m_Waves)
  {
    const plAiEncounterWaveComponent* pWave = nullptr;

    // a wave deleted mid-encounter doesn't hold the encounter up
    if (!GetWorld()->TryGetComponent(wave.m_hWave, pWave))
      wave.m_State = plAiEncounterWaveState::Cleared;
    else
      UpdateWave(wave, *pWave, fTarget);

    bAllCleared = bAllCleared && wave.m_State == plAiEncounterWaveState::Cleared;
  }

  if (bAllCleared && !m_Waves.IsEmpty())
    m_State = EncounterState::Finished;
}

void plAiEncounterDirectorComponent::CollectWaves(plDynamicArray<WaveStatus>& out_waves) const
{
  plHybridArray<const plAiEncounterWaveComponent*, 16> waves;
  CollectWavesInSubtree(GetOwner(), waves);

  out_waves.Clear();
  out_waves.Reserve(waves.GetCount());

  for (const plAiEncounterWaveComponent* pWave : waves)
  {
    WaveStatus& status = out_waves.ExpandAndGetRef();
    status.m_hWave = pWave->GetHandle();
    status.m_hSpawner = pWave->GetSpawner();
    status.m_sSquad = pWave->m_sSquadId;
    status.m_CalmSince = plTime::MakeFromSeconds(-1.0);

    const plGameObject* pSpawner = nullptr;
    if (status.m_sSquad.IsEmpty() && GetWorld()->TryGetObject(status.m_hSpawner, pSpawner))
      status.m_sSquad = ReadSpawnerSquadId(pSpawner);
  }

  // the trigger's AfterWave names an object; find its wave
  for (plUInt32 i = 0; i < waves.GetCount(); ++i)
  {
    for (plUInt32 j = 0; j < waves.GetCount(); ++j)
    {
      if (waves[j]->GetOwner()->GetHandle() == waves[i]->GetAfterWave())
        out_waves[i].m_iAfterWave = static_cast<plInt32>(j);
    }
  }
}

void plAiEncounterDirectorComponent::Measure(float fDeltaSeconds)
{
  m_uiEngaged = CountEngagedAgents();
  const float fStimuli = CollectPlayerStimuli();

  m_Measured.Update(m_Intensity, fDeltaSeconds, m_uiEngaged, fStimuli, m_fPendingDamage);
  m_fPendingDamage = 0.0f;
}

plUInt32 plAiEncounterDirectorComponent::CountEngagedAgents() const
{
  const plAiBrainWorldModule* pBrain = GetWorld()->GetModule<plAiBrainWorldModule>();
  if (pBrain == nullptr)
    return 0;

  plDynamicArray<plAiAgentDebugSummary> agents;
  pBrain->GetAgentSummaries(agents);

  plUInt32 uiEngaged = 0;

  for (const plAiAgentDebugSummary& agent : agents)
  {
    if (agent.m_uiAlertLevel != plAiAlertLevel::Alert)
      continue;

    const plGameObject* pTarget = nullptr;
    if (GetWorld()->TryGetObject(agent.m_hTarget, pTarget) && GetTeamOf(pTarget) == m_sPlayerTeam)
      ++uiEngaged;
  }

  return uiEngaged;
}

float plAiEncounterDirectorComponent::CollectPlayerStimuli()
{
  const plAiPerceptionWorldModule* pPerception = GetWorld()->GetModule<plAiPerceptionWorldModule>();
  if (pPerception == nullptr)
    return 0.0f;

  float fStrength = 0.0f;

  for (const plAiPerceptionWorldModule::TimedStimulus& timed : pPerception->GetRecentStimuli())
  {
    if (timed.m_uiSequence <= m_uiLastStimulusSequence)
      continue;

    const plAiStimulus& stimulus = timed.m_Stimulus;
    const bool bCounts = stimulus.m_Type == plAiStimulusType::Sound || stimulus.m_Type == plAiStimulusType::Damage;

    // squad shares relay what an agent perceived; the original stimulus already counted
    if (bCounts && !stimulus.m_bReport && stimulus.m_sSourceTeam == m_sPlayerTeam)
      fStrength += stimulus.m_fStrength;
  }

  m_uiLastStimulusSequence = pPerception->GetLatestStimulusSequence();
  return fStrength;
}

void plAiEncounterDirectorComponent::UpdateWave(WaveStatus& ref_wave, const plAiEncounterWaveComponent& wave, float fTarget)
{
  switch (ref_wave.m_State)
  {
    case plAiEncounterWaveState::Waiting:
      if (HasTriggered(ref_wave, wave))
      {
        ref_wave.m_DueSince = m_Elapsed;
        UpdateDueWave(ref_wave, wave, fTarget);
      }
      break;

    case plAiEncounterWaveState::Held:
      UpdateDueWave(ref_wave, wave, fTarget);
      break;

    case plAiEncounterWaveState::Released:
      UpdateReleasedWave(ref_wave);
      break;

    default:
      break;
  }
}

bool plAiEncounterDirectorComponent::HasTriggered(WaveStatus& ref_wave, const plAiEncounterWaveComponent& wave) const
{
  if (ref_wave.m_bScriptTriggered)
    return true;

  const WaveStatus* pAfter = ref_wave.m_iAfterWave >= 0 ? &m_Waves[ref_wave.m_iAfterWave] : nullptr;

  switch (wave.m_Trigger)
  {
    case plAiEncounterTrigger::EncounterStart:
      return true;

    case plAiEncounterTrigger::WaveCleared:
      return pAfter != nullptr && pAfter->m_State == plAiEncounterWaveState::Cleared;

    case plAiEncounterTrigger::WaveDown:
      if (pAfter == nullptr)
        return false;

      // "down to N" only counts once the wave had agents; before that, zero alive means it hasn't spawned yet
      return pAfter->m_State == plAiEncounterWaveState::Cleared ||
             (pAfter->m_State == plAiEncounterWaveState::Released && pAfter->m_iPeakAlive > 0 && pAfter->m_iAlive <= static_cast<plInt32>(wave.m_uiAliveAtMost));

    case plAiEncounterTrigger::Time:
      return m_Elapsed.AsFloatInSeconds() >= wave.m_fTime;

    case plAiEncounterTrigger::Calm:
      if (m_Measured.GetIntensity() >= wave.m_fCalmBelow)
      {
        ref_wave.m_CalmSince = plTime::MakeFromSeconds(-1.0);
        return false;
      }

      if (ref_wave.m_CalmSince.IsNegative())
        ref_wave.m_CalmSince = m_Elapsed;

      return (m_Elapsed - ref_wave.m_CalmSince).AsFloatInSeconds() >= wave.m_fCalmFor;

    default:
      return false;
  }
}

void plAiEncounterDirectorComponent::UpdateDueWave(WaveStatus& ref_wave, const plAiEncounterWaveComponent& wave, float fTarget)
{
  bool bHold = wave.m_bHoldWhenIntense && m_Measured.GetIntensity() > fTarget + m_fHoldMargin;

  if (bHold && wave.m_fMaxHold > 0.0f && (m_Elapsed - ref_wave.m_DueSince).AsFloatInSeconds() >= wave.m_fMaxHold)
    bHold = false;

  if (!bHold)
  {
    Release(ref_wave, wave);
    return;
  }

  if (ref_wave.m_State == plAiEncounterWaveState::Held)
    return;

  ref_wave.m_State = plAiEncounterWaveState::Held;

  plGameObject* pSpawner = nullptr;
  if (GetWorld()->TryGetObject(ref_wave.m_hSpawner, pSpawner))
  {
    plMsgAiEncounterWave msg;
    msg.m_Event = plMsgAiEncounterWave::Event::Held;
    msg.m_sWave = wave.GetOwner()->GetNameHashed();
    pSpawner->SendMessage(msg);
  }
}

void plAiEncounterDirectorComponent::IssueWaveOrder(const WaveStatus& wave_status, const plAiEncounterWaveComponent& wave)
{
  if (wave.m_OrderIntent == plAiSquadIntent::None || wave_status.m_sSquad.IsEmpty())
    return;

  plAiSquadWorldModule* pSquads = GetWorld()->GetModule<plAiSquadWorldModule>();
  if (pSquads == nullptr)
    return;

  plAiSquadOrder order;
  order.m_Intent = wave.m_OrderIntent;
  order.m_Duration = plTime::MakeFromSeconds(wave.m_fOrderDuration);
  order.m_bNoRetreat = wave.m_bOrderNoRetreat;
  plStringBuilder sIssuer;
  sIssuer.SetFormat("Encounter: {}", wave.GetOwner()->GetName());
  order.m_sIssuer = sIssuer;

  if (wave.m_fOrderRadius > 0.0f)
  {
    const plVec2 vCenter = wave.GetOwner()->GetGlobalPosition().GetAsVec2();
    order.m_bHasRegion = true;
    order.m_vRegionMin = vCenter - plVec2(wave.m_fOrderRadius);
    order.m_vRegionMax = vCenter + plVec2(wave.m_fOrderRadius);
  }

  pSquads->IssueOrder(wave_status.m_sSquad, order);
}

void plAiEncounterDirectorComponent::Release(WaveStatus& ref_wave, const plAiEncounterWaveComponent& wave)
{
  ref_wave.m_State = plAiEncounterWaveState::Released;
  ref_wave.m_ReleasedAt = m_Elapsed;

  // the squad gets its order as soon as it forms
  IssueWaveOrder(ref_wave, wave);

  plGameObject* pSpawner = nullptr;
  if (!GetWorld()->TryGetObject(ref_wave.m_hSpawner, pSpawner))
  {
    plLog::Warning("Encounter '{}': wave '{}' has no spawner object.", GetOwner()->GetName(), wave.GetOwner()->GetName());
    return;
  }

  plMsgAiEncounterWave msg;
  msg.m_Event = plMsgAiEncounterWave::Event::Released;
  msg.m_sWave = wave.GetOwner()->GetNameHashed();

  if (pSpawner->SendMessage(msg))
  {
    ref_wave.m_szReleasedBy = "message";
    return;
  }

  plVariant result;
  if (CallFunction(pSpawner, wave.m_sReleaseFunction, result))
  {
    ref_wave.m_szReleasedBy = "function";
    return;
  }

  plSpawnComponent* pSpawn = nullptr;
  if (pSpawner->TryGetComponentOfBaseType(pSpawn))
  {
    pSpawn->TriggerManualSpawn(true);
    ref_wave.m_szReleasedBy = "plSpawnComponent";
    return;
  }

  plLog::Warning("Encounter '{}': nothing on spawner '{}' starts wave '{}' (no handler for plMsgAiEncounterWave, no function '{}', no plSpawnComponent).",
    GetOwner()->GetName(), pSpawner->GetName(), wave.GetOwner()->GetName(), wave.m_sReleaseFunction);
}

void plAiEncounterDirectorComponent::UpdateReleasedWave(WaveStatus& ref_wave)
{
  ref_wave.m_iAlive = CountAlive(ref_wave);
  ref_wave.m_iPeakAlive = plMath::Max(ref_wave.m_iPeakAlive, ref_wave.m_iAlive);

  plGameObject* pSpawner = nullptr;
  const bool bHasSpawner = GetWorld()->TryGetObject(ref_wave.m_hSpawner, pSpawner);

  // a spawner that knows when it is done decides
  plVariant cleared;
  if (bHasSpawner && CallFunction(pSpawner, "IsCleared", cleared))
  {
    if (cleared.IsA<bool>() && cleared.Get<bool>())
    {
      ref_wave.m_State = plAiEncounterWaveState::Cleared;
      ref_wave.m_ClearedAt = m_Elapsed;
    }

    return;
  }

  if (ref_wave.m_iAlive > 0)
  {
    ref_wave.m_EmptySince = plTime::MakeZero();
    return;
  }

  const bool bGoneLongEnough = ref_wave.m_iPeakAlive > 0 && ref_wave.m_EmptySince.IsPositive() && m_Elapsed - ref_wave.m_EmptySince >= s_ClearedGrace;
  const bool bNeverSpawned = ref_wave.m_iPeakAlive == 0 && m_Elapsed - ref_wave.m_ReleasedAt >= s_EmptyWaveTimeout;

  if (ref_wave.m_iPeakAlive > 0 && !ref_wave.m_EmptySince.IsPositive())
    ref_wave.m_EmptySince = m_Elapsed;

  if (bGoneLongEnough || bNeverSpawned)
  {
    ref_wave.m_State = plAiEncounterWaveState::Cleared;
    ref_wave.m_ClearedAt = m_Elapsed;
  }
}

plInt32 plAiEncounterDirectorComponent::CountAlive(const WaveStatus& wave)
{
  if (!wave.m_sSquad.IsEmpty())
  {
    const plAiSquadWorldModule* pSquads = GetWorld()->GetModule<plAiSquadWorldModule>();
    return pSquads != nullptr ? static_cast<plInt32>(pSquads->GetSquadSize(wave.m_sSquad)) : 0;
  }

  plGameObject* pSpawner = nullptr;
  plVariant alive;
  if (GetWorld()->TryGetObject(wave.m_hSpawner, pSpawner) && CallFunction(pSpawner, "GetAliveCount", alive) && alive.CanConvertTo<plInt32>())
    return alive.ConvertTo<plInt32>();

  return 0;
}

plUInt32 plAiEncounterDirectorComponent::CountClearedWaves() const
{
  plUInt32 uiCleared = 0;
  for (const WaveStatus& wave : m_Waves)
    uiCleared += wave.m_State == plAiEncounterWaveState::Cleared ? 1 : 0;

  return uiCleared;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Encounter_Components_EncounterDirectorComponent);
