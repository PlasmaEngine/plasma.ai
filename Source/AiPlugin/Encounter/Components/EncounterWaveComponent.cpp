#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Encounter/Components/EncounterWaveComponent.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiEncounterWaveComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Spawner", DummyGetter, SetSpawnerReference)->AddAttributes(new plGameObjectReferenceAttribute()),
    PL_MEMBER_PROPERTY("SquadId", m_sSquadId),
    PL_ENUM_MEMBER_PROPERTY("Trigger", plAiEncounterTrigger, m_Trigger),
    PL_ACCESSOR_PROPERTY("AfterWave", DummyGetter, SetAfterWaveReference)->AddAttributes(new plGameObjectReferenceAttribute()),
    PL_MEMBER_PROPERTY("AliveAtMost", m_uiAliveAtMost)->AddAttributes(new plDefaultValueAttribute(1)),
    PL_MEMBER_PROPERTY("Time", m_fTime)->AddAttributes(new plDefaultValueAttribute(60.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_MEMBER_PROPERTY("CalmBelow", m_fCalmBelow)->AddAttributes(new plDefaultValueAttribute(0.4f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("CalmFor", m_fCalmFor)->AddAttributes(new plDefaultValueAttribute(10.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_MEMBER_PROPERTY("HoldWhenIntense", m_bHoldWhenIntense)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("MaxHold", m_fMaxHold)->AddAttributes(new plDefaultValueAttribute(45.0f), new plClampValueAttribute(0.0f, plVariant())),
    PL_MEMBER_PROPERTY("ReleaseFunction", m_sReleaseFunction)->AddAttributes(new plDefaultValueAttribute("Activate")),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Encounter"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiEncounterWaveComponent::plAiEncounterWaveComponent() = default;
plAiEncounterWaveComponent::~plAiEncounterWaveComponent() = default;

void plAiEncounterWaveComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  inout_stream.WriteGameObjectHandle(m_hSpawner);
  inout_stream.WriteGameObjectHandle(m_hAfterWave);
  s << m_sSquadId;
  s << m_Trigger;
  s << m_uiAliveAtMost;
  s << m_fTime;
  s << m_fCalmBelow;
  s << m_fCalmFor;
  s << m_bHoldWhenIntense;
  s << m_fMaxHold;
  s << m_sReleaseFunction;
}

void plAiEncounterWaveComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  m_hSpawner = inout_stream.ReadGameObjectHandle();
  m_hAfterWave = inout_stream.ReadGameObjectHandle();
  s >> m_sSquadId;
  s >> m_Trigger;
  s >> m_uiAliveAtMost;
  s >> m_fTime;
  s >> m_fCalmBelow;
  s >> m_fCalmFor;
  s >> m_bHoldWhenIntense;
  s >> m_fMaxHold;
  s >> m_sReleaseFunction;
}

void plAiEncounterWaveComponent::SetSpawnerReference(const char* szReference)
{
  auto resolver = GetWorld()->GetGameObjectReferenceResolver();
  if (resolver.IsValid())
    m_hSpawner = resolver(szReference, GetHandle(), "Spawner");
}

void plAiEncounterWaveComponent::SetAfterWaveReference(const char* szReference)
{
  auto resolver = GetWorld()->GetGameObjectReferenceResolver();
  if (resolver.IsValid())
    m_hAfterWave = resolver(szReference, GetHandle(), "AfterWave");
}

plGameObjectHandle plAiEncounterWaveComponent::GetSpawner() const
{
  return m_hSpawner.IsInvalidated() ? GetOwner()->GetHandle() : m_hSpawner;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Encounter_Components_EncounterWaveComponent);
