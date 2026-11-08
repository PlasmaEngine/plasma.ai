#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Math/Vec2.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Time/Time.h>

/// \brief The squad-level intent, published to every member's blackboard as 'Ai_SquadIntent'.
///
/// Intents BIAS individual behaviors, they never override them: behaviors add a consideration on
/// the intent with a floored response curve (e.g. map 'mismatch' to 0.3 instead of 0), so squad
/// guidance shifts the utility balance without vetoing self-preservation.
struct PL_AIPLUGIN_DLL plAiSquadIntent
{
  using StorageType = plUInt8;

  enum Enum
  {
    None,    ///< not in a squad / no guidance
    Engage,  ///< fight from current positions
    Advance, ///< close in on the shared target (bounding: move-token holders bound, the rest cover)
    Hold,    ///< no target knowledge; hold positions
    Retreat, ///< fall back to the rally point

    Default = None
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiSquadIntent);

/// \brief An order for a whole squad, from game code, a script, the encounter director or the AI Debugger.
///
/// While an order is active, the squad's intent is the order's intent: the squad's own reasoning (shared confidence,
/// losses, exposure) does not change it, and the members still read it as 'Ai_SquadIntent', so their behaviors need no
/// change. With a region, the members' 'Ai_SquadMovePos' is a spot inside it; they hold it, advance into it or fall back
/// to it. The order ends when its duration is up, or when it is cancelled. Losses break it too (the squad retreats), unless
/// the order says there is no retreat.
struct PL_AIPLUGIN_DLL plAiSquadOrder
{
  plEnum<plAiSquadIntent> m_Intent;     ///< Engage, Advance, Hold or Retreat
  bool m_bHasRegion = false;
  plVec2 m_vRegionMin = plVec2::MakeZero(); ///< world XY corners of the region
  plVec2 m_vRegionMax = plVec2::MakeZero();
  plTime m_Duration;                    ///< zero = until cancelled
  bool m_bNoRetreat = false;            ///< losses don't break the order
  plString m_sIssuer;                   ///< who gave it, for the AI Debugger: "Encounter: Bridge_Wave2", "Script", "AI Debugger"

  /// \brief The center of the region.
  plVec2 GetRegionCenter() const { return (m_vRegionMin + m_vRegionMax) * 0.5f; }

  /// \brief The spot of slot uiIndex out of uiCount inside the region, spread in a grid. Without a region, the origin.
  plVec2 GetRegionSlot(plUInt32 uiIndex, plUInt32 uiCount) const;
};
