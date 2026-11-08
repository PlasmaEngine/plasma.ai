#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Tactical/SquadWorldModule.h>
#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionWorldModule.h>
#include <Core/Utils/Blackboard.h>
#include <Core/World/World.h>
#include <Foundation/Configuration/CVar.h>
#include <GameEngine/Gameplay/BlackboardComponent.h>
#include <RendererCore/Debug/DebugRenderer.h>

// NOTE: what a squad IS (token count, sharing tuning) lives in the AI project settings
// (plAiSquadConfig). The cvars below are debug toggles, budgets and the MANEUVER heuristics -
// the latter are experimental this round and get promoted to settings once proven.
plCVarBool cvar_SquadShowDebug("AI.Squad.ShowDebug", false, plCVarFlags::Default, "Visualize squads: member links, attack tokens, shared targets, intent.");
plCVarBool cvar_SquadMuteSharing("AI.Squad.MuteSharing", false, plCVarFlags::Default, "Disable squad target sharing (for A/B debugging).");
plCVarInt cvar_SquadMaxTicksPerFrame("AI.Squad.MaxSquadTicksPerFrame", 8, plCVarFlags::Default, "How many squads may run their coordination tick per frame.");
plCVarFloat cvar_SquadTickInterval("AI.Squad.TickInterval", 0.25f, plCVarFlags::Default, "Target interval between coordination ticks of the same squad, in seconds.");
plCVarFloat cvar_SquadTokenMinHold("AI.Squad.TokenMinHold", 2.0f, plCVarFlags::Default, "Minimum seconds an attack token is held before a better candidate may take it.");
plCVarFloat cvar_SquadIntentMinDwell("AI.Squad.IntentMinDwell", 3.0f, plCVarFlags::Default, "Minimum seconds between squad intent changes (except escalation to Retreat).");
plCVarFloat cvar_SquadRetreatLossFraction("AI.Squad.RetreatLossFraction", 0.5f, plCVarFlags::Default, "Fraction of peak squad size lost that triggers a squad retreat.");
plCVarFloat cvar_SquadRetreatHoldTime("AI.Squad.RetreatHoldTime", 10.0f, plCVarFlags::Default, "How long a squad stays in Retreat before re-evaluating, in seconds.");
plCVarFloat cvar_SquadRetreatDistance("AI.Squad.RetreatDistance", 15.0f, plCVarFlags::Default, "How far away from the threat the fallback rally point is pushed, in meters.");
plCVarFloat cvar_SquadAdvanceTriggerDistance("AI.Squad.AdvanceTriggerDistance", 18.0f, plCVarFlags::Default, "Centroid distance to the shared target beyond which the squad advances.");
plCVarFloat cvar_SquadBoundStep("AI.Squad.BoundStep", 6.0f, plCVarFlags::Default, "How far the moving group bounds toward the target per leg, in meters.");
plCVarFloat cvar_SquadBoundPeriod("AI.Squad.BoundPeriod", 3.0f, plCVarFlags::Default, "How often the moving and covering groups swap during an advance, in seconds.");

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiSquadIntent, 1)
  PL_ENUM_CONSTANTS(plAiSquadIntent::None, plAiSquadIntent::Engage, plAiSquadIntent::Advance, plAiSquadIntent::Hold, plAiSquadIntent::Retreat)
PL_END_STATIC_REFLECTED_ENUM;

PL_IMPLEMENT_WORLD_MODULE(plAiSquadWorldModule);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiSquadWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plVec2 plAiSquadOrder::GetRegionSlot(plUInt32 uiIndex, plUInt32 uiCount) const
{
  if (!m_bHasRegion || uiCount == 0)
    return plVec2::MakeZero();

  // a grid as square as the count allows, the spots in the middle of its cells
  const plUInt32 uiColumns = static_cast<plUInt32>(plMath::Ceil(plMath::Sqrt(static_cast<float>(uiCount))));
  const plUInt32 uiRows = (uiCount + uiColumns - 1) / uiColumns;

  const plUInt32 uiColumn = uiIndex % uiColumns;
  const plUInt32 uiRow = uiIndex / uiColumns;

  return plVec2(plMath::Lerp(m_vRegionMin.x, m_vRegionMax.x, (uiColumn + 0.5f) / uiColumns), plMath::Lerp(m_vRegionMin.y, m_vRegionMax.y, (uiRow + 0.5f) / uiRows));
}

namespace
{
  const char* GetIntentName(plAiSquadIntent::Enum intent)
  {
    switch (intent)
    {
      case plAiSquadIntent::Engage:
        return "Engage";
      case plAiSquadIntent::Advance:
        return "Advance";
      case plAiSquadIntent::Hold:
        return "Hold";
      case plAiSquadIntent::Retreat:
        return "Retreat";
      default:
        return "None";
    }
  }

  bool IsOrderIntent(plAiSquadIntent::Enum intent)
  {
    return intent == plAiSquadIntent::Engage || intent == plAiSquadIntent::Advance || intent == plAiSquadIntent::Hold || intent == plAiSquadIntent::Retreat;
  }

  constexpr plUInt32 s_uiMaxHistory = 12;
} // namespace

plAiSquadWorldModule::plAiSquadWorldModule(plWorld* pWorld)
  : plWorldModule(pWorld)
{
}

plAiSquadWorldModule::~plAiSquadWorldModule() = default;

