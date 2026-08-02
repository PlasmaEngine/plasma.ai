#pragma once

#include <AiPlugin/Eqs/EqsTypes.h>

/// \brief Base class of all EQS tests: filters and/or scores every candidate item.
///
/// A test computes a raw [0,1] value per item (written to plAiEqsItem::m_fRaw). The module then
/// applies the test's purpose (a hard filter discards items at raw 0), response curve and weight -
/// the item's final score is the weight-normalized sum over all scoring tests, exactly like the
/// tactical candidate set today.
///
/// Tests run batched (one virtual call per test over all surviving items, not per item) on a
/// worker thread and must be stateless. Cheap tests (pure math, snapshot lookups) always run
/// before expensive ones (physics raycasts, navmesh queries), so the raycast budget is spent only
/// on candidates that survived the cheap filters. An expensive test that exhausts its budget
/// returns early; the query suspends and resumes at the same item next frame.
class PL_AIPLUGIN_DLL plAiEqsTest : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsTest, plReflectedClass);

public:
  enum class Cost : plUInt8
  {
    Cheap,    ///< pure math / snapshot lookups; never suspends
    Expensive ///< consumes raycast/path budgets; may suspend the query
  };

  plAiEqsTest() = default;
  virtual ~plAiEqsTest() = default;

  /// \brief Computes raw scores for items[uiFirstItem..], writing each item's m_fRaw.
  ///
  /// Returns the number of items processed. Expensive tests return less than requested when their
  /// budget runs out - the query suspends and resumes at the returned index next frame.
  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const = 0;

  virtual Cost GetCost() const { return Cost::Cheap; }

  /// \brief The context slot this test measures against; empty when the test uses none.
  virtual const char* GetContextProperty() const { return nullptr; }

  plEnum<plAiEqsTestPurpose> m_Purpose;
  float m_fWeight = 1.0f;
};

/// \brief 2D distance from each item to a context, scored as a trapezoid band:
/// full score inside [BandMin, BandMax], linear falloff outside.
class PL_AIPLUGIN_DLL plAiEqsTest_Distance : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsTest_Distance, plAiEqsTest);

public:
  plAiEqsTest_Distance();

  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override;
  virtual const char* GetContextProperty() const override { return GetContext(); }

  void SetContext(const char* szName) { m_sContext.Assign(szName); } // [ property ]
  const char* GetContext() const { return m_sContext.GetData(); }    // [ property ]

  plHashedString m_sContext; ///< default "Querier"
  float m_fBandMin = 0.0f;
  float m_fBandMax = 10.0f;
  plEnum<plAiEqsContextCombine> m_Combine;
};

/// \brief Alignment of the item's direction (from the context) with a desired angle relative to
/// the context->querier direction. 0 deg = towards the querier, 180 deg = the far side.
/// The classic flank scorer (DesiredAngle ~100 deg).
class PL_AIPLUGIN_DLL plAiEqsTest_Direction : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsTest_Direction, plAiEqsTest);

public:
  plAiEqsTest_Direction();

  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override;
  virtual const char* GetContextProperty() const override { return GetContext(); }

  void SetContext(const char* szName) { m_sContext.Assign(szName); } // [ property ]
  const char* GetContext() const { return m_sContext.GetData(); }    // [ property ]

  plHashedString m_sContext; ///< default "Threat"
  plAngle m_DesiredAngle = plAngle::MakeFromDegree(100);
};

/// \brief Cover payload quality: 0 below MinQuality, otherwise scored by tier (Low 0.5, High 1).
/// Items without a cover payload pass with 1.
class PL_AIPLUGIN_DLL plAiEqsTest_CoverQuality : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsTest_CoverQuality, plAiEqsTest);

public:
  plAiEqsTest_CoverQuality();

  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override;

  plEnum<plAiCoverQuality> m_MinQuality;
};

/// \brief The cover payload's wall must face the context (the wall is between us and them).
/// Items without a cover payload pass.
class PL_AIPLUGIN_DLL plAiEqsTest_CoverFacing : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsTest_CoverFacing, plAiEqsTest);

public:
  plAiEqsTest_CoverFacing();

  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override;
  virtual const char* GetContextProperty() const override { return GetContext(); }

  void SetContext(const char* szName) { m_sContext.Assign(szName); } // [ property ]
  const char* GetContext() const { return m_sContext.GetData(); }    // [ property ]

  plHashedString m_sContext; ///< default "Threat"
  float m_fMinDot = 0.2f;    ///< min dot of wallDir vs to-context direction
};

/// \brief Cover/smart-object payload must not be claimed by another agent (the querier's own
/// claim passes). Items without a claimable payload pass.
class PL_AIPLUGIN_DLL plAiEqsTest_Unclaimed : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsTest_Unclaimed, plAiEqsTest);

public:
  plAiEqsTest_Unclaimed();

  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override;
};

/// \brief Physics raycast between each item's eye position and the context's eye position.
/// PreferVisible false = cover semantics (blocked scores 1), true = flank/attack semantics.
/// Multi-position contexts combine per the Combine policy ("hidden from ALL enemies" = Min).
class PL_AIPLUGIN_DLL plAiEqsTest_LineOfSight : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsTest_LineOfSight, plAiEqsTest);

public:
  plAiEqsTest_LineOfSight();

  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override;
  virtual Cost GetCost() const override { return Cost::Expensive; }
  virtual const char* GetContextProperty() const override { return GetContext(); }

  void SetContext(const char* szName) { m_sContext.Assign(szName); } // [ property ]
  const char* GetContext() const { return m_sContext.GetData(); }    // [ property ]

  plHashedString m_sContext;          ///< default "Threat"
  bool m_bPreferVisible = false;      ///< false: blocked line of sight scores 1 (cover); true: visible scores 1
  float m_fItemEyeHeight = 1.55f;     ///< eye height at the item
  float m_fCoverEyeHeight = 0.6f;     ///< eye height used instead for cover items (they will be used crouching)
  float m_fContextEyeHeight = 1.55f;  ///< eye height at the context position
  plEnum<plAiEqsContextCombine> m_Combine;
};

/// \brief Navmesh raycast from the querier toward each item: cheap straight-line reachability.
/// A clear walk scores 1, a detour 0.25 (matches the tactical ReachableApprox scorer).
class PL_AIPLUGIN_DLL plAiEqsTest_ReachableApprox : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsTest_ReachableApprox, plAiEqsTest);

public:
  plAiEqsTest_ReachableApprox();

  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override;
  virtual Cost GetCost() const override { return Cost::Expensive; }
};

/// \brief Real Detour path length from the querier to each item, scored as a trapezoid band on
/// the path distance. Unreachable items score 0 (hard-fail when the purpose filters).
/// The accuracy win over ReachableApprox - and the most expensive test.
class PL_AIPLUGIN_DLL plAiEqsTest_PathLength : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsTest_PathLength, plAiEqsTest);

public:
  plAiEqsTest_PathLength();

  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override;
  virtual Cost GetCost() const override { return Cost::Expensive; }

  float m_fBandMin = 0.0f;
  float m_fBandMax = 20.0f;
};
