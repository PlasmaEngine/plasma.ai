#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/Components/NavLinkComponent.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <RendererCore/Debug/DebugRenderer.h>

// clang-format off
PL_IMPLEMENT_MESSAGE_TYPE(plMsgAiNavLinkTraverse);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgAiNavLinkTraverse, 1, plRTTIDefaultAllocator<plMsgAiNavLinkTraverse>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Start", m_vStart),
    PL_MEMBER_PROPERTY("End", m_vEnd),
    PL_ENUM_MEMBER_PROPERTY("Type", plAiNavLinkType, m_Type),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_COMPONENT_TYPE(plAiNavLinkComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("EndOffset", m_vEndOffset)->AddAttributes(new plDefaultValueAttribute(plVec3(2, 0, 0))),
    PL_MEMBER_PROPERTY("Bidirectional", m_bBidirectional)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("Radius", m_fRadius)->AddAttributes(new plDefaultValueAttribute(0.3f), new plClampValueAttribute(0.05f, 5.0f)),
    PL_ENUM_MEMBER_PROPERTY("LinkType", plAiNavLinkType, m_LinkType),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Navigation"),
    // manipulator/visualizer attributes are TYPE attributes - on a property they are never found
    new plTransformManipulatorAttribute("EndOffset"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiNavLinkComponent::plAiNavLinkComponent() = default;
plAiNavLinkComponent::~plAiNavLinkComponent() = default;

void plAiNavLinkComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_vEndOffset;
  s << m_bBidirectional;
  s << m_fRadius;
  s << m_LinkType;
}

void plAiNavLinkComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_vEndOffset;
  s >> m_bBidirectional;
  s >> m_fRadius;
  s >> m_LinkType;
}

void plAiNavLinkComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  plAiNavLinkData linkData;
  linkData.m_vStart = GetOwner()->GetGlobalPosition();
  linkData.m_vEnd = GetOwner()->GetGlobalTransform().TransformPosition(m_vEndOffset);
  linkData.m_fRadius = m_fRadius;
  linkData.m_bBidirectional = m_bBidirectional;
  linkData.m_Type = m_LinkType;
  linkData.m_uiAreaID = 1; // <Default> ground type

  m_pNavMeshModule = GetWorld()->GetOrCreateModule<plAiNavMeshWorldModule>();
  m_LinkID = m_pNavMeshModule->RegisterNavLink(linkData, GetHandle());
}

void plAiNavLinkComponent::Update()
{
  // only draw in edit mode - while simulating, AI.Navmesh.VisualizeLinks shows the REGISTERED
  // links (which is what agents actually use), so drawing here would just double up
  if (GetWorld()->GetWorldSimulationEnabled())
    return;

  plColor color = plColor::White;
  switch (m_LinkType)
  {
    case plAiNavLinkType::Jump:   color = plColor::Orange; break;
    case plAiNavLinkType::Vault:  color = plColor::Yellow; break;
    case plAiNavLinkType::Ladder: color = plColor::CornflowerBlue; break;
    case plAiNavLinkType::Drop:   color = plColor::OrangeRed; break;
    case plAiNavLinkType::Door:   color = plColor::LawnGreen; break;
    case plAiNavLinkType::Custom: color = plColor::Violet; break;
  }

  const plVec3 vStart = GetOwner()->GetGlobalPosition();
  const plVec3 vEnd = GetOwner()->GetGlobalTransform().TransformPosition(m_vEndOffset);

  // same parabolic arc the runtime visualization draws
  const float fArcHeight = plMath::Clamp((vEnd - vStart).GetLength() * 0.3f, 0.3f, 1.2f);

  plHybridArray<plDebugRenderer::Line, 12> lines;
  plVec3 vPrev = vStart;
  constexpr plUInt32 uiSegments = 8;

  for (plUInt32 i = 1; i <= uiSegments; ++i)
  {
    const float t = static_cast<float>(i) / uiSegments;
    plVec3 vPoint = plMath::Lerp(vStart, vEnd, t);
    vPoint.z += 4.0f * fArcHeight * t * (1.0f - t);

    auto& line = lines.ExpandAndGetRef();
    line.m_start = vPrev;
    line.m_end = vPoint;
    line.m_startColor = line.m_endColor = color;
    vPrev = vPoint;
  }

  plDebugRenderer::DrawLines(GetWorld(), lines, plColor::White);

  // endpoint circles: bright = end (where the manipulator moves), dim = start (the owner);
  // one-way links only connect start -> end
  plDebugRenderer::DrawLineSphere(GetWorld(), plBoundingSphere::MakeFromCenterAndRadius(vEnd, m_fRadius), color);
  plDebugRenderer::DrawLineSphere(GetWorld(), plBoundingSphere::MakeFromCenterAndRadius(vStart, m_fRadius), m_bBidirectional ? color : color * 0.35f);
}

void plAiNavLinkComponent::OnDeactivated()
{
  if (m_pNavMeshModule != nullptr && m_LinkID != plAiNavMeshWorldModule::InvalidNavLinkID)
  {
    m_pNavMeshModule->UnregisterNavLink(m_LinkID);
    m_LinkID = plAiNavMeshWorldModule::InvalidNavLinkID;
    m_pNavMeshModule = nullptr;
  }

  SUPER::OnDeactivated();
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Components_NavLinkComponent);
