#pragma once

#include <AiPlugin/UtilityAI/Decision/AiScoring.h>
#include <Foundation/Time/Timestamp.h>
#include <Foundation/Tracks/Curve1D.h>

#include <QColor>
#include <QRectF>
#include <QString>

class plAiInput;
class plAiConsiderationObject;
class plSingleCurveData;
class plRTTI;
class QPainter;

/// \brief Shared helpers for the custom AI asset editor UIs.
///
/// All colors are sourced from plColorScheme so the AI editors automatically match the AI
/// category accents used for components, sliders and icons everywhere else in the editor.
namespace plAiAssetUi
{
  /// \brief The AI category accent (plColorScheme::GetCategoryColor("AI") - pastel dusty rose).
  QColor AccentColor();

  /// \brief A dimmed variant of the accent for borders / selection rails.
  QColor AccentColorDim();

  /// \brief The color used to draw response curves (scheme blue).
  QColor CurveColor();

  /// \brief Ladder color for a plAiBehaviorCategory value (plColorScheme::LightUI).
  QColor CategoryColor(plInt32 iCategory);

  /// \brief Display name for a plAiBehaviorCategory value.
  const char* CategoryName(plInt32 iCategory);

  /// \brief "plAiInput_DistanceToTarget" -> "Distance To Target".
  QString PrettyInputName(const plRTTI* pRtti);

  /// \brief Short one-line summary of an input's configuration ("'Ai_CoverStatus'", "matches Retreat", ...).
  QString InputParamSummary(const plAiInput* pInput);

  /// \brief Builds an evaluable runtime curve from edit-time curve data.
  ///
  /// Mirrors what plAiConsiderationDesc does after deserialization (sort, tangents, clamp,
  /// linear approximation), so editor previews match runtime behavior exactly.
  void BuildRuntimeCurve(const plSingleCurveData& data, plCurve1D& out_curve);

  /// \brief Evaluates one consideration the way plAiConsiderationDesc::Evaluate does:
  /// normalize raw into [min,max], remap through the curve (empty curve = pass-through), clamp to [0,1].
  float EvaluateConsideration(const plCurve1D& curve, float fRaw, float fMin, float fMax, float* out_pNormalized = nullptr, bool bLegacyDomain = false);

  /// \brief Combines consideration outputs into the final utility, mirroring plAiUtilityEvaluator::ScoreUtility
  /// (product with veto, n-axis compensation, weight scaling, clamp).
  float ComposeUtility(plArrayPtr<const float> considerationValues, float fWeight, float fWeightScale);

  /// \brief Paints a curve into the given rect (grid + curve). A null/empty curve draws the identity diagonal.
  void PaintCurve(QPainter& p, const QRectF& rect, const plCurve1D* pCurve, const QColor& curveColor, bool bLegacyDomain = false);
} // namespace plAiAssetUi

/// \brief One consideration of a referenced behavior asset, for read-only display.
struct plAiBehaviorPeekConsideration
{
  QString m_sLabel;
  float m_fInputMin = 0.0f;
  float m_fInputMax = 1.0f;
  bool m_bNeedsTarget = false;
  bool m_bLegacyCurveDomain = false;
  plCurve1D m_Curve; ///< empty = pass-through
};

/// \brief Read-only summary of an AI behavior asset, parsed from its document file.
///
/// Used by the archetype editor to show base weights, categories and consideration curves of
/// referenced behaviors without opening their documents.
struct plAiBehaviorPeek
{
  bool m_bValid = false;
  QString m_sName;
  QString m_sAbsPath;
  QString m_sLogic;
  plInt32 m_iCategory = plAiBehaviorCategory::Default;
  float m_fWeight = 1.0f;
  float m_fCommitBonus = 0.2f;
  double m_fCooldownSeconds = 0.0;
  plHybridArray<plAiBehaviorPeekConsideration, 8> m_Considerations;

  plTimestamp m_FileModified;
};

/// \brief Timestamp-validated cache of plAiBehaviorPeek entries, keyed by asset reference string.
class plAiBehaviorPeekCache
{
public:
  /// \brief Returns the (possibly freshly parsed) peek data for the given behavior asset reference (GUID string or path).
  static const plAiBehaviorPeek& Get(plStringView sAssetRef);

  /// \brief Drops all cached entries.
  static void Clear();

private:
  static plMap<plString, plAiBehaviorPeek> s_Cache;
};
