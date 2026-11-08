#include <AiPlugin/Encounter/Components/EncounterDirectorComponent.h>
#include <AiPlugin/Encounter/Components/EncounterWaveComponent.h>
#include <AiPlugin/Encounter/EncounterTypes.h>
#include <AiPlugin/Tactical/SquadWorldModule.h>
#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionWorldModule.h>
#include <Core/World/ComponentManager.h>
#include <Foundation/Strings/StringBuilder.h>
#include <Tests/AiTestCheck.h>
#include <Tests/AiTestWorld.h>

using plAiTestSpawnerComponentManager = plComponentManager<class plAiTestSpawnerComponent, plBlockStorageType::Compact>;
using plAiTestMessageSpawnerComponentManager = plComponentManager<class plAiTestMessageSpawnerComponent, plBlockStorageType::Compact>;

/// \brief A spawner like a game's spawn controller: Activate() creates Count squad agents.
class plAiTestSpawnerComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiTestSpawnerComponent, plComponent, plAiTestSpawnerComponentManager);

public:
  void Activate();

  plHashedString m_sSquadId;
  plUInt32 m_uiCount = 2;
  plUInt32 m_uiActivations = 0;
  plDynamicArray<plGameObjectHandle> m_Spawned;
};

/// \brief A spawner that handles the director's message, and also has an Activate() that must then not be called.
class plAiTestMessageSpawnerComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiTestMessageSpawnerComponent, plComponent, plAiTestMessageSpawnerComponentManager);

public:
  void Activate() { ++m_uiActivations; }
  void OnWave(plMsgAiEncounterWave& ref_msg);

  plUInt32 m_uiReleased = 0;
  plUInt32 m_uiHeld = 0;
  plUInt32 m_uiActivations = 0;
};

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiTestSpawnerComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("SquadId", m_sSquadId),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(Activate),
  }
  PL_END_FUNCTIONS;
}
PL_END_COMPONENT_TYPE

PL_BEGIN_COMPONENT_TYPE(plAiTestMessageSpawnerComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgAiEncounterWave, OnWave),
  }
  PL_END_MESSAGEHANDLERS;

  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(Activate),
  }
  PL_END_FUNCTIONS;
}
PL_END_COMPONENT_TYPE
// clang-format on

void plAiTestSpawnerComponent::Activate()
{
  ++m_uiActivations;

  for (plUInt32 i = 0; i < m_uiCount; ++i)
  {
    plGameObjectDesc desc;
    desc.m_LocalPosition = GetOwner()->GetGlobalPosition() + plVec3(static_cast<float>(i), 0, 0);

    plGameObject* pObject = nullptr;
    m_Spawned.PushBack(GetWorld()->CreateObject(desc, pObject));

    plAiAgentComponent* pAgent = nullptr;
    plAiAgentComponent::CreateComponent(pObject, pAgent);
    pAgent->m_sSquadId = m_sSquadId;
  }
}

void plAiTestMessageSpawnerComponent::OnWave(plMsgAiEncounterWave& ref_msg)
{
  if (ref_msg.m_Event == plMsgAiEncounterWave::Event::Released)
    ++m_uiReleased;
  else
    ++m_uiHeld;
}

namespace
{
  plStringBuilder MakeReference(const plGameObjectHandle& hObject)
  {
    plStringBuilder sReference;
    sReference.SetFormat("#!GGOR-{}", static_cast<plInt64>(hObject.GetInternalID().m_Data));
    return sReference;
  }

  plAiEncounterDirectorComponent* CreateDirector(plAiTestWorld& ref_world, plGameObjectHandle& out_hDirector)
  {
    plGameObject* pObject = ref_world.CreateObject("Encounter", plVec3::MakeZero(), &out_hDirector);

    PL_LOCK(ref_world.GetWorld().GetWriteMarker());

    plAiEncounterDirectorComponent* pDirector = nullptr;
    plAiEncounterDirectorComponent::CreateComponent(pObject, pDirector);
    pDirector->m_Intensity.m_fSmoothingSeconds = 0.0f;
    return pDirector;
  }

