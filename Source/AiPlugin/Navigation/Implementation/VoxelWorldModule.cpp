#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/Components/VoxelGridSettingsComponent.h>
#include <AiPlugin/Navigation/VoxelWorldModule.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/World/World.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/Threading/TaskSystem.h>
#include <RendererCore/Debug/DebugRendererContext.h>

plCVarBool cvar_VoxelGridVisualize("AI.VoxelGrid.Visualize", false, plCVarFlags::None, "Visualize the voxel grid.");

// clang-format off
PL_IMPLEMENT_WORLD_MODULE(plAiVoxelWorldModule);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiVoxelWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiVoxelWorldModule::plAiVoxelWorldModule(plWorld* pWorld)
  : plWorldModule(pWorld)
{
}

plAiVoxelWorldModule::~plAiVoxelWorldModule() = default;

void plAiVoxelWorldModule::Initialize()
{
  SUPER::Initialize();

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiVoxelWorldModule::Update, this);
    updateDesc.m_Phase = plWorldUpdatePhase::PostTransform;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;

    RegisterUpdateFunction(updateDesc);
  }

  m_bNeedsVoxelization = true;
}

void plAiVoxelWorldModule::Deinitialize()
{
  SUPER::Deinitialize();
}

void plAiVoxelWorldModule::Update(const UpdateContext& ctxt)
{
  if (m_uiUpdateDelay > 0)
  {
    --m_uiUpdateDelay;
    return;
  }

  if (m_bNeedsVoxelization)
  {
    m_bNeedsVoxelization = false;

    // Read settings from the settings component if one exists in the scene
    if (auto* pSettingsManager = GetWorld()->GetComponentManager<plAiVoxelGridSettingsComponentManager>())
    {
      if (auto* pSettings = pSettingsManager->GetSingletonComponent())
      {
        m_uiResolutionX = pSettings->GetResolutionX();
        m_uiResolutionY = pSettings->GetResolutionY();
        m_uiResolutionZ = pSettings->GetResolutionZ();
        m_fVoxelSize = pSettings->GetVoxelSize();
        m_uiCollisionLayer = pSettings->GetCollisionLayer();
        m_vGridCenter = pSettings->GetOwner()->GetGlobalPosition();
      }
    }

    m_VoxelGrid.Init(m_uiResolutionX, m_uiResolutionY, m_uiResolutionZ);
    m_VoxelGrid.SetWorldParameters(m_vGridCenter, m_fVoxelSize);
    VoxelizeWorld(m_uiCollisionLayer);
    m_bIsReady = true;
  }

  if (cvar_VoxelGridVisualize)
  {
    m_VoxelGrid.DebugDraw(GetWorld(), plColor::LimeGreen.WithAlpha(0.1f));
  }
}

