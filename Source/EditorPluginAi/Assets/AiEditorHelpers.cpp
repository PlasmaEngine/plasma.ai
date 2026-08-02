#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiBehaviorAsset.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <AiPlugin/UtilityAI/Decision/AiInput.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Math/ColorScheme.h>
#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <Foundation/Serialization/DdlSerializer.h>
#include <Foundation/Serialization/RttiConverter.h>
#include <GuiFoundation/GuiFoundationDLL.h>
#include <GuiFoundation/Widgets/CurveEditData.h>

#include <QPainter>
#include <QPainterPath>

namespace plAiAssetUi
{
  static QColor ToQt(const plColor& c)
  {
    return plToQtColor(plColorGammaUB(c));
  }

  QColor AccentColor()
  {
    return ToQt(plColorScheme::GetCategoryColor("AI", plColorScheme::CategoryColorUsage::BorderColor));
  }

  QColor AccentColorDim()
  {
    plColor c = plColorScheme::GetCategoryColor("AI", plColorScheme::CategoryColorUsage::BorderColor);
    return ToQt(c * 0.55f);
  }

  QColor CurveColor()
  {
    return ToQt(plColorScheme::LightUI(plColorScheme::Blue));
  }

  QColor CategoryColor(plInt32 iCategory)
  {
    switch (iCategory)
    {
      case plAiBehaviorCategory::Interrupt:
        return ToQt(plColorScheme::LightUI(plColorScheme::Red));
      case plAiBehaviorCategory::Combat:
        return ToQt(plColorScheme::LightUI(plColorScheme::Orange));
      case plAiBehaviorCategory::Command:
        return ToQt(plColorScheme::LightUI(plColorScheme::Yellow));
      case plAiBehaviorCategory::Investigate:
        return ToQt(plColorScheme::LightUI(plColorScheme::Cyan));
      case plAiBehaviorCategory::ActiveIdle:
        return ToQt(plColorScheme::LightUI(plColorScheme::Blue));
      case plAiBehaviorCategory::Idle:
        return ToQt(plColorScheme::LightUI(plColorScheme::Gray));
      case plAiBehaviorCategory::Fallback:
      default:
        return ToQt(plColorScheme::LightUI(plColorScheme::Gray) * 0.7f);
    }
  }

  const char* CategoryName(plInt32 iCategory)
  {
    switch (iCategory)
    {
      case plAiBehaviorCategory::Fallback:
        return "Fallback";
      case plAiBehaviorCategory::Idle:
        return "Idle";
      case plAiBehaviorCategory::ActiveIdle:
        return "ActiveIdle";
      case plAiBehaviorCategory::Investigate:
        return "Investigate";
      case plAiBehaviorCategory::Command:
        return "Command";
      case plAiBehaviorCategory::Combat:
        return "Combat";
      case plAiBehaviorCategory::Interrupt:
        return "Interrupt";
      default:
        return "<unknown>";
    }
  }

  QString PrettyInputName(const plRTTI* pRtti)
  {
    if (pRtti == nullptr)
      return QStringLiteral("<no input>");

    plStringBuilder sName = pRtti->GetTypeName();
    sName.TrimWordStart("plAiInput_");
    sName.TrimWordStart("plAiInput");

    QString sIn = QString::fromUtf8(sName.GetData());
    QString sOut;
    sOut.reserve(sIn.size() + 6);

    for (int i = 0; i < sIn.size(); ++i)
    {
      if (i > 0 && sIn[i].isUpper() && !sIn[i - 1].isUpper())
        sOut.append(' ');

      sOut.append(sIn[i]);
    }

    return sOut;
  }

