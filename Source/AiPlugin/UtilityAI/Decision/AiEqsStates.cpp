#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <AiPlugin/UtilityAI/Decision/AiEqsStates.h>
#include <Core/Utils/Blackboard.h>
#include <Core/World/World.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plStateMachineState_AiRunEqsQuery, 1, plRTTIDefaultAllocator<plStateMachineState_AiRunEqsQuery>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Query", GetQueryFile, SetQueryFile)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_AiEqsQuery", plDependencyFlags::Package)),
    PL_ACCESSOR_PROPERTY("TargetEntry", GetTargetEntry, SetTargetEntry)->AddAttributes(new plDefaultValueAttribute(plStringView("Ai_TacticalTarget"))),
    PL_ACCESSOR_PROPERTY("ResultEntry", GetResultEntry, SetResultEntry)->AddAttributes(new plDefaultValueAttribute(plStringView("Ai_TacticalResult"))),
    PL_MEMBER_PROPERTY("ClaimCover", m_bClaimCover)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("ClaimSmartObject", m_bClaimSmartObject)->AddAttributes(new plDefaultValueAttribute(true)),
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

  void SetBlackboardIntEqs(plStateMachineInstance& ref_instance, const plHashedString& sEntry, plInt32 iValue)
  {
    if (sEntry.IsEmpty())
      return;

    if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
    {
      pBlackboard->SetEntryValue(sEntry, iValue);
    }
  }
} // namespace

plStateMachineState_AiRunEqsQuery::plStateMachineState_AiRunEqsQuery(plStringView sName)
  : plStateMachineState(sName)
{
  m_sTargetEntry.Assign("Ai_TacticalTarget");
  m_sResultEntry.Assign("Ai_TacticalResult");
}

plStateMachineState_AiRunEqsQuery::~plStateMachineState_AiRunEqsQuery() = default;

void plStateMachineState_AiRunEqsQuery::SetQueryFile(const char* szFile)
{
  plAiEqsQueryResourceHandle hResource;

  if (!plStringUtils::IsNullOrEmpty(szFile))
  {
    hResource = plResourceManager::LoadResource<plAiEqsQueryResource>(szFile);
    plResourceManager::PreloadResource(hResource);
  }

  m_hQuery = hResource;
}

const char* plStateMachineState_AiRunEqsQuery::GetQueryFile() const
{
  if (!m_hQuery.IsValid())
    return "";

  return m_hQuery.GetResourceID();
}

bool plStateMachineState_AiRunEqsQuery::SubmitQuery(plStateMachineInstance& ref_instance, InstanceData* pData) const
{
  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObject(ref_instance);

  if (pWorld == nullptr || pOwner == nullptr || !m_hQuery.IsValid())
    return false;

  // the module is created by plAiAgentComponent / the EQS components at simulation start;
  // creating world modules from inside a module update is not safe
  auto* pEqs = pWorld->GetModule<plAiEqsWorldModule>();

  if (pEqs == nullptr)
    return false;

  plAiEqsQueryParams params;
  params.m_hQuerier = pOwner->GetHandle();

  pData->m_QueryId = pEqs->SubmitQuery(m_hQuery, params);
  return pData->m_QueryId != plAiEqsWorldModule::InvalidQueryID;
}

void plStateMachineState_AiRunEqsQuery::OnEnter(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pFromState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);
  pData->m_QueryId = plAiEqsWorldModule::InvalidQueryID;
  pData->m_uiRetries = 0;
  pData->m_bDone = false;

  if (!SubmitQuery(ref_instance, pData))
  {
    SetBlackboardIntEqs(ref_instance, m_sResultEntry, 2);
    pData->m_bDone = true;
    return;
  }

  SetBlackboardIntEqs(ref_instance, m_sResultEntry, 0);
}

