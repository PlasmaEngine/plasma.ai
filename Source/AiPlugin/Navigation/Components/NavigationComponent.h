#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Navigation/CrowdWorldModule.h>
#include <AiPlugin/Navigation/NavMeshQuery.h>
#include <AiPlugin/Navigation/Navigation.h>
#include <AiPlugin/Navigation/Steering.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

PL_DECLARE_FLAGS(plUInt32, plAiNavigationDebugFlags, PrintState, VisPathCorridor, VisPathLine, VisTarget);
PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiNavigationDebugFlags);

/// \brief Describes the different states a navigating object may be in.
struct plAiNavigationComponentState
{
  using StorageType = plUInt8;

  enum Enum
  {
    Idle,    ///< Currently not navigating.
    Moving,  ///< Moving or waiting for a path to be computed.
    Turning,
    Falling, ///< High up above the ground, falling downwards.
    Fallen,  ///< Was high up, now reached the ground. May happen if spawned in air, otherwise should never happen, so this is a kind of error state.
    Failed,  ///< Path could not be found, either because start position is invalid (off mesh) or destination is not reachable.
    TraversingLink, ///< Crossing an off-mesh nav link (jump, ladder, door...), steering is frozen.

    Default = Idle
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiNavigationComponentState);

/// \brief How plAiNavigationComponent applies the computed movement to the game object.
struct plAiMovementMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    DirectTransform,  ///< write the transform directly (cheapest, no physics interaction)
    PhysicsCharacter, ///< drive a character controller component via plMsgMoveCharacterController
    RootMotion,       ///< publish movement intent to the blackboard for the animation system, softly correct drift

    Default = DirectTransform
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiMovementMode);

using plAiNavigationComponentManager = plComponentManagerSimple<class plAiNavigationComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Adds functionality to navigate on a navmesh.
///
/// Call SetDestination() to have the component move the parent game object along a path towards the goal.
/// Call GetState() to query whether it is moving and how.
///
/// This component is still very much work-in-progress. Things that need improvement:
///   * The state reporting is still limited, there is no distinction between failure states (invalid start position, target position, partial path)
///   * The 'destination reached' implementation is quite hacky.
///   * There is no way to stop navigating, but come to a stop smoothly (slowing down).
///   * Crowd avoidance only changes the steered position, so PhysicsCharacter movement does not avoid other agents.
///   * It is not designed to be pushed around dynamically. There is no physics character controller use to prevent it from being pushed into walls.
///   * If it somehow leaves the navmesh area, it just fails, there is no recovery mechanism.
class PL_AIPLUGIN_DLL plAiNavigationComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiNavigationComponent, plComponent, plAiNavigationComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  //  plAiNavMeshPathTestComponent

