#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Components/AiLodCenterComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiLodCenterComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Components"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiLodCenterComponent::plAiLodCenterComponent() = default;
plAiLodCenterComponent::~plAiLodCenterComponent() = default;

void plAiLodCenterComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  GetWorld()->GetOrCreateModule<plAiBrainWorldModule>()->RegisterLodCenter(GetHandle());
}

void plAiLodCenterComponent::OnDeactivated()
{
  if (plAiBrainWorldModule* pBrain = GetWorld()->GetModule<plAiBrainWorldModule>())
  {
    pBrain->UnregisterLodCenter(GetHandle());
  }

  SUPER::OnDeactivated();
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Components_AiLodCenterComponent);
