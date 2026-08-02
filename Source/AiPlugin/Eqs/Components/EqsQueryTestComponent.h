#pragma once

#include <AiPlugin/Eqs/EqsWorldModule.h>
#include <Core/World/Component.h>
#include <Core/World/ComponentManager.h>
#include <Core/World/World.h>

using plAiEqsQueryTestComponentManager = plComponentManagerSimple<class plAiEqsQueryTestComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Editor/testing utility: repeatedly runs an EQS query asset from this object's position
/// and draws the scored results. Drag the object around while simulating to inspect query behavior.
///
/// Set a global key on another object and reference it via ThreatObjectKey: its position is passed
/// as a per-submit override for the context slot named ThreatSlot (default 'Threat'), so
/// threat-relative queries work without a perceiving AI agent.
class PL_AIPLUGIN_DLL plAiEqsQueryTestComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiEqsQueryTestComponent, plComponent, plAiEqsQueryTestComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiEqsQueryTestComponent

public:
  plAiEqsQueryTestComponent();
  ~plAiEqsQueryTestComponent();

  void SetQueryFile(const char* szFile); // [ property ]
  const char* GetQueryFile() const;      // [ property ]

  plAiEqsQueryResourceHandle m_hQuery;

  plString m_sThreatObjectKey;                   ///< [ property ] Global key of the object acting as the threat (optional).
  plHashedString m_sThreatSlot;                  ///< [ property ] Context slot the threat position is passed to (default 'Threat').
  bool m_bClaimBest = false;                     ///< [ property ] Claim the best cover result (tests claim exclusion).
  plTime m_QueryInterval = plTime::Seconds(0.5); ///< [ property ] How often to re-run the query.

  void SetThreatSlot(const char* szName) { m_sThreatSlot.Assign(szName); } // [ property ]
  const char* GetThreatSlot() const { return m_sThreatSlot.GetData(); }    // [ property ]

protected:
  void Update();
  void DrawResult();

  plAiEqsWorldModule* m_pEqsModule = nullptr;
  plAiEqsWorldModule::QueryID m_QueryId = plAiEqsWorldModule::InvalidQueryID;
  plAiEqsQueryResult m_LastResult;
  plVec3 m_vLastQuerier = plVec3::MakeZero();
  plTime m_NextQuery;
};
