#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Strings/String.h>

class plDocument;

struct plAiValidationSeverity
{
  enum Enum : plUInt8
  {
    Error,
    Warning,
    Info,
  };
};

/// \brief An element of an AI asset: what a row selects in its window and what a fix edits in its asset.
struct plAiValidationElement
{
  enum Kind : plUInt8
  {
    Root,          ///< the asset's properties
    Consideration, ///< behavior: consideration m_uiIndex
    Logic,         ///< behavior: the logic object
    BehaviorRow,   ///< archetype: behavior row m_uiIndex
    ContextSlot,   ///< EQS: context slot m_uiIndex
    Generator,     ///< EQS: the generator
    Test,          ///< EQS: test m_uiIndex
  };

  Kind m_Kind = Root;
  plUInt32 m_uiIndex = 0;
};

/// \brief A quick fix of an issue. Each one is an undoable step in the document of the asset it edits.
struct plAiValidationFix
{
  enum Kind : plUInt8
  {
    ChooseKey,            ///< sets the consideration input's blackboard key
    AddKeyToTemplate,     ///< adds m_sKey, typed as the input reads it, to the template m_sTargetAsset
    AssignConfig,         ///< sets the query's m_sProperty ("NavmeshConfig" or "PathSearchConfig")
    ChooseContext,        ///< sets the test's or the generator's context
    ChooseBehavior,       ///< picks the behavior asset of an archetype row
    AddBehavior,          ///< picks a behavior asset and adds a row for it
    ChooseTemplate,       ///< picks the archetype's blackboard template
    ChooseStateMachine,   ///< picks the state machine asset of the behavior's logic
    AddStateMachineLogic, ///< gives the behavior a state machine logic
    SetWeight,            ///< sets the behavior's weight to m_sValue
    SetWeightScale,       ///< sets the weight scale of archetype row m_Element.m_uiIndex to m_sValue
    OpenAsset,            ///< opens m_sTargetAsset
  };

  Kind m_Kind = OpenAsset;
  plString m_sLabel;
  plString m_sTooltip;

  /// Applied directly when set; otherwise m_Choices are offered in a menu (an empty choice is the default value).
  plString m_sValue;
  plHybridArray<plString, 8> m_Choices;

  /// The asset the fix edits or opens when it is not the issue's asset (a template, a state machine).
  plString m_sTargetAsset;
  plString m_sTargetName;

  /// Other assets the edit affects; they are listed before the fix applies.
  plHybridArray<plString, 4> m_AffectedAssets;

  /// AddKeyToTemplate: the key and the value type the input reads ("number" or "Vec3").
  plString m_sKey;
  plString m_sValueType;

  /// AssignConfig: the query property.
  plString m_sProperty;
};

/// \brief One problem, as the Validation dock lists it.
struct plAiValidationIssue
{
  plAiValidationSeverity::Enum m_Severity = plAiValidationSeverity::Error;

  plString m_sAsset;          ///< the file name of the asset the problem is in
  plString m_sAssetGuid;      ///< that asset's GUID string
  bool m_bDependency = false; ///< the asset is not the validated document but one it references

  plString m_sElement;  ///< "Consideration 2: Cover Availability"
  plString m_sKey;      ///< the key or reference: "Ai_CoverStaus", "context \"Enemy\""
  plString m_sExpected; ///< "number", "context slot"
  plString m_sActual;   ///< "missing", "Vec3"
  plString m_sMessage;

  plAiValidationElement m_Element; ///< in the asset the problem is in (what fixes edit)
  plAiValidationElement m_Reveal;  ///< in the validated document's window (what clicking the row selects)

  plHybridArray<plAiValidationFix, 2> m_Fixes;

  /// Identifies the issue across validation runs, so the selected row survives a refresh.
  plString GetId() const;
};

/// \brief Everything the dock shows for one document.
struct plAiValidationReport
{
  plDynamicArray<plAiValidationIssue> m_Issues;

  plString m_sOwner;        ///< the resolved blackboard owner and template
  plString m_sOwnerTooltip; ///< how the runtime resolves the board
};

/// \brief The validation rules of the AI Behavior, Archetype and EQS Query assets.
///
/// Behaviors: considerations without an input, empty input ranges, blackboard keys missing from the templates of the
/// archetypes that use the behavior (framework keys are always known), keys read as the wrong type, missing logic or
/// state machine, weight 0, and state machines in which no state ends the behavior. Archetypes: missing, unreadable
/// and duplicate behaviors, no behaviors, a missing template, behaviors that always score the same, and the issues of
/// the referenced behaviors with their keys checked against this archetype's template. EQS queries: the asset's own
/// root and test validation, and navmesh and path search config names the project's AI navigation config lacks.
///
/// Referenced assets are read from their open documents, else from their files (cached until a file changes).
namespace plAiValidation
{
  /// Validates the document and the assets it references; issues in those are marked as dependencies.
  void Validate(const plDocument* pDocument, plAiValidationReport& out_report);

  /// Drops the cached asset reads.
  void ClearCache();
} // namespace plAiValidation
