#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/ComponentManager.h>
#include <Core/World/EventMessageHandlerComponent.h>

struct plMsgDestructibleChanged;

using plAiDestructionResponseComponentManager = plComponentManager<class plAiDestructionResponseComponent, plBlockStorageType::Compact>;

/// \brief Invalidates navmesh (and, through it, cover) sectors when destructible geometry breaks.
///
/// Registers itself as a GLOBAL event handler, so it receives plMsgDestructibleChanged from any
/// destructible in the world and dirties the overlapping navmesh sectors - agents then re-path
/// through new gaps and the tactical layer re-bakes cover for the changed sectors automatically.
///
/// A single instance is spawned automatically by plAiNavMeshWorldModule whenever a navmesh exists,
/// so no manual setup is required.
class PL_AIPLUGIN_DLL plAiDestructionResponseComponent : public plEventMessageHandlerComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiDestructionResponseComponent, plEventMessageHandlerComponent, plAiDestructionResponseComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

protected:
  virtual void OnActivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiDestructionResponseComponent

public:
  plAiDestructionResponseComponent();
  ~plAiDestructionResponseComponent();

private:
  void OnMsgDestructibleChanged(plMsgDestructibleChanged& ref_msg); // [ msg handler ]
};
