#include <Tests/AiTestWorld.h>

plAiTestWorld::plAiTestWorld(const char* szName)
{
  plWorldDesc desc(szName);
  m_pWorld = PL_DEFAULT_NEW(plWorld, desc);

  PL_LOCK(m_pWorld->GetWriteMarker());
  m_pWorld->SetWorldSimulationEnabled(true);
  m_pWorld->GetClock().SetFixedTimeStep(plTime::MakeFromSeconds(1.0 / 60.0));
}

plAiTestWorld::~plAiTestWorld()
{
  m_pWorld.Clear();
}

void plAiTestWorld::Step(plUInt32 uiFrames)
{
  for (plUInt32 i = 0; i < uiFrames; ++i)
  {
    PL_LOCK(m_pWorld->GetWriteMarker());
    m_pWorld->Update();
  }
}

plGameObject* plAiTestWorld::CreateObject(const char* szName, const plVec3& vPosition, plGameObjectHandle* out_pHandle)
{
  PL_LOCK(m_pWorld->GetWriteMarker());

  plGameObjectDesc desc;
  desc.m_sName.Assign(szName);
  desc.m_LocalPosition = vPosition;

  plGameObject* pObject = nullptr;
  const plGameObjectHandle hObject = m_pWorld->CreateObject(desc, pObject);

  if (out_pHandle != nullptr)
  {
    *out_pHandle = hObject;
  }

  return pObject;
}