void plAiSquadWorldModule::Initialize()
{
  SUPER::Initialize();

  ReloadSquadConfig();

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiSquadWorldModule::UpdateSquads, this);
    updateDesc.m_Phase = plWorldModule::UpdateFunctionDesc::Phase::PostAsync;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;
    // priority ordering (m_DependsOn hard-asserts on missing modules): brain apply = 0,
    // tactical probes = -1000, squads last so they read THIS frame's published member state
    updateDesc.m_fPriority = -1100.0f;

    RegisterUpdateFunction(updateDesc);
  }
}

void plAiSquadWorldModule::ReloadSquadConfig()
{
  plAiNavigationConfig cfg;

  if (cfg.Load().Succeeded())
  {
    m_Config = cfg.m_SquadConfig;
  }
}

plAiSquadWorldModule::MemberID plAiSquadWorldModule::RegisterMember(const plHashedString& sSquadId, plComponentHandle hAgentComponent)
{
  if (sSquadId.IsEmpty())
    return InvalidMemberID;

  // safe module-creation context: we are inside a component's OnSimulationStarted, not a module Initialize
  if (m_pPerception == nullptr)
  {
    m_pPerception = GetWorld()->GetOrCreateModule<plAiPerceptionWorldModule>();
  }

  plUInt32 uiSquad = plInvalidIndex;

  if (!m_SquadByName.TryGetValue(sSquadId, uiSquad))
  {
    if (!m_FreeSquads.IsEmpty())
    {
      uiSquad = m_FreeSquads.PeekBack();
      m_FreeSquads.PopBack();
    }
    else
    {
      uiSquad = m_Squads.GetCount();
      m_Squads.ExpandAndGetRef();
    }

    Squad& newSquad = m_Squads[uiSquad];
    newSquad = Squad();
    newSquad.m_sId = sSquadId;
    newSquad.m_bInUse = true;

    m_SquadByName[sSquadId] = uiSquad;

    // an order that was given before the squad existed (an encounter wave releases its order with the wave)
    PendingOrder pending;
    if (m_PendingOrders.TryGetValue(sSquadId, pending))
    {
      StartOrder(newSquad, pending.m_Order, pending.m_Start);
      m_PendingOrders.Remove(sSquadId);
    }
  }

  MemberID id = InvalidMemberID;

  if (!m_FreeMembers.IsEmpty())
  {
    id = m_FreeMembers.PeekBack();
    m_FreeMembers.PopBack();
  }
  else
  {
    id = m_Members.GetCount();
    m_Members.ExpandAndGetRef();
  }

  Squad& squad = m_Squads[uiSquad];

  Member& member = m_Members[id];
  member = Member();
  member.m_hComponent = hAgentComponent;
  member.m_uiSquadIndex = uiSquad;
  member.m_bInUse = true;
  // join the smaller bounding group: after members left, alternating by count would unbalance them
  plUInt32 uiInGroupB = 0;
  for (MemberID other : squad.m_Members)
  {
    uiInGroupB += m_Members[other].m_uiBoundGroup;
  }

  member.m_uiBoundGroup = (uiInGroupB * 2 < squad.m_Members.GetCount()) ? 1 : 0;

  squad.m_Members.PushBack(id);

  if (squad.m_uiLosses > 0)
  {
    // reinforced: the squad as it is now is the new full strength
    squad.m_uiLosses = 0;
    squad.m_uiPeakSize = static_cast<plUInt16>(squad.m_Members.GetCount());
  }
  else
  {
    squad.m_uiPeakSize = plMath::Max<plUInt16>(squad.m_uiPeakSize, static_cast<plUInt16>(squad.m_Members.GetCount()));
  }

  return id;
}

void plAiSquadWorldModule::UnregisterMember(MemberID id)
{
  if (id >= m_Members.GetCount() || !m_Members[id].m_bInUse)
    return;

  Member& member = m_Members[id];

  if (member.m_uiSquadIndex < m_Squads.GetCount())
  {
    Squad& squad = m_Squads[member.m_uiSquadIndex];
    squad.m_Members.RemoveAndCopy(id);

    // a deactivated member counts as lost too (the world can't tell it from a death); if it comes back, it reinforces
    ++squad.m_uiLosses;

    if (squad.m_Members.IsEmpty())
    {
      m_SquadByName.Remove(squad.m_sId);
      squad.m_bInUse = false;
      m_FreeSquads.PushBack(member.m_uiSquadIndex);
    }
  }

  member.m_bInUse = false;
  member.m_hComponent.Invalidate();
  m_FreeMembers.PushBack(id);
}

plUInt32 plAiSquadWorldModule::GetSquadSize(const plTempHashedString& sSquadId) const
{
  plUInt32 uiSquad = plInvalidIndex;
  if (!m_SquadByName.TryGetValue(sSquadId, uiSquad))
    return 0;

  const plWorld* pWorld = GetWorld();
  plUInt32 uiActive = 0;

  // members deleted since the last squad tick are still listed until the next sweep
  for (MemberID id : m_Squads[uiSquad].m_Members)
  {
    const plComponent* pComponent = nullptr;
    if (pWorld->TryGetComponent(m_Members[id].m_hComponent, pComponent) && pComponent->IsActiveAndSimulating())
      ++uiActive;
  }

  return uiActive;
}

//////////////////////////////////////////////////////////////////////////
// orders

void plAiSquadWorldModule::IssueOrder(const plHashedString& sSquadId, const plAiSquadOrder& order)
{
  if (sSquadId.IsEmpty())
    return;

  if (!IsOrderIntent(order.m_Intent))
  {
    plLog::Warning("Squad order for '{}' ignored: the intent must be Engage, Advance, Hold or Retreat.", sSquadId);
    return;
  }

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  plUInt32 uiSquad = plInvalidIndex;
  if (m_SquadByName.TryGetValue(sSquadId, uiSquad))
  {
    StartOrder(m_Squads[uiSquad], order, now);
    m_PendingOrders.Remove(sSquadId);
    return;
  }

  PendingOrder& pending = m_PendingOrders[sSquadId];
  pending.m_Order = order;
  pending.m_Start = now;
}

