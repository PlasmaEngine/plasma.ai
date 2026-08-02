#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/World.h>

#include <AiPlugin/UtilityAI/Framework/AiActionQueue.h>
#include <AiPlugin/UtilityAI/Framework/AiBehaviorManager.h>
#include <AiPlugin/UtilityAI/Framework/AiPerceptionManager.h>
#include <AiPlugin/UtilityAI/Framework/AiSensorManager.h>

using plAiComponentManager = plComponentManagerSimple<class plAiComponent, plComponentUpdateType::WhenSimulating, plBlockStorageType::FreeList>;

/// \brief DEPRECATED: legacy hardcoded utility AI component.
///
/// Use plAiAgentComponent with 'AI Behavior' / 'AI Archetype' assets instead (data-driven,
/// centrally updated by plAiBrainWorldModule). This component and the manager-based framework
/// (plAiBehaviorManager & friends) are only kept until dependent game code has been ported.
class PL_AIPLUGIN_DLL plAiComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiComponent, plComponent, plAiComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiComponent

  void Update();

public:
  plAiComponent();
  ~plAiComponent();

  bool m_bDebugInfo = false;

protected:
  plAiActionQueue m_ActionQueue;
  plTime m_LastAiUpdate;

  plAiSensorManager m_SensorManager;
  plAiPerceptionManager m_PerceptionManager;
  plAiBehaviorManager m_BehaviorManager;
  float m_fLastScore = 0.0f;
};
