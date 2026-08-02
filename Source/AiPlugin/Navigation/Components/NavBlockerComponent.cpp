#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/Components/NavBlockerComponent.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiNavBlockerComponent, 2, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("BoxSize", m_vBoxSize)->AddAttributes(new plDefaultValueAttribute(plVec3(1.0f)), new plClampValueAttribute(plVec3(0.01f), plVariant())),
    PL_ACCESSOR_PROPERTY("Blocked", GetBlocked, SetBlocked)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_ENUM_MEMBER_PROPERTY("Mode", plAiNavObstacleMode, m_Mode),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(SetBlocked, In, "Blocked"),
  }
  PL_END_FUNCTIONS;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Navigation"),
    new plBoxManipulatorAttribute("BoxSize", 1.0f, false),
    new plBoxVisualizerAttribute("BoxSize", 1.0f, plColorScheme::LightUI(plColorScheme::Red)),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiNavBlockerComponent::plAiNavBlockerComponent() = default;
plAiNavBlockerComponent::~plAiNavBlockerComponent() = default;

void plAiNavBlockerComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_vBoxSize;
  s << m_bBlocked;
  s << m_Mode;
}

void plAiNavBlockerComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& s = inout_stream.GetStream();

  s >> m_vBoxSize;
  s >> m_bBlocked;

  if (uiVersion >= 2)
  {
    s >> m_Mode;
  }
}

plBoundingBox plAiNavBlockerComponent::GetWorldBox() const
{
  const plTransform ownerTransform = GetOwner()->GetGlobalTransform();
  const plVec3 vHalfSize = m_vBoxSize * 0.5f;

  plBoundingBox worldBox = plBoundingBox::MakeInvalid();

  for (plUInt32 i = 0; i < 8; ++i)
  {
    const plVec3 vCorner((i & 1) ? vHalfSize.x : -vHalfSize.x, (i & 2) ? vHalfSize.y : -vHalfSize.y, (i & 4) ? vHalfSize.z : -vHalfSize.z);
    worldBox.ExpandToInclude(ownerTransform.TransformPosition(vCorner));
  }

  return worldBox;
}

void plAiNavBlockerComponent::SetBlocked(bool bBlocked)
{
  if (m_bBlocked == bBlocked)
    return;

  m_bBlocked = bBlocked;

  if (m_pNavMeshModule != nullptr && m_BlockerID != plAiNavMeshWorldModule::InvalidBlockerID)
  {
    m_pNavMeshModule->SetNavBlockerActive(m_BlockerID, m_bBlocked);
  }
}

void plAiNavBlockerComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_pNavMeshModule = GetWorld()->GetOrCreateModule<plAiNavMeshWorldModule>();
  m_BlockerID = m_pNavMeshModule->RegisterNavBlocker(GetWorldBox(), m_bBlocked, m_Mode);
  m_vLastPosition = GetOwner()->GetGlobalPosition();
}

void plAiNavBlockerComponent::OnDeactivated()
{
  if (m_pNavMeshModule != nullptr && m_BlockerID != plAiNavMeshWorldModule::InvalidBlockerID)
  {
    m_pNavMeshModule->UnregisterNavBlocker(m_BlockerID);
    m_BlockerID = plAiNavMeshWorldModule::InvalidBlockerID;
    m_pNavMeshModule = nullptr;
  }

  SUPER::OnDeactivated();
}

void plAiNavBlockerComponent::Update()
{
  if (m_pNavMeshModule == nullptr || m_BlockerID == plAiNavMeshWorldModule::InvalidBlockerID)
    return;

  if (!GetOwner()->IsDynamic())
    return;

  const plVec3 vPosition = GetOwner()->GetGlobalPosition();

  if ((vPosition - m_vLastPosition).GetLengthSquared() > plMath::Square(0.25f))
  {
    m_vLastPosition = vPosition;
    m_pNavMeshModule->UpdateNavBlockerBounds(m_BlockerID, GetWorldBox());
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Components_NavBlockerComponent);
