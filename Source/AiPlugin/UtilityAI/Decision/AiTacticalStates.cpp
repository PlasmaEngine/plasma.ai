#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Decision/AiTacticalStates.h>
#include <Core/Utils/Blackboard.h>
#include <Core/World/World.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiTacticalThreatSource, 1)
  PL_ENUM_CONSTANTS(plAiTacticalThreatSource::PerceivedTarget, plAiTacticalThreatSource::BlackboardEntry, plAiTacticalThreatSource::None)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiPickTacticalPoint, 1, plRTTIDefaultAllocator<plStateMachineState_AiPickTacticalPoint>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Preset", plAiTacticalQueryPreset, m_Preset),
    PL_ENUM_MEMBER_PROPERTY("ThreatSource", plAiTacticalThreatSource, m_ThreatSource),
    PL_ACCESSOR_PROPERTY("ThreatEntry", GetThreatEntry, SetThreatEntry),
    PL_MEMBER_PROPERTY("RadiusMin", m_fRadiusMin)->AddAttributes(new plDefaultValueAttribute(2.0f), new plClampValueAttribute(0.0f, 100.0f)),
    PL_MEMBER_PROPERTY("RadiusMax", m_fRadiusMax)->AddAttributes(new plDefaultValueAttribute(12.0f), new plClampValueAttribute(0.5f, 200.0f)),
    PL_MEMBER_PROPERTY("FlankAngle", m_FlankAngle)->AddAttributes(new plDefaultValueAttribute(plAngle::MakeFromDegree(100))),
    PL_ENUM_MEMBER_PROPERTY("MinCoverQuality", plAiCoverQuality, m_MinQuality),
    PL_ACCESSOR_PROPERTY("TargetEntry", GetTargetEntry, SetTargetEntry)->AddAttributes(new plDefaultValueAttribute(plStringView("Ai_TacticalTarget"))),
    PL_ACCESSOR_PROPERTY("ResultEntry", GetResultEntry, SetResultEntry)->AddAttributes(new plDefaultValueAttribute(plStringView("Ai_TacticalResult"))),
    PL_MEMBER_PROPERTY("ClaimCover", m_bClaimCover)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("ClaimTimeout", m_ClaimTimeout)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(20.0))),
    PL_MEMBER_PROPERTY("ReleaseClaimOnExit", m_bReleaseClaimOnExit),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

namespace
{
  plGameObject* GetOwnerObject(plStateMachineInstance& ref_instance)
  {
    if (plComponent* pComponent = plDynamicCast<plComponent*>(&ref_instance.GetOwner()))
    {
      return pComponent->GetOwner();
    }

    return nullptr;
  }

  void SetBlackboardIntTactical(plStateMachineInstance& ref_instance, const plHashedString& sEntry, plInt32 iValue)
  {
    if (sEntry.IsEmpty())
      return;

    if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
    {
      pBlackboard->SetEntryValue(sEntry, iValue);
    }
  }

  bool ResolveThreatPosition(plStateMachineInstance& ref_instance, plAiTacticalThreatSource::Enum source, const plHashedString& sThreatEntry, plVec3& out_vThreat)
  {
    const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard();

    if (pBlackboard == nullptr)
      return false;

    if (source == plAiTacticalThreatSource::PerceivedTarget)
    {
      static const plHashedString sTargetPosition = plMakeHashedString("Ai_TargetPosition");
      static const plHashedString sTargetConfidence = plMakeHashedString("Ai_TargetConfidence");

      const plVariant pos = pBlackboard->GetEntryValue(sTargetPosition);
      const plVariant conf = pBlackboard->GetEntryValue(sTargetConfidence);

      if (pos.IsA<plVec3>() && conf.IsValid() && conf.CanConvertTo<float>() && conf.ConvertTo<float>() > 0.01f)
      {
        out_vThreat = pos.Get<plVec3>();
        return true;
      }

      return false;
    }

    if (source == plAiTacticalThreatSource::BlackboardEntry)
    {
      const plVariant pos = pBlackboard->GetEntryValue(sThreatEntry);

      if (pos.IsA<plVec3>())
      {
        out_vThreat = pos.Get<plVec3>();
        return true;
      }
    }

    return false;
  }
} // namespace

