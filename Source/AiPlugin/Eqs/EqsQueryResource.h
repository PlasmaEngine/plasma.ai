#pragma once

#include <AiPlugin/Eqs/EqsContext.h>
#include <AiPlugin/Eqs/EqsGenerator.h>
#include <AiPlugin/Eqs/EqsTest.h>
#include <Core/ResourceManager/Resource.h>
#include <Foundation/Tracks/Curve1D.h>

using plAiEqsQueryResourceHandle = plTypedResourceHandle<class plAiEqsQueryResource>;

/// \brief One named context slot of a query ("Threat" = PerceivedTarget, ...).
struct PL_AIPLUGIN_DLL plAiEqsContextSlotDesc
{
  plAiEqsContextSlotDesc();
  ~plAiEqsContextSlotDesc();
  plAiEqsContextSlotDesc(plAiEqsContextSlotDesc&&) = default;
  plAiEqsContextSlotDesc& operator=(plAiEqsContextSlotDesc&&) = default;

  plHashedString m_sName;
  plUniquePtr<plAiEqsContext> m_pContext;
};

/// \brief One test step of a query: the test object plus its response curve.
///
/// The curve remaps the test's raw [0,1] value before weighting; an empty curve means identity.
/// Hard filtering always applies to the RAW value (a curve cannot rescue a zero).
struct PL_AIPLUGIN_DLL plAiEqsTestDesc
{
  plAiEqsTestDesc();
  ~plAiEqsTestDesc();
  plAiEqsTestDesc(plAiEqsTestDesc&&) = default;
  plAiEqsTestDesc& operator=(plAiEqsTestDesc&&) = default;

  plUniquePtr<plAiEqsTest> m_pTest;
  plCurve1D m_ScoreCurve;

  /// \brief Sorts control points and builds the linear approximation. Call once after filling the curve.
  void PrepareCurve();

  /// \brief Remaps a raw [0,1] value through the curve (identity when empty), clamped to [0,1].
  float ApplyCurve(float fRaw) const;
};

/// \brief Everything that defines one EQS query: generator, ordered tests, context slots, run mode.
class PL_AIPLUGIN_DLL plAiEqsQueryDesc
{
public:
  plAiEqsQueryDesc();
  ~plAiEqsQueryDesc();
  plAiEqsQueryDesc(plAiEqsQueryDesc&&) = default;
  plAiEqsQueryDesc& operator=(plAiEqsQueryDesc&&) = default;

  plHashedString m_sName;
  plEnum<plAiEqsRunMode> m_RunMode;
  float m_fTopPercent = 25.0f; ///< for RandomOfTopPercent
  plUInt8 m_uiCandidates = 32; ///< generated candidates, clamped to 64
  plUInt8 m_uiMaxResults = 8;  ///< top-N results kept, clamped to 16

  plHashedString m_sNavmeshConfig;    ///< empty = first configured navmesh
  plHashedString m_sPathSearchConfig; ///< empty = default filter

  plDynamicArray<plAiEqsContextSlotDesc> m_ContextSlots;
  plUniquePtr<plAiEqsGenerator> m_pGenerator;
  plDynamicArray<plAiEqsTestDesc> m_Tests;

  plResult Serialize(plStreamWriter& inout_stream) const;
  plResult Deserialize(plStreamReader& inout_stream);
};

/// \brief A data-driven EQS query, authored through the 'AI EQS Query' asset.
class PL_AIPLUGIN_DLL plAiEqsQueryResource : public plResource
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsQueryResource, plResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plAiEqsQueryResource);
  PL_RESOURCE_DECLARE_CREATEABLE(plAiEqsQueryResource, plAiEqsQueryDesc);

public:
  plAiEqsQueryResource();
  ~plAiEqsQueryResource();

  const plAiEqsQueryDesc& GetDescriptor() const { return m_Descriptor; }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* Stream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage) override;

  plAiEqsQueryDesc m_Descriptor;
};
