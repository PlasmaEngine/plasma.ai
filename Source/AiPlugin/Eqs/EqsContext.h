#pragma once

#include <AiPlugin/Eqs/EqsTypes.h>

class plWorld;
class plGameObject;
class plBlackboard;

/// \brief Everything available while resolving contexts, on the main thread at submit time.
struct PL_AIPLUGIN_DLL plAiEqsResolveContext
{
  plWorld* m_pWorld = nullptr;
  plGameObject* m_pQuerier = nullptr; ///< may be null (position-only submits)
  plVec3 m_vQuerierPosition = plVec3::MakeZero();
  const plBlackboard* m_pBlackboard = nullptr; ///< the querier's blackboard, if any
  const plAiEqsQueryParams* m_pParams = nullptr;
  plHashedString m_sSlotName; ///< the name of the slot currently being resolved
};

/// \brief Base class of all EQS contexts: resolves "relative to WHAT?" into one or more world
/// positions / objects.
///
/// Contexts are resolved on the main thread at submit time into a snapshot, so worker-thread query
/// execution never touches live world state. Like plAiInput, contexts are shared immutable objects
/// owned by the query resource and must be stateless.
class PL_AIPLUGIN_DLL plAiEqsContext : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsContext, plReflectedClass);

public:
  plAiEqsContext() = default;
  virtual ~plAiEqsContext() = default;

  /// \brief Fills out_slot with the resolved positions/objects. Empty positions = context unavailable.
  virtual void Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const = 0;
};

/// \brief The asking agent itself. (The "Querier" slot is always available implicitly; this class
/// exists so additional slots can alias the querier.)
class PL_AIPLUGIN_DLL plAiEqsContext_Querier : public plAiEqsContext
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsContext_Querier, plAiEqsContext);

public:
  virtual void Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const override;
};

/// \brief The last-known position of the querier's perceived target, as published by the AI brain
/// ('Ai_TargetPosition' / 'Ai_TargetConfidence'). Unavailable while nothing is perceived.
class PL_AIPLUGIN_DLL plAiEqsContext_PerceivedTarget : public plAiEqsContext
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsContext_PerceivedTarget, plAiEqsContext);

public:
  virtual void Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const override;
};

/// \brief A plVec3 blackboard entry of the querier.
class PL_AIPLUGIN_DLL plAiEqsContext_BlackboardVec3 : public plAiEqsContext
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsContext_BlackboardVec3, plAiEqsContext);

public:
  virtual void Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const override;

  void SetEntryName(const char* szName) { m_sEntryName.Assign(szName); } // [ property ]
  const char* GetEntryName() const { return m_sEntryName.GetData(); }    // [ property ]

  plHashedString m_sEntryName;
};

/// \brief A game object handle stored in a blackboard entry of the querier (door, objective, ...).
class PL_AIPLUGIN_DLL plAiEqsContext_BlackboardObject : public plAiEqsContext
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsContext_BlackboardObject, plAiEqsContext);

public:
  virtual void Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const override;

  void SetEntryName(const char* szName) { m_sEntryName.Assign(szName); } // [ property ]
  const char* GetEntryName() const { return m_sEntryName.GetData(); }    // [ property ]

  plHashedString m_sEntryName;
};

/// \brief A position passed in plAiEqsQueryParams::m_Positions under this slot's name - the
/// C++/script escape hatch.
class PL_AIPLUGIN_DLL plAiEqsContext_ExplicitPosition : public plAiEqsContext
{
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsContext_ExplicitPosition, plAiEqsContext);

public:
  virtual void Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const override;
};