void plAiSquadWorldModule::CancelOrder(const plTempHashedString& sSquadId)
{
  plUInt32 uiSquad = plInvalidIndex;
  if (m_SquadByName.TryGetValue(sSquadId, uiSquad) && m_Squads[uiSquad].m_bHasOrder)
  {
    EndOrder(m_Squads[uiSquad], GetWorld()->GetClock().GetAccumulatedTime(), "cancelled", false);
  }

  for (auto it = m_PendingOrders.GetIterator(); it.IsValid(); ++it)
  {
    if (it.Key() == sSquadId)
    {
      m_PendingOrders.Remove(it);
      break;
    }
  }
}

bool plAiSquadWorldModule::GetOrder(const plTempHashedString& sSquadId, plAiSquadOrder& out_order, plTime& out_remaining) const
{
  plUInt32 uiSquad = plInvalidIndex;
  if (!m_SquadByName.TryGetValue(sSquadId, uiSquad) || !m_Squads[uiSquad].m_bHasOrder)
    return false;

  const Squad& squad = m_Squads[uiSquad];
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  out_order = squad.m_Order;
  out_remaining = squad.m_Order.m_Duration.IsPositive() ? plMath::Max(plTime::MakeZero(), squad.m_Order.m_Duration - (now - squad.m_OrderStart)) : plTime::MakeZero();
  return true;
}

void plAiSquadWorldModule::StartOrder(Squad& squad, const plAiSquadOrder& order, plTime start)
{
  if (squad.m_bHasOrder)
  {
    EndOrder(squad, start, "replaced", true);
  }

  squad.m_bHasOrder = true;
  squad.m_Order = order;
  squad.m_OrderStart = start;
  squad.m_bOrderLossNoted = false;

  plStringBuilder sText;
  sText.SetFormat("Order {} issued by {}", GetIntentName(order.m_Intent), order.m_sIssuer.IsEmpty() ? plStringView("code") : order.m_sIssuer.GetView());

  if (order.m_Duration.IsPositive())
  {
    sText.AppendFormat(" for {} s", plArgF(order.m_Duration.GetSeconds(), 0));
  }

  if (order.m_bNoRetreat)
  {
    sText.Append(" \xC2\xB7 no retreat");
  }

  AddHistory(squad, start, sText);

  // the squad follows at once, instead of at the next dwell
  SetIntent(squad, order.m_Intent, start, {});
}

void plAiSquadWorldModule::EndOrder(Squad& squad, plTime now, plStringView sHow, bool bKeepIntentClock)
{
  if (!squad.m_bHasOrder)
    return;

  plStringBuilder sText;
  sText.SetFormat("Order {} {}", GetIntentName(squad.m_Order.m_Intent), sHow);
  AddHistory(squad, now, sText);

  squad.m_bHasOrder = false;
  squad.m_Order = plAiSquadOrder();

  if (!bKeepIntentClock)
  {
    // the squad's own reasoning takes over right away: its dwell has long passed
    squad.m_IntentSince = now - plTime::MakeFromSeconds(1.0e6);
  }
}

void plAiSquadWorldModule::AddHistory(Squad& squad, plTime now, plStringView sText)
{
  Squad::HistoryEntry& entry = squad.m_History.ExpandAndGetRef();
  entry.m_Time = now;
  entry.m_sText = sText;

  while (squad.m_History.GetCount() > s_uiMaxHistory)
  {
    squad.m_History.PopFront();
  }
}

void plAiSquadWorldModule::SetIntent(Squad& squad, plAiSquadIntent::Enum intent, plTime now, plStringView sHow)
{
  if (squad.m_Intent == intent)
    return;

  squad.m_Intent = intent;
  squad.m_IntentSince = now;

  if (!sHow.IsEmpty())
  {
    plStringBuilder sText;
    sText.SetFormat("{} intent {}", sHow, GetIntentName(intent));
    AddHistory(squad, now, sText);
  }
}

void plAiSquadWorldModule::GetSquadInfos(plDynamicArray<SquadInfo>& out_squads) const
{
  out_squads.Clear();

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  for (const Squad& squad : m_Squads)
  {
    if (!squad.m_bInUse || squad.m_Members.IsEmpty())
      continue;

    SquadInfo& info = out_squads.ExpandAndGetRef();
    info.m_sId = squad.m_sId;
    info.m_uiPeakSize = squad.m_uiPeakSize;
    info.m_uiLosses = squad.m_uiLosses;
    info.m_uiRetreatLosses = static_cast<plUInt32>(plMath::Ceil(plMath::Clamp(cvar_SquadRetreatLossFraction.GetValue(), 0.05f, 1.0f) * squad.m_uiPeakSize));
    info.m_Intent = squad.m_Intent;
    info.m_bHasOrder = squad.m_bHasOrder;
    info.m_Order = squad.m_Order;
    info.m_fOrderRemaining = (squad.m_bHasOrder && squad.m_Order.m_Duration.IsPositive()) ? plMath::Max(0.0, (squad.m_Order.m_Duration - (now - squad.m_OrderStart)).GetSeconds()) : 0.0;
    info.m_vHome = squad.m_vHome;
    info.m_bHasTarget = squad.m_fSharedConfidence > 0.1f;
    info.m_vTargetPosition = squad.m_vSharedTargetPos;

    for (const MemberID id : squad.m_Members)
    {
      const Member& member = m_Members[id];

      const plComponent* pComponent = nullptr;
      if (!GetWorld()->TryGetComponent(member.m_hComponent, pComponent))
        continue;

      MemberInfo& memberInfo = info.m_Members.ExpandAndGetRef();
      memberInfo.m_hOwner = pComponent->GetOwner()->GetHandle();
      memberInfo.m_vPosition = pComponent->GetOwner()->GetGlobalPosition();
      memberInfo.m_bAttackToken = member.m_bAttackToken;
      memberInfo.m_bMoveToken = member.m_bMoveToken;
    }

    for (plUInt32 i = squad.m_History.GetCount(); i-- > 0;)
    {
      HistoryInfo& entry = info.m_History.ExpandAndGetRef();
      entry.m_fTime = squad.m_History[i].m_Time.GetSeconds();
      entry.m_sText = squad.m_History[i].m_sText;
    }
  }
}