plStateMachineState_AiPickTacticalPoint::plStateMachineState_AiPickTacticalPoint(plStringView sName)
  : plStateMachineState(sName)
{
  m_sTargetEntry.Assign("Ai_TacticalTarget");
  m_sResultEntry.Assign("Ai_TacticalResult");
}

plStateMachineState_AiPickTacticalPoint::~plStateMachineState_AiPickTacticalPoint() = default;

bool plStateMachineState_AiPickTacticalPoint::SubmitQuery(plStateMachineInstance& ref_instance, InstanceData* pData) const
{
  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObject(ref_instance);

  if (pWorld == nullptr || pOwner == nullptr)
    return false;

  auto* pTactical = pWorld->GetModule<plAiTacticalWorldModule>();

  if (pTactical == nullptr)
    return false;

  const plVec3 vQuerier = pOwner->GetGlobalPosition();
  plVec3 vThreat = plVec3::MakeZero();
  const bool bHasThreat = ResolveThreatPosition(ref_instance, static_cast<plAiTacticalThreatSource::Enum>(m_ThreatSource.GetValue()), m_sThreatEntry, vThreat);

  plAiTacticalQueryDesc desc = plAiTacticalQueryDesc::MakeFromPreset(static_cast<plAiTacticalQueryPreset::Enum>(m_Preset.GetValue()), vQuerier, vThreat, bHasThreat, m_fRadiusMin, m_fRadiusMax, m_FlankAngle, static_cast<plAiCoverQuality::Enum>(m_MinQuality.GetValue()));
  desc.m_hClaimant = pOwner->GetHandle();

  pData->m_QueryId = pTactical->SubmitQuery(desc);
  return pData->m_QueryId != plAiTacticalWorldModule::InvalidQueryID;
}

void plStateMachineState_AiPickTacticalPoint::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);
  pData->m_QueryId = plAiTacticalWorldModule::InvalidQueryID;
  pData->m_uiRetries = 0;
  pData->m_bDone = false;

  if (!SubmitQuery(ref_instance, pData))
  {
    SetBlackboardIntTactical(ref_instance, m_sResultEntry, 2);
    pData->m_bDone = true;
    return;
  }

  SetBlackboardIntTactical(ref_instance, m_sResultEntry, 0);
}

void plStateMachineState_AiPickTacticalPoint::Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  if (pData->m_bDone)
    return;

  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObject(ref_instance);

  if (pWorld == nullptr || pOwner == nullptr)
    return;

  auto* pTactical = pWorld->GetModule<plAiTacticalWorldModule>();

  if (pTactical == nullptr)
  {
    SetBlackboardIntTactical(ref_instance, m_sResultEntry, 2);
    pData->m_bDone = true;
    return;
  }

  // waiting for a scheduled retry (area was not loaded yet)
  if (pData->m_QueryId == plAiTacticalWorldModule::InvalidQueryID)
  {
    if (pWorld->GetClock().GetAccumulatedTime() >= pData->m_RetryAt)
    {
      if (!SubmitQuery(ref_instance, pData))
      {
        SetBlackboardIntTactical(ref_instance, m_sResultEntry, 2);
        pData->m_bDone = true;
      }
    }

    return;
  }

  plAiTacticalQueryResult result;

  if (!pTactical->TryGetResult(pData->m_QueryId, result))
    return; // still pending

  pTactical->ReleaseQuery(pData->m_QueryId);
  pData->m_QueryId = plAiTacticalWorldModule::InvalidQueryID;

  if (result.m_Status == plAiTacticalQueryResult::Status::AreaNotReady)
  {
    if (pData->m_uiRetries < 3)
    {
      ++pData->m_uiRetries;
      pData->m_RetryAt = pWorld->GetClock().GetAccumulatedTime() + plTime::Seconds(0.5);
    }
    else
    {
      SetBlackboardIntTactical(ref_instance, m_sResultEntry, 2);
      pData->m_bDone = true;
    }

    return;
  }

  if (result.m_Status != plAiTacticalQueryResult::Status::Ready)
  {
    SetBlackboardIntTactical(ref_instance, m_sResultEntry, 2);
    pData->m_bDone = true;
    return;
  }

  // pick the best candidate we can actually use: cover points may have been claimed by another
  // agent since the query executed (one frame ago) - fall through to the next candidate then
  for (const auto& candidate : result.m_TopN)
  {
    if (candidate.m_CoverHandle.IsValid() && m_bClaimCover)
    {
      if (!pTactical->ClaimCover(candidate.m_CoverHandle, pOwner->GetHandle(), m_ClaimTimeout))
        continue;
    }

    if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
    {
      pBlackboard->SetEntryValue(m_sTargetEntry, candidate.m_vPosition);
    }

    SetBlackboardIntTactical(ref_instance, m_sResultEntry, 1);
    pData->m_bDone = true;
    return;
  }

  // every candidate was snatched away - try once more, then give up
  if (pData->m_uiRetries < 3)
  {
    ++pData->m_uiRetries;
    pData->m_RetryAt = pWorld->GetClock().GetAccumulatedTime() + plTime::Seconds(0.25);
    return;
  }

  SetBlackboardIntTactical(ref_instance, m_sResultEntry, 2);
  pData->m_bDone = true;
}