void plAiVoxelWorldModule::VoxelizeWorld(plUInt32 uiCollisionLayer)
{
  auto* pPhysics = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();
  if (pPhysics == nullptr)
  {
    plLog::Warning("plAiVoxelWorldModule: No physics module available for voxelization.");
    return;
  }

  m_VoxelGrid.ClearData();

  const plPhysicsQueryParameters queryParams(uiCollisionLayer, plPhysicsShapeType::Static | plPhysicsShapeType::Dynamic);
  const float fVoxelSize = m_VoxelGrid.GetVoxelSize();
  const plBoundingBox gridAABB = m_VoxelGrid.GetAABB();

  const plUInt32 uiDimX = m_VoxelGrid.GetDimX();
  const plUInt32 uiDimY = m_VoxelGrid.GetDimY();
  const plUInt32 uiDimZ = m_VoxelGrid.GetDimZ();

  // The whole grid can be > 1M voxels; doing the physics queries serially on the main thread freezes
  // startup for many seconds. Both phases are read-only against the physics system (no simulation step
  // runs concurrently with this PostTransform update), so the Jolt narrow-phase queries are safe to run
  // from multiple worker threads at once. We parallelize via plTaskSystem::ParallelForIndexed, which
  // blocks until all sub-ranges finish.

  // Phase 1: Raycast along all 3 axes to catch thin geometry (walls, floors, ceilings).
  // For each row of voxels along an axis, cast a ray and mark hit voxels as solid.
  // Hits can land in any voxel, so writes use SetVoxelThreadSafe (atomic) to avoid clobbering voxels
  // that share a 64-bit block.
  {
    const float fPadding = fVoxelSize * 0.5f;

    // Rays along +X axis (catches walls perpendicular to X). One ray per (z, y) cell.
    const float fRayLenX = gridAABB.m_vMax.x - gridAABB.m_vMin.x + fPadding * 2.0f;
    plTaskSystem::ParallelForIndexed(0u, uiDimZ * uiDimY, [this, pPhysics, &queryParams, gridAABB, fVoxelSize, fPadding, fRayLenX, uiDimY](plUInt32 uiFirst, plUInt32 uiEnd) {
      plPhysicsCastResultArray hitResults;
      for (plUInt32 i = uiFirst; i < uiEnd; ++i)
      {
        const plUInt32 z = i / uiDimY;
        const plUInt32 y = i % uiDimY;
        const plVec3 vRayStart(
          gridAABB.m_vMin.x - fPadding,
          gridAABB.m_vMin.y + (y + 0.5f) * fVoxelSize,
          gridAABB.m_vMin.z + (z + 0.5f) * fVoxelSize);

        hitResults.m_Results.Clear();
        if (pPhysics->RaycastAll(hitResults, vRayStart, plVec3(1, 0, 0), fRayLenX, queryParams))
        {
          for (const auto& hit : hitResults.m_Results)
          {
            plVec3I32 vCoord;
            if (m_VoxelGrid.WorldToCoord(hit.m_vPosition, vCoord))
              m_VoxelGrid.SetVoxelThreadSafe(vCoord);
          }
        }
      }
    },
      "VoxelizeRaycastX");

    // Rays along +Y axis (catches walls perpendicular to Y). One ray per (z, x) cell.
    const float fRayLenY = gridAABB.m_vMax.y - gridAABB.m_vMin.y + fPadding * 2.0f;
    plTaskSystem::ParallelForIndexed(0u, uiDimZ * uiDimX, [this, pPhysics, &queryParams, gridAABB, fVoxelSize, fPadding, fRayLenY, uiDimX](plUInt32 uiFirst, plUInt32 uiEnd) {
      plPhysicsCastResultArray hitResults;
      for (plUInt32 i = uiFirst; i < uiEnd; ++i)
      {
        const plUInt32 z = i / uiDimX;
        const plUInt32 x = i % uiDimX;
        const plVec3 vRayStart(
          gridAABB.m_vMin.x + (x + 0.5f) * fVoxelSize,
          gridAABB.m_vMin.y - fPadding,
          gridAABB.m_vMin.z + (z + 0.5f) * fVoxelSize);

        hitResults.m_Results.Clear();
        if (pPhysics->RaycastAll(hitResults, vRayStart, plVec3(0, 1, 0), fRayLenY, queryParams))
        {
          for (const auto& hit : hitResults.m_Results)
          {
            plVec3I32 vCoord;
            if (m_VoxelGrid.WorldToCoord(hit.m_vPosition, vCoord))
              m_VoxelGrid.SetVoxelThreadSafe(vCoord);
          }
        }
      }
    },
      "VoxelizeRaycastY");

    // Rays along +Z axis (catches floors and ceilings). One ray per (y, x) cell.
    const float fRayLenZ = gridAABB.m_vMax.z - gridAABB.m_vMin.z + fPadding * 2.0f;
    plTaskSystem::ParallelForIndexed(0u, uiDimY * uiDimX, [this, pPhysics, &queryParams, gridAABB, fVoxelSize, fPadding, fRayLenZ, uiDimX](plUInt32 uiFirst, plUInt32 uiEnd) {
      plPhysicsCastResultArray hitResults;
      for (plUInt32 i = uiFirst; i < uiEnd; ++i)
      {
        const plUInt32 y = i / uiDimX;
        const plUInt32 x = i % uiDimX;
        const plVec3 vRayStart(
          gridAABB.m_vMin.x + (x + 0.5f) * fVoxelSize,
          gridAABB.m_vMin.y + (y + 0.5f) * fVoxelSize,
          gridAABB.m_vMin.z - fPadding);

        hitResults.m_Results.Clear();
        if (pPhysics->RaycastAll(hitResults, vRayStart, plVec3(0, 0, 1), fRayLenZ, queryParams))
        {
          for (const auto& hit : hitResults.m_Results)
          {
            plVec3I32 vCoord;
            if (m_VoxelGrid.WorldToCoord(hit.m_vPosition, vCoord))
              m_VoxelGrid.SetVoxelThreadSafe(vCoord);
          }
        }
      }
    },
      "VoxelizeRaycastZ");
  }

  // Phase 2: Overlap test to fill in volumetric geometry that rays might pass through.
  // Use a slightly expanded box to catch edges.
  //
  // Parallelized at 4x4x4-block granularity: each block index maps to exactly one task, so the voxel
  // writes within a block are owned by a single thread and need no atomics. (Phase 1 completed before
  // this runs, so reading its results here is also race-free.)
  //
  // Per block we first do ONE coarse overlap test covering all 64 voxel boxes. If nothing overlaps the
  // enclosing box, none of the 64 fine boxes can either, so we skip the whole block. For mostly-empty
  // grids this replaces ~64 fine tests with a single coarse one.
  {
    const float fOverlapExtent = fVoxelSize * 1.1f;
    const plVec3 vBoxExtents(fOverlapExtent, fOverlapExtent, fOverlapExtent);

    // Dims are rounded up to multiples of 4 in Init(), so every block is fully in-range.
    const plUInt32 uiBlocksX = uiDimX / 4u;
    const plUInt32 uiBlocksY = uiDimY / 4u;
    const plUInt32 uiBlocksZ = uiDimZ / 4u;
    const plUInt32 uiNumBlocks = uiBlocksX * uiBlocksY * uiBlocksZ;

    // Coarse box enclosing every voxel test box in a 4x4x4 block: voxel centers span +/-1.5 voxels from
    // the block center, plus each fine box's 0.55-voxel half-extent. OverlapTestBox takes full extents.
    const float fCoarseExtent = (1.5f + 0.55f) * fVoxelSize * 2.0f;
    const plVec3 vCoarseExtents(fCoarseExtent, fCoarseExtent, fCoarseExtent);

    plTaskSystem::ParallelForIndexed(0u, uiNumBlocks, [this, pPhysics, &queryParams, vBoxExtents, vCoarseExtents, fVoxelSize, uiBlocksX, uiBlocksY](plUInt32 uiFirst, plUInt32 uiEnd) {
      const plTransform xformIdentity = plTransform::MakeIdentity();
      for (plUInt32 b = uiFirst; b < uiEnd; ++b)
      {
        const plUInt32 bx = b % uiBlocksX;
        const plUInt32 by = (b / uiBlocksX) % uiBlocksY;
        const plUInt32 bz = b / (uiBlocksX * uiBlocksY);

        // Coarse rejection: block center is the center-of-first-voxel offset by 1.5 voxels per axis.
        const plVec3 vBlockCenter = m_VoxelGrid.CoordToWorld(plVec3I32((plInt32)bx * 4, (plInt32)by * 4, (plInt32)bz * 4)) + plVec3(1.5f * fVoxelSize);
        if (!pPhysics->OverlapTestBox(vCoarseExtents, vBlockCenter, xformIdentity, queryParams))
          continue;

        for (plUInt32 dz = 0; dz < 4u; ++dz)
        {
          for (plUInt32 dy = 0; dy < 4u; ++dy)
          {
            for (plUInt32 dx = 0; dx < 4u; ++dx)
            {
              const plVec3I32 vCoord((plInt32)(bx * 4u + dx), (plInt32)(by * 4u + dy), (plInt32)(bz * 4u + dz));

              // Skip already-marked voxels from raycast phase
              if (m_VoxelGrid.CheckVoxel(vCoord))
                continue;

              const plVec3 vWorldPos = m_VoxelGrid.CoordToWorld(vCoord);

              plTransform xform = plTransform::MakeIdentity();
              xform.m_vPosition = vWorldPos;

              if (pPhysics->OverlapTestBox(vBoxExtents, vWorldPos, xform, queryParams))
                m_VoxelGrid.SetVoxel(vCoord, true);
            }
          }
        }
      }
    },
      "VoxelizeOverlap");
  }

  const plBoundingBox finalAABB = m_VoxelGrid.GetAABB();
  plLog::Info("plAiVoxelWorldModule: Voxelized world ({}x{}x{}, voxel size {}). Grid bounds: ({}, {}, {}) to ({}, {}, {}).",
    uiDimX, uiDimY, uiDimZ, m_VoxelGrid.GetVoxelSize(),
    finalAABB.m_vMin.x, finalAABB.m_vMin.y, finalAABB.m_vMin.z,
    finalAABB.m_vMax.x, finalAABB.m_vMax.y, finalAABB.m_vMax.z);
}

void plAiVoxelWorldModule::InjectObstacle(const plBoundingBox& box)
{
  m_VoxelGrid.InjectBox(box);
}

void plAiVoxelWorldModule::RemoveObstacle(const plBoundingBox& box)
{
  m_VoxelGrid.SubtractBox(box);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Implementation_VoxelWorldModule);
