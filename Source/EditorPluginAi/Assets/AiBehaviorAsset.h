#pragma once

#include <AiPlugin/UtilityAI/Decision/AiBehaviorResource.h>
#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <GuiFoundation/Widgets/CurveEditData.h>

/// \brief One consideration in the AI behavior asset: input provider + normalization range + response curve.
class plAiConsiderationObject : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiConsiderationObject, plReflectedClass);

public:
  plAiConsiderationObject();
  ~plAiConsiderationObject();

  plAiInput* m_pInput = nullptr; // owned
  float m_fInputMin = 0.0f;
  float m_fInputMax = 1.0f;
  plSingleCurveData m_ResponseCurve;
};

/// \brief Editor-side object model of the 'AI Behavior' asset.
class plAiBehaviorAssetObject : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiBehaviorAssetObject, plReflectedClass);

public:
  plAiBehaviorAssetObject();
  ~plAiBehaviorAssetObject();

  plString m_sName; ///< display name used in debug overlays; falls back to the asset name
  plEnum<plAiBehaviorCategory> m_Category;
  float m_fWeight = 1.0f;
  float m_fCommitBonus = 0.2f;
  plTime m_CooldownDuration;

  plDynamicArray<plAiConsiderationObject*> m_Considerations; // owned
  plAiBehaviorLogic* m_pLogic = nullptr;                     // owned
};

class plAiBehaviorAssetDocument : public plSimpleAssetDocument<plAiBehaviorAssetObject>
{
  PL_ADD_DYNAMIC_REFLECTION(plAiBehaviorAssetDocument, plSimpleAssetDocument<plAiBehaviorAssetObject>);

public:
  plAiBehaviorAssetDocument(plStringView sDocumentPath);

  plStatus WriteAsset(plStreamWriter& inout_stream, const plPlatformProfile* pAssetProfile) const;

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& inout_stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;
};
