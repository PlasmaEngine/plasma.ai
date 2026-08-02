#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/EqsWorldModule.h>
#include <AiPlugin/Tactical/SquadWorldModule.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <AiPlugin/UtilityAI/Components/AiAgentComponent.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiAgentComponent, 2, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Archetype", GetArchetypeFile, SetArchetypeFile)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_AiArchetype", plDependencyFlags::Package)),
    PL_MEMBER_PROPERTY("Team", m_sTeam),
    PL_MEMBER_PROPERTY("SquadId", m_sSquadId),
    PL_MEMBER_PROPERTY("DebugInfo", m_bDebugInfo),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgAiStimulus, OnMsgAiStimulus),
  }
  PL_END_MESSAGEHANDLERS;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Components"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiAgentComponent::plAiAgentComponent() = default;
plAiAgentComponent::~plAiAgentComponent() = default;

void plAiAgentComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_hArchetype;
  s << m_sTeam;
  s << m_bDebugInfo;
  s << m_sSquadId;
}

void plAiAgentComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& s = inout_stream.GetStream();

  s >> m_hArchetype;
  s >> m_sTeam;
  s >> m_bDebugInfo;

  if (uiVersion >= 2)
  {
    s >> m_sSquadId;
  }
}

plHashedString plAiAgentComponent::GetResolvedTeam() const
{
  if (!m_sTeam.IsEmpty())
    return m_sTeam;

  if (m_hArchetype.IsValid())
  {
    plResourceLock<plAiArchetypeResource> pArchetype(m_hArchetype, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
    if (pArchetype.GetAcquireResult() == plResourceAcquireResult::Final)
    {
      return pArchetype->GetDescriptor().m_sTeam;
    }
  }

  return plHashedString();
}

void plAiAgentComponent::OnMsgAiStimulus(plMsgAiStimulus& ref_msg)
{
  plAiPerceptionWorldModule* pPerception = GetWorld()->GetOrCreateModule<plAiPerceptionWorldModule>();

  plAiStimulus stimulus;
  stimulus.m_Type = ref_msg.m_Type;
  stimulus.m_sCustomType = ref_msg.m_sCustomType;
  stimulus.m_vGlobalPosition = GetOwner()->GetGlobalPosition();
  stimulus.m_fRadius = ref_msg.m_fRadius;
  stimulus.m_fStrength = ref_msg.m_fStrength;
  stimulus.m_hSource = GetOwner()->GetHandle();
  stimulus.m_sSourceTeam = GetResolvedTeam();

  pPerception->PostStimulus(stimulus);
}

void plAiAgentComponent::SetArchetypeFile(const char* szFile)
{
  plAiArchetypeResourceHandle hResource;

  if (!plStringUtils::IsNullOrEmpty(szFile))
  {
    hResource = plResourceManager::LoadResource<plAiArchetypeResource>(szFile);
    plResourceManager::PreloadResource(hResource);
  }

  m_hArchetype = hResource;
}

const char* plAiAgentComponent::GetArchetypeFile() const
{
  if (!m_hArchetype.IsValid())
    return "";

  return m_hArchetype.GetResourceID();
}

void plAiAgentComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  plAiBrainWorldModule* pBrain = GetWorld()->GetOrCreateModule<plAiBrainWorldModule>();
  m_uiAgentIndex = pBrain->RegisterAgent(this);

  // tactical situation probes (target exposure, cover status) for the new consideration inputs
  plAiTacticalWorldModule* pTactical = GetWorld()->GetOrCreateModule<plAiTacticalWorldModule>();
  m_uiTacticalAgentID = pTactical->RegisterAgent(GetHandle());

  // EQS queries (plStateMachineState_AiRunEqsQuery) need the module to exist - SM states run
  // inside the brain module's update, where world module creation is not safe
  GetWorld()->GetOrCreateModule<plAiEqsWorldModule>();

  if (!m_sSquadId.IsEmpty())
  {
    plAiSquadWorldModule* pSquads = GetWorld()->GetOrCreateModule<plAiSquadWorldModule>();
    m_uiSquadMemberID = pSquads->RegisterMember(m_sSquadId, GetHandle());
  }
}

void plAiAgentComponent::OnDeactivated()
{
  if (m_uiAgentIndex != plInvalidIndex)
  {
    if (plAiBrainWorldModule* pBrain = GetWorld()->GetModule<plAiBrainWorldModule>())
    {
      pBrain->UnregisterAgent(m_uiAgentIndex);
    }

    m_uiAgentIndex = plInvalidIndex;
  }

  if (m_uiTacticalAgentID != plInvalidIndex)
  {
    if (plAiTacticalWorldModule* pTactical = GetWorld()->GetModule<plAiTacticalWorldModule>())
    {
      pTactical->UnregisterAgent(m_uiTacticalAgentID);
    }

    m_uiTacticalAgentID = plInvalidIndex;
  }

  if (m_uiSquadMemberID != plInvalidIndex)
  {
    if (plAiSquadWorldModule* pSquads = GetWorld()->GetModule<plAiSquadWorldModule>())
    {
      pSquads->UnregisterMember(m_uiSquadMemberID);
    }

    m_uiSquadMemberID = plInvalidIndex;
  }

  SUPER::OnDeactivated();
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_UtilityAI_Components_AiAgentComponent);
