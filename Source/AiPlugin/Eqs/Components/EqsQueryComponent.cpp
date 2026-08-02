#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/Components/EqsQueryComponent.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>

// clang-format off
PL_IMPLEMENT_MESSAGE_TYPE(plMsgAiEqsQueryFinished);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgAiEqsQueryFinished, 1, plRTTIDefaultAllocator<plMsgAiEqsQueryFinished>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Success", m_bSuccess),
    PL_MEMBER_PROPERTY("BestPosition", m_vBestPosition),
    PL_MEMBER_PROPERTY("BestScore", m_fBestScore),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_COMPONENT_TYPE(plAiEqsQueryComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Query", GetQueryFile, SetQueryFile)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_AiEqsQuery", plDependencyFlags::Package)),
    PL_MEMBER_PROPERTY("AutoRunInterval", m_AutoRunInterval),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_MESSAGESENDERS
  {
    PL_MESSAGE_SENDER(m_QueryFinishedSender)
  }
  PL_END_MESSAGESENDERS;

  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(RunQuery),
  }
  PL_END_FUNCTIONS;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI/Tactical"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiEqsQueryComponent::plAiEqsQueryComponent() = default;
plAiEqsQueryComponent::~plAiEqsQueryComponent() = default;

void plAiEqsQueryComponent::SetQueryFile(const char* szFile)
{
  plAiEqsQueryResourceHandle hResource;

  if (!plStringUtils::IsNullOrEmpty(szFile))
  {
    hResource = plResourceManager::LoadResource<plAiEqsQueryResource>(szFile);
    plResourceManager::PreloadResource(hResource);
  }

  m_hQuery = hResource;
}

const char* plAiEqsQueryComponent::GetQueryFile() const
{
  if (!m_hQuery.IsValid())
    return "";

  return m_hQuery.GetResourceID();
}

void plAiEqsQueryComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_hQuery;
  s << m_AutoRunInterval;
}

void plAiEqsQueryComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_hQuery;
  s >> m_AutoRunInterval;
}

void plAiEqsQueryComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_pEqsModule = GetWorld()->GetOrCreateModule<plAiEqsWorldModule>();
  m_bRunOnce = false;
  m_NextQuery = plTime::MakeZero();
}

void plAiEqsQueryComponent::OnDeactivated()
{
  if (m_pEqsModule != nullptr)
  {
    if (m_QueryId != plAiEqsWorldModule::InvalidQueryID)
    {
      m_pEqsModule->ReleaseQuery(m_QueryId);
      m_QueryId = plAiEqsWorldModule::InvalidQueryID;
    }

    m_pEqsModule = nullptr;
  }

  SUPER::OnDeactivated();
}

void plAiEqsQueryComponent::RunQuery()
{
  if (m_pEqsModule == nullptr || !m_hQuery.IsValid() || m_QueryId != plAiEqsWorldModule::InvalidQueryID)
    return;

  plAiEqsQueryParams params;
  params.m_hQuerier = GetOwner()->GetHandle();

  m_QueryId = m_pEqsModule->SubmitQuery(m_hQuery, params);
}

void plAiEqsQueryComponent::Update()
{
  if (m_pEqsModule == nullptr || !m_hQuery.IsValid())
    return;

  // poll the in-flight query
  if (m_QueryId != plAiEqsWorldModule::InvalidQueryID)
  {
    plAiEqsQueryResult result;

    if (m_pEqsModule->TryGetResult(m_QueryId, result))
    {
      m_pEqsModule->ReleaseQuery(m_QueryId);
      m_QueryId = plAiEqsWorldModule::InvalidQueryID;

      if (result.m_Status == plAiEqsQueryResult::Status::AreaNotReady)
      {
        // streaming - retry shortly instead of reporting a failure
        m_NextQuery = GetWorld()->GetClock().GetAccumulatedTime() + plTime::Seconds(0.5);
        m_bRunOnce = false;
      }
      else
      {
        plMsgAiEqsQueryFinished msg;
        msg.m_bSuccess = result.m_Status == plAiEqsQueryResult::Status::Ready;

        if (msg.m_bSuccess)
        {
          msg.m_vBestPosition = result.m_TopN[0].m_vPosition;
          msg.m_fBestScore = result.m_TopN[0].m_fScore;
          msg.m_hBestObject = result.m_TopN[0].m_hObject;

          m_vLastResult = msg.m_vBestPosition;
          m_bHasResult = true;
        }

        m_QueryFinishedSender.SendEventMessage(msg, this, GetOwner());
      }
    }

    return;
  }

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  if (m_AutoRunInterval.IsZero())
  {
    if (!m_bRunOnce && now >= m_NextQuery)
    {
      m_bRunOnce = true;
      RunQuery();
    }
  }
  else if (now >= m_NextQuery)
  {
    m_NextQuery = now + m_AutoRunInterval;
    RunQuery();
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Eqs_Components_EqsQueryComponent);
