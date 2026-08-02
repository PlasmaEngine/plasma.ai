#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/Components/EqsQueryTestComponent.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <RendererCore/Debug/DebugRenderer.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiEqsQueryTestComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Query", GetQueryFile, SetQueryFile)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_AiEqsQuery", plDependencyFlags::Package)),
    PL_MEMBER_PROPERTY("ThreatObjectKey", m_sThreatObjectKey),
    PL_ACCESSOR_PROPERTY("ThreatSlot", GetThreatSlot, SetThreatSlot)->AddAttributes(new plDefaultValueAttribute(plStringView("Threat"))),
    PL_MEMBER_PROPERTY("ClaimBest", m_bClaimBest),
    PL_MEMBER_PROPERTY("QueryInterval", m_QueryInterval)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(0.5))),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Tactical"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiEqsQueryTestComponent::plAiEqsQueryTestComponent()
{
  m_sThreatSlot.Assign("Threat");
}

plAiEqsQueryTestComponent::~plAiEqsQueryTestComponent() = default;

void plAiEqsQueryTestComponent::SetQueryFile(const char* szFile)
{
  plAiEqsQueryResourceHandle hResource;

  if (!plStringUtils::IsNullOrEmpty(szFile))
  {
    hResource = plResourceManager::LoadResource<plAiEqsQueryResource>(szFile);
    plResourceManager::PreloadResource(hResource);
  }

  m_hQuery = hResource;
}

const char* plAiEqsQueryTestComponent::GetQueryFile() const
{
  if (!m_hQuery.IsValid())
    return "";

  return m_hQuery.GetResourceID();
}

void plAiEqsQueryTestComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_hQuery;
  s << m_sThreatObjectKey;
  s << m_sThreatSlot;
  s << m_bClaimBest;
  s << m_QueryInterval;
}

void plAiEqsQueryTestComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_hQuery;
  s >> m_sThreatObjectKey;
  s >> m_sThreatSlot;
  s >> m_bClaimBest;
  s >> m_QueryInterval;
}

void plAiEqsQueryTestComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_pEqsModule = GetWorld()->GetOrCreateModule<plAiEqsWorldModule>();

  // cover baking and claims live in the tactical module - make sure it exists so cover-based
  // query assets have data to work with
  GetWorld()->GetOrCreateModule<plAiTacticalWorldModule>();
}

void plAiEqsQueryTestComponent::OnDeactivated()
{
  if (m_pEqsModule != nullptr)
  {
    if (m_QueryId != plAiEqsWorldModule::InvalidQueryID)
    {
      m_pEqsModule->ReleaseQuery(m_QueryId);
      m_QueryId = plAiEqsWorldModule::InvalidQueryID;
    }

    m_pEqsModule = nullptr;
  }

  if (auto* pTactical = GetWorld()->GetModule<plAiTacticalWorldModule>())
  {
    pTactical->ReleaseClaim(GetOwner()->GetHandle());
  }

  SUPER::OnDeactivated();
}

void plAiEqsQueryTestComponent::Update()
{
  if (m_pEqsModule == nullptr || !m_hQuery.IsValid())
    return;

  // poll the in-flight query
  if (m_QueryId != plAiEqsWorldModule::InvalidQueryID)
  {
    plAiEqsQueryResult result;

    if (m_pEqsModule->TryGetResult(m_QueryId, result))
    {
      m_LastResult = result;
      m_pEqsModule->ReleaseQuery(m_QueryId);
      m_QueryId = plAiEqsWorldModule::InvalidQueryID;

      if (m_bClaimBest && result.m_Status == plAiEqsQueryResult::Status::Ready && result.m_TopN[0].m_hCover.IsValid())
      {
        if (auto* pTactical = GetWorld()->GetModule<plAiTacticalWorldModule>())
        {
          pTactical->ClaimCover(result.m_TopN[0].m_hCover, GetOwner()->GetHandle(), plTime::Seconds(10.0));
        }
      }
    }
  }

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  // kick off the next query
  if (m_QueryId == plAiEqsWorldModule::InvalidQueryID && now >= m_NextQuery)
  {
    m_NextQuery = now + m_QueryInterval;
    m_vLastQuerier = GetOwner()->GetGlobalPosition();

    plAiEqsQueryParams params;
    params.m_hQuerier = GetOwner()->GetHandle();

    if (!m_sThreatObjectKey.IsEmpty())
    {
      plGameObject* pThreat = nullptr;

      if (GetWorld()->TryGetObjectWithGlobalKey(plTempHashedString(m_sThreatObjectKey.GetData()), pThreat))
      {
        auto& named = params.m_Positions.ExpandAndGetRef();
        named.m_sName = m_sThreatSlot;
        named.m_vPosition = pThreat->GetGlobalPosition();
      }
    }

    m_QueryId = m_pEqsModule->SubmitQuery(m_hQuery, params);
  }

  DrawResult();
}

void plAiEqsQueryTestComponent::DrawResult()
{
  // self-sufficient drawing - no cvar required
  if (m_LastResult.m_Status == plAiEqsQueryResult::Status::Ready)
  {
    for (plUInt32 i = 0; i < m_LastResult.m_TopN.GetCount(); ++i)
    {
      const auto& cand = m_LastResult.m_TopN[i];
      const plColor color = (i == 0) ? plColor::White : plMath::Lerp(plColor::Red, plColor::LawnGreen, plMath::Saturate(cand.m_fScore));

      plDebugRenderer::DrawLineSphere(GetWorld(), plBoundingSphere::MakeFromCenterAndRadius(cand.m_vPosition + plVec3(0, 0, 0.15f), 0.15f), color);
      plDebugRenderer::Draw3DText(GetWorld(), plFmt("{}", plArgF(cand.m_fScore, 2)), cand.m_vPosition + plVec3(0, 0, 0.5f), color);
    }

    const auto& winner = m_LastResult.m_TopN[0];

    plDebugRenderer::Line line(m_vLastQuerier + plVec3(0, 0, 0.3f), winner.m_vPosition + plVec3(0, 0, 0.3f));
    line.m_startColor = line.m_endColor = plColor::White;
    plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(&line, 1), plColor::White);

    if (winner.m_uiRecordedTests > 0)
    {
      plStringBuilder sBreakdown;

      for (plUInt32 t = 0; t < winner.m_uiRecordedTests; ++t)
      {
        sBreakdown.AppendFormat("T{}: {}\n", t, plArgF(winner.m_TestScores[t], 2));
      }

      sBreakdown.AppendFormat("= {}", plArgF(winner.m_fScore, 2));

      plDebugRenderer::Draw3DText(GetWorld(), sBreakdown, winner.m_vPosition + plVec3(0, 0, 1.2f), plColor::MediumSpringGreen);
    }
  }
  else if (m_LastResult.m_Status == plAiEqsQueryResult::Status::NoResult)
  {
    plDebugRenderer::Draw3DText(GetWorld(), "EQS: no result", GetOwner()->GetGlobalPosition() + plVec3(0, 0, 1.0f), plColor::OrangeRed);
  }
  else if (m_LastResult.m_Status == plAiEqsQueryResult::Status::AreaNotReady)
  {
    plDebugRenderer::Draw3DText(GetWorld(), "EQS: area not ready", GetOwner()->GetGlobalPosition() + plVec3(0, 0, 1.0f), plColor::Yellow);
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Eqs_Components_EqsQueryTestComponent);
