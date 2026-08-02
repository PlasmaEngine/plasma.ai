#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Navigation/VoxelNavigation.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

PL_DECLARE_FLAGS(plUInt32, plAiVoxelNavigationDebugFlags, PrintState, VisPath, VisGrid);
PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiVoxelNavigationDebugFlags);

/// \brief Describes the different states a voxel-navigating object may be in.
struct plAiVoxelNavigationComponentState
{
  using StorageType = plUInt8;

  enum Enum
  {
    Idle,   ///< Currently not navigating.
    Moving, ///< Moving along a computed 3D path.
    Failed, ///< Path could not be found.

    Default = Idle
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiVoxelNavigationComponentState);

using plAiVoxelNavigationComponentManager = plComponentManagerSimple<class plAiVoxelNavigationComponent, plComponentUpdateType::WhenSimulating>;

/// Navigates a game object through 3D space using a voxel grid.
///
/// Suitable for flying creatures, underwater movement, or space navigation.
/// Call SetDestination() with a world-space target. The component computes
/// a 3D A* path through free voxels and moves the game object along that path.
class PL_AIPLUGIN_DLL plAiVoxelNavigationComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiVoxelNavigationComponent, plComponent, plAiVoxelNavigationComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiVoxelNavigationComponent

public:
  plAiVoxelNavigationComponent();
  ~plAiVoxelNavigationComponent();

  /// Sets the target position to navigate towards.
  void SetDestination(const plVec3& vGlobalPos); ///< [ scriptable ]

  /// Stops all navigation.
  void CancelNavigation(); ///< [ scriptable ]

  /// Returns the current navigation state.
  plEnum<plAiVoxelNavigationComponentState> GetState() const { return m_State; } ///< [ scriptable ]

  void SetNavigationTargetReference(const char* szReference); // [ property ]
  void SetNavigationTarget(plGameObjectHandle hObject);

  float m_fSpeed = 5.0f;           ///< [ property ] Target movement speed.
  float m_fAcceleration = 3.0f;    ///< [ property ] How fast to gain speed.
  float m_fDeceleration = 8.0f;    ///< [ property ] How fast to brake.
  float m_fReachedDistance = 1.0f; ///< [ property ] Distance at which destination is considered reached.
  bool m_bApplySteering = true;    ///< [ property ] Whether to apply movement to the game object.

  plBitflags<plAiVoxelNavigationDebugFlags> m_DebugFlags; ///< [ property ]

  plVec3 GetSteeringPosition() const; ///< [ scriptable ]
  plQuat GetSteeringRotation() const; ///< [ scriptable ]

protected:
  void Update();
  void MoveTowardsWaypoint(float fTimeDiff);

  plAiVoxelNavigation m_Navigation;
  plEnum<plAiVoxelNavigationComponentState> m_State;
  plGameObjectHandle m_hNavigationTarget;

  plVec3 m_vVelocity = plVec3::MakeZero();
  plVec3 m_vSteerPosition = plVec3::MakeZero();
  plQuat m_qSteerRotation = plQuat::MakeIdentity();

  float m_fVoxelSize = 0.5f;
  plUInt8 m_uiSkipNextFrames = 0;

private:
  const char* DummyGetter() const { return nullptr; }
};
