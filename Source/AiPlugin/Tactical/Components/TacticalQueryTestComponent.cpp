#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Tactical/Components/TacticalQueryTestComponent.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <RendererCore/Debug/DebugRenderer.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiTacticalQueryTestComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Preset", plAiTacticalQueryPreset, m_Preset),
    PL_MEMBER_PROPERTY("RadiusMin", m_fRadiusMin)->AddAttributes(new plDefaultValueAttribute(2.0f), new plClampValueAttribute(0.0f, 100.0f)),
    PL_MEMBER_PROPERTY("RadiusMax", m_fRadiusMax)->AddAttributes(new plDefaultValueAttribute(12.0f), new plClampValueAttribute(0.5f, 200.0f)),
    PL_MEMBER_PROPERTY("ThreatObjectKey", m_sThreatObjectKey),
    PL_MEMBER_PROPERTY("ClaimBest", m_bClaimBest),
    PL_MEMBER_PROPERTY("QueryInterval", m_QueryInterval)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(0.5))),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Navigation"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiTacticalQueryTestComponent::plAiTacticalQueryTestComponent() = default;
plAiTacticalQueryTestComponent::~plAiTacticalQueryTestComponent() = default;

void plAiTacticalQueryTestComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_Preset;
  s << m_fRadiusMin;
  s << m_fRadiusMax;
  s << m_sThreatObjectKey;
  s << m_bClaimBest;
  s << m_QueryInterval;
}

void plAiTacticalQueryTestComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_Preset;
  s >> m_fRadiusMin;
  s >> m_fRadiusMax;
  s >> m_sThreatObjectKey;
  s >> m_bClaimBest;
  s >> m_QueryInterval;
}

void plAiTacticalQueryTestComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_pTacticalModule = GetWorld()->GetOrCreateModule<plAiTacticalWorldModule>();
}

void plAiTacticalQueryTestComponent::OnDeactivated()
{
  if (m_pTacticalModule != nullptr)
  {
    if (m_QueryId != plAiTacticalWorldModule::InvalidQueryID)
    {
      m_pTacticalModule->ReleaseQuery(m_QueryId);
      m_QueryId = plAiTacticalWorldModule::InvalidQueryID;
    }

    m_pTacticalModule->ReleaseClaim(GetOwner()->GetHandle());
    m_pTacticalModule = nullptr;
  }

  SUPER::OnDeactivated();
}

void plAiTacticalQueryTestComponent::Update()
{
  if (m_pTacticalModule == nullptr)
    return;

  // poll the in-flight query
  if (m_QueryId != plAiTacticalWorldModule::InvalidQueryID)
  {
    plAiTacticalQueryResult result;

    if (m_pTacticalModule->TryGetResult(m_QueryId, result))
    {
      m_LastResult = result;
      m_pTacticalModule->ReleaseQuery(m_QueryId);
      m_QueryId = plAiTacticalWorldModule::InvalidQueryID;

      if (m_bClaimBest && result.m_Status == plAiTacticalQueryResult::Status::Ready && result.m_TopN[0].m_CoverHandle.IsValid())
      {
        m_pTacticalModule->ClaimCover(result.m_TopN[0].m_CoverHandle, GetOwner()->GetHandle(), plTime::Seconds(10.0));
      }
    }
  }

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  // kick off the next query
  if (m_QueryId == plAiTacticalWorldModule::InvalidQueryID && now >= m_NextQuery)
  {
    m_NextQuery = now + m_QueryInterval;

    plVec3 vThreat = plVec3::MakeZero();
    bool bHasThreat = false;

    if (!m_sThreatObjectKey.IsEmpty())
    {
      plGameObject* pThreat = nullptr;

      if (GetWorld()->TryGetObjectWithGlobalKey(plTempHashedString(m_sThreatObjectKey.GetData()), pThreat))
      {
        vThreat = pThreat->GetGlobalPosition();
        bHasThreat = true;
      }
    }

    m_vLastQuerier = GetOwner()->GetGlobalPosition();

    plAiTacticalQueryDesc desc = plAiTacticalQueryDesc::MakeFromPreset(static_cast<plAiTacticalQueryPreset::Enum>(m_Preset.GetValue()), m_vLastQuerier, vThreat, bHasThreat, m_fRadiusMin, m_fRadiusMax);
    desc.m_hClaimant = GetOwner()->GetHandle();

    m_QueryId = m_pTacticalModule->SubmitQuery(desc);
  }

  // draw the last result (self-sufficient - no cvar required)
  if (m_LastResult.m_Status == plAiTacticalQueryResult::Status::Ready)
  {
    for (plUInt32 i = 0; i < m_LastResult.m_TopN.GetCount(); ++i)
    {
      const auto& cand = m_LastResult.m_TopN[i];
      const plColor color = (i == 0) ? plColor::White : plMath::Lerp(plColor::Red, plColor::LawnGreen, plMath::Saturate(cand.m_fScore));

      plDebugRenderer::DrawLineSphere(GetWorld(), plBoundingSphere::MakeFromCenterAndRadius(cand.m_vPosition + plVec3(0, 0, 0.15f), 0.15f), color);
      plDebugRenderer::Draw3DText(GetWorld(), plFmt("{}", plArgF(cand.m_fScore, 2)), cand.m_vPosition + plVec3(0, 0, 0.5f), color);
    }

    plDebugRenderer::Line line(m_vLastQuerier + plVec3(0, 0, 0.3f), m_LastResult.m_TopN[0].m_vPosition + plVec3(0, 0, 0.3f));
    line.m_startColor = line.m_endColor = plColor::White;
    plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(&line, 1), plColor::White);
  }
  else if (m_LastResult.m_Status == plAiTacticalQueryResult::Status::NoResult)
  {
    plDebugRenderer::Draw3DText(GetWorld(), "no result", GetOwner()->GetGlobalPosition() + plVec3(0, 0, 1.0f), plColor::OrangeRed);
  }
  else if (m_LastResult.m_Status == plAiTacticalQueryResult::Status::AreaNotReady)
  {
    plDebugRenderer::Draw3DText(GetWorld(), "area not ready", GetOwner()->GetGlobalPosition() + plVec3(0, 0, 1.0f), plColor::Yellow);
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Components_TacticalQueryTestComponent);
