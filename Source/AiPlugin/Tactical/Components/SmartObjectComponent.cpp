#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Tactical/Components/SmartObjectComponent.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiSmartObjectComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Type", m_sType),
    PL_MEMBER_PROPERTY("UseRange", m_fUseRange)->AddAttributes(new plDefaultValueAttribute(1.5f), new plClampValueAttribute(0.2f, 20.0f)),
    PL_MEMBER_PROPERTY("UseDuration", m_UseDuration)->AddAttributes(new plDefaultValueAttribute(plTime::Seconds(3.0))),
    PL_ARRAY_MEMBER_PROPERTY("Slots", m_Slots),
    PL_ARRAY_MEMBER_PROPERTY("UserEntries", m_UserEntries),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(FinishUse, In, "User"),
  }
  PL_END_FUNCTIONS;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

namespace
{
  // plBlackboardEntry::Serialize is not exported from the GameEngine DLL, so serialize the fields manually
  void WriteSmartObjectBlackboardEntries(plStreamWriter& inout_stream, const plDynamicArray<plBlackboardEntry>& entries)
  {
    inout_stream << entries.GetCount();

    for (const auto& entry : entries)
    {
      inout_stream << entry.m_sName;
      inout_stream << entry.m_InitialValue;
      inout_stream << entry.m_Flags;
    }
  }

  void ReadSmartObjectBlackboardEntries(plStreamReader& inout_stream, plDynamicArray<plBlackboardEntry>& out_entries)
  {
    plUInt32 uiCount = 0;
    inout_stream >> uiCount;

    out_entries.Clear();
    out_entries.Reserve(uiCount);

    for (plUInt32 i = 0; i < uiCount; ++i)
    {
      auto& entry = out_entries.ExpandAndGetRef();
      inout_stream >> entry.m_sName;
      inout_stream >> entry.m_InitialValue;
      inout_stream >> entry.m_Flags;
    }
  }
} // namespace

plAiSmartObjectComponent::plAiSmartObjectComponent() = default;
plAiSmartObjectComponent::~plAiSmartObjectComponent() = default;

void plAiSmartObjectComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_sType;
  s << m_fUseRange;
  s << m_UseDuration;

  s << m_Slots.GetCount();

  for (const auto& slot : m_Slots)
  {
    s << slot.m_vLocalPosition;
    s << slot.m_qLocalRotation;
  }

  WriteSmartObjectBlackboardEntries(s, m_UserEntries);
}

void plAiSmartObjectComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_sType;
  s >> m_fUseRange;
  s >> m_UseDuration;

  plUInt32 uiSlots = 0;
  s >> uiSlots;

  m_Slots.Clear();
  m_Slots.Reserve(uiSlots);

  for (plUInt32 i = 0; i < uiSlots; ++i)
  {
    auto& slot = m_Slots.ExpandAndGetRef();
    s >> slot.m_vLocalPosition;
    s >> slot.m_qLocalRotation;
  }

  ReadSmartObjectBlackboardEntries(s, m_UserEntries);
}

void plAiSmartObjectComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_pTacticalModule = GetWorld()->GetOrCreateModule<plAiTacticalWorldModule>();
  m_SmartObjectID = m_pTacticalModule->RegisterSmartObject(GetHandle());
}

void plAiSmartObjectComponent::OnDeactivated()
{
  if (m_pTacticalModule != nullptr && m_SmartObjectID != plAiTacticalWorldModule::InvalidSmartObjectID)
  {
    m_pTacticalModule->UnregisterSmartObject(m_SmartObjectID);
    m_SmartObjectID = plAiTacticalWorldModule::InvalidSmartObjectID;
    m_pTacticalModule = nullptr;
  }

  SUPER::OnDeactivated();
}

plTransform plAiSmartObjectComponent::GetSlotGlobalTransform(plUInt8 uiSlot) const
{
  const plTransform ownerTransform = GetOwner()->GetGlobalTransform();

  if (uiSlot >= m_Slots.GetCount())
    return ownerTransform; // the implicit slot

  plTransform local(m_Slots[uiSlot].m_vLocalPosition, m_Slots[uiSlot].m_qLocalRotation);
  return plTransform::MakeGlobalTransform(ownerTransform, local);
}

void plAiSmartObjectComponent::FinishUse(plGameObjectHandle hUser)
{
  if (m_pTacticalModule != nullptr)
  {
    m_pTacticalModule->FinishUse(hUser);
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Components_SmartObjectComponent);
