#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Uuid.h>

class plStreamReader;
class plStreamWriter;

/// \brief The editor's Encounter panel and the engine process exchange encounter data with these messages
/// (plSimpleDocumentConfigMsgToEngine "AiEncounter.Poll" and plSimpleDocumentConfigMsgToEditor "AiEncounter.Frame",
/// payload value = data buffer), on the same channel as the AI Debugger.
///
/// The editor polls a scene document a few times per second, naming the director it shows. The engine answers with
/// every director of the world and the state of the named one (or the first one). Outside simulation the frame
/// describes the authored encounter. Objects are identified by their editor GUIDs.
struct PL_AIPLUGIN_DLL plAiEncounterDebugRequest
{
  static constexpr const char* s_szWhatToDo = "AiEncounter.Poll";

  plUInt32 m_uiRequestId = 0;
  plUuid m_Director; ///< the director's game object; invalid = the first one
  bool m_bRestart = false; ///< restarts the shown director's encounter (while simulating)

  void Write(plStreamWriter& inout_stream) const;
  plResult Read(plStreamReader& inout_stream);
  void ToBuffer(plDataBuffer& out_buffer) const;
  plResult FromBuffer(const plDataBuffer& buffer);
};

struct PL_AIPLUGIN_DLL plAiEncounterDebugFrame
{
  static constexpr const char* s_szWhatToDo = "AiEncounter.Frame";

  struct Director
  {
    plUuid m_Object;
    plString m_sName;
  };

  struct Phase
  {
    plString m_sName;
    float m_fDuration = 0.0f;
    float m_fTarget = 0.0f;
  };

  struct Wave
  {
    plUuid m_Object;
    plString m_sName;
    plUuid m_Spawner;
    plString m_sSpawner;      ///< the spawner object's name
    plString m_sSpawnerType;  ///< the spawner component's type, empty when nothing on it can be released
    plString m_sSquad;        ///< empty when the squad is unknown
    plString m_sTrigger;      ///< the trigger as one line: "W1 cleared", "Intensity < 0.40 for 10 s"
    plString m_sReleasePath;  ///< how a release reaches the spawner: "plMsgAiEncounterWave", "Activate()", "plSpawnComponent"
    plString m_sProblem;      ///< why the wave can't work as set up, empty when it can
    plUInt8 m_uiState = 0;    ///< plAiEncounterWaveState
    plInt32 m_iAlive = 0;
    plInt32 m_iPeakAlive = 0;
    double m_fDueSince = 0.0; ///< encounter seconds
    double m_fReleasedAt = 0.0;
    double m_fClearedAt = 0.0;
    bool m_bHoldWhenIntense = true;
    float m_fMaxHold = 0.0f;
  };

  plUInt32 m_uiRequestId = 0;
  bool m_bSimulating = false;

  plDynamicArray<Director> m_Directors; ///< every director in the world

  bool m_bHasDirector = false;
  plUuid m_Director;
  plString m_sDirectorName;
  plUInt8 m_uiState = 0;          ///< plAiEncounterDirectorComponent::EncounterState
  double m_fEncounterTime = 0.0;  ///< seconds since the encounter started
  float m_fPacingPosition = 0.0f; ///< seconds or cleared waves, as the pacing counts
  plUInt8 m_uiPacingBy = 0;       ///< plAiEncounterPacingBy
  plUInt32 m_uiPhase = 0xFFFFFFFF;
  float m_fIntensity = 0.0f;
  float m_fTarget = 1.0f;
  float m_fHoldMargin = 0.0f;
  float m_fParts[3] = {};   ///< engaged agents, player stimuli, player damage (plAiEncounterIntensity::Part)
  float m_fWeights[3] = {}; ///< their weights
  float m_fSmoothing = 0.0f;
  plUInt32 m_uiEngaged = 0;
  plDynamicArray<Phase> m_Phases;
  plDynamicArray<Wave> m_Waves;

  void Write(plStreamWriter& inout_stream) const;
  plResult Read(plStreamReader& inout_stream);
  void ToBuffer(plDataBuffer& out_buffer) const;
  plResult FromBuffer(const plDataBuffer& buffer);
};
