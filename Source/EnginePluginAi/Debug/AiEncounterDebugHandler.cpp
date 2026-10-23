#include <EnginePluginAi/EnginePluginAiPCH.h>

#include <AiPlugin/Encounter/Components/EncounterDirectorComponent.h>
#include <AiPlugin/Encounter/Components/EncounterWaveComponent.h>
#include <AiPlugin/Encounter/EncounterDebugProtocol.h>
#include <EditorEngineProcessFramework/EngineProcess/EngineProcessDocumentContext.h>
#include <EnginePluginAi/Debug/AiDebugHandler.h>
#include <EnginePluginAi/Debug/AiEncounterDebugHandler.h>

namespace
{
  using WaveStatus = plAiEncounterDirectorComponent::WaveStatus;

  plString GetObjectName(const plWorld* pWorld, const plGameObjectHandle& hObject)
  {
    const plGameObject* pObject = nullptr;
    if (!pWorld->TryGetObject(hObject, pObject))
      return {};

    return pObject->GetName();
  }

  plString DescribeTrigger(const plAiEncounterWaveComponent& wave, const WaveStatus& status)
  {
    plStringBuilder sTrigger;

    switch (wave.m_Trigger)
    {
      case plAiEncounterTrigger::EncounterStart:
        sTrigger = "Encounter starts";
        break;

      case plAiEncounterTrigger::WaveCleared:
        if (status.m_iAfterWave < 0)
          sTrigger = "After a wave (not set)";
        else
          sTrigger.SetFormat("W{} cleared", status.m_iAfterWave + 1);
        break;

      case plAiEncounterTrigger::WaveDown:
        if (status.m_iAfterWave < 0)
          sTrigger = "After a wave (not set)";
        else
          sTrigger.SetFormat("W{} \xE2\x89\xA4 {} alive", status.m_iAfterWave + 1, wave.m_uiAliveAtMost);
        break;

      case plAiEncounterTrigger::Time:
      {
        const plUInt32 uiSeconds = static_cast<plUInt32>(wave.m_fTime + 0.5f);
        sTrigger.SetFormat("Time {}:{}", uiSeconds / 60, plArgU(uiSeconds % 60, 2, true));
        break;
      }

      case plAiEncounterTrigger::Calm:
        sTrigger.SetFormat("Intensity < {} for {} s", plArgF(wave.m_fCalmBelow, 2), plArgF(wave.m_fCalmFor, 0));
        break;

      default:
        sTrigger = "Script (TriggerWave)";
        break;
    }

    return sTrigger;
  }

  /// What keeps a wave from working as set up; empty when nothing does.
  plString FindProblem(const plWorld* pWorld, const plAiEncounterWaveComponent& wave, const WaveStatus& status, bool bReleasable)
  {
    const plGameObject* pSpawner = nullptr;
    if (!pWorld->TryGetObject(status.m_hSpawner, pSpawner))
      return "The spawner object is missing.";

    if (!bReleasable)
      return "Nothing on the spawner starts the wave: it needs a component that handles plMsgAiEncounterWave, the release function, or a plSpawnComponent.";

    const bool bNeedsAfterWave = wave.m_Trigger == plAiEncounterTrigger::WaveCleared || wave.m_Trigger == plAiEncounterTrigger::WaveDown;
    if (bNeedsAfterWave && status.m_iAfterWave < 0)
      return "AfterWave doesn't name a wave of this encounter.";

    if (status.m_sSquad.IsEmpty() && !plAiEncounterDirectorComponent::HasFunction(*pSpawner, "GetAliveCount"))
      return "No squad id: set SquadId on the wave or its spawner, or the director can't count the wave's agents.";

    return {};
  }

  void FillWave(plEngineProcessDocumentContext* pContext, const plWorld* pWorld, const WaveStatus& status, plAiEncounterDebugFrame::Wave& out_wave)
  {
    const plAiEncounterWaveComponent* pWave = nullptr;
    if (!pWorld->TryGetComponent(status.m_hWave, pWave))
      return;

    out_wave.m_Object = plAiDebugHandler::GetObjectId(pContext, pWave->GetOwner()->GetHandle());
    out_wave.m_sName = pWave->GetOwner()->GetName();
    out_wave.m_Spawner = plAiDebugHandler::GetObjectId(pContext, status.m_hSpawner);
    out_wave.m_sSpawner = GetObjectName(pWorld, status.m_hSpawner);
    out_wave.m_sSquad = status.m_sSquad.GetView();
    out_wave.m_sTrigger = DescribeTrigger(*pWave, status);
    out_wave.m_uiState = status.m_State.GetValue();
    out_wave.m_iAlive = status.m_iAlive;
    out_wave.m_iPeakAlive = status.m_iPeakAlive;
    out_wave.m_fDueSince = status.m_DueSince.GetSeconds();
    out_wave.m_fReleasedAt = status.m_ReleasedAt.GetSeconds();
    out_wave.m_fClearedAt = status.m_ClearedAt.GetSeconds();
    out_wave.m_bHoldWhenIntense = pWave->m_bHoldWhenIntense;
    out_wave.m_fMaxHold = pWave->m_fMaxHold;

    bool bReleasable = false;

    const plGameObject* pSpawner = nullptr;
    if (pWorld->TryGetObject(status.m_hSpawner, pSpawner))
    {
      plStringBuilder sPath;
      if (const plComponent* pTarget = plAiEncounterDirectorComponent::FindReleaseTarget(*pSpawner, pWave->m_sReleaseFunction, sPath))
      {
        out_wave.m_sSpawnerType = pTarget->GetDynamicRTTI()->GetTypeName();
        out_wave.m_sReleasePath = sPath;
        bReleasable = true;
      }
    }

    out_wave.m_sProblem = FindProblem(pWorld, *pWave, status, bReleasable);
  }