void plAiSquadWorldModule::UpdateSquads(const UpdateContext& ctxt)
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  // orders for squads that never formed run out too
  for (auto it = m_PendingOrders.GetIterator(); it.IsValid();)
  {
    if (it.Value().m_Order.m_Duration.IsPositive() && (now - it.Value().m_Start) >= it.Value().m_Order.m_Duration)
      it = m_PendingOrders.Remove(it);
    else
      ++it;
  }

  if (m_Squads.IsEmpty())
    return;

  const plUInt32 uiMaxTicks = static_cast<plUInt32>(plMath::Clamp<plInt32>(cvar_SquadMaxTicksPerFrame, 1, 64));

  plUInt32 uiTicked = 0;

  for (plUInt32 i = 0; i < m_Squads.GetCount() && uiTicked < uiMaxTicks; ++i)
  {
    m_uiTickCursor = (m_uiTickCursor + 1) % m_Squads.GetCount();
    Squad& squad = m_Squads[m_uiTickCursor];

    if (!squad.m_bInUse || now < squad.m_NextTick)
      continue;

    squad.m_NextTick = now + plTime::Seconds(plMath::Max(0.05f, cvar_SquadTickInterval.GetValue()));
    ++uiTicked;

    TickSquad(squad, now);
  }

  if (cvar_SquadShowDebug)
  {
    for (const Squad& squad : m_Squads)
    {
      if (squad.m_bInUse)
      {
        DrawSquadDebug(squad);
      }
    }
  }
}

void plAiSquadWorldModule::TickSquad(Squad& squad, plTime now)
{
  SweepMembers(squad);

  if (squad.m_Members.IsEmpty())
    return;

  ReadMemberScratch(squad);

  if (!squad.m_bHomeCaptured)
  {
    plVec3 vCentroid = plVec3::MakeZero();

    for (MemberID id : squad.m_Members)
    {
      vCentroid += m_Members[id].m_vPosition;
    }

    squad.m_vHome = vCentroid / static_cast<float>(squad.m_Members.GetCount());
    squad.m_bHomeCaptured = true;
  }

  ShareTargets(squad, now);
  EvaluateIntent(squad, now);
  AssignAttackTokens(squad, now);
  AssignMoveTokensAndPositions(squad, now);
  PublishMemberEntries(squad);
}

void plAiSquadWorldModule::SweepMembers(Squad& squad)
{
  for (plUInt32 i = squad.m_Members.GetCount(); i-- > 0;)
  {
    const MemberID id = squad.m_Members[i];
    plComponent* pComponent = nullptr;

    if (!GetWorld()->TryGetComponent(m_Members[id].m_hComponent, pComponent) || !pComponent->IsActiveAndSimulating())
    {
      squad.m_Members.RemoveAtAndCopy(i);
      ++squad.m_uiLosses;

      plStringBuilder sLost;
      sLost.SetFormat("Member lost \xC2\xB7 {} loss{}", squad.m_uiLosses, squad.m_uiLosses > 1 ? "es" : "");
      AddHistory(squad, GetWorld()->GetClock().GetAccumulatedTime(), sLost);

      m_Members[id].m_bInUse = false;
      m_Members[id].m_hComponent.Invalidate();
      m_FreeMembers.PushBack(id);
    }
  }
}

void plAiSquadWorldModule::ReadMemberScratch(Squad& squad)
{
  static const plHashedString sTargetPosition = plMakeHashedString("Ai_TargetPosition");
  static const plHashedString sTargetConfidence = plMakeHashedString("Ai_TargetConfidence");
  static const plHashedString sTargetObject = plMakeHashedString("Ai_TargetObject");
  static const plHashedString sTargetExposure = plMakeHashedString("Ai_TargetExposure");
  static const plHashedString sCoverStatus = plMakeHashedString("Ai_CoverStatus");

  for (MemberID id : squad.m_Members)
  {
    Member& member = m_Members[id];

    plComponent* pComponent = nullptr;
    GetWorld()->TryGetComponent(member.m_hComponent, pComponent); // swept above; cannot fail this tick

    plGameObject* pOwner = pComponent->GetOwner();
    member.m_vPosition = pOwner->GetGlobalPosition();

    member.m_fTargetConfidence = 0.0f;
    member.m_fExposure = 0.0f;
    member.m_fCoverStatus = 0.0f;
    member.m_hTarget.Invalidate();

    const plSharedPtr<plBlackboard>& pBlackboard = plBlackboardComponent::FindBlackboard(pOwner);
    member.m_bHasBlackboard = pBlackboard != nullptr;

    if (pBlackboard == nullptr)
      continue;

    const plVariant conf = pBlackboard->GetEntryValue(sTargetConfidence);
    if (conf.IsValid() && conf.CanConvertTo<float>())
      member.m_fTargetConfidence = conf.ConvertTo<float>();

    const plVariant pos = pBlackboard->GetEntryValue(sTargetPosition);
    if (pos.IsA<plVec3>())
      member.m_vTargetPos = pos.Get<plVec3>();

    const plVariant target = pBlackboard->GetEntryValue(sTargetObject);
    if (target.IsA<plGameObjectHandle>())
      member.m_hTarget = target.Get<plGameObjectHandle>();

    const plVariant exposure = pBlackboard->GetEntryValue(sTargetExposure);
    if (exposure.IsValid() && exposure.CanConvertTo<float>())
      member.m_fExposure = exposure.ConvertTo<float>();

    const plVariant cover = pBlackboard->GetEntryValue(sCoverStatus);
    if (cover.IsValid() && cover.CanConvertTo<float>())
      member.m_fCoverStatus = cover.ConvertTo<float>();
  }
}