  QString InputParamSummary(const plAiInput* pInput)
  {
    if (pInput == nullptr)
      return QStringLiteral("select an input type");

    if (auto pBb = plDynamicCast<const plAiInput_BlackboardValue*>(pInput))
    {
      return QString("'%1' (fallback %2)").arg(QString::fromUtf8(pBb->GetEntryName())).arg(pBb->m_fFallback);
    }

    if (auto pDist = plDynamicCast<const plAiInput_DistanceToPosition*>(pInput))
    {
      return QString("distance to '%1'").arg(QString::fromUtf8(pDist->GetEntryName()));
    }

    if (auto pConst = plDynamicCast<const plAiInput_Constant*>(pInput))
    {
      return QString("value %1").arg(pConst->m_fValue);
    }

    if (auto pCover = plDynamicCast<const plAiInput_CoverAvailability*>(pInput))
    {
      return pCover->m_Mode == plAiCoverAvailabilityMode::ClaimedQuality ? QStringLiteral("claimed cover quality (Ai_CoverStatus)")
                                                                         : QStringLiteral("unclaimed cover nearby (Ai_CoverNearby)");
    }

    if (auto pIntent = plDynamicCast<const plAiInput_SquadIntent*>(pInput))
    {
      const char* szIntent = "None";
      switch (pIntent->m_DesiredIntent.GetValue())
      {
        case plAiSquadIntent::Engage:
          szIntent = "Engage";
          break;
        case plAiSquadIntent::Advance:
          szIntent = "Advance";
          break;
        case plAiSquadIntent::Hold:
          szIntent = "Hold";
          break;
        case plAiSquadIntent::Retreat:
          szIntent = "Retreat";
          break;
        default:
          break;
      }
      return QString("1 when squad intent matches %1").arg(szIntent);
    }

    if (plDynamicCast<const plAiInput_TargetConfidence*>(pInput))
      return QStringLiteral("perception record confidence 0-1");
    if (plDynamicCast<const plAiInput_DistanceToTarget*>(pInput))
      return QStringLiteral("meters to the scored target");
    if (plDynamicCast<const plAiInput_AngleToTarget*>(pInput))
      return QStringLiteral("degrees between forward and target (0-180)");
    if (plDynamicCast<const plAiInput_TargetExposure*>(pInput))
      return QStringLiteral("target sees me (Ai_TargetExposure)");
    if (plDynamicCast<const plAiInput_SquadHasAttackToken*>(pInput))
      return QStringLiteral("Ai_HasAttackToken - squadless agents = 1");
    if (plDynamicCast<const plAiInput_TimeSinceActivation*>(pInput))
      return QStringLiteral("seconds this behavior has been active");
    if (plDynamicCast<const plAiInput_TimeSinceDeactivation*>(pInput))
      return QStringLiteral("seconds since last deactivation");

    return QString();
  }

  void BuildRuntimeCurve(const plSingleCurveData& data, plCurve1D& out_curve)
  {
    out_curve.Clear();
    data.ConvertToRuntimeData(out_curve);

    if (out_curve.IsEmpty())
      return;

    // same preparation the runtime descriptor performs after deserialization
    out_curve.SortControlPoints();
    out_curve.ApplyTangentModes();
    out_curve.ClampTangents();
    out_curve.CreateLinearApproximation();
  }

  float EvaluateConsideration(const plCurve1D& curve, float fRaw, float fMin, float fMax, float* out_pNormalized)
  {
    const float fRange = fMax - fMin;
    float fNormalized = (plMath::Abs(fRange) < plMath::SmallEpsilon<float>()) ? 0.0f : (fRaw - fMin) / fRange;
    fNormalized = plMath::Clamp(fNormalized, 0.0f, 1.0f);

    if (out_pNormalized != nullptr)
      *out_pNormalized = fNormalized;

    if (curve.IsEmpty())
      return fNormalized;

    const double fPos = curve.ConvertNormalizedPos(fNormalized);
    const float fValue = static_cast<float>(curve.Evaluate(fPos));
    return plMath::Clamp(fValue, 0.0f, 1.0f);
  }

  float ComposeUtility(plArrayPtr<const float> considerationValues, float fWeight, float fWeightScale)
  {
    float fScore = 1.0f;
    plUInt32 uiNum = 0;

    for (float fValue : considerationValues)
    {
      fScore *= fValue;
      ++uiNum;
    }

    if (fScore <= 0.0f)
      return 0.0f;

    if (uiNum > 1)
    {
      // compensation factor: counteracts the score shrinking with every additional consideration
      const float fModification = 1.0f - (1.0f / static_cast<float>(uiNum));
      fScore = fScore + ((1.0f - fScore) * fScore * fModification);
    }

    fScore *= fWeight * fWeightScale;

    return plMath::Clamp(fScore, 0.0f, 1.0f);
  }

  void PaintCurve(QPainter& p, const QRectF& rect, const plCurve1D* pCurve, const QColor& curveColor)
  {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // inset background + grid
    p.fillRect(rect, QColor(31, 32, 33));
    QPen gridPen(QColor(44, 46, 48));
    gridPen.setWidthF(1.0);
    p.setPen(gridPen);
    p.drawLine(QPointF(rect.left(), rect.center().y()), QPointF(rect.right(), rect.center().y()));
    p.drawLine(QPointF(rect.center().x(), rect.top()), QPointF(rect.center().x(), rect.bottom()));

    const double fPad = 2.0;
    const QRectF inner = rect.adjusted(fPad, fPad, -fPad, -fPad);

    QPainterPath path;
    const plUInt32 uiSamples = 48;

    for (plUInt32 i = 0; i <= uiSamples; ++i)
    {
      const double t = static_cast<double>(i) / uiSamples;
      double fValue = t; // identity when no curve is set

      if (pCurve != nullptr && !pCurve->IsEmpty())
      {
        fValue = plMath::Clamp(pCurve->Evaluate(pCurve->ConvertNormalizedPos(t)), 0.0, 1.0);
      }

      const QPointF pt(inner.left() + t * inner.width(), inner.bottom() - fValue * inner.height());

      if (i == 0)
        path.moveTo(pt);
      else
        path.lineTo(pt);
    }

    QPen curvePen(curveColor);
    curvePen.setWidthF(2.0);
    p.setPen(curvePen);
    p.drawPath(path);

    p.restore();
  }
} // namespace plAiAssetUi