  /// A wave object below the director; the wave is its own spawner.
  plAiEncounterWaveComponent* CreateWave(plAiTestWorld& ref_world, const plGameObjectHandle& hDirector, const char* szName, plAiEncounterTrigger::Enum trigger, plGameObjectHandle& out_hWave)
  {
    plGameObject* pObject = ref_world.CreateObject(szName, plVec3::MakeZero(), &out_hWave);

    PL_LOCK(ref_world.GetWorld().GetWriteMarker());
    pObject->SetParent(hDirector);

    plAiEncounterWaveComponent* pWave = nullptr;
    plAiEncounterWaveComponent::CreateComponent(pObject, pWave);
    pWave->m_Trigger = trigger;
    return pWave;
  }

  plAiTestSpawnerComponent* AddSpawner(plAiTestWorld& ref_world, const plGameObjectHandle& hObject, const char* szSquad)
  {
    PL_LOCK(ref_world.GetWorld().GetWriteMarker());

    plGameObject* pObject = nullptr;
    ref_world.GetWorld().TryGetObject(hObject, pObject);

    plAiTestSpawnerComponent* pSpawner = nullptr;
    plAiTestSpawnerComponent::CreateComponent(pObject, pSpawner);
    pSpawner->m_sSquadId.Assign(szSquad);
    return pSpawner;
  }

  void DeleteSpawned(plAiTestWorld& ref_world, plAiTestSpawnerComponent& ref_spawner)
  {
    PL_LOCK(ref_world.GetWorld().GetWriteMarker());

    for (const plGameObjectHandle& hObject : ref_spawner.m_Spawned)
      ref_world.GetWorld().DeleteObjectNow(hObject);

    ref_spawner.m_Spawned.Clear();
  }

  plAiEncounterWaveState::Enum GetWaveState(plAiTestWorld& ref_world, const plAiEncounterDirectorComponent& director, plUInt32 uiWave)
  {
    PL_LOCK(ref_world.GetWorld().GetReadMarker());

    plDynamicArray<plAiEncounterDirectorComponent::WaveStatus> waves;
    director.GetWaves(waves);
    return uiWave < waves.GetCount() ? static_cast<plAiEncounterWaveState::Enum>(waves[uiWave].m_State.GetValue()) : plAiEncounterWaveState::Waiting;
  }

  void TestPacingCurve()
  {
    Check(plAiEvaluatePacing({}, 10.0f) == 1.0f, "E: without phases the pacing never holds a wave");

    plAiEncounterPhase phases[2];
    phases[0].m_sName = "Build-up";
    phases[0].m_fDuration = 10.0f;
    phases[0].m_fTarget = 0.8f;
    phases[1].m_sName = "Relax";
    phases[1].m_fDuration = 10.0f;
    phases[1].m_fTarget = 0.4f;

    plUInt32 uiPhase = plInvalidIndex;
    Check(plMath::IsEqual(plAiEvaluatePacing(phases, 5.0f, &uiPhase), 0.4f, 0.001f) && uiPhase == 0, "E: the first phase rises from 0 to its target");
    Check(plMath::IsEqual(plAiEvaluatePacing(phases, 15.0f, &uiPhase), 0.6f, 0.001f) && uiPhase == 1, "E: a phase goes from the previous target to its own");
    Check(plMath::IsEqual(plAiEvaluatePacing(phases, 40.0f, &uiPhase), 0.4f, 0.001f) && uiPhase == 1, "E: past the last phase its target stays");
  }

  void TestIntensityParts()
  {
    plAiEncounterIntensitySettings settings;
    settings.m_fSmoothingSeconds = 0.0f;

    plAiEncounterIntensity intensity;
    intensity.Update(settings, 0.1f, 3, 0.0f, 0.0f);
    Check(plMath::IsEqual(intensity.GetPart(plAiEncounterIntensity::Engaged), 0.5f, 0.001f), "E: 3 of 6 engaged agents are half the engaged part");
    Check(plMath::IsEqual(intensity.GetIntensity(), 0.25f, 0.001f), "E: the intensity is the weighted sum of its parts");

    intensity.Update(settings, 0.1f, 0, 0.0f, 50.0f);
    Check(plMath::IsEqual(intensity.GetPart(plAiEncounterIntensity::Damage), 1.0f, 0.001f), "E: damage within the window counts fully at first");

    intensity.Update(settings, 4.0f, 0, 0.0f, 0.0f);
    Check(plMath::IsEqual(intensity.GetPart(plAiEncounterIntensity::Damage), plMath::Exp(-1.0f), 0.01f), "E: damage falls off over the window");

    settings.m_fSmoothingSeconds = 4.0f;
    plAiEncounterIntensity smoothed;
    smoothed.Update(settings, 0.1f, 6, 0.0f, 0.0f);
    Check(smoothed.GetIntensity() > 0.0f && smoothed.GetIntensity() < 0.05f, "E: smoothing makes the intensity follow the measurement slowly");
  }