void plAiSquadWorldModule::ShareTargets(Squad& squad, plTime now)
{
  // track the squad's best target knowledge (also drives intent + debug, even when sharing is muted)
  squad.m_fSharedConfidence = 0.0f;
  squad.m_hSharedTarget.Invalidate();

  for (MemberID id : squad.m_Members)
  {
    const Member& member = m_Members[id];

    if (member.m_fTargetConfidence > squad.m_fSharedConfidence && !member.m_hTarget.IsInvalidated())
    {
      squad.m_fSharedConfidence = member.m_fTargetConfidence;
      squad.m_hSharedTarget = member.m_hTarget;
      squad.m_vSharedTargetPos = member.m_vTargetPos;
    }
  }

  if (cvar_SquadMuteSharing || m_pPerception == nullptr)
    return;

  if (squad.m_fSharedConfidence < m_Config.m_fShareConfidence)
    return;

  if (now - squad.m_LastShare < plTime::Seconds(plMath::Max(0.05f, m_Config.m_fShareInterval)))
    return;

  squad.m_LastShare = now;

  // the stimulus position determines WHO receives it - post at the squad centroid with a radius
  // covering the spread, so every member gets the knowledge no matter where the enemy is
  plVec3 vCentroid = plVec3::MakeZero();

  for (MemberID id : squad.m_Members)
  {
    vCentroid += m_Members[id].m_vPosition;
  }

  vCentroid /= static_cast<float>(squad.m_Members.GetCount());

  float fMaxDistSqr = 0.0f;

  for (MemberID id : squad.m_Members)
  {
    fMaxDistSqr = plMath::Max(fMaxDistSqr, (m_Members[id].m_vPosition - vCentroid).GetLengthSquared());
  }

  // the enemy's team, so the faction filter delivers to its enemies (= this squad) only
  plHashedString sSourceTeam;

  if (plGameObject* pTarget = nullptr; GetWorld()->TryGetObject(squad.m_hSharedTarget, pTarget))
  {
    plAiAgentComponent* pTargetAgent = nullptr;

    if (pTarget->TryGetComponentOfBaseType(pTargetAgent))
    {
      sSourceTeam = pTargetAgent->GetResolvedTeam();
    }
  }
  else
  {
    return; // shared target died - nothing to share
  }

  plAiStimulus stimulus;
  stimulus.m_Type = plAiStimulusType::Custom;
  stimulus.m_sCustomType.Assign("SquadShare");
  stimulus.m_vGlobalPosition = vCentroid;
  stimulus.m_fRadius = plMath::Sqrt(fMaxDistSqr) + 5.0f;
  stimulus.m_fStrength = plMath::Clamp(m_Config.m_fShareStrength, 0.0f, 1.0f);
  stimulus.m_hSource = squad.m_hSharedTarget;
  stimulus.m_sSourceTeam = sSourceTeam;

  // broadcast around the squad, but members remember where the target was reported
  stimulus.m_vEventPosition = squad.m_vSharedTargetPos;
  stimulus.m_bHasEventPosition = true;
  stimulus.m_bReport = true;

  m_pPerception->PostStimulus(stimulus);
  ++m_uiStatSharesPosted;
}

