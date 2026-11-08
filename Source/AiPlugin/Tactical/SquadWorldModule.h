#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Navigation/NavigationConfig.h>
#include <AiPlugin/Tactical/SquadTypes.h>
#include <Core/World/WorldModule.h>
#include <Foundation/Containers/Deque.h>
#include <Foundation/Containers/HashTable.h>

class plAiPerceptionWorldModule;

/// \brief Coordinates AI agents that share a squad id: squad-wide target sharing, attack tokens
/// (capping simultaneous shooters) and squad maneuvers (advance/retreat intents).
///
/// The module is strictly one-directional: it READS member blackboards (entries the brain and
/// tactical modules published earlier in the same frame) and WRITES its guidance back as
/// blackboard entries - 'Ai_HasAttackToken', 'Ai_SquadIntent', 'Ai_SquadMovePos' - which
/// consideration inputs and SM states consume. Target sharing goes through the perception
/// module's stimulus pipeline. It never calls into the brain.
///
/// Members are registered by plAiAgentComponent when its SquadId property is set. Guidance is
/// re-derived from the surviving members every squad tick, so member death cannot leak tokens.
/// Members need a plBlackboardComponent (like the tactical probes).
class PL_AIPLUGIN_DLL plAiSquadWorldModule final : public plWorldModule
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiSquadWorldModule, plWorldModule);
  PL_DISALLOW_COPY_AND_ASSIGN(plAiSquadWorldModule);

public:
  plAiSquadWorldModule(plWorld* pWorld);
  ~plAiSquadWorldModule();

  virtual void Initialize() override;

  using MemberID = plUInt32;
  static constexpr MemberID InvalidMemberID = plInvalidIndex;

  /// \brief Adds an agent to the squad with the given id (squads auto-form on first member).
  MemberID RegisterMember(const plHashedString& sSquadId, plComponentHandle hAgentComponent);
  void UnregisterMember(MemberID id);

  /// \brief How many active members the squad has; 0 for unknown squads.
  plUInt32 GetSquadSize(const plTempHashedString& sSquadId) const;

  /// \brief Re-reads the squad settings from the AI plugin config file.
  void ReloadSquadConfig();

  /// \name Squad orders
  ///@{

  /// \brief Orders a squad, replacing the order it has. A squad that doesn't exist yet takes the order as soon as its
  /// first member registers, with the time counting from now. Intents other than Engage, Advance, Hold and Retreat are
  /// ignored.
  void IssueOrder(const plHashedString& sSquadId, const plAiSquadOrder& order);

  /// \brief Ends the squad's order; it goes back to its automatic intent.
  void CancelOrder(const plTempHashedString& sSquadId);

  /// \brief The order of a squad and the time left on it (zero without a duration). False when it has none.
  bool GetOrder(const plTempHashedString& sSquadId, plAiSquadOrder& out_order, plTime& out_remaining) const;

  ///@}

  /// \name Debugging tools
  ///@{

  struct MemberInfo
  {
    plGameObjectHandle m_hOwner;
    plVec3 m_vPosition = plVec3::MakeZero();
    bool m_bAttackToken = false;
    bool m_bMoveToken = false;
  };

  struct HistoryInfo
  {
    double m_fTime = 0.0; ///< world seconds
    plString m_sText;
  };

  struct SquadInfo
  {
    plHashedString m_sId;
    plUInt32 m_uiPeakSize = 0;
    plUInt32 m_uiLosses = 0;
    plUInt32 m_uiRetreatLosses = 0; ///< the losses that break an order or make the squad retreat
    plEnum<plAiSquadIntent> m_Intent;
    bool m_bHasOrder = false;
    plAiSquadOrder m_Order;
    double m_fOrderRemaining = 0.0; ///< seconds; zero without a duration
    plVec3 m_vHome = plVec3::MakeZero();
    bool m_bHasTarget = false;
    plVec3 m_vTargetPosition = plVec3::MakeZero();
    plDynamicArray<MemberInfo> m_Members;
    plDynamicArray<HistoryInfo> m_History; ///< newest first
  };

  /// \brief One entry per squad that has members.
  void GetSquadInfos(plDynamicArray<SquadInfo>& out_squads) const;

  ///@}

