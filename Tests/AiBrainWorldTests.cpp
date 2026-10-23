#include <AiPlugin/Tactical/Components/SmartObjectComponent.h>
#include <AiPlugin/Tactical/SmartObjects.h>
#include <AiPlugin/Tactical/SquadTypes.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Components/AiStimulusSourceComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiArchetypeResource.h>
#include <AiPlugin/UtilityAI/Decision/AiBehaviorLogic.h>
#include <AiPlugin/UtilityAI/Decision/AiBehaviorResource.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>
#include <AiPlugin/UtilityAI/Decision/AiDebugProtocol.h>
#include <AiPlugin/UtilityAI/Decision/AiInput.h>
#include <AiPlugin/UtilityAI/Decision/AiUtilityEvaluator.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionWorldModule.h>
#include <AiPlugin/Utils/AiCVars.h>
#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/IO/MemoryStream.h>
#include <GameEngine/Gameplay/BlackboardComponent.h>
#include <Tests/AiTestCheck.h>
#include <Tests/AiTestWorld.h>

/// \brief Behavior logic that records what the brain does with it, and can delete its own owner.
class plAiTestLogic : public plAiBehaviorLogic
{
  PL_ADD_DYNAMIC_REFLECTION(plAiTestLogic, plAiBehaviorLogic);

public:
  ~plAiTestLogic();

  virtual void Activate(plAiAgentContext& ref_context) override;
  virtual void Reactivate(plAiAgentContext& ref_context) override;
  virtual State Update(plAiAgentContext& ref_context) override;
  virtual void Deactivate(plAiAgentContext& ref_context, bool bInterrupted) override;

  plString m_sTag;
  bool m_bDeleteOwnerInUpdate = false;
  bool m_bDeleteOwnerInDeactivate = false;
  bool m_bFinishImmediately = false; ///< Update reports Done
  bool m_bAlwaysLock = false;        ///< Update reports Locked

private:
  void DeleteOwner(plAiAgentContext& ref_context);
};

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiTestLogic, 1, plRTTIDefaultAllocator<plAiTestLogic>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Tag", m_sTag),
    PL_MEMBER_PROPERTY("DeleteOwnerInUpdate", m_bDeleteOwnerInUpdate),
    PL_MEMBER_PROPERTY("DeleteOwnerInDeactivate", m_bDeleteOwnerInDeactivate),
    PL_MEMBER_PROPERTY("FinishImmediately", m_bFinishImmediately),
    PL_MEMBER_PROPERTY("AlwaysLock", m_bAlwaysLock),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

class plAiTestUseListenerComponent;
using plAiTestUseListenerComponentManager = plComponentManager<plAiTestUseListenerComponent, plBlockStorageType::Compact>;

/// \brief Counts the use messages its smart object receives.
class plAiTestUseListenerComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiTestUseListenerComponent, plComponent, plAiTestUseListenerComponentManager);

public:
  void OnMsgUse(plMsgAiSmartObjectUse& ref_msg) { ++(ref_msg.m_bStart ? s_uiStarts : s_uiEnds); }

  static plUInt32 s_uiStarts;
  static plUInt32 s_uiEnds;
};

plUInt32 plAiTestUseListenerComponent::s_uiStarts = 0;
plUInt32 plAiTestUseListenerComponent::s_uiEnds = 0;

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiTestUseListenerComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgAiSmartObjectUse, OnMsgUse),
  }
  PL_END_MESSAGEHANDLERS;
}
PL_END_COMPONENT_TYPE
// clang-format on

namespace
{
  /// What the test logic instances observed. Reset by each test.
  struct TestRecord
  {
    plHybridArray<plString, 16> m_Activations;
    plGameObjectHandle m_hLastTarget; ///< set by Activate and Reactivate
    plUInt32 m_uiReactivations = 0;
    const plAiTestLogic* m_pRunning = nullptr; // logic whose callback is on the stack
    plUInt32 m_uiFreedWhileRunning = 0;
    plUInt32 m_uiDestroyedInRunningWorld = 0; ///< logic destroyed while the world was usable
    plUInt32 m_uiDestroyedInShutdown = 0;     ///< logic destroyed while the brain shut down with its world
  };

  TestRecord s_Record;

  bool WasActivated(const char* szTag)
  {
    return s_Record.m_Activations.Contains(szTag);
  }

  const char* LastActivation()
  {
    return s_Record.m_Activations.IsEmpty() ? "" : s_Record.m_Activations.PeekBack().GetData();
  }
} // namespace

plAiTestLogic::~plAiTestLogic()
{
  if (s_Record.m_pRunning == this)
  {
    ++s_Record.m_uiFreedWhileRunning;
  }

  if (plAiBehaviorLogic::IsWorldShuttingDown())
    ++s_Record.m_uiDestroyedInShutdown;
  else
    ++s_Record.m_uiDestroyedInRunningWorld;
}

void plAiTestLogic::Activate(plAiAgentContext& ref_context)
{
  s_Record.m_Activations.PushBack(m_sTag);
  s_Record.m_hLastTarget = (ref_context.m_pTarget != nullptr) ? ref_context.m_pTarget->m_hTarget : plGameObjectHandle();
}

void plAiTestLogic::Reactivate(plAiAgentContext& ref_context)
{
  ++s_Record.m_uiReactivations;
  s_Record.m_hLastTarget = (ref_context.m_pTarget != nullptr) ? ref_context.m_pTarget->m_hTarget : plGameObjectHandle();
}

plAiBehaviorLogic::State plAiTestLogic::Update(plAiAgentContext& ref_context)
{
  if (m_bDeleteOwnerInUpdate)
  {
    DeleteOwner(ref_context);
  }

  if (m_bFinishImmediately)
    return State::Done;

  return m_bAlwaysLock ? State::Locked : State::Running;
}

void plAiTestLogic::Deactivate(plAiAgentContext& ref_context, bool /*bInterrupted*/)
{
  if (m_bDeleteOwnerInDeactivate)
  {
    DeleteOwner(ref_context);
  }
}

void plAiTestLogic::DeleteOwner(plAiAgentContext& ref_context)
{
  s_Record.m_pRunning = this;
  ref_context.m_pWorld->DeleteObjectNow(ref_context.m_pOwner->GetHandle());
  s_Record.m_pRunning = nullptr;
}

namespace
{
  template <typename T>
  plUniquePtr<T> MakeUnique()
  {
    return plUniquePtr<T>(PL_DEFAULT_NEW(T));
  }

  plAiBehaviorResourceDescriptor MakeBehavior(const char* szTag, plAiBehaviorCategory::Enum category, float fWeight)
  {
    plUniquePtr<plAiTestLogic> pLogic = MakeUnique<plAiTestLogic>();
    pLogic->m_sTag = szTag;

    plAiBehaviorResourceDescriptor desc;
    desc.m_sName.Assign(szTag);
    desc.m_Category = category;
    desc.m_fWeight = fWeight;
    desc.m_pLogic = std::move(pLogic);
    return desc;
  }

  plAiTestLogic& GetLogic(plAiBehaviorResourceDescriptor& ref_desc)
  {
    return *static_cast<plAiTestLogic*>(ref_desc.m_pLogic.Borrow());
  }

  void AddConsideration(plAiBehaviorResourceDescriptor& ref_desc, plUniquePtr<plAiInput>&& pInput)
  {
    plAiConsiderationDesc& consideration = ref_desc.m_Considerations.ExpandAndGetRef();
    consideration.m_pInput = std::move(pInput);
    consideration.m_fInputMin = 0.0f;
    consideration.m_fInputMax = 1.0f;
  }

  plAiBehaviorResourceHandle CreateBehavior(plStringView sResourceID, plAiBehaviorResourceDescriptor&& desc)
  {
    return plResourceManager::CreateResource<plAiBehaviorResource>(sResourceID, std::move(desc));
  }

