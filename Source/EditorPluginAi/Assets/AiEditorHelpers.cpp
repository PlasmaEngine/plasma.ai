#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiBehaviorAsset.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <AiPlugin/UtilityAI/Decision/AiInput.h>
#include <AiPlugin/Utils/AiResponseCurve.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <Foundation/Serialization/DdlSerializer.h>
#include <Foundation/Serialization/RttiConverter.h>
#include <GuiFoundation/GuiFoundationDLL.h>
#include <GuiFoundation/Widgets/CurveEditData.h>

namespace plAiAssetUi
{
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

  void GetPrettyInputName(const plRTTI* pRtti, plStringBuilder& out_sName)
  {
    out_sName.Clear();

    if (pRtti == nullptr)
    {
      out_sName = "<no input>";
      return;
    }

    plStringBuilder sName = pRtti->GetTypeName();
    sName.TrimWordStart("plAiInput_");
    sName.TrimWordStart("plAiInput");

    // a space before each capital that follows a lower-case letter
    bool bPreviousUpper = true;
    for (auto it = sName.GetIteratorFront(); it.IsValid(); ++it)
    {
      const plUInt32 uiChar = it.GetCharacter();
      const bool bUpper = uiChar >= 'A' && uiChar <= 'Z';

      if (bUpper && !bPreviousUpper)
        out_sName.Append(' ');

      out_sName.Append(uiChar);
      bPreviousUpper = bUpper;
    }
  }

  void GetInputParamSummary(const plAiInput* pInput, plStringBuilder& out_sSummary)
  {
    out_sSummary.Clear();

    if (pInput == nullptr)
    {
      out_sSummary = "select an input type";
      return;
    }

    if (auto pBb = plDynamicCast<const plAiInput_BlackboardValue*>(pInput))
    {
      out_sSummary.SetFormat("'{}' (fallback {})", pBb->GetEntryName(), pBb->m_fFallback);
      return;
    }

    if (auto pDist = plDynamicCast<const plAiInput_DistanceToPosition*>(pInput))
    {
      out_sSummary.SetFormat("distance to '{}'", pDist->GetEntryName());
      return;
    }

    if (auto pConst = plDynamicCast<const plAiInput_Constant*>(pInput))
    {
      out_sSummary.SetFormat("value {}", pConst->m_fValue);
      return;
    }

    if (auto pCover = plDynamicCast<const plAiInput_CoverAvailability*>(pInput))
    {
      out_sSummary = pCover->m_Mode == plAiCoverAvailabilityMode::ClaimedQuality ? "claimed cover quality (Ai_CoverStatus)" : "unclaimed cover nearby (Ai_CoverNearby)";
      return;
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
      out_sSummary.SetFormat("1 when squad intent matches {}", szIntent);
      return;
    }

    if (plDynamicCast<const plAiInput_TargetConfidence*>(pInput))
      out_sSummary = "perception record confidence 0-1";
    else if (plDynamicCast<const plAiInput_DistanceToTarget*>(pInput))
      out_sSummary = "meters to the scored target";
    else if (plDynamicCast<const plAiInput_AngleToTarget*>(pInput))
      out_sSummary = "degrees between forward and target (0-180)";
    else if (plDynamicCast<const plAiInput_TargetExposure*>(pInput))
      out_sSummary = "target sees me (Ai_TargetExposure)";
    else if (plDynamicCast<const plAiInput_SquadHasAttackToken*>(pInput))
      out_sSummary = "Ai_HasAttackToken - squadless agents = 1";
    else if (plDynamicCast<const plAiInput_TimeSinceActivation*>(pInput))
      out_sSummary = "seconds this behavior has been active";
    else if (plDynamicCast<const plAiInput_TimeSinceDeactivation*>(pInput))
      out_sSummary = "seconds since last deactivation";
  }

  const char* GetInputGroup(const plRTTI* pRtti)
  {
    const plStringView sName = pRtti->GetTypeName();

    if (sName.FindSubString("DistanceToTarget") || sName.FindSubString("TargetConfidence") || sName.FindSubString("AngleToTarget"))
      return "Target";
    if (sName.FindSubString("Blackboard") || sName.FindSubString("DistanceToPosition"))
      return "Blackboard";
    if (sName.FindSubString("TimeSince"))
      return "Timing";
    if (sName.FindSubString("Cover") || sName.FindSubString("Exposure"))
      return "Tactical";
    if (sName.FindSubString("Squad"))
      return "Squad";
    return "Misc";
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

  float EvaluateConsideration(const plCurve1D& curve, float fRaw, float fMin, float fMax, float* out_pNormalized, bool bLegacyDomain)
  {
    const float fRange = fMax - fMin;
    float fNormalized = (plMath::Abs(fRange) < plMath::SmallEpsilon<float>()) ? 0.0f : (fRaw - fMin) / fRange;
    fNormalized = plMath::Clamp(fNormalized, 0.0f, 1.0f);

    if (out_pNormalized != nullptr)
      *out_pNormalized = fNormalized;

    if (curve.IsEmpty())
      return fNormalized;

    return plAiEvaluateResponseCurve(curve, fNormalized, bLegacyDomain);
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
  plString sAssetName;
  {
    auto asset = plAssetCurator::GetSingleton()->FindSubAsset(sAssetRef);
    if (!asset.isValid() || asset->m_pAssetInfo == nullptr)
    {
      peek = plAiBehaviorPeek();
      return peek;
    }

    sAbsPath = asset->m_pAssetInfo->m_Path.GetAbsolutePath();
    sAssetName = asset->GetName();
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
  peek.m_sAbsPath = sAbsPath;

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
  peek.m_sName = obj.m_sName.IsEmpty() ? sAssetName : obj.m_sName;
  peek.m_iCategory = obj.m_Category.GetValue();
  peek.m_fWeight = obj.m_fWeight;
  peek.m_fCommitBonus = obj.m_fCommitBonus;
  peek.m_fCooldownSeconds = obj.m_CooldownDuration.GetSeconds();

  if (obj.m_pLogic != nullptr)
  {
    plStringBuilder sLogic = obj.m_pLogic->GetDynamicRTTI()->GetTypeName();
    sLogic.TrimWordStart("plAiBehaviorLogic_");
    peek.m_sLogic = sLogic;
  }

  for (const plAiConsiderationObject* pCons : obj.m_Considerations)
  {
    if (pCons == nullptr)
      continue;

    auto& out = peek.m_Considerations.ExpandAndGetRef();
    out.m_fInputMin = pCons->m_fInputMin;
    out.m_fInputMax = pCons->m_fInputMax;
    out.m_bLegacyCurveDomain = pCons->m_bLegacyCurveDomain;

    if (pCons->m_pInput != nullptr)
    {
      out.m_bHasInput = true;
      out.m_bNeedsTarget = pCons->m_pInput->NeedsTarget();

      plStringBuilder sLabel, sSummary;
      plAiAssetUi::GetPrettyInputName(pCons->m_pInput->GetDynamicRTTI(), sLabel);
      plAiAssetUi::GetInputParamSummary(pCons->m_pInput, sSummary);

      if (!sSummary.IsEmpty())
        sLabel.AppendFormat(" - {}", sSummary);

      out.m_sLabel = sLabel;
    }
    else
    {
      out.m_sLabel = "<no input>";
    }

    plAiAssetUi::BuildRuntimeCurve(pCons->m_ResponseCurve, out.m_Curve);
  }

  return peek;
}
