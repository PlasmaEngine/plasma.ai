#pragma once

#include <EditorPluginAi/Validation/AiValidation.h>

#include <Foundation/Types/Status.h>

/// \brief Applies the quick fixes of the Validation dock.
///
/// Every edit is one undoable step in the document of the asset it changes. A fix for another asset than the
/// validated document opens that asset's window first and applies there, so its undo history holds the step.
namespace plAiValidationFixes
{
  /// Whether the fix edits or opens another asset than pDocument's.
  bool TargetsOtherAsset(const plDocument* pDocument, const plAiValidationIssue& issue, const plAiValidationFix& fix);

  /// The file name of the asset the fix edits or opens.
  plString GetTargetName(const plAiValidationIssue& issue, const plAiValidationFix& fix);

  /// Applies the fix with sChoice (a value from fix.m_Choices, or fix.m_sValue). Fixes that pick an asset show the
  /// asset picker; call them outside a UI dispatch.
  plStatus Apply(const plDocument* pDocument, const plAiValidationIssue& issue, const plAiValidationFix& fix, plStringView sChoice);
} // namespace plAiValidationFixes