  plAiEncounterDirectorComponent* FindDirector(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plUuid& director, plAiEncounterDebugFrame& out_frame)
  {
    auto* pManager = pWorld->GetComponentManager<plAiEncounterDirectorComponentManager>();
    if (pManager == nullptr)
      return nullptr;

    plAiEncounterDirectorComponent* pShown = nullptr;

    for (auto it = pManager->GetComponents(); it.IsValid(); ++it)
    {
      if (!it->IsActive())
        continue;

      plAiEncounterDebugFrame::Director& entry = out_frame.m_Directors.ExpandAndGetRef();
      entry.m_Object = plAiDebugHandler::GetObjectId(pContext, it->GetOwner()->GetHandle());
      entry.m_sName = it->GetOwner()->GetName();

      if (pShown == nullptr || entry.m_Object == director)
        pShown = &(*it);
    }

    return pShown;
  }
} // namespace

void plAiEncounterDebugHandler::Startup()
{
  plEngineProcessDocumentContext::s_ToolConfigEvents.AddEventHandler(OnToolConfig);
}

void plAiEncounterDebugHandler::Shutdown()
{
  plEngineProcessDocumentContext::s_ToolConfigEvents.RemoveEventHandler(OnToolConfig);
}

void plAiEncounterDebugHandler::OnToolConfig(const plEngineProcessToolConfigEvent& e)
{
  if (e.m_pContext == nullptr || e.m_pMsg == nullptr || e.m_pMsg->m_sWhatToDo != plAiEncounterDebugRequest::s_szWhatToDo)
    return;

  if (!e.m_pMsg->m_PayloadValue.IsA<plDataBuffer>())
    return;

  plAiEncounterDebugRequest request;
  if (request.FromBuffer(e.m_pMsg->m_PayloadValue.Get<plDataBuffer>()).Failed())
    return;

  plAiEncounterDebugFrame frame;
  frame.m_uiRequestId = request.m_uiRequestId;

  if (plWorld* pWorld = e.m_pContext->GetWorld())
  {
    PL_LOCK(pWorld->GetWriteMarker());
    BuildFrame(e.m_pContext, pWorld, request, frame);
  }

  plSimpleDocumentConfigMsgToEditor msg;
  msg.m_DocumentGuid = e.m_pMsg->m_DocumentGuid;
  msg.m_sWhatToDo = plAiEncounterDebugFrame::s_szWhatToDo;

  plDataBuffer buffer;
  frame.ToBuffer(buffer);
  msg.m_PayloadValue = buffer;

  e.m_pContext->SendProcessMessage(&msg);
}

void plAiEncounterDebugHandler::BuildFrame(plEngineProcessDocumentContext* pContext, plWorld* pWorld, const plAiEncounterDebugRequest& request, plAiEncounterDebugFrame& out_frame)
{
  const plWorld* pReadWorld = pWorld;
  out_frame.m_bSimulating = pReadWorld->GetWorldSimulationEnabled();

  plAiEncounterDirectorComponent* pDirector = FindDirector(pContext, pWorld, request.m_Director, out_frame);
  if (pDirector == nullptr)
    return;

  if (request.m_bRestart && out_frame.m_bSimulating)
    pDirector->StartEncounter();

  out_frame.m_bHasDirector = true;
  out_frame.m_Director = plAiDebugHandler::GetObjectId(pContext, pDirector->GetOwner()->GetHandle());
  out_frame.m_sDirectorName = pDirector->GetOwner()->GetName();
  out_frame.m_uiState = static_cast<plUInt8>(pDirector->GetEncounterState());
  out_frame.m_fEncounterTime = pDirector->GetEncounterTime().GetSeconds();
  out_frame.m_fPacingPosition = pDirector->GetPacingPosition();
  out_frame.m_uiPacingBy = pDirector->m_PacingBy.GetValue();
  out_frame.m_uiPhase = pDirector->GetCurrentPhase();
  out_frame.m_fIntensity = pDirector->GetIntensity();
  out_frame.m_fTarget = pDirector->GetPacingTarget();
  out_frame.m_fHoldMargin = pDirector->m_fHoldMargin;
  out_frame.m_fSmoothing = pDirector->m_Intensity.m_fSmoothingSeconds;
  out_frame.m_uiEngaged = pDirector->GetEngagedAgents();

  const plAiEncounterIntensity& measured = pDirector->GetIntensityMeasurement();
  const plAiEncounterIntensitySettings& settings = pDirector->m_Intensity;
  const float weights[3] = {settings.m_fEngagedWeight, settings.m_fStimulusWeight, settings.m_fDamageWeight};

  for (plUInt32 i = 0; i < 3; ++i)
  {
    out_frame.m_fParts[i] = measured.GetPart(static_cast<plAiEncounterIntensity::Part>(i));
    out_frame.m_fWeights[i] = weights[i];
  }

  for (const plAiEncounterPhase& phase : pDirector->m_Pacing)
  {
    plAiEncounterDebugFrame::Phase& entry = out_frame.m_Phases.ExpandAndGetRef();
    entry.m_sName = phase.m_sName;
    entry.m_fDuration = phase.m_fDuration;
    entry.m_fTarget = phase.m_fTarget;
  }

  plDynamicArray<WaveStatus> waves;
  pDirector->GetWaves(waves);

  out_frame.m_Waves.SetCount(waves.GetCount());
  for (plUInt32 i = 0; i < waves.GetCount(); ++i)
    FillWave(pContext, pReadWorld, waves[i], out_frame.m_Waves[i]);
}
