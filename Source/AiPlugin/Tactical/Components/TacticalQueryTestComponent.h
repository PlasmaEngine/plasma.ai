#pragma once

#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <Core/World/Component.h>
#include <Core/World/ComponentManager.h>
#include <Core/World/World.h>

using plAiTacticalQueryTestComponentManager = plComponentManagerSimple<class plAiTacticalQueryTestComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Editor/testing utility: repeatedly runs a tactical query from this object's position and
/// draws the scored results. Drag the object around while simulating to inspect query behavior.
///
/// Set a global key on another object and reference it as the 'threat' to test threat-relative
/// presets (take cover, flank). With ClaimBest enabled, two of these components demonstrate that
/// claimed cover points are excluded from each other's results.
class PL_AIPLUGIN_DLL plAiTacticalQueryTestComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiTacticalQueryTestComponent, plComponent, plAiTacticalQueryTestComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiTacticalQueryTestComponent

public:
  plAiTacticalQueryTestComponent();
  ~plAiTacticalQueryTestComponent();

  plEnum<plAiTacticalQueryPreset> m_Preset;         ///< [ property ] Which canned query to run.
  float m_fRadiusMin = 2.0f;                        ///< [ property ] Inner generator radius.
  float m_fRadiusMax = 12.0f;                       ///< [ property ] Outer generator radius.
  plString m_sThreatObjectKey;                      ///< [ property ] Global key of the object acting as the threat (optional).
  bool m_bClaimBest = false;                        ///< [ property ] Claim the best cover result (tests claim exclusion).
  plTime m_QueryInterval = plTime::Seconds(0.5);    ///< [ property ] How often to re-run the query.

protected:
  void Update();

  plAiTacticalWorldModule* m_pTacticalModule = nullptr;
  plAiTacticalWorldModule::QueryID m_QueryId = plAiTacticalWorldModule::InvalidQueryID;
  plAiTacticalQueryResult m_LastResult;
  plVec3 m_vLastQuerier = plVec3::MakeZero();
  plTime m_NextQuery;
};
