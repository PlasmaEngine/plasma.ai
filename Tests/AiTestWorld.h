#pragma once

#include <Core/World/World.h>
#include <Foundation/Configuration/CVar.h>

/// \brief A simulating world that updates at a fixed 60 Hz timestep.
class plAiTestWorld
{
public:
  explicit plAiTestWorld(const char* szName);
  ~plAiTestWorld();

  plWorld& GetWorld() { return *m_pWorld; }

  /// Runs the given number of world updates.
  void Step(plUInt32 uiFrames);

  plGameObject* CreateObject(const char* szName, const plVec3& vPosition, plGameObjectHandle* out_pHandle = nullptr);

private:
  plUniquePtr<plWorld> m_pWorld;
};

/// \brief Sets a cvar for the lifetime of the scope and restores it afterwards.
template <typename CVarType, typename ValueType>
class plAiScopedCVar
{
public:
  plAiScopedCVar(CVarType& ref_cvar, ValueType value)
    : m_CVar(ref_cvar)
    , m_PreviousValue(ref_cvar.GetValue())
  {
    m_CVar = value;
  }

  ~plAiScopedCVar() { m_CVar = m_PreviousValue; }

private:
  CVarType& m_CVar;
  ValueType m_PreviousValue;
};
