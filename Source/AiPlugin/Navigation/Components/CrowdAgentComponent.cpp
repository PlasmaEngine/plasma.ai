#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/Components/CrowdAgentComponent.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiCrowdAgentComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Radius", m_fRadius)->AddAttributes(new plDefaultValueAttribute(0.4f), new plClampValueAttribute(0.05f, 5.0f)),
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

plAiCrowdAgentComponent::plAiCrowdAgentComponent() = default;
plAiCrowdAgentComponent::~plAiCrowdAgentComponent() = default;

void plAiCrowdAgentComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_fRadius;
}

void plAiCrowdAgentComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_fRadius;
}

void plAiCrowdAgentComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_pCrowdModule = GetWorld()->GetOrCreateModule<plAiCrowdWorldModule>();
  m_CrowdAgentID = m_pCrowdModule->RegisterAgent();
  m_vLastPosition = GetOwner()->GetGlobalPosition();
}

void plAiCrowdAgentComponent::OnDeactivated()
{
  if (m_pCrowdModule != nullptr && m_CrowdAgentID != plAiCrowdWorldModule::InvalidAgentID)
  {
    m_pCrowdModule->UnregisterAgent(m_CrowdAgentID);
    m_CrowdAgentID = plAiCrowdWorldModule::InvalidAgentID;
    m_pCrowdModule = nullptr;
  }

  SUPER::OnDeactivated();
}

void plAiCrowdAgentComponent::Update()
{
  if (m_pCrowdModule == nullptr || m_CrowdAgentID == plAiCrowdWorldModule::InvalidAgentID)
    return;

  const plVec3 vPosition = GetOwner()->GetGlobalPosition();

  plVec3 vVelocity = GetOwner()->GetLinearVelocity();

  if (vVelocity.IsZero(0.001f))
  {
    // objects moved by teleport/scripts have no linear velocity - derive it from movement
    const float fTimeDiff = GetWorld()->GetClock().GetTimeDiff().AsFloatInSeconds();
    if (fTimeDiff > 0.0001f)
    {
      vVelocity = (vPosition - m_vLastPosition) / fTimeDiff;
    }
  }

  m_vLastPosition = vPosition;

  plAiCrowdAgentState state;
  state.m_vPosition = vPosition;
  state.m_vVelocity = vVelocity;
  state.m_vDesiredVelocity = vVelocity; // best prediction for a non-AI mover: keeps going
  state.m_fRadius = m_fRadius;
  state.m_fMaxSpeed = plMath::Max(1.0f, vVelocity.GetLength());
  state.m_bObstacleOnly = true;

  m_pCrowdModule->SubmitAgentState(m_CrowdAgentID, state);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Components_CrowdAgentComponent);