  /// \param customize called with the descriptor before the resource is created
  template <typename Customize>
  plAiArchetypeResourceHandle CreateArchetype(plStringView sResourceID, std::initializer_list<plAiBehaviorResourceHandle> behaviors, Customize customize)
  {
    plAiArchetypeResourceDescriptor desc;

    for (const plAiBehaviorResourceHandle& hBehavior : behaviors)
    {
      desc.m_Behaviors.ExpandAndGetRef().m_hBehavior = hBehavior;
    }

    customize(desc);
    return plResourceManager::CreateResource<plAiArchetypeResource>(sResourceID, std::move(desc));
  }

  plAiArchetypeResourceHandle CreateArchetype(plStringView sResourceID, std::initializer_list<plAiBehaviorResourceHandle> behaviors)
  {
    return CreateArchetype(sResourceID, behaviors, [](plAiArchetypeResourceDescriptor&) {});
  }

  /// Creates an object with an agent component and, optionally, a local blackboard the agent picks up.
  plGameObjectHandle SpawnAgent(plAiTestWorld& ref_world, const char* szName, const plAiArchetypeResourceHandle& hArchetype, plSharedPtr<plBlackboard>* out_pBlackboard = nullptr, plComponentHandle* out_pAgent = nullptr)
  {
    plGameObjectHandle hObject;
    plGameObject* pObject = ref_world.CreateObject(szName, plVec3::MakeZero(), &hObject);

    PL_LOCK(ref_world.GetWorld().GetWriteMarker());

    plAiAgentComponent* pAgent = nullptr;
    const plComponentHandle hAgent = plAiAgentComponent::CreateComponent(pObject, pAgent);
    pAgent->m_hArchetype = hArchetype;

    if (out_pAgent != nullptr)
    {
      *out_pAgent = hAgent;
    }

    if (out_pBlackboard != nullptr)
    {
      plLocalBlackboardComponent* pBlackboard = nullptr;
      plLocalBlackboardComponent::CreateComponent(pObject, pBlackboard);
      *out_pBlackboard = pBlackboard->GetBoard();
    }

    return hObject;
  }

  bool ObjectExists(plAiTestWorld& ref_world, const plGameObjectHandle& hObject)
  {
    PL_LOCK(ref_world.GetWorld().GetReadMarker());
    plGameObject* pObject = nullptr;
    return ref_world.GetWorld().TryGetObject(hObject, pObject);
  }

  //////////////////////////////////////////////////////////////////////////

  void TestCategoryRule()
  {
    // Command at 4.99 with the default bonus of 0.2 must not hold off Combat at 5.05
    Check(plAiBehaviorScore::ShouldReplaceActive(5.05f, 4.99f, 0.2f), "R1: a higher category beats the commit bonus");
    Check(plAiBehaviorScore::ShouldReplaceActive(6.01f, 5.99f, 1.0f), "R1: Interrupt beats Combat at the maximum bonus");
    Check(!plAiBehaviorScore::ShouldReplaceActive(4.99f, 5.05f, 0.0f), "R1: a lower category never replaces");
    Check(!plAiBehaviorScore::ShouldReplaceActive(5.3f, 5.2f, 0.2f), "R1: the bonus still defends within a category");
    Check(plAiBehaviorScore::ShouldReplaceActive(5.5f, 5.2f, 0.2f), "R1: a clearly better same-category challenger wins");
    Check(plAiBehaviorScore::ShouldReplaceActive(2.1f, 0.0f, 0.2f), "R1: an active behavior that no longer scores gives way");
  }

  void TestCategoryBeatsCommitBonus()
  {
    s_Record = TestRecord();
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 1000.0f);

    plAiBehaviorResourceDescriptor command = MakeBehavior("Command", plAiBehaviorCategory::Command, 0.99f);

