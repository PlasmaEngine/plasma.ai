#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

using plAiDangerAreaComponentManager = plComponentManagerSimple<class plAiDangerAreaComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Makes the area around an object dangerous to the AI: a grenade, a fire, a collapsing floor.
///
/// While the component is active, a danger area of Radius follows its object. Agents prefer cover outside it, and
/// an agent whose claimed cover lies inside it ends its behavior and picks its cover again. A Duration above zero fades
/// the danger over that time and removes the area; the object can go on. An empty Team makes it danger for every team.
///
/// Scripts and state machines can also add areas anywhere with AddDangerArea, from any object that has this component.
class PL_AIPLUGIN_DLL plAiDangerAreaComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiDangerAreaComponent, plComponent, plAiDangerAreaComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiDangerAreaComponent

public:
  plAiDangerAreaComponent();
  ~plAiDangerAreaComponent();

  /// \brief Starts the area around the owner, if it is not running. Starts the clock of a timed area again.
  void Activate(); // [ scriptable ]

  /// \brief Removes the area around the owner.
  void Deactivate(); // [ scriptable ]

  /// \brief Adds an area of its own at a position, whatever the component's settings are. Team empty = every team.
  void AddDangerArea(const plVec3& vPosition, float fRadius, float fDurationSeconds, const char* szTeam); // [ scriptable ]

  void Update();

  float m_fRadius = 5.0f;        ///< [ property ]
  plTime m_Duration;             ///< [ property ] zero = while the component is active
  plHashedString m_sTeam;        ///< [ property ] empty = danger for every team
  bool m_bStartActive = true;    ///< [ property ] the area starts with the simulation

private:
  plUInt32 m_uiDangerId = plInvalidIndex;
  plVec3 m_vLastPosition = plVec3::MakeZero();
};
