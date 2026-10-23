#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Components/AiStimulusSourceComponent.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiStimulusSourceComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Type", plAiStimulusType, m_Type),
    PL_MEMBER_PROPERTY("CustomType", m_sCustomType),
    PL_MEMBER_PROPERTY("Radius", m_fRadius)->AddAttributes(new plDefaultValueAttribute(15.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_MEMBER_PROPERTY("Strength", m_fStrength)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("Interval", m_Interval)->AddAttributes(new plClampValueAttribute(plTime::MakeZero(), plVariant())),
    PL_MEMBER_PROPERTY("Team", m_sTeam),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(Emit),
  }
  PL_END_FUNCTIONS;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Components"),
    new plSphereVisualizerAttribute("Radius", plColor::Orange),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiStimulusSourceComponent::plAiStimulusSourceComponent() = default;
plAiStimulusSourceComponent::~plAiStimulusSourceComponent() = default;

void plAiStimulusSourceComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_Type;
  s << m_sCustomType;
  s << m_fRadius;
  s << m_fStrength;
  s << m_Interval;
  s << m_sTeam;
}

void plAiStimulusSourceComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_Type;
  s >> m_sCustomType;
  s >> m_fRadius;
  s >> m_fStrength;
  s >> m_Interval;
  s >> m_sTeam;
}

void plAiStimulusSourceComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  // created here, not in Emit, which scripts may call during the world update
  GetWorld()->GetOrCreateModule<plAiPerceptionWorldModule>();

  m_NextEmit = GetWorld()->GetClock().GetAccumulatedTime();
}

void plAiStimulusSourceComponent::Emit()
{
  plAiPerceptionWorldModule* pPerception = GetWorld()->GetModule<plAiPerceptionWorldModule>();
  if (pPerception == nullptr)
    return;

  plAiStimulus stimulus;
  stimulus.m_Type = m_Type;
  stimulus.m_sCustomType = m_sCustomType;
  stimulus.m_vGlobalPosition = GetOwner()->GetGlobalPosition();
  stimulus.m_fRadius = m_fRadius;
  stimulus.m_fStrength = m_fStrength;
  stimulus.m_hSource = GetOwner()->GetHandle();
  stimulus.m_sSourceTeam = m_sTeam;

  pPerception->PostStimulus(stimulus);
}

void plAiStimulusSourceComponent::Update()
{
  if (!m_Interval.IsPositive())
    return;

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  if (now < m_NextEmit)
    return;

  m_NextEmit = now + m_Interval;
  Emit();
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Components_AiStimulusSourceComponent);
