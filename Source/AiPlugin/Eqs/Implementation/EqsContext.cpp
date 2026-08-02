#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/EqsContext.h>
#include <Core/Utils/Blackboard.h>
#include <Core/World/World.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsContext, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsContext_Querier, 1, plRTTIDefaultAllocator<plAiEqsContext_Querier>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsContext_PerceivedTarget, 1, plRTTIDefaultAllocator<plAiEqsContext_PerceivedTarget>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsContext_BlackboardVec3, 1, plRTTIDefaultAllocator<plAiEqsContext_BlackboardVec3>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Entry", GetEntryName, SetEntryName),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsContext_BlackboardObject, 1, plRTTIDefaultAllocator<plAiEqsContext_BlackboardObject>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Entry", GetEntryName, SetEntryName),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsContext_ExplicitPosition, 1, plRTTIDefaultAllocator<plAiEqsContext_ExplicitPosition>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

void plAiEqsContext_Querier::Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const
{
  out_slot.m_Positions.PushBack(ctx.m_vQuerierPosition);

  if (ctx.m_pQuerier != nullptr)
  {
    out_slot.m_Objects.PushBack(ctx.m_pQuerier->GetHandle());
  }
}

void plAiEqsContext_PerceivedTarget::Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const
{
  if (ctx.m_pBlackboard == nullptr)
    return;

  static const plHashedString sTargetPosition = plMakeHashedString("Ai_TargetPosition");
  static const plHashedString sTargetConfidence = plMakeHashedString("Ai_TargetConfidence");
  static const plHashedString sTargetObject = plMakeHashedString("Ai_TargetObject");

  const plVariant pos = ctx.m_pBlackboard->GetEntryValue(sTargetPosition);
  const plVariant conf = ctx.m_pBlackboard->GetEntryValue(sTargetConfidence);

  if (!pos.IsA<plVec3>() || !conf.IsValid() || !conf.CanConvertTo<float>() || conf.ConvertTo<float>() <= 0.01f)
    return;

  out_slot.m_Positions.PushBack(pos.Get<plVec3>());

  const plVariant target = ctx.m_pBlackboard->GetEntryValue(sTargetObject);

  if (target.IsA<plGameObjectHandle>())
  {
    out_slot.m_Objects.PushBack(target.Get<plGameObjectHandle>());
  }
}

void plAiEqsContext_BlackboardVec3::Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const
{
  if (ctx.m_pBlackboard == nullptr || m_sEntryName.IsEmpty())
    return;

  const plVariant pos = ctx.m_pBlackboard->GetEntryValue(m_sEntryName);

  if (pos.IsA<plVec3>())
  {
    out_slot.m_Positions.PushBack(pos.Get<plVec3>());
  }
}

void plAiEqsContext_BlackboardObject::Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const
{
  if (ctx.m_pWorld == nullptr || ctx.m_pBlackboard == nullptr || m_sEntryName.IsEmpty())
    return;

  const plVariant value = ctx.m_pBlackboard->GetEntryValue(m_sEntryName);

  if (!value.IsA<plGameObjectHandle>())
    return;

  plGameObject* pObject = nullptr;

  if (ctx.m_pWorld->TryGetObject(value.Get<plGameObjectHandle>(), pObject))
  {
    out_slot.m_Positions.PushBack(pObject->GetGlobalPosition());
    out_slot.m_Objects.PushBack(pObject->GetHandle());
  }
}

void plAiEqsContext_ExplicitPosition::Resolve(const plAiEqsResolveContext& ctx, plAiEqsResolvedSlot& out_slot) const
{
  if (ctx.m_pParams == nullptr)
    return;

  for (const auto& named : ctx.m_pParams->m_Positions)
  {
    if (named.m_sName == ctx.m_sSlotName)
    {
      out_slot.m_Positions.PushBack(named.m_vPosition);
    }
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Eqs_Implementation_EqsContext);
