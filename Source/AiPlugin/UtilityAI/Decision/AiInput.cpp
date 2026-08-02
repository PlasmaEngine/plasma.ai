#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/UtilityAI/Decision/AiInput.h>
#include <Core/Utils/Blackboard.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_DistanceToTarget, 1, plRTTIDefaultAllocator<plAiInput_DistanceToTarget>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_TargetConfidence, 1, plRTTIDefaultAllocator<plAiInput_TargetConfidence>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_BlackboardValue, 1, plRTTIDefaultAllocator<plAiInput_BlackboardValue>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Entry", GetEntryName, SetEntryName),
    PL_MEMBER_PROPERTY("Fallback", m_fFallback),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_TimeSinceActivation, 1, plRTTIDefaultAllocator<plAiInput_TimeSinceActivation>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_TimeSinceDeactivation, 1, plRTTIDefaultAllocator<plAiInput_TimeSinceDeactivation>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_Constant, 1, plRTTIDefaultAllocator<plAiInput_Constant>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Value", m_fValue)->AddAttributes(new plDefaultValueAttribute(1.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_DistanceToPosition, 1, plRTTIDefaultAllocator<plAiInput_DistanceToPosition>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Entry", GetEntryName, SetEntryName),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_AngleToTarget, 1, plRTTIDefaultAllocator<plAiInput_AngleToTarget>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiCoverAvailabilityMode, 1)
  PL_ENUM_CONSTANTS(plAiCoverAvailabilityMode::ClaimedQuality, plAiCoverAvailabilityMode::NearbyAvailable)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_CoverAvailability, 1, plRTTIDefaultAllocator<plAiInput_CoverAvailability>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Mode", plAiCoverAvailabilityMode, m_Mode),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_TargetExposure, 1, plRTTIDefaultAllocator<plAiInput_TargetExposure>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_SquadHasAttackToken, 1, plRTTIDefaultAllocator<plAiInput_SquadHasAttackToken>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiInput_SquadIntent, 1, plRTTIDefaultAllocator<plAiInput_SquadIntent>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("DesiredIntent", plAiSquadIntent, m_DesiredIntent),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

float plAiInput_DistanceToTarget::Evaluate(const plAiScoringContext& context) const
{
  if (context.m_pTarget == nullptr)
    return plMath::HighValue<float>();

  return (context.m_pTarget->m_vLastKnownPosition - context.m_vOwnerPosition).GetLength();
}

float plAiInput_TargetConfidence::Evaluate(const plAiScoringContext& context) const
{
  if (context.m_pTarget == nullptr)
    return 0.0f;

  return context.m_pTarget->m_fConfidence;
}

float plAiInput_BlackboardValue::Evaluate(const plAiScoringContext& context) const
{
  const plVariant value = context.GetBlackboardValue(m_sEntryName);

  if (value.IsValid() && value.CanConvertTo<float>())
    return value.ConvertTo<float>();

  return m_fFallback;
}

void plAiInput_BlackboardValue::CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const
{
  if (!m_sEntryName.IsEmpty())
  {
    out_entries.PushBack(m_sEntryName);
  }
}

float plAiInput_CoverAvailability::Evaluate(const plAiScoringContext& context) const
{
  static const plHashedString sCoverStatus = plMakeHashedString("Ai_CoverStatus");
  static const plHashedString sCoverNearby = plMakeHashedString("Ai_CoverNearby");

  const plVariant value = context.GetBlackboardValue(m_Mode == plAiCoverAvailabilityMode::ClaimedQuality ? sCoverStatus : sCoverNearby);

  if (value.IsValid() && value.CanConvertTo<float>())
    return value.ConvertTo<float>();

  return 0.0f;
}

void plAiInput_CoverAvailability::CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const
{
  out_entries.PushBack(plMakeHashedString("Ai_CoverStatus"));
  out_entries.PushBack(plMakeHashedString("Ai_CoverNearby"));
}

float plAiInput_TargetExposure::Evaluate(const plAiScoringContext& context) const
{
  static const plHashedString sTargetExposure = plMakeHashedString("Ai_TargetExposure");

  const plVariant value = context.GetBlackboardValue(sTargetExposure);

  if (value.IsValid() && value.CanConvertTo<float>())
    return value.ConvertTo<float>();

  return 0.0f;
}

void plAiInput_TargetExposure::CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const
{
  out_entries.PushBack(plMakeHashedString("Ai_TargetExposure"));
}

float plAiInput_SquadHasAttackToken::Evaluate(const plAiScoringContext& context) const
{
  static const plHashedString sHasAttackToken = plMakeHashedString("Ai_HasAttackToken");

  const plVariant value = context.GetBlackboardValue(sHasAttackToken);

  if (value.IsValid() && value.CanConvertTo<float>())
    return value.ConvertTo<float>();

  return 1.0f; // agents without a squad fight unrestricted
}

void plAiInput_SquadHasAttackToken::CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const
{
  out_entries.PushBack(plMakeHashedString("Ai_HasAttackToken"));
}

float plAiInput_SquadIntent::Evaluate(const plAiScoringContext& context) const
{
  static const plHashedString sSquadIntent = plMakeHashedString("Ai_SquadIntent");

  const plVariant value = context.GetBlackboardValue(sSquadIntent);
  plInt32 iIntent = plAiSquadIntent::None;

  if (value.IsValid() && value.CanConvertTo<plInt32>())
  {
    iIntent = value.ConvertTo<plInt32>();
  }

  return (iIntent == static_cast<plInt32>(m_DesiredIntent.GetValue())) ? 1.0f : 0.0f;
}

void plAiInput_SquadIntent::CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const
{
  out_entries.PushBack(plMakeHashedString("Ai_SquadIntent"));
}

float plAiInput_TimeSinceActivation::Evaluate(const plAiScoringContext& context) const
{
  return static_cast<float>(context.m_TimeSinceActivation.GetSeconds());
}

float plAiInput_TimeSinceDeactivation::Evaluate(const plAiScoringContext& context) const
{
  return static_cast<float>(context.m_TimeSinceDeactivation.GetSeconds());
}

float plAiInput_Constant::Evaluate(const plAiScoringContext& context) const
{
  return m_fValue;
}

float plAiInput_DistanceToPosition::Evaluate(const plAiScoringContext& context) const
{
  const plVariant value = context.GetBlackboardValue(m_sEntryName);

  if (value.IsA<plVec3>())
  {
    return (value.Get<plVec3>() - context.m_vOwnerPosition).GetLength();
  }

  return plMath::HighValue<float>();
}

void plAiInput_DistanceToPosition::CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const
{
  if (!m_sEntryName.IsEmpty())
  {
    out_entries.PushBack(m_sEntryName);
  }
}

float plAiInput_AngleToTarget::Evaluate(const plAiScoringContext& context) const
{
  if (context.m_pTarget == nullptr)
    return 180.0f;

  plVec3 vToTarget = context.m_pTarget->m_vLastKnownPosition - context.m_vOwnerPosition;
  vToTarget.z = 0.0f;

  if (vToTarget.NormalizeIfNotZero(plVec3::MakeAxisX()).Failed())
    return 0.0f;

  plVec3 vForward = context.m_qOwnerRotation * plVec3::MakeAxisX();
  vForward.z = 0.0f;
  vForward.NormalizeIfNotZero(plVec3::MakeAxisX()).IgnoreResult();

  const float fDot = plMath::Clamp(vForward.Dot(vToTarget), -1.0f, 1.0f);
  return plMath::ACos(fDot).GetDegree();
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Decision_AiInput);