void plStateMachineState_AiRunEqsQuery::Update(plStateMachineInstance& ref_instance, void* pInstanceData, plTime deltaTime) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  if (pData->m_bDone)
    return;

  plWorld* pWorld = ref_instance.GetOwnerWorld();
  plGameObject* pOwner = GetOwnerObject(ref_instance);

  if (pWorld == nullptr || pOwner == nullptr)
    return;

  auto* pEqs = pWorld->GetModule<plAiEqsWorldModule>();

  if (pEqs == nullptr)
  {
    SetBlackboardIntEqs(ref_instance, m_sResultEntry, 2);
    pData->m_bDone = true;
    return;
  }

  // waiting for a scheduled retry (area was not loaded yet)
  if (pData->m_QueryId == plAiEqsWorldModule::InvalidQueryID)
  {
    if (pWorld->GetClock().GetAccumulatedTime() >= pData->m_RetryAt)
    {
      if (!SubmitQuery(ref_instance, pData))
      {
        SetBlackboardIntEqs(ref_instance, m_sResultEntry, 2);
        pData->m_bDone = true;
      }
    }

    return;
  }

  plAiEqsQueryResult result;

  if (!pEqs->TryGetResult(pData->m_QueryId, result))
    return; // still pending

  pEqs->ReleaseQuery(pData->m_QueryId);
  pData->m_QueryId = plAiEqsWorldModule::InvalidQueryID;

  if (result.m_Status == plAiEqsQueryResult::Status::AreaNotReady)
  {
    if (pData->m_uiRetries < 3)
    {
      ++pData->m_uiRetries;
      pData->m_RetryAt = pWorld->GetClock().GetAccumulatedTime() + plTime::Seconds(0.5);
    }
    else
    {
      SetBlackboardIntEqs(ref_instance, m_sResultEntry, 2);
      pData->m_bDone = true;
    }

    return;
  }

  if (result.m_Status != plAiEqsQueryResult::Status::Ready)
  {
    SetBlackboardIntEqs(ref_instance, m_sResultEntry, 2);
    pData->m_bDone = true;
    return;
  }

  auto* pTactical = pWorld->GetModule<plAiTacticalWorldModule>();

  // pick the best candidate we can actually use: cover points may have been claimed by another
  // agent since the query executed (one frame ago) - fall through to the next candidate then
  for (const auto& candidate : result.m_TopN)
  {
    if (candidate.m_hCover.IsValid() && m_bClaimCover && pTactical != nullptr)
    {
      if (!pTactical->ClaimCover(candidate.m_hCover, pOwner->GetHandle(), m_ClaimTimeout))
        continue;
    }

    if (candidate.m_hSmartObject.IsValid() && m_bClaimSmartObject && pTactical != nullptr)
    {
      if (!pTactical->ClaimSmartObject(candidate.m_hSmartObject, pOwner->GetHandle(), m_ClaimTimeout))
        continue;
    }

    if (const plSharedPtr<plBlackboard>& pBlackboard = ref_instance.GetBlackboard())
    {
      pBlackboard->SetEntryValue(m_sTargetEntry, candidate.m_vPosition);
    }

    SetBlackboardIntEqs(ref_instance, m_sResultEntry, 1);
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

  SetBlackboardIntEqs(ref_instance, m_sResultEntry, 2);
  pData->m_bDone = true;
}

void plStateMachineState_AiRunEqsQuery::OnExit(plStateMachineInstance& ref_instance, void* pInstanceData, const plStateMachineState* pToState) const
{
  auto pData = static_cast<InstanceData*>(pInstanceData);

  plWorld* pWorld = ref_instance.GetOwnerWorld();

  if (pWorld == nullptr)
    return;

  if (auto* pEqs = pWorld->GetModule<plAiEqsWorldModule>())
  {
    if (pData->m_QueryId != plAiEqsWorldModule::InvalidQueryID)
    {
      pEqs->ReleaseQuery(pData->m_QueryId);
      pData->m_QueryId = plAiEqsWorldModule::InvalidQueryID;
    }
  }

  if (m_bReleaseClaimOnExit)
  {
    if (auto* pTactical = pWorld->GetModule<plAiTacticalWorldModule>())
    {
      if (plGameObject* pOwner = GetOwnerObject(ref_instance))
      {
        pTactical->ReleaseClaim(pOwner->GetHandle());
        pTactical->ReleaseSmartObjectClaim(pOwner->GetHandle());
      }
    }
  }
}

plResult plStateMachineState_AiRunEqsQuery::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));

  inout_stream << m_hQuery;
  inout_stream << m_sTargetEntry;
  inout_stream << m_sResultEntry;
  inout_stream << m_bClaimCover;
  inout_stream << m_bClaimSmartObject;
  inout_stream << m_ClaimTimeout;
  inout_stream << m_bReleaseClaimOnExit;

  return PL_SUCCESS;
}

plResult plStateMachineState_AiRunEqsQuery::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));

  inout_stream >> m_hQuery;
  inout_stream >> m_sTargetEntry;
  inout_stream >> m_sResultEntry;
  inout_stream >> m_bClaimCover;
  inout_stream >> m_bClaimSmartObject;
  inout_stream >> m_ClaimTimeout;
  inout_stream >> m_bReleaseClaimOnExit;

  return PL_SUCCESS;
}

bool plStateMachineState_AiRunEqsQuery::GetInstanceDataDesc(plInstanceDataDesc& out_desc)
{
  out_desc.FillFromType<InstanceData>();
  return true;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiEqsStates);