//////////////////////////////////////////////////////////////////////////

plMap<plString, plAiBehaviorPeek> plAiBehaviorPeekCache::s_Cache;

void plAiBehaviorPeekCache::Clear()
{
  s_Cache.Clear();
}

const plAiBehaviorPeek& plAiBehaviorPeekCache::Get(plStringView sAssetRef)
{
  plAiBehaviorPeek& peek = s_Cache[sAssetRef];

  // resolve the reference through the curator
  plStringBuilder sAbsPath;
  QString sAssetName;
  {
    auto asset = plAssetCurator::GetSingleton()->FindSubAsset(sAssetRef);
    if (!asset.isValid() || asset->m_pAssetInfo == nullptr)
    {
      peek = plAiBehaviorPeek();
      return peek;
    }

    sAbsPath = asset->m_pAssetInfo->m_Path.GetAbsolutePath();
    sAssetName = QString::fromUtf8(asset->GetName().GetStartPointer(), static_cast<int>(asset->GetName().GetElementCount()));
  }

  plFileStats stats;
  if (plOSFile::GetFileStats(sAbsPath, stats).Failed())
  {
    peek = plAiBehaviorPeek();
    return peek;
  }

  if (peek.m_bValid && stats.m_LastModificationTime.Compare(peek.m_FileModified, plTimestamp::CompareMode::FileTimeEqual))
  {
    return peek; // cache hit
  }

  peek = plAiBehaviorPeek();
  peek.m_FileModified = stats.m_LastModificationTime;
  peek.m_sAbsPath = QString::fromUtf8(sAbsPath.GetData());

  plFileReader file;
  if (file.Open(sAbsPath).Failed())
    return peek;

  plUniquePtr<plAbstractObjectGraph> pHeader;
  plUniquePtr<plAbstractObjectGraph> pObjects;
  plUniquePtr<plAbstractObjectGraph> pTypes;

  if (plAbstractGraphDdlSerializer::ReadDocument(file, pHeader, pObjects, pTypes, true).Failed() || pObjects == nullptr)
    return peek;

  const plAbstractObjectNode* pPropertiesNode = nullptr;
  for (auto it = pObjects->GetAllNodes().GetIterator(); it.IsValid(); ++it)
  {
    if (it.Value()->GetType().IsEqual("plAiBehaviorAssetObject"))
    {
      pPropertiesNode = it.Value();
      break;
    }
  }

  if (pPropertiesNode == nullptr)
    return peek;

  // instantiate the editor-side object model from the graph (sub-objects are owned and cleaned up by the object)
  plAiBehaviorAssetObject obj;
  {
    plRttiConverterContext context;
    plRttiConverterReader reader(pObjects.Borrow(), &context);
    reader.ApplyPropertiesToObject(pPropertiesNode, plAiBehaviorAssetObject::GetStaticRTTI(), &obj);
  }

  peek.m_bValid = true;
  peek.m_sName = obj.m_sName.IsEmpty() ? sAssetName : QString::fromUtf8(obj.m_sName.GetData());
  peek.m_iCategory = obj.m_Category.GetValue();
  peek.m_fWeight = obj.m_fWeight;
  peek.m_fCommitBonus = obj.m_fCommitBonus;
  peek.m_fCooldownSeconds = obj.m_CooldownDuration.GetSeconds();

  if (obj.m_pLogic != nullptr)
  {
    plStringBuilder sLogic = obj.m_pLogic->GetDynamicRTTI()->GetTypeName();
    sLogic.TrimWordStart("plAiBehaviorLogic_");
    peek.m_sLogic = QString::fromUtf8(sLogic.GetData());
  }

  for (const plAiConsiderationObject* pCons : obj.m_Considerations)
  {
    if (pCons == nullptr)
      continue;

    auto& out = peek.m_Considerations.ExpandAndGetRef();
    out.m_fInputMin = pCons->m_fInputMin;
    out.m_fInputMax = pCons->m_fInputMax;

    if (pCons->m_pInput != nullptr)
    {
      out.m_bNeedsTarget = pCons->m_pInput->NeedsTarget();
      const QString sSummary = plAiAssetUi::InputParamSummary(pCons->m_pInput);
      out.m_sLabel = plAiAssetUi::PrettyInputName(pCons->m_pInput->GetDynamicRTTI());
      if (!sSummary.isEmpty())
        out.m_sLabel += QString(" - %1").arg(sSummary);
    }
    else
    {
      out.m_sLabel = QStringLiteral("<no input>");
    }

    plAiAssetUi::BuildRuntimeCurve(pCons->m_ResponseCurve, out.m_Curve);
  }

  return peek;
}