void plAiSquadWorldModule::EvaluateIntent(Squad& squad, plTime now)
{
  const float fDwell = plMath::Max(0.0f, cvar_SquadIntentMinDwell.GetValue());
  const bool bDwellElapsed = (now - squad.m_IntentSince) >= plTime::Seconds(fDwell);

  plAiSquadIntent::Enum newIntent = static_cast<plAiSquadIntent::Enum>(squad.m_Intent.GetValue());

  const plTime retreatHold = plTime::Seconds(plMath::Max(0.0f, cvar_SquadRetreatHoldTime.GetValue()));
  const plUInt16 uiRetreatLosses = static_cast<plUInt16>(plMath::Ceil(plMath::Clamp(cvar_SquadRetreatLossFraction.GetValue(), 0.05f, 1.0f) * squad.m_uiPeakSize));
  const bool bLossesReached = squad.m_uiLosses >= uiRetreatLosses && squad.m_uiPeakSize >= 2;

  // an order decides the intent, until it runs out or the losses break it
  if (squad.m_bHasOrder)
  {
    if (squad.m_Order.m_Duration.IsPositive() && (now - squad.m_OrderStart) >= squad.m_Order.m_Duration)
    {
      EndOrder(squad, now, "expired", false);
      EvaluateIntent(squad, now); // the dwell has passed: the automatic intent follows right away
      return;
    }

    if (bLossesReached && !squad.m_Order.m_bNoRetreat)
    {
      EndOrder(squad, now, "broken by losses", true);
    }
    else
    {
      if (bLossesReached && !squad.m_bOrderLossNoted)
      {
        squad.m_bOrderLossNoted = true;
        AddHistory(squad, now, "Losses reached the retreat limit \xC2\xB7 no retreat keeps the order");
      }

      SetIntent(squad, static_cast<plAiSquadIntent::Enum>(squad.m_Order.m_Intent.GetValue()), now, {});
      return;
    }
  }

  // a retreat that has held long enough is over: the survivors are the squad now, only new losses retreat again
  if (squad.m_Intent == plAiSquadIntent::Retreat && (now - squad.m_IntentSince) >= retreatHold && squad.m_uiLosses > 0)
  {
    squad.m_uiLosses = 0;
    squad.m_uiPeakSize = static_cast<plUInt16>(squad.m_Members.GetCount());
    AddHistory(squad, now, "Retreat held \xC2\xB7 the survivors are the squad");
  }

  // retreat escalates immediately and holds for a while
  if (bLossesReached && squad.m_uiLosses >= uiRetreatLosses)
  {
    SetIntent(squad, plAiSquadIntent::Retreat, now, "Automatic");
    return;
  }

  if (squad.m_Intent == plAiSquadIntent::Retreat && (now - squad.m_IntentSince) < retreatHold)
    return;

  if (!bDwellElapsed)
    return;

  if (squad.m_fSharedConfidence > 0.6f)
  {
    // known target: advance when the squad is far and nobody is under direct fire, else engage
    plVec3 vCentroid = plVec3::MakeZero();
    float fMaxExposure = 0.0f;

    for (MemberID id : squad.m_Members)
    {
      vCentroid += m_Members[id].m_vPosition;
      fMaxExposure = plMath::Max(fMaxExposure, m_Members[id].m_fExposure);
    }

    vCentroid /= static_cast<float>(squad.m_Members.GetCount());

    const float fDist = (squad.m_vSharedTargetPos - vCentroid).GetAsVec2().GetLength();

    newIntent = (fDist > cvar_SquadAdvanceTriggerDistance.GetValue() && fMaxExposure < 0.5f) ? plAiSquadIntent::Advance : plAiSquadIntent::Engage;
  }
  else if (squad.m_fSharedConfidence > 0.1f)
  {
    newIntent = plAiSquadIntent::Engage; // stale knowledge - keep fighting posture
  }
  else
  {
    newIntent = plAiSquadIntent::Hold;
  }

  SetIntent(squad, newIntent, now, "Automatic");
}

void plAiSquadWorldModule::AssignAttackTokens(Squad& squad, plTime now)
{
  const plUInt32 uiMaxTokens = m_Config.m_uiAttackTokens;
  const plTime minHold = plTime::Seconds(plMath::Max(0.0f, cvar_SquadTokenMinHold.GetValue()));

  auto rankMember = [&](const Member& member) -> float {
    const float fDist = (member.m_vPosition - squad.m_vSharedTargetPos).GetAsVec2().GetLength();
    return member.m_fTargetConfidence + 0.5f * member.m_fCoverStatus + 0.25f * (1.0f - member.m_fExposure) - 0.02f * fDist;
  };

  // holders keep their token while they still track a target (keep-threshold below acquire-threshold)
  plUInt32 uiHeld = 0;

  for (MemberID id : squad.m_Members)
  {
    Member& member = m_Members[id];

    if (!member.m_bAttackToken)
      continue;

    if (member.m_fTargetConfidence <= 0.3f || uiHeld >= uiMaxTokens)
    {
      member.m_bAttackToken = false;
      continue;
    }

    ++uiHeld;
  }

  // hand out free tokens to the best candidates
  while (uiHeld < uiMaxTokens)
  {
    MemberID bestId = InvalidMemberID;
    float fBestRank = -plMath::HighValue<float>();

    for (MemberID id : squad.m_Members)
    {
      Member& member = m_Members[id];

      if (member.m_bAttackToken || member.m_fTargetConfidence <= 0.6f)
        continue;

      const float fRank = rankMember(member);

      if (fRank > fBestRank)
      {
        fBestRank = fRank;
        bestId = id;
      }
    }

    if (bestId == InvalidMemberID)
      break;

    m_Members[bestId].m_bAttackToken = true;
    m_Members[bestId].m_TokenSince = now;
    ++uiHeld;
    ++m_uiStatTokensGranted;
  }

  // a strictly better candidate may replace the weakest long-enough holder
  if (uiHeld >= uiMaxTokens && uiMaxTokens > 0)
  {
    MemberID weakestHolder = InvalidMemberID;
    float fWeakestRank = plMath::HighValue<float>();
    MemberID bestFree = InvalidMemberID;
    float fBestFreeRank = -plMath::HighValue<float>();

    for (MemberID id : squad.m_Members)
    {
      Member& member = m_Members[id];
      const float fRank = rankMember(member);

      if (member.m_bAttackToken)
      {
        if ((now - member.m_TokenSince) >= minHold && fRank < fWeakestRank)
        {
          fWeakestRank = fRank;
          weakestHolder = id;
        }
      }
      else if (member.m_fTargetConfidence > 0.6f && fRank > fBestFreeRank)
      {
        fBestFreeRank = fRank;
        bestFree = id;
      }
    }

    if (weakestHolder != InvalidMemberID && bestFree != InvalidMemberID && fBestFreeRank > fWeakestRank + 0.25f)
    {
      m_Members[weakestHolder].m_bAttackToken = false;
      m_Members[bestFree].m_bAttackToken = true;
      m_Members[bestFree].m_TokenSince = now;
    }
  }
}

