#pragma once

#include <AiPlugin/Eqs/EqsTypes.h>

/// \brief Base class of all EQS generators: produces the candidate item set of a query.
///
/// One generator per query. Generate() runs on a worker thread inside the EQS module's budgeted
/// parallel batch and may only touch the passed eval context (navmesh scratch query, snapshots,
/// tactical registries). Generators that need live world data (smart objects) get it pre-collected
/// at submit time via NeedsMainThreadCollect().
class PL_AIPLUGIN_DLL plAiEqsGenerator : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsGenerator, plReflectedClass);

public:
  plAiEqsGenerator() = default;
  virtual ~plAiEqsGenerator() = default;

  /// \brief Fills out_items with up to ctx.m_uiMaxCandidates candidates. Worker thread; stateless.
  virtual void Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const = 0;

  /// \brief The payload type of generated items.
  virtual plAiEqsPayloadType::Enum GetPayloadType() const { return plAiEqsPayloadType::None; }

  /// \brief When true, the module calls CollectOnMainThread() at submit time and passes the
  /// result via plAiEqsEvalContext::m_PreCollectedItems.
  virtual bool NeedsMainThreadCollect() const { return false; }

  /// \brief Main-thread pre-collection at submit time (see NeedsMainThreadCollect()).
  virtual void CollectOnMainThread(const plVec3& vCenter, plGameObjectHandle hClaimant, const plAiTacticalWorldModule& tactical, plDynamicArray<plAiEqsItem>& out_items) const {}

  /// \brief Which context the generator centers on ("Querier", "Threat", ...).
  void SetCenterContext(const char* szName) { m_sCenterContext.Assign(szName); } // [ property ]
  const char* GetCenterContext() const { return m_sCenterContext.GetData(); }    // [ property ]

  plHashedString m_sCenterContext; ///< defaults to "Querier"
  float m_fRadiusMin = 2.0f;
  float m_fRadiusMax = 12.0f;

  /// How far above/below the center samples may land: the vertical half-extent for projecting
  /// samples onto the navmesh (Ring/Grid/NavmeshRandom - small values keep samples off other
  /// floors/bridges), and the |z| filter for registry candidates (CoverPoints).
  float m_fVerticalRange = 3.0f;

protected:
  /// \brief The first position of the center context slot; querier position when unresolvable.
  plVec3 GetCenter(const plAiEqsEvalContext& ctx, bool* out_pValid = nullptr) const;
};

/// \brief Navmesh-projected points on golden-angle rings between RadiusMin and RadiusMax around
/// the center context. Deterministic pattern; the classic flank/retreat generator.
class PL_AIPLUGIN_DLL plAiEqsGenerator_Ring : public plAiEqsGenerator
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsGenerator_Ring, plAiEqsGenerator);

public:
  virtual void Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const override;
};

/// \brief Navmesh-projected grid around the center context - the workhorse for positioning.
/// RadiusMax is the grid half-extent; RadiusMin cuts a hole in the middle when > 0.
class PL_AIPLUGIN_DLL plAiEqsGenerator_Grid : public plAiEqsGenerator
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsGenerator_Grid, plAiEqsGenerator);

public:
  virtual void Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const override;

  float m_fSpacing = 1.5f; ///< grid spacing in meters (spacing grows automatically when the extent needs more cells than the candidate budget)
};

/// \brief Random reachable navmesh points around the center context (Detour findRandomPointAroundCircle-style sampling).
class PL_AIPLUGIN_DLL plAiEqsGenerator_NavmeshRandom : public plAiEqsGenerator
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsGenerator_NavmeshRandom, plAiEqsGenerator);

public:
  virtual void Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const override;
};

/// \brief Baked cover points within RadiusMax of the center context, payload = cover handle.
class PL_AIPLUGIN_DLL plAiEqsGenerator_CoverPoints : public plAiEqsGenerator
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsGenerator_CoverPoints, plAiEqsGenerator);

public:
  virtual void Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const override;
  virtual plAiEqsPayloadType::Enum GetPayloadType() const override { return plAiEqsPayloadType::CoverPoint; }
};

/// \brief Free slots of matching smart objects within RadiusMax, payload = slot handle.
///
/// Slots are collected on the main thread at submit time (their positions require live object
/// transforms), so the item set reflects the world state of the submit frame.
class PL_AIPLUGIN_DLL plAiEqsGenerator_SmartObjects : public plAiEqsGenerator
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsGenerator_SmartObjects, plAiEqsGenerator);

public:
  virtual void Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const override;
  virtual plAiEqsPayloadType::Enum GetPayloadType() const override { return plAiEqsPayloadType::SmartObjectSlot; }
  virtual bool NeedsMainThreadCollect() const override { return true; }
  virtual void CollectOnMainThread(const plVec3& vCenter, plGameObjectHandle hClaimant, const plAiTacticalWorldModule& tactical, plDynamicArray<plAiEqsItem>& out_items) const override;

  void SetType(const char* szType) { m_sType.Assign(szType); } // [ property ]
  const char* GetType() const { return m_sType.GetData(); }    // [ property ]

  plHashedString m_sType; ///< empty = any smart object type
};