public:
  plAiNavigationComponent();
  ~plAiNavigationComponent();

  /// \brief Sets the target position to reach.
  ///
  /// If bAllowPartialPath is false, and a complete path can't be found (too far or simply not reachable),
  /// the 'Failed' state is used.
  /// Otherwise the 'Moving' state indicates that the character is navigating.
  void SetDestination(const plVec3& vGlobalPos, bool bAllowPartialPath); ///< [ scriptable ]

  /// \brief Can be called at any time to stop moving.
  void CancelNavigation();                    ///< [ scriptable ]

  void StopWalking(float fWithinDistance);    ///< [ scriptable ]

  void TurnTowards(const plVec2& vGlobalPos); ///< [ scriptable ]

  /// \brief How much the object would have to turn, to look at the position.
  plAngle GetTurnAngleTowards(const plVec2& vGlobalPos) const; ///< [ scriptable ]

  plHashedString m_sNavmeshConfig;                             ///< [ property ] Which navmesh to walk on.
  plHashedString m_sPathSearchConfig;                          ///< [ property ] What constraints there are for walking on the navmesh.

  float m_fReachedDistance = 1.0f;                             ///< [ property ] The distance at which the destination is considered to be reached.
  float m_fSpeed = 5.0f;                                       ///< [ property ] The target speed to reach.
  float m_fFootRadius = 0.15f;                                 ///< [ property ] The footprint to determine whether the character is standing on solid ground.
  plUInt32 m_uiCollisionLayer = 0;                             ///< [ property ] The physics collision layer for determining what ground one can stand on.
  float m_fFallHeight = 0.7f;                                  ///< [ property ] If there is more distance below the character than this, it is considered to be falling.
  float m_fAcceleration = 3.0f;                                ///< [ property ] How fast to gain speed.
  float m_fDecceleration = 8.0f;                               ///< [ property ] How fast to brake.

  bool m_bCrowdAvoidance = true;                               ///< [ property ] Steer around other agents instead of walking through them.
  float m_fAgentRadius = 0.3f;                                 ///< [ property ] Body radius used for local avoidance between agents.
  plEnum<plAiCrowdAvoidanceQuality> m_AvoidanceQuality;        ///< [ property ] How much CPU this agent's avoidance may use.

  plEnum<plAiMovementMode> m_MovementMode;                     ///< [ property ] How movement is applied: direct transform, character controller or root motion.
  float m_fLinkTraverseSpeed = 3.0f;                           ///< [ property ] Speed of the built-in nav link traversal (when no game code handles the traverse message).
  float m_fCCWalkSpeed = 1.5f;                                 ///< [ property ] PhysicsCharacter mode: the character controller's configured walk speed.
  float m_fCCRunSpeed = 3.5f;                                  ///< [ property ] PhysicsCharacter mode: the character controller's configured run speed.
  plAngle m_CCRotateSpeed = plAngle::MakeFromDegree(90);       ///< [ property ] PhysicsCharacter mode: the character controller's configured rotation speed.

  /// \brief Completes a nav link traversal that game code handled (after receiving plMsgAiNavLinkTraverse).
  void FinishLinkTraversal(); ///< [ scriptable ]

  plBitflags<plAiNavigationDebugFlags> m_DebugFlags;           ///< [ property ] What aspects of the navigation to visualize.

  /// \brief Returns the current navigation state.
  plEnum<plAiNavigationComponentState> GetState() const { return m_State; } ///< [ scriptable ]


  /// \brief Checks whether the area around the given point is loaded and thus queries would succeed.
  ///
  /// If the area is not fully loaded, the function returns false.
  /// In this case, queries in that area will probably fail and should be delayed to a later point,
  /// since the navmesh first has to be generated.
  bool EnsureNavMeshSectorAvailable(const plVec3& vCenter, float fRadius); ///< [ scriptable ]

  /// \brief Attempts to find a random point on the navmesh. The circle limits which navmesh polygons are visited.
  ///
  /// The result may be outside the circle, if the circle overlaps with a large navmesh polygon.
  bool FindRandomPointAroundCircle(const plVec3& vCenter, float fRadius, plVec3& out_vPoint); ///< [ scriptable ]

  bool RaycastNavMesh(const plVec3& vStart, const plVec3& vDirection, float fDistance, plVec3& out_vPoint, float& out_fDistance);

  plVec3 GetSteeringPosition() const; ///< [ scriptable ]
  plQuat GetSteeringRotation() const; ///< [ scriptable ]

protected:
  void Update();
  void Steer(plTransform& transform, float tDiff);
  void Turn(plTransform& transform, float tDiff);
  void PlaceOnGround(plTransform& transform, float tDiff);
  bool PrepareQueryObject();

  void StartLinkTraversal(dtPolyRef linkPoly);
  void TraverseLink(plTransform& transform, float tDiff);
  void ApplyMovement(const plTransform& transform, float tDiff);
  void MapVelocityToCharacterInput(const plVec3& vDesiredVelocity, float tDiff);

  plAiNavmeshQuery m_Query;
  plEnum<plAiNavigationComponentState> m_State;
  plAiSteering m_Steering;
  plAiNavigation m_Navigation;

  plAiCrowdWorldModule* m_pCrowdModule = nullptr;
  plAiCrowdWorldModule::AgentID m_CrowdAgentID = plAiCrowdWorldModule::InvalidAgentID;
  plVec3 m_vCrowdVelocity = plVec3::MakeZero(); ///< Avoidance velocity, approaches the solved velocity within the acceleration limits.

  // nav link traversal state
  plVec3 m_vLinkStart = plVec3::MakeZero();
  plVec3 m_vLinkEnd = plVec3::MakeZero();
  plEnum<plAiNavLinkType> m_TraversingLinkType;
  float m_fLinkProgress = 0.0f;
  bool m_bLinkHandledExternally = false;
  float m_fFallSpeed = 0.0f;
  bool m_bAllowPartialPath = false;
  bool m_bApplySteering = true;
  plUInt8 m_uiSkipNextFrames = 0;
  float m_fStopWalkDistance = plMath::HighValue<float>();
  plVec2 m_vTurnTowardsPos = plVec2::MakeZero();

  plVec3 m_vSteerPosition;
  plQuat m_qSteerRotation;

  plVec3 m_vPreviousPosition = plVec3::MakeZero();
  float m_fPreviousTimeStep = 0.0f;

private:
  const char* DummyGetter() const { return nullptr; }
};
