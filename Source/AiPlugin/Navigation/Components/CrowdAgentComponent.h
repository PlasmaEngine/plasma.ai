#pragma once

#include <AiPlugin/Navigation/CrowdWorldModule.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

using plAiCrowdAgentComponentManager = plComponentManagerSimple<class plAiCrowdAgentComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Makes a non-AI object (typically the player) known to the AI crowd avoidance.
///
/// Navigating AI agents will steer around this object instead of walking through it.
/// The object itself is never influenced - it only acts as a moving obstacle.
/// plAiNavigationComponent registers itself automatically; this component is only needed for
/// objects that move by other means (input, physics, animation).
class PL_AIPLUGIN_DLL plAiCrowdAgentComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiCrowdAgentComponent, plComponent, plAiCrowdAgentComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiCrowdAgentComponent

public:
  plAiCrowdAgentComponent();
  ~plAiCrowdAgentComponent();

  float m_fRadius = 0.4f; ///< [ property ] Body radius that AI agents keep their distance from.

protected:
  void Update();

  plAiCrowdWorldModule* m_pCrowdModule = nullptr;
  plAiCrowdWorldModule::AgentID m_CrowdAgentID = plAiCrowdWorldModule::InvalidAgentID;
  plVec3 m_vLastPosition = plVec3::MakeZero();
};