  void TestWavesReleaseAndClear()
  {
    plAiTestWorld world("AiTest E waves");

    plGameObjectHandle hDirector, hFirst, hSecond;
    plAiEncounterDirectorComponent* pDirector = CreateDirector(world, hDirector);
    CreateWave(world, hDirector, "First", plAiEncounterTrigger::EncounterStart, hFirst);
    plAiEncounterWaveComponent* pSecond = CreateWave(world, hDirector, "Second", plAiEncounterTrigger::WaveCleared, hSecond);

    {
      PL_LOCK(world.GetWorld().GetWriteMarker());
      pSecond->SetAfterWaveReference(MakeReference(hFirst));
    }

    plAiTestSpawnerComponent* pFirstSpawner = AddSpawner(world, hFirst, "SquadFirst");
    plAiTestSpawnerComponent* pSecondSpawner = AddSpawner(world, hSecond, "SquadSecond");

    world.Step(10);
    Check(pFirstSpawner->m_uiActivations == 1, "E: a wave triggered by the encounter start calls its spawner's Activate()");
    Check(pSecondSpawner->m_uiActivations == 0, "E: a wave waiting for another one isn't released");
    Check(GetWaveState(world, *pDirector, 0) == plAiEncounterWaveState::Released, "E: the first wave is released");

    {
      const plWorld& readWorld = world.GetWorld();
      PL_LOCK(readWorld.GetReadMarker());
      Check(readWorld.GetModule<plAiSquadWorldModule>()->GetSquadSize(plTempHashedString("SquadFirst")) == 2, "E: the squad module counts the wave's agents");
    }

    // the wave's squad id comes from the spawner's SquadId property; once its agents are gone (plus the grace time), it is cleared
    world.Step(30);
    DeleteSpawned(world, *pFirstSpawner);
    world.Step(60);
    Check(GetWaveState(world, *pDirector, 0) == plAiEncounterWaveState::Released, "E: a wave isn't cleared the moment its agents are gone");

    world.Step(120);
    Check(GetWaveState(world, *pDirector, 0) == plAiEncounterWaveState::Cleared, "E: a wave whose agents stay gone is cleared");
    Check(pSecondSpawner->m_uiActivations == 1, "E: the wave after a cleared wave is released");

    world.Step(30);
    DeleteSpawned(world, *pSecondSpawner);
    world.Step(180);
    Check(pDirector->IsFinished(), "E: the encounter finishes when every wave is cleared");
  }

  void TestWaveGivesOrder()
  {
    plAiTestWorld world("AiTest A3 wave order");

    plGameObjectHandle hDirector, hWave;
    CreateDirector(world, hDirector);
    plAiEncounterWaveComponent* pWave = CreateWave(world, hDirector, "Bridge", plAiEncounterTrigger::EncounterStart, hWave);

    {
      PL_LOCK(world.GetWorld().GetWriteMarker());

      plGameObject* pObject = nullptr;
      world.GetWorld().TryGetObject(hWave, pObject);
      pObject->SetLocalPosition(plVec3(10, 5, 0));

      pWave->m_OrderIntent = plAiSquadIntent::Hold;
      pWave->m_fOrderDuration = 30.0f;
      pWave->m_fOrderRadius = 8.0f;
      pWave->m_bOrderNoRetreat = true;
    }

    AddSpawner(world, hWave, "SquadBridge");

    world.Step(20); // released, spawned, registered

    plAiSquadOrder order;
    plTime remaining;
    bool bGot = false;

    {
      const plWorld& readWorld = world.GetWorld();
      PL_LOCK(readWorld.GetReadMarker());
      bGot = readWorld.GetModule<plAiSquadWorldModule>()->GetOrder(plTempHashedString("SquadBridge"), order, remaining);
    }

    Check(bGot && order.m_Intent == plAiSquadIntent::Hold, "A3: a wave with an order gives it to the squad it spawns");
    Check(order.m_bHasRegion && order.m_vRegionMin.IsEqual(plVec2(2, -3), 0.01f) && order.m_vRegionMax.IsEqual(plVec2(18, 13), 0.01f), "A3: the order's region is a square around the wave's object");
    Check(order.m_bNoRetreat && order.m_sIssuer == "Encounter: Bridge" && remaining.GetSeconds() > 28.0, "A3: the order names the wave, keeps its settings and counts from the release");
  }

