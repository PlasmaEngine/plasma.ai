#pragma once

#include <AiPlugin/Eqs/EqsWorldModule.h>
#include <Core/Messages/EventMessage.h>
#include <Core/World/Component.h>
#include <Core/World/ComponentManager.h>
#include <Core/World/World.h>

/// \brief Sent by plAiEqsQueryComponent when a query run finished (successfully or not).
struct PL_AIPLUGIN_DLL plMsgAiEqsQueryFinished : public plEventMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgAiEqsQueryFinished, plEventMessage);

  bool m_bSuccess = false;                     ///< true when at least one candidate survived
  plVec3 m_vBestPosition = plVec3::MakeZero(); ///< the winning position (valid when m_bSuccess)
  float m_fBestScore = 0.0f;
  plGameObjectHandle m_hBestObject; ///< the winning item's object payload, if any
};

using plAiEqsQueryComponentManager = plComponentManagerSimple<class plAiEqsQueryComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Runs an EQS query asset from this object's position, without needing an AI agent -
/// spawner placement, camera anchors, scripted sequences.
///
/// With AutoRunInterval zero the query runs once when simulation starts (and again on RunQuery()
/// calls); otherwise it re-runs on the interval. Every finished run raises a
/// plMsgAiEqsQueryFinished event message; the best position is also available via
/// GetLastResultPosition().
class PL_AIPLUGIN_DLL plAiEqsQueryComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiEqsQueryComponent, plComponent, plAiEqsQueryComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiEqsQueryComponent

public:
  plAiEqsQueryComponent();
  ~plAiEqsQueryComponent();

  void SetQueryFile(const char* szFile); // [ property ]
  const char* GetQueryFile() const;      // [ property ]

  plAiEqsQueryResourceHandle m_hQuery;
  plTime m_AutoRunInterval;              ///< [ property ] 0 = run once at simulation start

  /// \brief Submits the query now (in addition to the automatic runs). [ scriptable ]
  void RunQuery();

  bool HasResult() const { return m_bHasResult; }                       // [ scriptable ]
  plVec3 GetLastResultPosition() const { return m_vLastResult; }        // [ scriptable ]

protected:
  void Update();

  plAiEqsWorldModule* m_pEqsModule = nullptr;
  plAiEqsWorldModule::QueryID m_QueryId = plAiEqsWorldModule::InvalidQueryID;
  plTime m_NextQuery;
  plVec3 m_vLastResult = plVec3::MakeZero();
  bool m_bHasResult = false;
  bool m_bRunOnce = false;

  plEventMessageSender<plMsgAiEqsQueryFinished> m_QueryFinishedSender; // [ event ]
};