void plStateMachineState_AiPickTacticalPoint::OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  plWorld* pWorld = ref_instance.GetOwnerWorld();

  if (pWorld == nullptr)
    return;

  auto* pTactical = pWorld->GetModule<plAiTacticalWorldModule>();

  if (pTactical == nullptr)
    return;

  if (pData->m_QueryId != plAiTacticalWorldModule::InvalidQueryID)
  {
    pTactical->ReleaseQuery(pData->m_QueryId);
    pData->m_QueryId = plAiTacticalWorldModule::InvalidQueryID;
  }

  if (m_bReleaseClaimOnExit)
  {
    if (plGameObject* pOwner = GetOwnerObject(ref_instance))
    {
      pTactical->ReleaseClaim(pOwner->GetHandle());
    }
  }
}

plResult plStateMachineState_AiPickTacticalPoint::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));

  inout_stream << m_Preset;
  inout_stream << m_ThreatSource;
  inout_stream << m_sThreatEntry;
  inout_stream << m_fRadiusMin;
  inout_stream << m_fRadiusMax;
  inout_stream << m_FlankAngle;
  inout_stream << m_MinQuality;
  inout_stream << m_sTargetEntry;
  inout_stream << m_sResultEntry;
  inout_stream << m_bClaimCover;
  inout_stream << m_ClaimTimeout;
  inout_stream << m_bReleaseClaimOnExit;

  return PL_SUCCESS;
}

plResult plStateMachineState_AiPickTacticalPoint::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));

  inout_stream >> m_Preset;
  inout_stream >> m_ThreatSource;
  inout_stream >> m_sThreatEntry;
  inout_stream >> m_fRadiusMin;
  inout_stream >> m_fRadiusMax;
  inout_stream >> m_FlankAngle;
  inout_stream >> m_MinQuality;
  inout_stream >> m_sTargetEntry;
  inout_stream >> m_sResultEntry;
  inout_stream >> m_bClaimCover;
  inout_stream >> m_ClaimTimeout;
  inout_stream >> m_bReleaseClaimOnExit;

  return PL_SUCCESS;
}

