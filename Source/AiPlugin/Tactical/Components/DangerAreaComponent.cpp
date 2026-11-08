#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Tactical/Components/DangerAreaComponent.h>
#include <AiPlugin/Tactical/ThreatWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiDangerAreaComponent, 1, plComponentMode::Dynamic)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Radius", m_fRadius)->AddAttributes(new plDefaultValueAttribute(5.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_MEMBER_PROPERTY("Duration", m_Duration)->AddAttributes(new plClampValueAttribute(plTime::MakeZero(), plVariant())),
    PL_MEMBER_PROPERTY("Team", m_sTeam),
    PL_MEMBER_PROPERTY("StartActive", m_bStartActive)->AddAttributes(new plDefaultValueAttribute(true)),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(Activate),
    PL_SCRIPT_FUNCTION_PROPERTY(Deactivate),
    PL_SCRIPT_FUNCTION_PROPERTY(AddDangerArea, In, "Position", In, "Radius", In, "DurationSeconds", In, "Team"),
  }
  PL_END_FUNCTIONS;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Components"),
    new plSphereVisualizerAttribute("Radius", plColorScheme::LightUI(plColorScheme::Red)),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiDangerAreaComponent::plAiDangerAreaComponent() = default;
plAiDangerAreaComponent::~plAiDangerAreaComponent() = default;

void plAiDangerAreaComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_fRadius;
  s << m_Duration;
  s << m_sTeam;
  s << m_bStartActive;
}

void plAiDangerAreaComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_fRadius;
  s >> m_Duration;
  s >> m_sTeam;
  s >> m_bStartActive;
}

void plAiDangerAreaComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  // created here, not in Activate or AddDangerArea, which scripts may call during the world update
  GetWorld()->GetOrCreateModule<plAiThreatWorldModule>();

  if (m_bStartActive)
  {
    Activate();
  }
}

void plAiDangerAreaComponent::OnDeactivated()
{
  Deactivate();

  SUPER::OnDeactivated();
}

void plAiDangerAreaComponent::Activate()
{
  plAiThreatWorldModule* pThreat = GetWorld()->GetModule<plAiThreatWorldModule>();
  if (pThreat == nullptr)
    return;

  Deactivate();

  m_vLastPosition = GetOwner()->GetGlobalPosition();
  m_uiDangerId = pThreat->AddDangerArea(m_vLastPosition, m_fRadius, m_Duration, m_sTeam.GetView());
}

void plAiDangerAreaComponent::Deactivate()
{
  if (m_uiDangerId == plInvalidIndex)
    return;

  if (plAiThreatWorldModule* pThreat = GetWorld()->GetModule<plAiThreatWorldModule>())
  {
    pThreat->RemoveDangerArea(m_uiDangerId);
  }

  m_uiDangerId = plInvalidIndex;
}

void plAiDangerAreaComponent::AddDangerArea(const plVec3& vPosition, float fRadius, float fDurationSeconds, const char* szTeam)
{
  if (plAiThreatWorldModule* pThreat = GetWorld()->GetModule<plAiThreatWorldModule>())
  {
    pThreat->AddDangerArea(vPosition, fRadius, plTime::MakeFromSeconds(fDurationSeconds), szTeam);
  }
}

void plAiDangerAreaComponent::Update()
{
  if (m_uiDangerId == plInvalidIndex)
    return;

  // the area follows its object
  const plVec3 vPosition = GetOwner()->GetGlobalPosition();

  if (!vPosition.IsEqual(m_vLastPosition, 0.05f))
  {
    m_vLastPosition = vPosition;

    if (plAiThreatWorldModule* pThreat = GetWorld()->GetModule<plAiThreatWorldModule>())
    {
      pThreat->MoveDangerArea(m_uiDangerId, vPosition);
    }
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Components_DangerAreaComponent);
