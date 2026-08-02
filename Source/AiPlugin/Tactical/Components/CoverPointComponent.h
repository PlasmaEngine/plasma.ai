#pragma once

#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

// updates in the editor too (plComponentUpdateType::Always) to draw the cover preview
using plAiCoverPointComponentManager = plComponentManagerSimple<class plAiCoverPointComponent, plComponentUpdateType::Always>;

/// \brief Marks the owner position as a designer-authored AI cover point or cover strip.
///
/// Injected into the tactical cover registry: claimable, queryable and visualized exactly like
/// baked cover (cyan in AI.Tactical.VisualizeCover), but owned by this component and never
/// re-baked. Use it where automatic generation cannot see cover: obstacles below the agent step
/// height never cut the navmesh (sandbags, low crates), dynamic props are not part of navmesh
/// generation, and sometimes the bake simply needs a manual override.
///
/// The wall direction is the owner's forward (+X) axis, projected to XY - point it at the
/// blocking geometry. Width > 0 turns the marker into a cover SURFACE: a strip of claimable
/// points along the wall (spaced like baked cover), centered on the owner, that agents can move
/// within. Height is the authored wall top (visualization + peek logic). With AutoProbe set,
/// quality/wall height are instead measured per point with physics raycasts at registration (and
/// positions are ground-snapped). When selected, a wall slab shows the authored cover extent.
/// Registration takes effect one maintenance update after simulation start; requires a
/// configured cover navmesh (the tactical registry is inert without one).
class PL_AIPLUGIN_DLL plAiCoverPointComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiCoverPointComponent, plComponent, plAiCoverPointComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  /// In edit mode: draws the cover this component will create (point strip, wall band, facing).
  /// While simulating, AI.Tactical.VisualizeCover shows the registered points instead.
  void Update();

  //////////////////////////////////////////////////////////////////////////
  // plAiCoverPointComponent

public:
  plAiCoverPointComponent();
  ~plAiCoverPointComponent();

  plEnum<plAiCoverQuality> m_Quality; ///< [ property ] protection level reported for this point (default High); superseded by AutoProbe measurements
  bool m_bAutoProbe = false;          ///< [ property ] measure quality/wall height with raycasts at registration instead of trusting Quality
  float m_fWidth = 0.0f;              ///< [ property ] length of the cover strip along the wall, centered on the owner; 0 = a single point
  float m_fHeight = 2.0f;             ///< [ property ] authored wall-top height (visualization + peek logic); superseded by AutoProbe measurements

  /// \brief Size of the selected-state wall-slab visualization (see the box visualizer attribute).
  plVec3 GetVisualizationSize() const { return plVec3(0.15f, plMath::Max(m_fWidth, 0.4f), plMath::Max(m_fHeight, 0.2f)); }

private:
  plAiTacticalWorldModule* m_pTacticalModule = nullptr;
  plAiTacticalWorldModule::AuthoredCoverID m_ID = plAiTacticalWorldModule::InvalidAuthoredCoverID;
};
