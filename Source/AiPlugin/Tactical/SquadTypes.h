#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Reflection/Reflection.h>

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