  void TestHoldWhileIntense()
  {
    plAiTestWorld world("AiTest E hold");

    plGameObjectHandle hDirector, hWave;
    plAiEncounterDirectorComponent* pDirector = CreateDirector(world, hDirector);
    plAiEncounterWaveComponent* pWave = CreateWave(world, hDirector, "Late", plAiEncounterTrigger::Time, hWave);

    plAiTestMessageSpawnerComponent* pSpawner = nullptr;

    {
      PL_LOCK(world.GetWorld().GetWriteMarker());

      // the pacing stays at 0: any intensity above the margin holds the wave
      plAiEncounterPhase& phase = pDirector->m_Pacing.ExpandAndGetRef();
      phase.m_fDuration = 100.0f;
      phase.m_fTarget = 0.0f;
      pDirector->m_Intensity.m_fDamageWeight = 1.0f;

      pWave->m_fTime = 0.5f;
      pWave->m_fMaxHold = 2.0f;

      plGameObject* pObject = nullptr;
      world.GetWorld().TryGetObject(hWave, pObject);
      plAiTestMessageSpawnerComponent::CreateComponent(pObject, pSpawner);
    }

    auto StepWithDamage = [&](plUInt32 uiFrames) {
      for (plUInt32 i = 0; i < uiFrames; ++i)
      {
        {
          PL_LOCK(world.GetWorld().GetWriteMarker());
          pDirector->ReportPlayerDamage(10.0f);
        }

        world.Step(1);
      }
    };

    StepWithDamage(60);
    Check(GetWaveState(world, *pDirector, 0) == plAiEncounterWaveState::Held, "E: a due wave is held while the intensity is above the pacing");
    Check(pSpawner->m_uiHeld == 1 && pSpawner->m_uiReleased == 0, "E: the spawner is told once that its wave is held");

    StepWithDamage(120);
    Check(GetWaveState(world, *pDirector, 0) == plAiEncounterWaveState::Released, "E: a held wave is released after its maximum hold");
    Check(pSpawner->m_uiReleased == 1, "E: a spawner that handles the message gets the release");
    Check(pSpawner->m_uiActivations == 0, "E: the release function isn't called when the message was handled");
  }

  void TestCalmTrigger()
  {
    plAiTestWorld world("AiTest E calm");

    plGameObjectHandle hDirector, hWave;
    plAiEncounterDirectorComponent* pDirector = CreateDirector(world, hDirector);
    plAiEncounterWaveComponent* pWave = CreateWave(world, hDirector, "Calm", plAiEncounterTrigger::Calm, hWave);
    plAiTestSpawnerComponent* pSpawner = AddSpawner(world, hWave, "SquadCalm");

    {
      PL_LOCK(world.GetWorld().GetWriteMarker());
      pWave->m_fCalmBelow = 0.4f;
      pWave->m_fCalmFor = 1.0f;
      pDirector->m_Intensity.m_fStimulusWeight = 1.0f;
      pDirector->m_Intensity.m_fWindowSeconds = 1.0f;
      pDirector->m_sPlayerTeam.Assign("Player");
    }

    world.Step(1);

    // player gunfire keeps the fight intense; posted as the player team
    for (plUInt32 i = 0; i < 90; ++i)
    {
      {
        PL_LOCK(world.GetWorld().GetWriteMarker());

        plAiStimulus stimulus;
        stimulus.m_Type = plAiStimulusType::Sound;
        stimulus.m_fStrength = 1.0f;
        stimulus.m_sSourceTeam.Assign("Player");
        world.GetWorld().GetModule<plAiPerceptionWorldModule>()->PostStimulus(stimulus);
      }

      world.Step(1);
    }

    Check(pSpawner->m_uiActivations == 0, "E: a calm trigger doesn't fire during a fight");
    Check(pDirector->GetIntensityMeasurement().GetPart(plAiEncounterIntensity::Stimuli) > 0.5f, "E: player-team stimuli count towards the intensity");

    world.Step(60 * 6);
    Check(pSpawner->m_uiActivations == 1, "E: a calm trigger fires once the intensity stayed low long enough");
  }
} // namespace

void RunEncounterTests()
{
  TestPacingCurve();
  TestIntensityParts();
  TestWavesReleaseAndClear();
  TestWaveGivesOrder();
  TestHoldWhileIntense();
  TestCalmTrigger();
}