private:
  void UpdateSquads(const UpdateContext& ctxt); // PostAsync, priority -1100 (after brain apply + tactical probes)

  struct Member
  {
    plComponentHandle m_hComponent;
    plUInt32 m_uiSquadIndex = plInvalidIndex;
    bool m_bInUse = false;

    bool m_bAttackToken = false;
    bool m_bMoveToken = false;
    plTime m_TokenSince;

    // per-tick scratch, read from the member's blackboard / world (valid within one squad tick)
    plVec3 m_vPosition = plVec3::MakeZero();
    plVec3 m_vTargetPos = plVec3::MakeZero();
    plGameObjectHandle m_hTarget;
    float m_fTargetConfidence = 0.0f;
    float m_fExposure = 0.0f;
    float m_fCoverStatus = 0.0f;
    bool m_bHasBlackboard = false;
    plUInt8 m_uiBoundGroup = 0; ///< bounding overwatch group (0/1); a new member joins the smaller one
  };

  struct Squad
  {
    plHashedString m_sId;
    bool m_bInUse = false;
    plHybridArray<MemberID, 8> m_Members;

    /// Members lost since the squad was last at full strength: a finished retreat and reinforcements reset both, so a
    /// squad only retreats again after new losses.
    plUInt16 m_uiPeakSize = 0;
    plUInt16 m_uiLosses = 0;

    plEnum<plAiSquadIntent> m_Intent;
    plTime m_IntentSince;

    bool m_bHomeCaptured = false;
    plVec3 m_vHome = plVec3::MakeZero(); ///< formation centroid; retreat rally anchor

    plGameObjectHandle m_hSharedTarget;
    plVec3 m_vSharedTargetPos = plVec3::MakeZero();
    float m_fSharedConfidence = 0.0f;
    plTime m_LastShare;

    plUInt8 m_uiMovingGroup = 0; ///< which bound group currently moves (Advance intent)
    plTime m_LastBoundSwap;
    plVec3 m_vSharedMovePos = plVec3::MakeZero(); ///< squad-level destination (retreat rally point)
    plTime m_RallyChosenFor = plTime::MakeFromSeconds(-1.0); ///< m_IntentSince of the retreat the rally point belongs to

    plTime m_NextTick;

    // ---- orders ----
    bool m_bHasOrder = false;
    plAiSquadOrder m_Order;
    plTime m_OrderStart;
    bool m_bOrderLossNoted = false; ///< the history already says that losses reached the limit under a no-retreat order

    struct HistoryEntry
    {
      plTime m_Time;
      plString m_sText;
    };

    plDeque<HistoryEntry> m_History; ///< oldest first
  };

  struct PendingOrder
  {
    plAiSquadOrder m_Order;
    plTime m_Start;
  };

  void StartOrder(Squad& squad, const plAiSquadOrder& order, plTime start);
  void EndOrder(Squad& squad, plTime now, plStringView sHow, bool bKeepIntentClock);
  void AddHistory(Squad& squad, plTime now, plStringView sText);
  void SetIntent(Squad& squad, plAiSquadIntent::Enum intent, plTime now, plStringView sHow);
  void DrawOrder(const Squad& squad);

  void TickSquad(Squad& squad, plTime now);
  void SweepMembers(Squad& squad);
  void ReadMemberScratch(Squad& squad);
  void ShareTargets(Squad& squad, plTime now);
  void EvaluateIntent(Squad& squad, plTime now);
  void AssignAttackTokens(Squad& squad, plTime now);
  void AssignMoveTokensAndPositions(Squad& squad, plTime now);
  void PublishMemberEntries(Squad& squad);
  void DrawSquadDebug(const Squad& squad);

  plHashTable<plHashedString, PendingOrder> m_PendingOrders; ///< orders for squads that have no member yet

  plDeque<Member> m_Members;
  plDynamicArray<MemberID> m_FreeMembers;
  plDeque<Squad> m_Squads;
  plDynamicArray<plUInt32> m_FreeSquads;
  plHashTable<plHashedString, plUInt32> m_SquadByName;

  plUInt32 m_uiTickCursor = 0;
  plAiSquadConfig m_Config;
  plAiPerceptionWorldModule* m_pPerception = nullptr; // resolved in RegisterMember (component context - safe creation point)

  // stats
  plUInt32 m_uiStatSharesPosted = 0;
  plUInt32 m_uiStatTokensGranted = 0;
};