bool plStateMachineState_AiPickTacticalPoint::GetInstanceDataDesc(plInstanceDataDesc& out_desc)
{
  out_desc.FillFromType<InstanceData>();
  return true;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiPickPeekPosition, 1, plRTTIDefaultAllocator<plStateMachineState_AiPickPeekPosition>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("ThreatSource", plAiTacticalThreatSource, m_ThreatSource),
    PL_ACCESSOR_PROPERTY("ThreatEntry", GetThreatEntry, SetThreatEntry),
    PL_MEMBER_PROPERTY("MaxSideStep", m_fMaxSideStep)->AddAttributes(new plDefaultValueAttribute(2.5f), new plClampValueAttribute(0.5f, 10.0f)),
    PL_ACCESSOR_PROPERTY("TargetEntry", GetTargetEntry, SetTargetEntry)->AddAttributes(new plDefaultValueAttribute(plStringView("Ai_PeekTarget"))),
    PL_ACCESSOR_PROPERTY("ResultEntry", GetResultEntry, SetResultEntry)->AddAttributes(new plDefaultValueAttribute(plStringView("Ai_PeekResult"))),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plStateMachineState_AiPickPeekPosition::plStateMachineState_AiPickPeekPosition(plStringView sName)
  : plStateMachineState(sName)
{
  m_sTargetEntry.Assign("Ai_PeekTarget");
  m_sResultEntry.Assign("Ai_PeekResult");
}

plStateMachineState_AiPickPeekPosition::~plStateMachineState_AiPickPeekPosition() = default;

void plStateMachineState_AiPickPeekPosition::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObject(ref_instance);

  if (pWorld == nullptr || pOwner == nullptr)
  {
    SetBlackboardIntTactical(ref_instance, m_sResultEntry, 2);
    return;
  }

  auto* pTactical = pWorld->GetModule<plAiTacticalWorldModule>();

  plVec3 vThreat = plVec3::MakeZero();

  if (pTactical == nullptr || !ResolveThreatPosition(ref_instance, static_cast<plAiTacticalThreatSource::Enum>(m_ThreatSource.GetValue()), m_sThreatEntry, vThreat))
  {
    SetBlackboardIntTactical(ref_instance, m_sResultEntry, 2);
    return;
  }

  plVec3 vPeekPos = plVec3::MakeZero();

  if (!pTactical->FindPeekPosition(pOwner->GetHandle(), vThreat, m_fMaxSideStep, vPeekPos))
  {
    SetBlackboardIntTactical(ref_instance, m_sResultEntry, 2);
    return;
  }

  if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
  {
    pBlackboard->SetEntryValue(m_sTargetEntry, vPeekPos);
  }

  SetBlackboardIntTactical(ref_instance, m_sResultEntry, 1);
}

plResult plStateMachineState_AiPickPeekPosition::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));

  inout_stream << m_ThreatSource;
  inout_stream << m_sThreatEntry;
  inout_stream << m_fMaxSideStep;
  inout_stream << m_sTargetEntry;
  inout_stream << m_sResultEntry;

  return PL_SUCCESS;
}

plResult plStateMachineState_AiPickPeekPosition::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));

  inout_stream >> m_ThreatSource;
  inout_stream >> m_sThreatEntry;
  inout_stream >> m_fMaxSideStep;
  inout_stream >> m_sTargetEntry;
  inout_stream >> m_sResultEntry;

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiReleaseCover, 1, plRTTIDefaultAllocator<plStateMachineState_AiReleaseCover>)
{
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plStateMachineState_AiReleaseCover::plStateMachineState_AiReleaseCover(plStringView sName)
  : plStateMachineState(sName)
{
}

plStateMachineState_AiReleaseCover::~plStateMachineState_AiReleaseCover() = default;

void plStateMachineState_AiReleaseCover::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObject(ref_instance);

  if (pWorld == nullptr || pOwner == nullptr)
    return;

  if (auto* pTactical = pWorld->GetModule<plAiTacticalWorldModule>())
  {
    pTactical->ReleaseClaim(pOwner->GetHandle());
  }
}

plResult plStateMachineState_AiReleaseCover::Serialize(plStreamWriter& inout_stream) const
{
  return SUPER::Serialize(inout_stream);
}

plResult plStateMachineState_AiReleaseCover::Deserialize(plStreamReader& inout_stream)
{
  return SUPER::Deserialize(inout_stream);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiTacticalStates);
