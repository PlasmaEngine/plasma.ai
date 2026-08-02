#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/Component.h>
#include <Core/World/ComponentManager.h>
#include <Foundation/Strings/HashedString.h>
#include <Foundation/Time/Time.h>

using plAiNavMeshPrebuildComponentManager = plComponentManagerSimple<class plAiNavMeshPrebuildComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Pre-builds all navmesh sectors overlapping a box around the owner.
///
/// Navmesh sectors (and the cover points baked from them) otherwise only build on demand - where
/// path searches and tactical/EQS queries request them - so areas no agent has visited have no
/// navigation data at all. Place this component around combat spaces, spawn areas or anywhere
/// agents must act the moment the player arrives.
///
/// The component re-requests the region until all sectors are built, then keeps re-pinning it at
/// a slow cadence (nearly free) so a later sector unload does not permanently drop the area.
/// Cover points appear automatically once the sectors exist (the tactical layer observes sector
/// changes).
class PL_AIPLUGIN_DLL plAiNavMeshPrebuildComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiNavMeshPrebuildComponent, plComponent, plAiNavMeshPrebuildComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;

  void Update();

  //////////////////////////////////////////////////////////////////////////
  // plAiNavMeshPrebuildComponent

public:
  plAiNavMeshPrebuildComponent();
  ~plAiNavMeshPrebuildComponent();

  plVec3 m_vHalfExtents = plVec3(32, 32, 4); ///< [ property ] box half-extents around the owner (sector selection uses XY; Z is for the gizmo)
  plHashedString m_sNavmeshConfig;           ///< [ property ] which navmesh to prebuild; empty = all configured navmeshes

private:
  plTime m_NextRequest;
};
