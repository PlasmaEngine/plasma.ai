#pragma once

#include <AiPlugin/Navigation/Navigation.h>
#include <Foundation/Math/Angle.h>
#include <Foundation/Math/Quat.h>
#include <Foundation/Math/Vec3.h>

/// \brief The canonical locomotion model used by plAiNavigationComponent.
///
/// Consumes the plAiSteeringInfo computed by plAiNavigation (next waypoint, turn constraints,
/// arrival distance) and integrates position and rotation:
/// - speed ramps with m_fAcceleration / m_fDecceleration towards m_fMaxSpeed,
///   clamped down near the goal (braking) and while turning sharply
/// - rotation turns towards the waypoint with a turn rate derived from speed and turn radius,
///   never slower than m_MinTurnSpeed
///
/// Inputs:  m_Info (per frame), current m_vPosition/m_qRotation/m_vVelocity, the tuning fields.
/// Outputs: updated m_vPosition/m_qRotation, and m_vDesiredVelocity — the velocity (world space,
///          Z = 0) that was integrated this frame. External systems (crowd avoidance, character
///          controllers, root motion) consume m_vDesiredVelocity and may override how it is
///          applied instead of using the integrated m_vPosition.
struct PL_AIPLUGIN_DLL plAiSteering
{
  plVec3 m_vPosition = plVec3::MakeZero();
  plQuat m_qRotation = plQuat::MakeIdentity();
  plVec3 m_vVelocity = plVec3::MakeZero();
  float m_fMaxSpeed = 6.0f;
  float m_fAcceleration = 5.0f;
  float m_fDecceleration = 10.0f;
  plAngle m_MinTurnSpeed = plAngle::MakeFromDegree(180);

  plAiSteeringInfo m_Info;

  /// The velocity that Calculate() integrated into m_vPosition this frame (world space, Z = 0).
  plVec3 m_vDesiredVelocity = plVec3::MakeZero();

  void Calculate(float fTimeDiff, plDebugRendererContext ctxt);
};