void plAiSquadWorldModule::AssignMoveTokensAndPositions(Squad& squad, plTime now)
{
  if (squad.m_Intent == plAiSquadIntent::Retreat)
  {
    for (MemberID id : squad.m_Members)
    {
      m_Members[id].m_bMoveToken = true;
    }

    // a retreat order with a region falls back into it
    if (squad.m_bHasOrder && squad.m_Order.m_bHasRegion)
    {
      const plVec2 vCenter = squad.m_Order.GetRegionCenter();
      squad.m_vSharedMovePos = vCenter.GetAsVec3(squad.m_vHome.z);
      return;
    }

    // the rally point is chosen once per retreat: computed from the moving centroid every tick, it would move with the
    // squad and the retreat would never arrive
    if (squad.m_RallyChosenFor == squad.m_IntentSince)
      return;

    squad.m_RallyChosenFor = squad.m_IntentSince;

    // everyone falls back to the rally point; push it away from the threat when home lies toward it
    plVec3 vRally = squad.m_vHome;

    if (!squad.m_hSharedTarget.IsInvalidated() || squad.m_fSharedConfidence > 0.1f)
    {
      plVec3 vCentroid = plVec3::MakeZero();

      for (MemberID id : squad.m_Members)
      {
        vCentroid += m_Members[id].m_vPosition;
      }

      vCentroid /= static_cast<float>(squad.m_Members.GetCount());

      plVec3 vAway = vCentroid - squad.m_vSharedTargetPos;
      vAway.z = 0;

      if ((squad.m_vHome - squad.m_vSharedTargetPos).GetAsVec2().GetLengthSquared() < (vCentroid - squad.m_vSharedTargetPos).GetAsVec2().GetLengthSquared() && vAway.NormalizeIfNotZero(plVec3(1, 0, 0)).Succeeded())
      {
        vRally = vCentroid + vAway * cvar_SquadRetreatDistance.GetValue();
      }
    }

    squad.m_vSharedMovePos = vRally;
    return;
  }

  if (squad.m_Intent != plAiSquadIntent::Advance)
  {
    for (MemberID id : squad.m_Members)
    {
      m_Members[id].m_bMoveToken = false;
    }

    return;
  }

  // bounding overwatch: one group moves toward the target, the other covers; swap periodically
  if ((now - squad.m_LastBoundSwap) >= plTime::Seconds(plMath::Max(0.5f, cvar_SquadBoundPeriod.GetValue())))
  {
    squad.m_uiMovingGroup ^= 1;
    squad.m_LastBoundSwap = now;
  }

  const float fStep = plMath::Max(1.0f, cvar_SquadBoundStep.GetValue());

  for (MemberID id : squad.m_Members)
  {
    Member& member = m_Members[id];
    member.m_bMoveToken = (member.m_uiBoundGroup == squad.m_uiMovingGroup);
  }
}

void plAiSquadWorldModule::PublishMemberEntries(Squad& squad)
{
  static const plHashedString sHasAttackToken = plMakeHashedString("Ai_HasAttackToken");
  static const plHashedString sSquadIntent = plMakeHashedString("Ai_SquadIntent");
  static const plHashedString sSquadMovePos = plMakeHashedString("Ai_SquadMovePos");

  static const plHashedString sOrderActive = plMakeHashedString("Ai_SquadOrderActive");
  static const plHashedString sRegionCenter = plMakeHashedString("Ai_SquadRegionCenter");

  const float fStep = plMath::Max(1.0f, cvar_SquadBoundStep.GetValue());
  const bool bRegion = squad.m_bHasOrder && squad.m_Order.m_bHasRegion;
  const plUInt32 uiMembers = squad.m_Members.GetCount();

  for (plUInt32 uiMember = 0; uiMember < uiMembers; ++uiMember)
  {
    Member& member = m_Members[squad.m_Members[uiMember]];

    if (!member.m_bHasBlackboard)
      continue;

    plComponent* pComponent = nullptr;
    GetWorld()->TryGetComponent(member.m_hComponent, pComponent);

    const plSharedPtr<plBlackboard>& pBlackboard = plBlackboardComponent::FindBlackboard(pComponent->GetOwner());

    if (pBlackboard == nullptr)
      continue;

    pBlackboard->SetEntryValue(sHasAttackToken, member.m_bAttackToken ? 1.0f : 0.0f);

    // members without a move token see intent Engage during an advance (they cover)
    plAiSquadIntent::Enum memberIntent = static_cast<plAiSquadIntent::Enum>(squad.m_Intent.GetValue());

    if (memberIntent == plAiSquadIntent::Advance && !member.m_bMoveToken)
    {
      memberIntent = plAiSquadIntent::Engage;
    }

    pBlackboard->SetEntryValue(sSquadIntent, static_cast<plInt32>(memberIntent));

    // per-member move destination
    plVec3 vMovePos = member.m_vPosition;

    if (bRegion)
    {
      // an order with a region: a spot of its own inside it. An advance gets there in bounds, the rest go straight to it
      const plVec3 vSlot = squad.m_Order.GetRegionSlot(uiMember, uiMembers).GetAsVec3(member.m_vPosition.z);

      if (memberIntent == plAiSquadIntent::Advance)
      {
        plVec3 vToSlot = vSlot - member.m_vPosition;
        vToSlot.z = 0;
        const float fDist = vToSlot.GetLength();

        if (fDist > 0.1f)
        {
          vMovePos = member.m_vPosition + vToSlot * (plMath::Min(fStep, fDist) / fDist);
        }
      }
      else
      {
        vMovePos = vSlot;
      }
    }
    else if (memberIntent == plAiSquadIntent::Retreat)
    {
      vMovePos = squad.m_vSharedMovePos;
    }
    else if (memberIntent == plAiSquadIntent::Advance)
    {
      plVec3 vToTarget = squad.m_vSharedTargetPos - member.m_vPosition;
      vToTarget.z = 0;
      const float fDist = vToTarget.GetLength();

      if (fDist > 0.1f)
      {
        vMovePos = member.m_vPosition + vToTarget * (plMath::Min(fStep, fDist) / fDist);
      }
    }

    pBlackboard->SetEntryValue(sSquadMovePos, vMovePos);
    pBlackboard->SetEntryValue(sOrderActive, squad.m_bHasOrder);
    pBlackboard->SetEntryValue(sRegionCenter, bRegion ? squad.m_Order.GetRegionCenter().GetAsVec3(member.m_vPosition.z) : member.m_vPosition);
  }
}

