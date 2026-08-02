#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Navigation/NavigationConfig.h>
#include <AiPlugin/Tactical/SquadTypes.h>
#include <Core/World/WorldModule.h>

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

  /// \brief Re-reads the squad settings from the AI plugin config file.
  void ReloadSquadConfig();

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
    plUInt8 m_uiBoundGroup = 0; ///< bounding overwatch group (0/1), assigned round-robin on join
  };

  struct Squad
  {
    plHashedString m_sId;
    bool m_bInUse = false;
    plHybridArray<MemberID, 8> m_Members;

    plUInt8 m_uiPeakSize = 0;
    plUInt8 m_uiLosses = 0;

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

    plTime m_NextTick;
  };

  void TickSquad(Squad& squad, plTime now);
  void SweepMembers(Squad& squad);
  void ReadMemberScratch(Squad& squad);
  void ShareTargets(Squad& squad, plTime now);
  void EvaluateIntent(Squad& squad, plTime now);
  void AssignAttackTokens(Squad& squad, plTime now);
  void AssignMoveTokensAndPositions(Squad& squad, plTime now);
  void PublishMemberEntries(Squad& squad);
  void DrawSquadDebug(const Squad& squad);

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
