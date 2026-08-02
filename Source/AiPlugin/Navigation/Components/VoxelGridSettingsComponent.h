#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/SettingsComponent.h>
#include <Core/World/SettingsComponentManager.h>

using plAiVoxelGridSettingsComponentManager = plSettingsComponentManager<class plAiVoxelGridSettingsComponent>;

/// Drop this component into a scene to configure the 3D voxel navigation grid.
///
/// Only one instance should exist per scene. It controls the grid resolution,
/// voxel size, and which collision layer to use for voxelization.
/// The grid is centered on this component's game object position.
class PL_AIPLUGIN_DLL plAiVoxelGridSettingsComponent : public plSettingsComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiVoxelGridSettingsComponent, plSettingsComponent, plAiVoxelGridSettingsComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiVoxelGridSettingsComponent

public:
  plAiVoxelGridSettingsComponent();
  ~plAiVoxelGridSettingsComponent();

  plUInt32 GetResolutionX() const { return m_uiResolutionX; } // [ property ]
  void SetResolutionX(plUInt32 uiValue);                      // [ property ]

  plUInt32 GetResolutionY() const { return m_uiResolutionY; } // [ property ]
  void SetResolutionY(plUInt32 uiValue);                      // [ property ]

  plUInt32 GetResolutionZ() const { return m_uiResolutionZ; } // [ property ]
  void SetResolutionZ(plUInt32 uiValue);                      // [ property ]

  float GetVoxelSize() const { return m_fVoxelSize; } // [ property ]
  void SetVoxelSize(float fValue);                    // [ property ]

  plUInt32 GetCollisionLayer() const { return m_uiCollisionLayer; } // [ property ]
  void SetCollisionLayer(plUInt32 uiValue);                         // [ property ]

private:
  plUInt32 m_uiResolutionX = 64;
  plUInt32 m_uiResolutionY = 64;
  plUInt32 m_uiResolutionZ = 32;
  float m_fVoxelSize = 0.5f;
  plUInt32 m_uiCollisionLayer = 0;
};