void plAiSquadWorldModule::DrawSquadDebug(const Squad& squad)
{
  if (squad.m_Members.IsEmpty())
    return;

  // stable per-squad color from the id hash
  const plUInt32 uiHash = squad.m_sId.GetHash();
  const plColor squadColor = plColor::MakeHSV(static_cast<float>(uiHash % 360u), 0.85f, 1.0f);

  plVec3 vCentroid = plVec3::MakeZero();
  plHybridArray<plDebugRenderer::Line, 16> lines;

  for (MemberID id : squad.m_Members)
  {
    vCentroid += m_Members[id].m_vPosition;
  }

  vCentroid /= static_cast<float>(squad.m_Members.GetCount());

  for (MemberID id : squad.m_Members)
  {
    const Member& member = m_Members[id];

    auto& line = lines.ExpandAndGetRef();
    line.m_start = vCentroid + plVec3(0, 0, 0.5f);
    line.m_end = member.m_vPosition + plVec3(0, 0, 0.5f);
    line.m_startColor = line.m_endColor = squadColor;

    if (member.m_bAttackToken)
    {
      plDebugRenderer::DrawLineSphere(GetWorld(), plBoundingSphere::MakeFromCenterAndRadius(member.m_vPosition + plVec3(0, 0, 2.4f), 0.15f), plColor::Red);
    }

    if (member.m_bMoveToken && squad.m_Intent == plAiSquadIntent::Advance)
    {
      plDebugRenderer::DrawLineSphere(GetWorld(), plBoundingSphere::MakeFromCenterAndRadius(member.m_vPosition + plVec3(0, 0, 2.7f), 0.12f), plColor::CornflowerBlue);
    }

    if (!member.m_bHasBlackboard)
    {
      plDebugRenderer::Draw3DText(GetWorld(), "NO BB", member.m_vPosition + plVec3(0, 0, 2.0f), plColor::OrangeRed);
    }
  }

  if (squad.m_fSharedConfidence > 0.1f)
  {
    auto& line = lines.ExpandAndGetRef();
    line.m_start = vCentroid + plVec3(0, 0, 0.5f);
    line.m_end = squad.m_vSharedTargetPos + plVec3(0, 0, 0.5f);
    line.m_startColor = line.m_endColor = plColor::OrangeRed;
  }

  plDebugRenderer::DrawLines(GetWorld(), lines, plColor::White);

  plStringBuilder sText;
  plStringView sIntent = "None";

  switch (squad.m_Intent.GetValue())
  {
    case plAiSquadIntent::Engage:  sIntent = "Engage"; break;
    case plAiSquadIntent::Advance: sIntent = "Advance"; break;
    case plAiSquadIntent::Hold:    sIntent = "Hold"; break;
    case plAiSquadIntent::Retreat: sIntent = "Retreat"; break;
    default: break;
  }

  sText.SetFormat("{}: {} ({} members, {} losses)", squad.m_sId, sIntent, squad.m_Members.GetCount(), squad.m_uiLosses);

  if (squad.m_bHasOrder)
  {
    sText.AppendFormat(" \xC2\xB7 order from {}", squad.m_Order.m_sIssuer.IsEmpty() ? plStringView("code") : squad.m_Order.m_sIssuer.GetView());
  }

  plDebugRenderer::Draw3DText(GetWorld(), sText, vCentroid + plVec3(0, 0, 1.0f), squadColor);

  DrawOrder(squad);
}

void plAiSquadWorldModule::DrawOrder(const Squad& squad)
{
  if (!squad.m_bHasOrder || !squad.m_Order.m_bHasRegion)
    return;

  const plAiSquadOrder& order = squad.m_Order;
  const float fZ = squad.m_vHome.z + 0.2f;
  const plColor color = plColorGammaUB(0xcd, 0x8c, 0x96);

  const plVec3 corners[4] = {
    plVec3(order.m_vRegionMin.x, order.m_vRegionMin.y, fZ),
    plVec3(order.m_vRegionMax.x, order.m_vRegionMin.y, fZ),
    plVec3(order.m_vRegionMax.x, order.m_vRegionMax.y, fZ),
    plVec3(order.m_vRegionMin.x, order.m_vRegionMax.y, fZ),
  };

  plHybridArray<plDebugRenderer::Line, 4> lines;

  for (plUInt32 i = 0; i < 4; ++i)
  {
    plDebugRenderer::Line& line = lines.ExpandAndGetRef();
    line.m_start = corners[i];
    line.m_end = corners[(i + 1) % 4];
    line.m_startColor = color;
    line.m_endColor = color;
  }

  plDebugRenderer::DrawLines(GetWorld(), lines, plColor::White);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Implementation_SquadWorldModule);
