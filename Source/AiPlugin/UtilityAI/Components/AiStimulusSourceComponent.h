#pragma once

#include <AiPlugin/UtilityAI/Perception/AiPerception.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

using plAiStimulusSourceComponentManager = plComponentManagerSimple<class plAiStimulusSourceComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Lets any object emit AI stimuli (noise, custom events) without being an agent.
///
/// Emits every Interval while simulating, or only when Emit() is called if Interval is zero. The
/// stimulus starts at the owner's position and names the owner as its source.
class PL_AIPLUGIN_DLL plAiStimulusSourceComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiStimulusSourceComponent, plComponent, plAiStimulusSourceComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiStimulusSourceComponent

public:
  plAiStimulusSourceComponent();
  ~plAiStimulusSourceComponent();

  /// \brief Emits one stimulus now.
  void Emit(); // [ scriptable ]

  void Update();

  plEnum<plAiStimulusType> m_Type; ///< [ property ]
  plHashedString m_sCustomType;    ///< [ property ] only for the Custom type
  float m_fRadius = 15.0f;         ///< [ property ] how far the stimulus carries
  float m_fStrength = 1.0f;        ///< [ property ] [0,1] the confidence it gives
  plTime m_Interval;               ///< [ property ] zero = only on Emit()
  plHashedString m_sTeam;          ///< [ property ] agents of a friendly team ignore it

private:
  plTime m_NextEmit;
};