    // Combat scores 5.05 once the blackboard reports a threat, 0 before
    plAiBehaviorResourceDescriptor combat = MakeBehavior("Combat", plAiBehaviorCategory::Combat, 0.05f);
    plUniquePtr<plAiInput_BlackboardValue> pThreat = MakeUnique<plAiInput_BlackboardValue>();
    pThreat->SetEntryName("Threat");
    AddConsideration(combat, std::move(pThreat));

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/R1/Archetype",
      {CreateBehavior("AiTest/R1/Command", std::move(command)), CreateBehavior("AiTest/R1/Combat", std::move(combat))});

    plAiTestWorld world("AiTest R1");
    plSharedPtr<plBlackboard> pBlackboard;
    SpawnAgent(world, "Agent", hArchetype, &pBlackboard);

    world.Step(10);
    Check(plStringView(LastActivation()) == "Command", "R1: Command runs while there is no threat");

    pBlackboard->SetEntryValue("Threat", 1.0f);
    world.Step(10);
    Check(plStringView(LastActivation()) == "Combat", "R1: Combat at 5.05 replaces Command at 4.99 despite the commit bonus");
  }

  /// The brain, with the world locked for as long as the returned object lives.
  class LockedBrain
  {
  public:
    explicit LockedBrain(plAiTestWorld& ref_world)
      : m_Lock(ref_world.GetWorld().GetWriteMarker())
      , m_pBrain(ref_world.GetWorld().GetModule<plAiBrainWorldModule>())
    {
    }

    plAiBrainWorldModule* operator->() const { return m_pBrain; }

  private:
    plLock<plInternal::WorldData::WriteMarker> m_Lock;
    plAiBrainWorldModule* m_pBrain;
  };

  LockedBrain GetBrain(plAiTestWorld& ref_world)
  {
    return LockedBrain(ref_world);
  }

  const plAiBehaviorTrace* FindTrace(const plAiAgentDebugSnapshot& snapshot, const char* szName)
  {
    for (const plAiBehaviorTrace& trace : snapshot.m_Behaviors)
    {
      if (trace.m_sName == plTempHashedString(szName))
        return &trace;
    }

    return nullptr;
  }

  void TestDebugSnapshot()
  {
    s_Record = TestRecord();
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 1000.0f);

    plAiBehaviorResourceDescriptor command = MakeBehavior("Command", plAiBehaviorCategory::Command, 0.99f);
    plAiBehaviorResourceDescriptor combat = MakeBehavior("Combat", plAiBehaviorCategory::Combat, 0.05f);
    plUniquePtr<plAiInput_BlackboardValue> pThreat = MakeUnique<plAiInput_BlackboardValue>();
    pThreat->SetEntryName("Threat");
    AddConsideration(combat, std::move(pThreat));

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/F3/Archetype",
      {CreateBehavior("AiTest/F3/Command", std::move(command)), CreateBehavior("AiTest/F3/Combat", std::move(combat))});

    plAiTestWorld world("AiTest F3");
    plSharedPtr<plBlackboard> pBlackboard;
    plComponentHandle hAgent;
    SpawnAgent(world, "Agent", hArchetype, &pBlackboard, &hAgent);

    world.Step(2);

    plAiAgentDebugSnapshot snapshot;
    Check(!GetBrain(world)->GetAgentDebugSnapshot(hAgent, snapshot), "F3: no snapshot for an agent nobody watches");

    GetBrain(world)->SetAgentWatched(hAgent, true);
    world.Step(3);

    Check(GetBrain(world)->GetAgentDebugSnapshot(hAgent, snapshot), "F3: a watched agent has a snapshot");
    Check(snapshot.m_Outcome == plAiDecisionOutcome::Kept, "F3: the running Command behavior is kept");

    const plAiBehaviorTrace* pCombat = FindTrace(snapshot, "Combat");
    Check(pCombat != nullptr && pCombat->m_Veto == plAiBehaviorVeto::ZeroUtility, "F3: Combat is vetoed while there is no threat");
    Check(pCombat != nullptr && pCombat->m_Considerations.GetCount() == 1 && pCombat->m_Considerations[0].m_fRaw == 0.0f, "F3: the vetoing consideration reads 0");

    pBlackboard->SetEntryValue("Threat", 1.0f);
    world.Step(1);

    Check(GetBrain(world)->GetAgentDebugSnapshot(hAgent, snapshot), "F3: the snapshot follows the decision");
    Check(snapshot.m_Outcome == plAiDecisionOutcome::HigherCategory, "F3: the switch is explained as a higher category");

    pCombat = FindTrace(snapshot, "Combat");
    const plAiBehaviorTrace* pCommand = FindTrace(snapshot, "Command");
    Check(pCombat != nullptr && pCombat->m_bActive && plMath::IsEqual(pCombat->m_fScore, 5.05f, 0.001f), "F3: Combat is active at 5.05");
    Check(pCommand != nullptr && plMath::IsEqual(pCommand->m_fScore, 4.99f, 0.001f), "F3: Command scored 4.99");

    bool bThreatOnBoard = false;
    for (const auto& entry : snapshot.m_Blackboard)
    {
      bThreatOnBoard |= entry.m_sName == plTempHashedString("Threat");
    }
    Check(bThreatOnBoard, "F3: the snapshot carries the agent's blackboard");

    plDynamicArray<plAiBrainWorldModule::HistoryEntry> history;
    Check(GetBrain(world)->GetAgentHistory(hAgent, history) && history.GetCount() == 2, "F3: history holds the start and the switch");
    Check(!history.IsEmpty() && history.PeekBack().m_Outcome == plAiDecisionOutcome::HigherCategory && history.PeekBack().m_sTo == plTempHashedString("Combat"),
      "F3: the history entry records why Combat took over");
    Check(!history.IsEmpty() && history.PeekBack().m_uiToCategory == plAiBehaviorCategory::Combat, "F3: the history entry records the new behavior's category");

    plDynamicArray<plAiAgentDebugSummary> agents;
    GetBrain(world)->GetAgentSummaries(agents);
    Check(agents.GetCount() == 1 && agents[0].m_sActiveBehavior == plTempHashedString("Combat") && agents[0].m_bWatched, "F3: the agent list shows the active behavior");
    Check(agents.GetCount() == 1 && agents[0].m_uiActiveCategory == plAiBehaviorCategory::Combat, "F3: the agent list shows the active category");

    const plAiBrainFrameStats& stats = GetBrain(world)->GetFrameStats();
    Check(stats.m_uiAgents == 1 && stats.m_uiDecisionBudget == static_cast<plUInt32>(cvar_AiAgentsMaxDecisionsPerFrame.GetValue()), "F3: the frame stats count the agent and the budget");
    Check(stats.m_uiDecisions <= 1 && stats.m_uiWokenDecisions <= stats.m_uiDecisions && stats.m_UpdateTime.IsPositive(), "F3: the frame stats count the decisions and the update time");

    GetBrain(world)->SetAgentWatched(hAgent, false);
    Check(!GetBrain(world)->GetAgentDebugSnapshot(hAgent, snapshot), "F3: unwatching drops the snapshot");
  }

  /// The AI Debugger's messages survive the trip through a data buffer.
  void TestDebugProtocol()
  {
    plAiDebugRequest request;
    request.m_uiRequestId = 7;
    request.m_WatchedObject = plUuid(11, 22);
    request.m_bCaptureEqs = true;
    request.m_fHistorySince = 12.5;

    plDataBuffer buffer;
    request.ToBuffer(buffer);

    plAiDebugRequest requestRead;
    Check(requestRead.FromBuffer(buffer).Succeeded() && requestRead.m_uiRequestId == 7 && requestRead.m_WatchedObject == plUuid(11, 22) && requestRead.m_bCaptureEqs && requestRead.m_fHistorySince == 12.5,
      "Debug protocol: the poll round-trips");

    plAiDebugFrame frame;
    frame.m_uiRequestId = 7;
    frame.m_fWorldTime = 42.0;
    frame.m_bSimulating = true;
    frame.m_uiOverlays = PL_BIT(3);

    plAiDebugFrame::Agent& agent = frame.m_Agents.ExpandAndGetRef();
    agent.m_Object = plUuid(1, 2);
    agent.m_sName = "Grunt_01";
    agent.m_sActiveBehavior = "TakeCover";
    agent.m_uiActiveCategory = plAiBehaviorCategory::Combat;
    agent.m_History.ExpandAndGetRef().m_sTo = "TakeCover";

    frame.m_bHasWatched = true;
    frame.m_Watched.m_Object = plUuid(1, 2);
    frame.m_Watched.m_bHasDecision = true;
    plAiDebugFrame::Behavior& behavior = frame.m_Watched.m_Behaviors.ExpandAndGetRef();
    behavior.m_sName = "TakeCover";
    behavior.m_fUtility = 0.75f;
    behavior.m_Considerations.ExpandAndGetRef().m_fRaw = 3.0f;
    frame.m_Watched.m_Targets.ExpandAndGetRef().m_sName = "Player";
    frame.m_Watched.m_Blackboard.ExpandAndGetRef().m_sValue = "InCover";
    frame.m_Stats.m_uiAgents = 1;

    frame.ToBuffer(buffer);

    plAiDebugFrame frameRead;
    Check(frameRead.FromBuffer(buffer).Succeeded(), "Debug protocol: the frame reads back");
    Check(frameRead.m_fWorldTime == 42.0 && frameRead.m_bSimulating && frameRead.m_uiOverlays == PL_BIT(3) && frameRead.m_Stats.m_uiAgents == 1, "Debug protocol: the frame header round-trips");
    Check(frameRead.m_Agents.GetCount() == 1 && frameRead.m_Agents[0].m_sName == "Grunt_01" && frameRead.m_Agents[0].m_Object == plUuid(1, 2) && frameRead.m_Agents[0].m_History.GetCount() == 1,
      "Debug protocol: the agent list round-trips");
    Check(frameRead.m_bHasWatched && frameRead.m_Watched.m_Behaviors.GetCount() == 1 && frameRead.m_Watched.m_Behaviors[0].m_Considerations.GetCount() == 1 &&
            frameRead.m_Watched.m_Behaviors[0].m_Considerations[0].m_fRaw == 3.0f && frameRead.m_Watched.m_Targets.GetCount() == 1 && frameRead.m_Watched.m_Blackboard[0].m_sValue == "InCover",
      "Debug protocol: the watched agent round-trips");
  }

  void TestLogicDeletesOwner(const char* szName, bool bInUpdate)
  {
    s_Record = TestRecord();
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 1000.0f);

    const plStringBuilder sPrefix("AiTest/A1/", szName, "/");
    auto MakeID = [&](const char* szResource) { return plStringBuilder(sPrefix, szResource); };

    // the agent activates "Deleter"; in the Deactivate variant "Stopper" replaces it a few frames later
    plAiBehaviorResourceDescriptor deleter = MakeBehavior("Deleter", plAiBehaviorCategory::Idle, 0.5f);
    GetLogic(deleter).m_bDeleteOwnerInUpdate = bInUpdate;
    GetLogic(deleter).m_bDeleteOwnerInDeactivate = !bInUpdate;

    plAiBehaviorResourceDescriptor stopper = MakeBehavior("Stopper", plAiBehaviorCategory::Combat, 0.5f);
    plUniquePtr<plAiInput_BlackboardValue> pGate = MakeUnique<plAiInput_BlackboardValue>();
    pGate->SetEntryName("Stop");
    AddConsideration(stopper, std::move(pGate));

    const plAiBehaviorResourceHandle hDeleter = CreateBehavior(MakeID("Deleter"), std::move(deleter));
    const plAiBehaviorResourceHandle hStopper = CreateBehavior(MakeID("Stopper"), std::move(stopper));
    const plAiArchetypeResourceHandle hArchetype = CreateArchetype(MakeID("Archetype"), {hDeleter, hStopper});

    plAiTestWorld world("AiTest A1");
    plSharedPtr<plBlackboard> pBlackboard;
    const plGameObjectHandle hAgent = SpawnAgent(world, "Agent", hArchetype, &pBlackboard);

    world.Step(3);

    if (!bInUpdate)
    {
      Check(WasActivated("Deleter"), "A1: the deleting behavior was activated");
      pBlackboard->SetEntryValue("Stop", 1.0f);
      world.Step(3);
    }

    world.Step(3);
    Check(WasActivated("Deleter"), "A1: the deleting behavior ran");
    Check(!ObjectExists(world, hAgent), "A1: the agent's owner was deleted by its own logic");
    Check(s_Record.m_uiFreedWhileRunning == 0, "A1: the logic was not freed while it was running");

    // the freed slot is reused by the next agent, which must work normally
    s_Record.m_Activations.Clear();
    plAiBehaviorResourceDescriptor plain = MakeBehavior("Plain", plAiBehaviorCategory::Idle, 0.5f);
    const plAiArchetypeResourceHandle hPlain = CreateArchetype(MakeID("PlainArchetype"), {CreateBehavior(MakeID("Plain"), std::move(plain))});

    SpawnAgent(world, "Successor", hPlain);
    world.Step(5);
    Check(WasActivated("Plain"), "A1: an agent spawned after the deletion runs its behavior");
  }

  void TestStimulusReachesAgent()
  {
    s_Record = TestRecord();

    // every frame is a decision frame, so the brain gathers before the perception drain each frame
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 1000.0f);

    plAiBehaviorResourceDescriptor chase = MakeBehavior("Chase", plAiBehaviorCategory::Investigate, 1.0f);
    AddConsideration(chase, MakeUnique<plAiInput_TargetConfidence>());

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/A3/Archetype", {CreateBehavior("AiTest/A3/Chase", std::move(chase))});

    plAiTestWorld world("AiTest A3");
    SpawnAgent(world, "Agent", hArchetype);

    plGameObjectHandle hSource;
    world.CreateObject("NoiseMaker", plVec3(5, 0, 0), &hSource);

    world.Step(5);
    Check(!WasActivated("Chase"), "A3: nothing to chase before the stimulus");

    {
      PL_LOCK(world.GetWorld().GetWriteMarker());

      plAiStimulus stimulus;
      stimulus.m_Type = plAiStimulusType::Sound;
      stimulus.m_vGlobalPosition = plVec3(5, 0, 0);
      stimulus.m_fRadius = 20.0f;
      stimulus.m_hSource = hSource;
      stimulus.m_sSourceTeam.Assign("Enemy");

      world.GetWorld().GetOrCreateModule<plAiPerceptionWorldModule>()->PostStimulus(stimulus);
    }

    world.Step(5);
    Check(WasActivated("Chase"), "A3: a stimulus published after the brain's gather still reaches the agent");
    Check(s_Record.m_hLastTarget == hSource, "A3: the stimulus source becomes the target");
  }

  //////////////////////////////////////////////////////////////////////////
  // F6: event-driven decisions and the agent's script API

  /// Runs a function on the agent component with the world locked.
  template <typename Function>
  void WithAgent(plAiTestWorld& ref_world, const plComponentHandle& hAgent, Function function)
  {
    PL_LOCK(ref_world.GetWorld().GetWriteMarker());

    plAiAgentComponent* pAgent = nullptr;
    if (ref_world.GetWorld().TryGetComponent(hAgent, pAgent))
    {
      function(*pAgent);
    }
  }

  void PostSound(plAiTestWorld& ref_world, const plGameObjectHandle& hSource, const plVec3& vPosition, float fStrength)
  {
    PL_LOCK(ref_world.GetWorld().GetWriteMarker());

    plAiStimulus stimulus;
    stimulus.m_Type = plAiStimulusType::Sound;
    stimulus.m_vGlobalPosition = vPosition;
    stimulus.m_fRadius = 20.0f;
    stimulus.m_fStrength = fStrength;
    stimulus.m_hSource = hSource;
    stimulus.m_sSourceTeam.Assign("Enemy");

    ref_world.GetWorld().GetOrCreateModule<plAiPerceptionWorldModule>()->PostStimulus(stimulus);
  }

  plAiBehaviorResourceDescriptor MakeGatedBehavior(const char* szTag, plAiBehaviorCategory::Enum category, float fWeight, const char* szBlackboardEntry)
  {
    plAiBehaviorResourceDescriptor desc = MakeBehavior(szTag, category, fWeight);
    plUniquePtr<plAiInput_BlackboardValue> pGate = MakeUnique<plAiInput_BlackboardValue>();
    pGate->SetEntryName(szBlackboardEntry);
    AddConsideration(desc, std::move(pGate));
    return desc;
  }

  void TestCommands()
  {
    s_Record = TestRecord();

    // scheduled decisions are 2 s apart, so anything faster comes from a wake
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 0.5f);

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/F6/Commands/Archetype",
      {CreateBehavior("AiTest/F6/Commands/Idle", MakeBehavior("Idle", plAiBehaviorCategory::Idle, 0.5f)),
        CreateBehavior("AiTest/F6/Commands/Obey", MakeGatedBehavior("Obey", plAiBehaviorCategory::Command, 1.0f, "Ai_CommandActive"))});

    plAiTestWorld world("AiTest F6 commands");
    plSharedPtr<plBlackboard> pBlackboard;
    plComponentHandle hAgent;
    SpawnAgent(world, "Agent", hArchetype, &pBlackboard, &hAgent);

    world.Step(3);
    Check(plStringView(LastActivation()) == "Idle", "F6: the agent idles without a command");

    // woken decisions are at least the archetype's 0.1 s wake spacing apart, so allow 8 frames
    WithAgent(world, hAgent, [](plAiAgentComponent& agent) { agent.IssueCommand("MoveTo", plVec3(3, 0, 0), plGameObjectHandle(), 0.5f); });
    world.Step(8);
    Check(plStringView(LastActivation()) == "Obey", "F6: a command is obeyed within the wake spacing, not at the next scheduled decision");
    Check(pBlackboard->GetEntryValue("Ai_CommandPosition") == plVariant(plVec3(3, 0, 0)), "F6: the command position is on the blackboard");

    WithAgent(world, hAgent, [](plAiAgentComponent& agent) { Check(plStringView(agent.GetActiveBehavior()) == "Obey", "F6: GetActiveBehavior names the running behavior"); });

    world.Step(40);
    Check(plStringView(LastActivation()) == "Idle", "F6: an expired command ends the commanded behavior");
    Check(pBlackboard->GetEntryValue("Ai_CommandActive") == plVariant(false), "F6: an expired command clears Ai_CommandActive");

    WithAgent(world, hAgent, [](plAiAgentComponent& agent) { agent.IssueCommand("Hold", plVec3::MakeZero(), plGameObjectHandle(), 0.0f); });
    world.Step(8);
    Check(plStringView(LastActivation()) == "Obey", "F6: a command without expiry is obeyed");

    WithAgent(world, hAgent, [](plAiAgentComponent& agent) { agent.CancelCommand(); });
    world.Step(8);
    Check(plStringView(LastActivation()) == "Idle", "F6: cancelling a command ends the commanded behavior");
  }

  void TestBehaviorSettings()
  {
    s_Record = TestRecord();
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 0.5f);

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/F6/Settings/Archetype",
      {CreateBehavior("AiTest/F6/Settings/Patrol", MakeBehavior("Patrol", plAiBehaviorCategory::ActiveIdle, 0.6f)),
        CreateBehavior("AiTest/F6/Settings/Wander", MakeBehavior("Wander", plAiBehaviorCategory::ActiveIdle, 0.5f))});

    plAiTestWorld world("AiTest F6 settings");
    plComponentHandle hAgent;
    SpawnAgent(world, "Agent", hArchetype, nullptr, &hAgent);

    world.Step(3);
    Check(plStringView(LastActivation()) == "Patrol", "F6: the higher-weighted behavior runs");
    GetBrain(world)->SetAgentWatched(hAgent, true);

    WithAgent(world, hAgent, [](plAiAgentComponent& agent) { agent.SetBehaviorEnabled("Patrol", false); });
    world.Step(8);
    Check(plStringView(LastActivation()) == "Wander", "F6: disabling the active behavior hands over within the wake spacing");

    plAiAgentDebugSnapshot snapshot;
    GetBrain(world)->GetAgentDebugSnapshot(hAgent, snapshot);
    const plAiBehaviorTrace* pPatrol = FindTrace(snapshot, "Patrol");
    Check(pPatrol != nullptr && pPatrol->m_Veto == plAiBehaviorVeto::Disabled, "F6: the debugger shows the disabled behavior as disabled");

    WithAgent(world, hAgent, [](plAiAgentComponent& agent) { agent.SetBehaviorEnabled("Patrol", true); });
    WithAgent(world, hAgent, [](plAiAgentComponent& agent) { agent.SetBehaviorWeight("Wander", 0.5f, 0.0f); });
    world.Step(8);
    Check(plStringView(LastActivation()) == "Patrol", "F6: a reduced weight lets the re-enabled behavior win again");

    // a temporary boost: Wander 0.5 x 1.9 = 0.95 beats Patrol 0.6 plus its 0.2 commit bonus, then wears off
    WithAgent(world, hAgent, [](plAiAgentComponent& agent) { agent.SetBehaviorWeight("Wander", 1.9f, 0.25f); });
    world.Step(8);
    Check(plStringView(LastActivation()) == "Wander", "F6: a temporary weight boost takes effect within the wake spacing");

    world.Step(20);
    GetBrain(world)->GetAgentDebugSnapshot(hAgent, snapshot);
    const plAiBehaviorTrace* pWander = FindTrace(snapshot, "Wander");
    Check(pWander != nullptr && plMath::IsEqual(pWander->m_fUtility, 0.5f, 0.001f), "F6: the weight override expires");
  }

  void TestBehaviorEndWake()
  {
    s_Record = TestRecord();
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 0.5f);

    plAiBehaviorResourceDescriptor once = MakeBehavior("Once", plAiBehaviorCategory::ActiveIdle, 0.9f);
    GetLogic(once).m_bFinishImmediately = true;
    once.m_CooldownDuration = plTime::MakeFromSeconds(10.0);

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/A5/Archetype",
      {CreateBehavior("AiTest/A5/Once", std::move(once)), CreateBehavior("AiTest/A5/After", MakeBehavior("After", plAiBehaviorCategory::Idle, 0.5f))});

    plAiTestWorld world("AiTest A5");
    SpawnAgent(world, "Agent", hArchetype);

    world.Step(10);
    Check(WasActivated("Once"), "A5: the one-shot behavior ran");
    Check(WasActivated("After"), "A5: a successor starts within frames of Done, not at the next scheduled decision");
  }

  void TestStimulusWake()
  {
    s_Record = TestRecord();
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 0.5f);

    plAiBehaviorResourceDescriptor chase = MakeBehavior("Chase", plAiBehaviorCategory::Investigate, 1.0f);
    AddConsideration(chase, MakeUnique<plAiInput_TargetConfidence>());

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/F6/Stimulus/Archetype", {CreateBehavior("AiTest/F6/Stimulus/Chase", std::move(chase))});

    plAiTestWorld world("AiTest F6 stimulus");
    SpawnAgent(world, "Agent", hArchetype);

    plGameObjectHandle hSource;
    world.CreateObject("NoiseMaker", plVec3(5, 0, 0), &hSource);

    world.Step(5);
    PostSound(world, hSource, plVec3(5, 0, 0), 1.0f);
    world.Step(8);
    Check(WasActivated("Chase"), "F6: a stimulus wakes an agent whose next scheduled decision is seconds away");
  }

  void TestLockTimeout()
  {
    s_Record = TestRecord();
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 1000.0f);

    plAiBehaviorResourceDescriptor locked = MakeBehavior("Locked", plAiBehaviorCategory::Idle, 0.5f);
    GetLogic(locked).m_bAlwaysLock = true;

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/F6/Lock/Archetype",
      {CreateBehavior("AiTest/F6/Lock/Locked", std::move(locked)), CreateBehavior("AiTest/F6/Lock/Combat", MakeGatedBehavior("Combat", plAiBehaviorCategory::Combat, 0.5f, "Threat"))},
      [](plAiArchetypeResourceDescriptor& ref_desc) { ref_desc.m_MaxLockDuration = plTime::MakeFromSeconds(0.5); });

    plAiTestWorld world("AiTest F6 lock");
    plSharedPtr<plBlackboard> pBlackboard;
    SpawnAgent(world, "Agent", hArchetype, &pBlackboard);

    world.Step(3);
    pBlackboard->SetEntryValue("Threat", 1.0f);
    world.Step(6);
    Check(plStringView(LastActivation()) == "Locked", "F6: a lock holds off a higher category until it times out");

    world.Step(30);
    Check(plStringView(LastActivation()) == "Combat", "F6: a lock older than MaxLockDuration no longer holds");
  }

  void TestTargetHysteresis()
  {
    s_Record = TestRecord();
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 1000.0f);

    plAiBehaviorResourceDescriptor chase = MakeBehavior("Chase", plAiBehaviorCategory::Investigate, 1.0f);
    AddConsideration(chase, MakeUnique<plAiInput_TargetConfidence>());

    // no decay, so confidences stay at the stimulus strengths
    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/A4/Archetype", {CreateBehavior("AiTest/A4/Chase", std::move(chase))},
      [](plAiArchetypeResourceDescriptor& ref_desc) { ref_desc.m_fConfidenceDecayPerSecond = 0.0f; });

    plAiTestWorld world("AiTest A4");
    SpawnAgent(world, "Agent", hArchetype);

    plGameObjectHandle hFirst, hSecond;
    world.CreateObject("First", plVec3(5, 0, 0), &hFirst);
    world.CreateObject("Second", plVec3(-5, 0, 0), &hSecond);

    world.Step(3);
    PostSound(world, hFirst, plVec3(5, 0, 0), 0.6f);
    world.Step(3);
    Check(s_Record.m_hLastTarget == hFirst, "A4: the agent chases the first source");

    // slightly better, but within the 0.1 margin
    PostSound(world, hSecond, plVec3(-5, 0, 0), 0.65f);
    world.Step(60);
    Check(s_Record.m_hLastTarget == hFirst && s_Record.m_uiReactivations == 0, "A4: a target only slightly better doesn't take over");

    // clearly better, and the first target was held longer than 0.5 s
    PostSound(world, hSecond, plVec3(-5, 0, 0), 1.0f);
    world.Step(3);
    Check(s_Record.m_hLastTarget == hSecond && s_Record.m_uiReactivations == 1, "A4: a clearly better target takes over once");
  }

  //////////////////////////////////////////////////////////////////////////
  // F8: senses, memory and alert levels

  /// A world with one watched agent that decides every frame and only perceives, plus helpers to read what it perceives.
  struct PerceptionFixture
  {
    template <typename Customize>
    PerceptionFixture(const char* szName, Customize customize)
      : m_World(szName)
      , m_DecisionRate(cvar_AiAgentsDecisionHz, 1000.0f)
    {
      plStringBuilder sID("AiTest/F8/", szName, "/");

      plAiBehaviorResourceDescriptor idle = MakeBehavior("Idle", plAiBehaviorCategory::Idle, 0.5f);
      const plAiBehaviorResourceHandle hIdle = CreateBehavior(plStringBuilder(sID, "Idle"), std::move(idle));
      const plAiArchetypeResourceHandle hArchetype = CreateArchetype(plStringBuilder(sID, "Archetype"), {hIdle}, customize);

      SpawnAgent(m_World, "Agent", hArchetype, &m_pBlackboard, &m_hAgent);
      m_World.Step(2);
      GetBrain(m_World)->SetAgentWatched(m_hAgent, true);
      m_World.Step(1);
    }

    void Post(const plAiStimulus& stimulus)
    {
      PL_LOCK(m_World.GetWorld().GetWriteMarker());
      m_World.GetWorld().GetOrCreateModule<plAiPerceptionWorldModule>()->PostStimulus(stimulus);
    }

    plAiStimulus MakeStimulus(plAiStimulusType::Enum type, const plVec3& vPosition, float fStrength, const plGameObjectHandle& hSource = plGameObjectHandle())
    {
      plAiStimulus stimulus;
      stimulus.m_Type = type;
      stimulus.m_vGlobalPosition = vPosition;
      stimulus.m_fRadius = 20.0f;
      stimulus.m_fStrength = fStrength;
      stimulus.m_hSource = hSource;
      stimulus.m_sSourceTeam.Assign("Enemy");
      return stimulus;
    }

    plHybridArray<plAiPerceivedTarget, 4> GetTargets()
    {
      plAiAgentDebugSnapshot snapshot;
      GetBrain(m_World)->GetAgentDebugSnapshot(m_hAgent, snapshot);
      return snapshot.m_Targets;
    }

    plInt32 GetAlertLevel() { return m_pBlackboard->GetEntryValue("Ai_AlertLevel").ConvertTo<plInt32>(); }

    plAiTestWorld m_World;
    plAiScopedCVar<plCVarFloat, float> m_DecisionRate;
    plSharedPtr<plBlackboard> m_pBlackboard;
    plComponentHandle m_hAgent;
  };

  void NoCustomization(plAiArchetypeResourceDescriptor&) {}

  void TestEventPosition()
  {
    PerceptionFixture fixture("EventPosition", NoCustomization);

    // the source stands elsewhere than the noise it made
    plGameObjectHandle hSource;
    fixture.m_World.CreateObject("Thrower", plVec3(8, 0, 0), &hSource);

    fixture.Post(fixture.MakeStimulus(plAiStimulusType::Sound, plVec3(5, 0, 0), 1.0f, hSource));
    fixture.m_World.Step(3);

    auto targets = fixture.GetTargets();
    Check(targets.GetCount() == 1 && targets[0].m_vLastKnownPosition.IsEqual(plVec3(5, 0, 0), 0.01f), "F8: a heard target is remembered where the noise was, not where its source stands");

    // a squad report: broadcast at the squad, about a target elsewhere
    plAiStimulus report = fixture.MakeStimulus(plAiStimulusType::Custom, plVec3(0, 0, 0), 0.5f, hSource);
    report.m_vEventPosition = plVec3(12, 0, 0);
    report.m_bHasEventPosition = true;
    report.m_bReport = true;
    fixture.Post(report);
    fixture.m_World.Step(3);

    targets = fixture.GetTargets();
    Check(targets.GetCount() == 1 && targets[0].m_vLastKnownPosition.IsEqual(plVec3(12, 0, 0), 0.01f), "F8: a report places the target at the reported position");
    Check(targets.GetCount() == 1 && targets[0].m_bReported, "F8: a report is marked as relayed, not first-hand");
  }

  void TestSourcelessEvents()
  {
    PerceptionFixture fixture("Sourceless", NoCustomization);

    fixture.Post(fixture.MakeStimulus(plAiStimulusType::Sound, plVec3(5, 0, 0), 1.0f));
    fixture.Post(fixture.MakeStimulus(plAiStimulusType::Sound, plVec3(-5, 0, 0), 1.0f));
    fixture.m_World.Step(3);
    Check(fixture.GetTargets().GetCount() == 2, "A7: two sourceless noises in different places are two records");

    fixture.Post(fixture.MakeStimulus(plAiStimulusType::Sound, plVec3(5.5f, 0, 0), 1.0f));
    fixture.m_World.Step(3);
    Check(fixture.GetTargets().GetCount() == 2, "A7: a sourceless noise next to a remembered one refreshes it");
  }

  void TestTargetCap()
  {
    PerceptionFixture fixture("Cap", [](plAiArchetypeResourceDescriptor& ref_desc) {
      ref_desc.m_uiMaxTargets = 2;
      ref_desc.m_fConfidenceDecayPerSecond = 0.0f;
    });

    fixture.Post(fixture.MakeStimulus(plAiStimulusType::Sound, plVec3(5, 0, 0), 0.3f));
    fixture.Post(fixture.MakeStimulus(plAiStimulusType::Sound, plVec3(-5, 0, 0), 0.6f));
    fixture.Post(fixture.MakeStimulus(plAiStimulusType::Sound, plVec3(0, 5, 0), 0.9f));
    fixture.m_World.Step(3);

    const auto targets = fixture.GetTargets();
    bool bWeakestDropped = targets.GetCount() == 2;
    for (const auto& target : targets)
    {
      bWeakestDropped &= target.m_fConfidence > 0.5f;
    }

    Check(bWeakestDropped, "F8: over the cap, the least confident record is dropped");
  }

  void TestAlertLevels()
  {
    // confidence 1 decays to 0 in 0.5 s, then is remembered for 2 s
    PerceptionFixture fixture("Alert", [](plAiArchetypeResourceDescriptor& ref_desc) {
      ref_desc.m_fConfidenceDecayPerSecond = 2.0f;
      ref_desc.m_TargetMemoryDuration = plTime::MakeFromSeconds(2.0);
    });

    Check(fixture.GetAlertLevel() == plAiAlertLevel::Unaware, "F8: Unaware without targets");

    fixture.Post(fixture.MakeStimulus(plAiStimulusType::Sound, plVec3(5, 0, 0), 1.0f));
    fixture.m_World.Step(3);
    Check(fixture.GetAlertLevel() == plAiAlertLevel::Suspicious, "F8: Suspicious after only hearing something");

    plGameObjectHandle hAttacker;
    fixture.m_World.CreateObject("Attacker", plVec3(3, 0, 0), &hAttacker);
    fixture.Post(fixture.MakeStimulus(plAiStimulusType::Damage, plVec3(3, 0, 0), 1.0f, hAttacker));
    fixture.m_World.Step(3);
    Check(fixture.GetAlertLevel() == plAiAlertLevel::Alert, "F8: Alert right after damage");

    fixture.m_World.Step(70);
    Check(fixture.GetAlertLevel() == plAiAlertLevel::Searching, "F8: Searching once the damage is more than a second old");

    fixture.m_World.Step(120);
    Check(fixture.GetAlertLevel() == plAiAlertLevel::Unaware, "F8: Unaware again once everything is forgotten");
  }

  void TestSenseProfiles()
  {
    PerceptionFixture deaf("Deaf", [](plAiArchetypeResourceDescriptor& ref_desc) { ref_desc.m_Hearing.m_bEnabled = false; });
    deaf.Post(deaf.MakeStimulus(plAiStimulusType::Sound, plVec3(5, 0, 0), 1.0f));
    deaf.m_World.Step(3);
    Check(deaf.GetTargets().IsEmpty(), "F8: an agent without hearing ignores sounds");

    deaf.Post(deaf.MakeStimulus(plAiStimulusType::Damage, plVec3(5, 0, 0), 1.0f));
    deaf.m_World.Step(3);
    Check(deaf.GetTargets().GetCount() == 1, "F8: the other senses still work");

    PerceptionFixture dull("Dull", [](plAiArchetypeResourceDescriptor& ref_desc) {
      ref_desc.m_Hearing.m_fRangeScale = 0.5f;
      ref_desc.m_Hearing.m_fStrengthScale = 0.5f;
      ref_desc.m_fConfidenceDecayPerSecond = 0.0f;
    });

    dull.Post(dull.MakeStimulus(plAiStimulusType::Sound, plVec3(15, 0, 0), 1.0f)); // radius 20 x 0.5 doesn't reach
    dull.m_World.Step(3);
    Check(dull.GetTargets().IsEmpty(), "F8: the hearing range scale shortens how far sounds carry");

    dull.Post(dull.MakeStimulus(plAiStimulusType::Sound, plVec3(5, 0, 0), 1.0f));
    dull.m_World.Step(3);
    const auto targets = dull.GetTargets();
    Check(targets.GetCount() == 1 && plMath::IsEqual(targets[0].m_fConfidence, 0.5f, 0.01f), "F8: the hearing strength scale scales confidence");
  }

  void TestStimulusSourceComponent()
  {
    PerceptionFixture fixture("SourceComponent", NoCustomization);

    plGameObject* pRadio = fixture.m_World.CreateObject("Radio", plVec3(4, 0, 0));
    plComponentHandle hSource;

    {
      PL_LOCK(fixture.m_World.GetWorld().GetWriteMarker());
      plAiStimulusSourceComponent* pSource = nullptr;
      hSource = plAiStimulusSourceComponent::CreateComponent(pRadio, pSource);
      pSource->m_Type = plAiStimulusType::Sound;
      pSource->m_sTeam.Assign("Enemy");
    }

    fixture.m_World.Step(5);
    Check(fixture.GetTargets().IsEmpty(), "F8: a source with no interval stays quiet");

    {
      PL_LOCK(fixture.m_World.GetWorld().GetWriteMarker());
      plAiStimulusSourceComponent* pSource = nullptr;
      if (fixture.m_World.GetWorld().TryGetComponent(hSource, pSource))
      {
        pSource->Emit();
      }
    }

    fixture.m_World.Step(3);
    const auto targets = fixture.GetTargets();
    Check(targets.GetCount() == 1 && targets[0].m_vLastKnownPosition.IsEqual(plVec3(4, 0, 0), 0.01f), "F8: Emit makes a prop heard at its position");
  }

  void TestArchetypeSerialization()
  {
    plAiArchetypeResourceDescriptor desc;
    desc.m_fConfidenceGainPerSecond = 3.0f;
    desc.m_Hearing.m_bEnabled = false;
    desc.m_Damage.m_bUseSightSettings = false;
    desc.m_Damage.m_MemoryDuration = plTime::MakeFromSeconds(4.0);
    desc.m_uiMaxTargets = 5;
    desc.m_bWakeOnStimulus = false;
    desc.m_fTargetSwitchMargin = 0.3f;
    desc.m_MaxLockDuration = plTime::MakeFromSeconds(2.0);
    desc.m_uiScoringVersion = 2;

    plDefaultMemoryStreamStorage storage;
    plMemoryStreamWriter writer(&storage);
    Check(desc.Serialize(writer).Succeeded(), "archetype: version 2 writes");

    plMemoryStreamReader reader(&storage);
    plAiArchetypeResourceDescriptor read;
    Check(read.Deserialize(reader).Succeeded(), "archetype: version 2 reads");
    Check(read.m_fConfidenceGainPerSecond == 3.0f && !read.m_Hearing.m_bEnabled && !read.m_Damage.m_bUseSightSettings &&
            read.m_Damage.m_MemoryDuration == plTime::MakeFromSeconds(4.0) && read.m_uiMaxTargets == 5 && !read.m_bWakeOnStimulus &&
            read.m_fTargetSwitchMargin == 0.3f && read.m_MaxLockDuration == plTime::MakeFromSeconds(2.0) && read.m_uiScoringVersion == 2,
      "archetype: version 2 round-trips every field");

    // the version 1 layout of assets transformed before this change
    plDefaultMemoryStreamStorage oldStorage;
    plMemoryStreamWriter oldWriter(&oldStorage);
    oldWriter.WriteVersion(1);
    oldWriter << plUInt32(0);    // behaviors
    oldWriter << plStringView(); // blackboard template
    oldWriter << plHashedString();
    oldWriter << plString();
    oldWriter << 2.0f;
    oldWriter << 0.25f;
    oldWriter << plTime::MakeFromSeconds(6.0);

    plMemoryStreamReader oldReader(&oldStorage);
    plAiArchetypeResourceDescriptor old;
    Check(old.Deserialize(oldReader).Succeeded(), "archetype: version 1 reads");
    Check(old.m_fConfidenceDecayPerSecond == 0.25f && old.m_TargetMemoryDuration == plTime::MakeFromSeconds(6.0), "archetype: version 1 keeps its perception tuning");
    Check(old.m_uiScoringVersion == 1 && old.m_Hearing.m_bUseSightSettings && old.m_uiMaxTargets == 8, "archetype: version 1 gets the defaults that behave as before");
  }

  void TestScoringVersion()
  {
    const float fProduct = 0.8f * 0.8f * 0.8f * 0.8f;
    Check(plMath::IsEqual(plAiUtilityEvaluator::CombineConsiderations(fProduct, 4, 1), 0.5904f, 0.001f), "C1: version 1 scores four 0.8 considerations at 0.59");
    Check(plMath::IsEqual(plAiUtilityEvaluator::CombineConsiderations(fProduct, 4, 2), 0.8f, 0.001f), "C1: version 2 scores four 0.8 considerations at 0.8");
    Check(plAiUtilityEvaluator::CombineConsiderations(0.0f, 3, 2) == 0.0f, "C1: version 2 keeps the veto");
  }

  //////////////////////////////////////////////////////////////////////////
  // B1: remaining findings

  void TestWorldShutdownScope()
  {
    Check(!plAiBehaviorLogic::IsWorldShuttingDown(), "A2: outside a shutdown, logic may use the world");

    {
      plAiBehaviorLogic::WorldShutdownScope outer;
      {
        plAiBehaviorLogic::WorldShutdownScope inner;
        Check(plAiBehaviorLogic::IsWorldShuttingDown(), "A2: a shutdown scope marks the world as shutting down");
      }
      Check(plAiBehaviorLogic::IsWorldShuttingDown(), "A2: shutdown scopes nest");
    }

    Check(!plAiBehaviorLogic::IsWorldShuttingDown(), "A2: the mark ends with the outermost scope");

    // an agent removed from a running world: its logic goes while the world is usable, so states release what they hold
    s_Record = TestRecord();
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 1000.0f);

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/A2/Archetype", {CreateBehavior("AiTest/A2/Held", MakeBehavior("Held", plAiBehaviorCategory::Idle, 0.5f))});

    plAiTestWorld world("AiTest A2");
    const plGameObjectHandle hAgent = SpawnAgent(world, "Agent", hArchetype);
    world.Step(3);
    Check(WasActivated("Held"), "A2: the agent runs its behavior");

    {
      PL_LOCK(world.GetWorld().GetWriteMarker());
      world.GetWorld().DeleteObjectNow(hAgent);
    }

    world.Step(1);
    Check(s_Record.m_uiDestroyedInRunningWorld >= 1 && s_Record.m_uiDestroyedInShutdown == 0, "A2: deleting an agent destroys its logic with the world still usable");
  }

  void TestStimulusKeptForSlowAgents()
  {
    s_Record = TestRecord();

    // one decision every 5 s, and no early decision on a stimulus
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 0.2f);

    plAiBehaviorResourceDescriptor chase = MakeBehavior("Chase", plAiBehaviorCategory::Investigate, 1.0f);
    AddConsideration(chase, MakeUnique<plAiInput_TargetConfidence>());

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/A6/Archetype", {CreateBehavior("AiTest/A6/Chase", std::move(chase))},
      [](plAiArchetypeResourceDescriptor& ref_desc) {
        ref_desc.m_bWakeOnStimulus = false;
        ref_desc.m_fConfidenceDecayPerSecond = 0.0f;
      });

    plAiTestWorld world("AiTest A6");
    SpawnAgent(world, "Agent", hArchetype);

    plGameObjectHandle hSource;
    world.CreateObject("NoiseMaker", plVec3(5, 0, 0), &hSource);

    world.Step(3);
    PostSound(world, hSource, plVec3(5, 0, 0), 1.0f);

    // 4.5 s: older than the 3 s the stimuli used to be kept, before the agent's next decision
    world.Step(270);
    Check(!WasActivated("Chase"), "A6: the agent hasn't decided since the stimulus");

    world.Step(60);
    Check(WasActivated("Chase"), "A6: a stimulus is kept until a slow agent has processed it");
  }

  void TestTargetlessBehaviorClearsPosition()
  {
    s_Record = TestRecord();
    plAiScopedCVar<plCVarFloat, float> decisionRate(cvar_AiAgentsDecisionHz, 1000.0f);

    plAiBehaviorResourceDescriptor chase = MakeBehavior("Chase", plAiBehaviorCategory::Investigate, 1.0f);
    AddConsideration(chase, MakeUnique<plAiInput_TargetConfidence>());

    // the target fades within frames, then the target-less Idle takes over
    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/A8/Archetype",
      {CreateBehavior("AiTest/A8/Chase", std::move(chase)), CreateBehavior("AiTest/A8/Idle", MakeBehavior("Idle", plAiBehaviorCategory::Idle, 0.5f))},
      [](plAiArchetypeResourceDescriptor& ref_desc) {
        ref_desc.m_fConfidenceDecayPerSecond = 4.0f;
        ref_desc.m_TargetMemoryDuration = plTime::MakeFromSeconds(0.1);
      });

    plAiTestWorld world("AiTest A8");
    plSharedPtr<plBlackboard> pBlackboard;
    SpawnAgent(world, "Agent", hArchetype, &pBlackboard);

    plGameObjectHandle hSource;
    world.CreateObject("NoiseMaker", plVec3(5, 0, 0), &hSource);

    world.Step(3);
    PostSound(world, hSource, plVec3(5, 0, 0), 1.0f);
    world.Step(3);
    Check(plStringView(LastActivation()) == "Chase", "A8: the agent chases the noise");
    Check(pBlackboard->GetEntryValue("Ai_TargetPosition").ConvertTo<plVec3>().IsEqual(plVec3(5, 0, 0), 0.01f), "A8: the target position is published");

    world.Step(60);
    Check(plStringView(LastActivation()) == "Idle", "A8: the target-less behavior runs once the target is forgotten");
    Check(pBlackboard->GetEntryValue("Ai_TargetPosition").ConvertTo<plVec3>().IsZero(), "A8: a behavior without a target doesn't see the previous target's position");
  }

  plGameObjectHandle SpawnSquadMember(plAiTestWorld& ref_world, const char* szName, const plVec3& vPosition, const plAiArchetypeResourceHandle& hArchetype, plSharedPtr<plBlackboard>& out_pBlackboard)
  {
    plGameObjectHandle hObject;
    plGameObject* pObject = ref_world.CreateObject(szName, vPosition, &hObject);

    PL_LOCK(ref_world.GetWorld().GetWriteMarker());

    plAiAgentComponent* pAgent = nullptr;
    plAiAgentComponent::CreateComponent(pObject, pAgent);
    pAgent->m_hArchetype = hArchetype;
    pAgent->m_sSquadId.Assign("Alpha");

    plLocalBlackboardComponent* pBlackboard = nullptr;
    plLocalBlackboardComponent::CreateComponent(pObject, pBlackboard);
    out_pBlackboard = pBlackboard->GetBoard();

    return hObject;
  }

  void TestSquadRetreatEnds()
  {
    s_Record = TestRecord();

    plCVarFloat* pHoldTime = static_cast<plCVarFloat*>(plCVar::FindCVarByName("AI.Squad.RetreatHoldTime"));
    Check(pHoldTime != nullptr, "T2: the retreat hold time cvar exists");
    if (pHoldTime == nullptr)
      return;

    plAiScopedCVar<plCVarFloat, float> holdTime(*pHoldTime, 1.0f);

    const plAiArchetypeResourceHandle hArchetype = CreateArchetype("AiTest/T2/Archetype", {CreateBehavior("AiTest/T2/Idle", MakeBehavior("Idle", plAiBehaviorCategory::Idle, 0.5f))});

    plAiTestWorld world("AiTest T2");
    plGameObjectHandle hMembers[4];
    plSharedPtr<plBlackboard> pBlackboards[4];

    for (plUInt32 i = 0; i < 4; ++i)
    {
      hMembers[i] = SpawnSquadMember(world, "Member", plVec3(static_cast<float>(i), 0, 0), hArchetype, pBlackboards[i]);
    }

    world.Step(30);

    {
      PL_LOCK(world.GetWorld().GetWriteMarker());
      world.GetWorld().DeleteObjectNow(hMembers[0]);
      world.GetWorld().DeleteObjectNow(hMembers[1]);
    }

    auto GetIntent = [&]() { return pBlackboards[2]->GetEntryValue("Ai_SquadIntent").ConvertTo<plInt32>(); };

    world.Step(30);
    Check(GetIntent() == plAiSquadIntent::Retreat, "T2: losing half the squad retreats");

    // past the hold time (and the 3 s an intent dwells), the two survivors are the squad: no new losses, no retreat
    world.Step(240);
    Check(GetIntent() != plAiSquadIntent::Retreat, "T2: a retreat ends after its hold time instead of lasting forever");
  }

  //////////////////////////////////////////////////////////////////////////
  // B2

  void TestLostSmartObjectClaimEndsUse()
  {
    plAiTestUseListenerComponent::s_uiStarts = 0;
    plAiTestUseListenerComponent::s_uiEnds = 0;

    plAiTestWorld world("AiTest T4");

    plGameObjectHandle hBench;
    plGameObject* pBench = world.CreateObject("Bench", plVec3(2, 0, 0), &hBench);

    plGameObjectHandle hUser;
    world.CreateObject("User", plVec3(1.5f, 0, 0), &hUser);

    {
      PL_LOCK(world.GetWorld().GetWriteMarker());

      plAiSmartObjectComponent* pSmartObject = nullptr;
      plAiSmartObjectComponent::CreateComponent(pBench, pSmartObject);
      pSmartObject->m_sType.Assign("Bench");

      plAiTestUseListenerComponent* pListener = nullptr;
      plAiTestUseListenerComponent::CreateComponent(pBench, pListener);
    }

    world.Step(2);

    auto StartUse = [&]() {
      PL_LOCK(world.GetWorld().GetWriteMarker());
      plAiTacticalWorldModule* pTactical = world.GetWorld().GetModule<plAiTacticalWorldModule>();

      plAiSmartObjectHandle hSlot;
      plVec3 vSlot;
      return pTactical != nullptr && pTactical->FindSmartObject(plVec3(1.5f, 0, 0), 5.0f, plTempHashedString("Bench"), hUser, hSlot, vSlot) &&
             pTactical->ClaimSmartObject(hSlot, hUser, plTime::MakeFromSeconds(10)) && pTactical->BeginUse(hUser);
    };

    Check(StartUse() && plAiTestUseListenerComponent::s_uiStarts == 1, "T4: the agent starts using the bench");

    // a cover claim, or anything else that drops the claim mid-use
    {
      PL_LOCK(world.GetWorld().GetWriteMarker());
      world.GetWorld().GetModule<plAiTacticalWorldModule>()->ReleaseSmartObjectClaim(hUser);
    }

    Check(plAiTestUseListenerComponent::s_uiEnds == 1, "T4: losing the claim mid-use tells the object the use ended");

    // the object goes away while in use
    Check(StartUse() && plAiTestUseListenerComponent::s_uiStarts == 2, "T4: the agent uses the bench again");

    {
      PL_LOCK(world.GetWorld().GetWriteMarker());
      world.GetWorld().DeleteObjectNow(hBench);
    }

    Check(plAiTestUseListenerComponent::s_uiEnds == 2, "T4: a smart object removed mid-use hears the use ended");
  }
} // namespace

void RunBrainWorldTests()
{
  TestCategoryRule();
  TestCategoryBeatsCommitBonus();
  TestDebugSnapshot();
  TestDebugProtocol();
  TestLogicDeletesOwner("Update", true);
  TestLogicDeletesOwner("Deactivate", false);
  TestStimulusReachesAgent();

  TestCommands();
  TestBehaviorSettings();
  TestBehaviorEndWake();
  TestStimulusWake();
  TestLockTimeout();
  TestTargetHysteresis();
  TestScoringVersion();
  TestArchetypeSerialization();

  TestEventPosition();
  TestSourcelessEvents();
  TestTargetCap();
  TestAlertLevels();
  TestSenseProfiles();
  TestStimulusSourceComponent();

  TestWorldShutdownScope();
  TestStimulusKeptForSlowAgents();
  TestTargetlessBehaviorClearsPosition();
  TestSquadRetreatEnds();

  TestLostSmartObjectClaimEndsUse();
}
