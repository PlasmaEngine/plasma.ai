#pragma once

#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <Core/Messages/EventMessage.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

/// \brief Sent to the agent's owner object when it starts traversing a nav link.
///
/// Handle this message (state machine, script, C++) to play a jump/vault/climb animation and move
/// the character from m_vStart to m_vEnd; call plAiNavigationComponent::FinishLinkTraversal() when
/// done. If nobody handles the message, the navigation component performs a built-in lerp
/// (parabolic for Jump/Vault/Drop) so links work without any game code.
struct PL_AIPLUGIN_DLL plMsgAiNavLinkTraverse : public plMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgAiNavLinkTraverse, plMessage);

  plVec3 m_vStart = plVec3::MakeZero();
  plVec3 m_vEnd = plVec3::MakeZero();
  plEnum<plAiNavLinkType> m_Type;
  plGameObjectHandle m_hLinkObject; ///< the object carrying the plAiNavLinkComponent
};

// updates in the editor too (plComponentUpdateType::Always) to draw the link visualization
using plAiNavLinkComponentManager = plComponentManagerSimple<class plAiNavLinkComponent, plComponentUpdateType::Always>;

/// \brief Authors an off-mesh connection (nav link) on the navmesh: a jump, vault, ladder, drop or door.
///
/// The link connects the owner's position (start) with a locally offset end point.
/// It is baked into the navmesh tiles containing its endpoints, so paths can route across gaps,
/// ledges and other discontinuities the walkable surface doesn't cover.
///
/// When an agent reaches the link, it sends plMsgAiNavLinkTraverse to its owner (see there) and
/// crosses via animation or the built-in fallback movement.
class PL_AIPLUGIN_DLL plAiNavLinkComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiNavLinkComponent, plComponent, plAiNavLinkComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  /// In edit mode: draws the link arc + endpoints (while simulating, AI.Navmesh.VisualizeLinks does).
  void Update();

  //////////////////////////////////////////////////////////////////////////
  // plAiNavLinkComponent

public:
  plAiNavLinkComponent();
  ~plAiNavLinkComponent();

  plVec3 m_vEndOffset = plVec3(2, 0, 0);    ///< [ property ] Link end, relative to the owner (the start).
  bool m_bBidirectional = true;             ///< [ property ] Whether agents may traverse in both directions.
  float m_fRadius = 0.3f;                   ///< [ property ] Endpoint snap tolerance onto the navmesh.
  plEnum<plAiNavLinkType> m_LinkType;       ///< [ property ] What kind of traversal this is (games pick animations by this).

private:
  plAiNavMeshWorldModule* m_pNavMeshModule = nullptr;
  plAiNavMeshWorldModule::NavLinkID m_LinkID = plAiNavMeshWorldModule::InvalidNavLinkID;
};
