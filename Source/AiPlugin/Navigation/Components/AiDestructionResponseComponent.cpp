#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/Components/AiDestructionResponseComponent.h>
#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Foundation/Math/BoundingBox.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiDestructionResponseComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgDestructibleChanged, OnMsgDestructibleChanged),
  }
  PL_END_MESSAGEHANDLERS;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Navigation"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiDestructionResponseComponent::plAiDestructionResponseComponent() = default;
plAiDestructionResponseComponent::~plAiDestructionResponseComponent() = default;

void plAiDestructionResponseComponent::OnActivated()
{
  SUPER::OnActivated();

  // receive plMsgDestructibleChanged from every destructible in the world, not just this object's subtree
  SetGlobalEventHandlerMode(true);
}

void plAiDestructionResponseComponent::OnMsgDestructibleChanged(plMsgDestructibleChanged& ref_msg)
{
  plAiNavMeshWorldModule* pNavModule = GetWorld()->GetModule<plAiNavMeshWorldModule>();
  if (pNavModule == nullptr)
    return;

  plBoundingBox box = plBoundingBox::MakeFromMinMax(ref_msg.m_vAffectedMin, ref_msg.m_vAffectedMax);

  if (!box.IsValid())
    return;

  pNavModule->InvalidateRegion(box);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Components_AiDestructionResponseComponent);
