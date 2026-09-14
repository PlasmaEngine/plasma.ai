#pragma once

#include <AiPlugin/Eqs/EqsQueryResource.h>
#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <GuiFoundation/Widgets/CurveEditData.h>

/// \brief One named context slot in the EQS query asset ("Threat" = Perceived Target, ...).
class plAiEqsContextSlotObject : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsContextSlotObject, plReflectedClass);

public:
  plAiEqsContextSlotObject();
  ~plAiEqsContextSlotObject();

  plString m_sName;
  plAiEqsContext* m_pContext = nullptr; // owned
};

/// \brief One test in the EQS query asset: the reflected test object plus its response curve.
class plAiEqsTestObject : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsTestObject, plReflectedClass);

public:
  plAiEqsTestObject();
  ~plAiEqsTestObject();

  plAiEqsTest* m_pTest = nullptr; // owned
  plSingleCurveData m_ScoreCurve;
  bool m_bLegacyCurveDomain = false; ///< Compatibility: map input onto the first-to-last point interval.
};

/// \brief Editor-side object model of the 'AI EQS Query' asset.
class plAiEqsQueryAssetObject : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsQueryAssetObject, plReflectedClass);

public:
  plAiEqsQueryAssetObject();
  ~plAiEqsQueryAssetObject();

  plString m_sName; ///< display name used in debug overlays; falls back to the asset name
  plEnum<plAiEqsRunMode> m_RunMode;
  float m_fTopPercent = 25.0f;
  plUInt8 m_uiCandidates = 32;
  plUInt8 m_uiMaxResults = 8;
  plString m_sNavmeshConfig;
  plString m_sPathSearchConfig;

  plDynamicArray<plAiEqsContextSlotObject*> m_ContextSlots; // owned
  plAiEqsGenerator* m_pGenerator = nullptr;                 // owned
  plDynamicArray<plAiEqsTestObject*> m_Tests;               // owned

  /// Empty means valid. Test messages are also displayed beside their cards.
  plString ValidateRoot() const;
  plString ValidateTest(plUInt32 uiIndex) const;
  bool HasContext(const char* szName) const;
};

class plAiEqsQueryAssetDocument : public plSimpleAssetDocument<plAiEqsQueryAssetObject>
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsQueryAssetDocument, plSimpleAssetDocument<plAiEqsQueryAssetObject>);

public:
  plAiEqsQueryAssetDocument(plStringView sDocumentPath);

  plStatus WriteAsset(plStreamWriter& inout_stream, const plPlatformProfile* pAssetProfile) const;

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& inout_stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;
};
