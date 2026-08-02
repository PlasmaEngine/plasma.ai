#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/Components/VoxelGridSettingsComponent.h>
#include <AiPlugin/Navigation/VoxelWorldModule.h>
#include <Core/World/World.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiVoxelGridSettingsComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("ResolutionX", GetResolutionX, SetResolutionX)->AddAttributes(new plDefaultValueAttribute(64), new plClampValueAttribute(4, 512)),
    PL_ACCESSOR_PROPERTY("ResolutionY", GetResolutionY, SetResolutionY)->AddAttributes(new plDefaultValueAttribute(64), new plClampValueAttribute(4, 512)),
    PL_ACCESSOR_PROPERTY("ResolutionZ", GetResolutionZ, SetResolutionZ)->AddAttributes(new plDefaultValueAttribute(32), new plClampValueAttribute(4, 256)),
    PL_ACCESSOR_PROPERTY("VoxelSize", GetVoxelSize, SetVoxelSize)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.1f, 10.0f)),
    PL_ACCESSOR_PROPERTY("CollisionLayer", GetCollisionLayer, SetCollisionLayer)->AddAttributes(new plDynamicEnumAttribute("PhysicsCollisionLayer")),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Navigation"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiVoxelGridSettingsComponent::plAiVoxelGridSettingsComponent() = default;
plAiVoxelGridSettingsComponent::~plAiVoxelGridSettingsComponent() = default;

void plAiVoxelGridSettingsComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  // Ensure the voxel world module is created when this settings component exists in the scene
  GetWorld()->GetOrCreateModule<plAiVoxelWorldModule>();
}

void plAiVoxelGridSettingsComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_uiResolutionX;
  s << m_uiResolutionY;
  s << m_uiResolutionZ;
  s << m_fVoxelSize;
  s << m_uiCollisionLayer;
}

void plAiVoxelGridSettingsComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_uiResolutionX;
  s >> m_uiResolutionY;
  s >> m_uiResolutionZ;
  s >> m_fVoxelSize;
  s >> m_uiCollisionLayer;
}

void plAiVoxelGridSettingsComponent::SetResolutionX(plUInt32 uiValue)
{
  m_uiResolutionX = uiValue;
  SetModified(PL_BIT(0));
}

void plAiVoxelGridSettingsComponent::SetResolutionY(plUInt32 uiValue)
{
  m_uiResolutionY = uiValue;
  SetModified(PL_BIT(1));
}

void plAiVoxelGridSettingsComponent::SetResolutionZ(plUInt32 uiValue)
{
  m_uiResolutionZ = uiValue;
  SetModified(PL_BIT(2));
}

void plAiVoxelGridSettingsComponent::SetVoxelSize(float fValue)
{
  m_fVoxelSize = fValue;
  SetModified(PL_BIT(3));
}

void plAiVoxelGridSettingsComponent::SetCollisionLayer(plUInt32 uiValue)
{
  m_uiCollisionLayer = uiValue;
  SetModified(PL_BIT(4));
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Components_VoxelGridSettingsComponent);
