#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

using plAiLodCenterComponentManager = plComponentManager<class plAiLodCenterComponent, plBlockStorageType::Compact>;

/// \brief Marks the owner object as a center of AI relevance (typically the player or the camera).
///
/// AI agents derive their LOD tier (decision and execution rates) from the distance to the nearest
/// LOD center. When no LOD center exists in a world, all agents run at full ('Hot') rates.
class PL_AIPLUGIN_DLL plAiLodCenterComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiLodCenterComponent, plComponent, plAiLodCenterComponentManager);

public:
  plAiLodCenterComponent();
  ~plAiLodCenterComponent();

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;
};
