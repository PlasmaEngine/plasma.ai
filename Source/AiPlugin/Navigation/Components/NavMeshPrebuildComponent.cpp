#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/Components/NavMeshPrebuildComponent.h>
#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <Core/World/World.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiNavMeshPrebuildComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("HalfExtents", m_vHalfExtents)->AddAttributes(new plDefaultValueAttribute(plVec3(32, 32, 4)), new plClampValueAttribute(plVec3(1), plVariant())),
    PL_MEMBER_PROPERTY("NavmeshConfig", m_sNavmeshConfig),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Navigation"),
    new plBoxManipulatorAttribute("HalfExtents", 2.0f, true),
    new plBoxVisualizerAttribute("HalfExtents", 2.0f, plColorScheme::LightUI(plColorScheme::Cyan)),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiNavMeshPrebuildComponent::plAiNavMeshPrebuildComponent() = default;
plAiNavMeshPrebuildComponent::~plAiNavMeshPrebuildComponent() = default;

void plAiNavMeshPrebuildComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_vHalfExtents;
  s << m_sNavmeshConfig;
}

void plAiNavMeshPrebuildComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_vHalfExtents;
  s >> m_sNavmeshConfig;
}

void plAiNavMeshPrebuildComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_NextRequest = plTime::MakeZero(); // request on the first update
}

void plAiNavMeshPrebuildComponent::Update()
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  if (now < m_NextRequest)
    return;

  auto* pModule = GetWorld()->GetOrCreateModule<plAiNavMeshWorldModule>();

  const plBoundingBox worldBox = plBoundingBox::MakeFromCenterAndHalfExtents(GetOwner()->GetGlobalPosition(), m_vHalfExtents);
  const bool bAllReady = pModule->RequestRegion(m_sNavmeshConfig.GetView(), worldBox);

  // retry quickly while sectors are still building; once built, keep re-pinning slowly so a
  // later sector unload does not permanently drop the area (re-requests on built sectors are
  // a handful of map lookups)
  m_NextRequest = now + (bAllReady ? plTime::MakeFromSeconds(2.0) : plTime::MakeFromSeconds(0.5));
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Components_NavMeshPrebuildComponent);
