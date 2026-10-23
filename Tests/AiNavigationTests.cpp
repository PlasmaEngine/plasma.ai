#include <AiPlugin/Navigation/Components/NavigationComponent.h>
#include <AiPlugin/Navigation/CrowdWorldModule.h>
#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <AiPlugin/Utils/RcMath.h>
#include <Core/Physics/SurfaceResource.h>
#include <Core/ResourceManager/ResourceManager.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <Foundation/Threading/ThreadUtils.h>
#include <Tests/AiTestCheck.h>
#include <Tests/AiTestPhysics.h>
#include <Tests/AiTestWorld.h>

namespace
{
  /// A world whose navmesh builds from a flat 200 x 200 m floor at z = 0.
  struct NavMeshWorld
  {
    explicit NavMeshWorld(bool bDefaultFloor = true)
      : m_World("AiTest navmesh")
    {
      PL_LOCK(m_World.GetWorld().GetWriteMarker());
      m_pGeo = m_World.GetWorld().GetOrCreateModule<plAiTestNavGeoWorldModule>();

      if (bDefaultFloor)
      {
        m_pGeo->AddFloor(plVec2(-100, -100), plVec2(100, 100), 0.0f);
      }

      m_pModule = m_World.GetWorld().GetOrCreateModule<plAiNavMeshWorldModule>();
      m_pNavMesh = m_pModule->GetNavMesh(m_pModule->GetConfig().m_NavmeshConfigs[0].m_sName);
    }

    plAiNavMeshSectorStatus GetStatus(const plVec2& vPosition)
    {
      PL_LOCK(m_World.GetWorld().GetReadMarker());

      const plAiNavMesh::SectorID id = m_pNavMesh->CalculateSectorID(m_pNavMesh->CalculateSectorCoord(vPosition.x, vPosition.y));

      plDynamicArray<plAiNavMeshSectorStatus> status;
      m_pNavMesh->GetSectorStatus(status);

      for (const plAiNavMeshSectorStatus& sector : status)
      {
        if (sector.m_uiSectorID == id)
          return sector;
      }

      return plAiNavMeshSectorStatus();
    }

    /// Steps until the sector at vPosition has been built uiBuildCount times and is idle, or 10 s passed.
    ///
    /// Builds run on worker threads, so this waits on real time; frames of an empty world take almost none.
    void StepUntilBuilt(const plVec2& vPosition, plUInt32 uiBuildCount)
    {
      const plTime deadline = plTime::Now() + plTime::MakeFromSeconds(10.0);

      while (plTime::Now() < deadline)
      {
        const plAiNavMeshSectorStatus status = GetStatus(vPosition);
        if (status.m_uiBuildCount >= uiBuildCount && (status.m_State == plAiNavMeshSectorState::Ready || status.m_State == plAiNavMeshSectorState::Failed))
          return;

        m_World.Step(1);
        plThreadUtils::Sleep(plTime::MakeFromMilliseconds(1));
      }
    }

    void Request(const plVec2& vPosition)
    {
      PL_LOCK(m_World.GetWorld().GetWriteMarker());
      m_pNavMesh->RequestSector(vPosition, plVec2(0.1f));
    }

    void Invalidate(const plVec2& vPosition, plAiNavMeshRebuildReason reason)
    {
      PL_LOCK(m_World.GetWorld().GetWriteMarker());
      m_pNavMesh->InvalidateSector(vPosition, plVec2(0.1f), true, reason);
    }

    /// The area id of the polygon at vPosition, 0xFF if there is none.
    plUInt8 GetAreaAt(const plVec3& vPosition)
    {
      PL_LOCK(m_World.GetWorld().GetReadMarker());

      dtNavMeshQuery query;
      query.init(m_pNavMesh->GetDetourNavMesh(), 64);

      dtQueryFilter filter;
      filter.setIncludeFlags(0xFFFF);
      filter.setExcludeFlags(0);
      filter.setIncludeAreaBits(~0ull);

      const plRcPos center = vPosition;
      const plRcPos halfExtents = plVec3(0.2f, 0.2f, 1.0f);
      dtPolyRef poly = 0;
      float nearest[3];

      if (dtStatusFailed(query.findNearestPoly(center, halfExtents, &filter, &poly, nearest)) || poly == 0)
        return 0xFF;

      unsigned char uiArea = 0xFF;
      m_pNavMesh->GetDetourNavMesh()->getPolyArea(poly, &uiArea);
      return uiArea;
    }

    /// The flags of the first baked nav link, 0xFFFF if there is none.
    plUInt16 GetFirstLinkFlags()
    {
      PL_LOCK(m_World.GetWorld().GetReadMarker());

      const dtNavMesh* pNavMesh = m_pNavMesh->GetDetourNavMesh();

      for (int t = 0; t < pNavMesh->getMaxTiles(); ++t)
      {
        const dtMeshTile* pTile = pNavMesh->getTile(t);
        if (pTile == nullptr || pTile->header == nullptr || pTile->header->offMeshConCount == 0)
          continue;

        unsigned short uiFlags = 0;
        pNavMesh->getPolyFlags(pNavMesh->getPolyRefBase(pTile) | static_cast<dtPolyRef>(pTile->offMeshCons[0].poly), &uiFlags);
        return uiFlags;
      }

      return 0xFFFF;
    }

    void BuildAround(std::initializer_list<plVec2> positions)
    {
      for (const plVec2& vPosition : positions)
      {
        Request(vPosition);
      }

      for (const plVec2& vPosition : positions)
      {
        StepUntilBuilt(vPosition, 1);
      }

      m_World.Step(2); // blockers stamp the fresh tiles
    }

    plAiTestWorld m_World;
    plAiTestNavGeoWorldModule* m_pGeo = nullptr;
    plAiNavMeshWorldModule* m_pModule = nullptr;
    plAiNavMesh* m_pNavMesh = nullptr;
  };

  void TestSectorStatus()
  {
    NavMeshWorld world;
    const plVec2 vSector(1, 1);

    world.Request(vSector);
    world.StepUntilBuilt(vSector, 1);

    const plAiNavMeshSectorStatus status = world.GetStatus(vSector);
    Check(status.m_State == plAiNavMeshSectorState::Ready, "F10: a requested sector reports Ready once built");
    Check(status.m_uiPolyCount > 0 && status.m_uiBuildCount == 1, "F10: the status reports polygons and one build");
    Check(status.m_LastReason == plAiNavMeshRebuildReason::FirstBuild && status.m_LastBuildDuration.IsPositive(), "F10: the status says why and how long it was built");
  }

  void TestInvalidateDuringBuild()
  {
    NavMeshWorld world;

    // during the first build: the build may have sampled the geometry before the change
    const plVec2 vFirst(1, 1);
    world.Request(vFirst);
    world.m_World.Step(1);
    Check(world.GetStatus(vFirst).m_State == plAiNavMeshSectorState::Building, "N2: the sector is building after one frame");

    world.Invalidate(vFirst, plAiNavMeshRebuildReason::Obstacle);
    world.StepUntilBuilt(vFirst, 2);
    Check(world.GetStatus(vFirst).m_uiBuildCount == 2, "N2: an invalidation during the first build rebuilds the sector afterwards");
    Check(world.GetStatus(vFirst).m_LastReason == plAiNavMeshRebuildReason::Obstacle, "F10: the rebuild reports the obstacle as its reason");

    // during a rebuild of a sector in use
    world.Invalidate(vFirst, plAiNavMeshRebuildReason::Obstacle);
    world.m_World.Step(1);
    Check(world.GetStatus(vFirst).m_State == plAiNavMeshSectorState::Rebuilding, "N2: a sector in use reports Rebuilding while its rebuild runs");

    world.Invalidate(vFirst, plAiNavMeshRebuildReason::NavLink);
    world.StepUntilBuilt(vFirst, 4);
    Check(world.GetStatus(vFirst).m_uiBuildCount == 4, "N2: an invalidation during a rebuild is not dropped");
    Check(world.GetStatus(vFirst).m_LastReason == plAiNavMeshRebuildReason::NavLink, "F10: the last rebuild reports the nav link as its reason");
  }

  void TestCrowdGridGrows()
  {
    plAiTestWorld world("AiTest R8");
    plAiCrowdWorldModule* pCrowd = nullptr;

    {
      PL_LOCK(world.GetWorld().GetWriteMarker());
      pCrowd = world.GetWorld().GetOrCreateModule<plAiCrowdWorldModule>();
    }

    const plUInt32 uiInitialCapacity = pCrowd->GetProximityGridCapacity();

    // 600 agents on cell corners each cover 4 cells: 2400 entries, more than the initial pool
    plDynamicArray<plAiCrowdWorldModule::AgentID> agents;
    for (plUInt32 i = 0; i < 600; ++i)
    {
      agents.PushBack(pCrowd->RegisterAgent());
    }

    for (plUInt32 uiFrame = 0; uiFrame < 3; ++uiFrame)
    {
      for (plUInt32 i = 0; i < agents.GetCount(); ++i)
      {
        plAiCrowdAgentState state;
        state.m_vPosition = plVec3(static_cast<float>(i % 30) * 4.0f, static_cast<float>(i / 30) * 4.0f, 0.0f);
        state.m_fRadius = 0.3f;
        pCrowd->SubmitAgentState(agents[i], state);
      }

      world.Step(1);
    }

    Check(uiInitialCapacity < 2400, "R8: the initial grid is smaller than the agents need");
    Check(pCrowd->GetProximityGridCellsUsed() == 2400 && pCrowd->GetProximityGridCapacity() >= 2400, "R8: the grid grows to hold every agent");
    Check(pCrowd->GetProximityGridCellsDropped() == 0, "R8: no agent is dropped from the grid");
  }
  void TestDefaultNavMeshInitializes()
  {
    Check(sizeof(dtPolyRef) == 8, "N1: Detour uses 64-bit polygon references");

    // the default grid: 64 x 64 sectors with up to 2^16 polygons each
    plAiNavmeshConfig config;
    config.m_sName = "Default";

    plAiNavMesh navMesh(config);
    Check(navMesh.IsValid(), "N1: the default navmesh configuration initializes");

    config.m_sName = "Large";
    config.m_uiNumSectorsX = 256;
    config.m_uiNumSectorsY = 256;

    plAiNavMesh largeNavMesh(config);
    Check(largeNavMesh.IsValid(), "N1: a 256 x 256 sector navmesh initializes");
  }

  void TestNavLinkIDs()
  {
    NavMeshWorld world;
    PL_LOCK(world.m_World.GetWorld().GetWriteMarker());

    plAiNavLinkData link;
    link.m_vStart = plVec3(1, 1, 0);
    link.m_vEnd = plVec3(3, 1, 0);

    const plAiNavMeshWorldModule::NavLinkID first = world.m_pModule->RegisterNavLink(link, plComponentHandle());
    Check(world.m_pModule->HasNavLink(first), "N7: a registered link resolves");

    world.m_pModule->UnregisterNavLink(first);
    const plAiNavMeshWorldModule::NavLinkID second = world.m_pModule->RegisterNavLink(link, plComponentHandle());

    Check(second != first, "N7: a link that reuses a registry entry gets a new ID");
    Check(!world.m_pModule->HasNavLink(first), "N7: a removed link's ID, still baked into old tiles, resolves to nothing");
    Check(world.m_pModule->HasNavLink(second), "N7: the new link resolves");

    world.m_pModule->UnregisterNavLink(first); // stale: must not remove the new link
    Check(world.m_pModule->HasNavLink(second), "N7: unregistering a stale ID leaves the new link alone");

    world.m_pModule->UnregisterNavLink(second);
  }

  void TestUnknownNavmeshConfig()
  {
    NavMeshWorld world;
    plGameObject* pObject = world.m_World.CreateObject("Agent", plVec3(1, 1, 0));

    PL_LOCK(world.m_World.GetWorld().GetWriteMarker());

    pObject->MakeDynamic();

    plAiNavigationComponent* pNavigation = nullptr;
    plAiNavigationComponent::CreateComponent(pObject, pNavigation);
    pNavigation->m_sNavmeshConfig.Assign("NoSuchConfig");

    plVec3 vPoint;
    Check(!pNavigation->FindRandomPointAroundCircle(plVec3(1, 1, 0), 5.0f, vPoint), "N7: a navmesh config the project lacks fails the query instead of crashing");
  }

  void TestBlockersKeepGroundType()
  {
    plSurfaceResourceDescriptor grassDesc;
    grassDesc.m_iGroundType = 3;
    const plSurfaceResourceHandle hGrass = plResourceManager::CreateResource<plSurfaceResource>("AiTest/N9/Grass", std::move(grassDesc));
    plResourceLock<plSurfaceResource> pGrass(hGrass, plResourceAcquireMode::BlockTillLoaded);

    NavMeshWorld world(false);
    plAiNavMeshWorldModule::BlockerID avoid = plAiNavMeshWorldModule::InvalidBlockerID;

    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      world.m_pGeo->AddFloor(plVec2(-30, -30), plVec2(30, 30), 0.0f, pGrass.GetPointer());
      world.m_pModule->RegisterNavBlocker(plBoundingBox::MakeFromMinMax(plVec3(2, 2, -1), plVec3(4, 4, 1)), true, plAiNavObstacleMode::Block);
      avoid = world.m_pModule->RegisterNavBlocker(plBoundingBox::MakeFromMinMax(plVec3(-4, -4, -1), plVec3(-2, -2, 1)), true, plAiNavObstacleMode::Avoid);
    }

    world.BuildAround({plVec2(8, 8), plVec2(3, 3), plVec2(-3, -3)});

    Check(world.GetAreaAt(plVec3(8, 8, 0)) == 3, "N9: the grass floor is baked with its ground type");
    Check(world.GetAreaAt(plVec3(3, 3, 0)) == 3, "N9: polygons under a blocker keep the ground type they stand on");
    Check(world.GetAreaAt(plVec3(-3, -3, 0)) == plAiNavMeshAvoidZoneAreaID, "N9: an active avoid blocker marks its polygons");

    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      world.m_pModule->SetNavBlockerActive(avoid, false);
    }

    world.m_World.Step(2);
    Check(world.GetAreaAt(plVec3(-3, -3, 0)) == 3, "N9: switching an avoid blocker off gives its polygons their ground type back");
  }

  void TestBlockerClosesNavLink()
  {
    NavMeshWorld world;
    plAiNavMeshWorldModule::BlockerID blocker = plAiNavMeshWorldModule::InvalidBlockerID;

    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());

      plAiNavLinkData link;
      link.m_vStart = plVec3(1, 1, 0);
      link.m_vEnd = plVec3(5, 1, 0);
      world.m_pModule->RegisterNavLink(link, plComponentHandle());

      blocker = world.m_pModule->RegisterNavBlocker(plBoundingBox::MakeFromMinMax(plVec3(0.5f, 0.5f, -1), plVec3(1.5f, 1.5f, 1)), true, plAiNavObstacleMode::Block);
    }

    world.BuildAround({plVec2(1, 1), plVec2(5, 1)});

    const plUInt16 uiClosed = world.GetFirstLinkFlags();
    Check(uiClosed != 0xFFFF && (uiClosed & plAiNavMeshPolyFlags::Blocked) != 0, "N9: a blocker on a link's end closes the link");

    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      world.m_pModule->SetNavBlockerActive(blocker, false);
    }

    world.m_World.Step(2);
    Check((world.GetFirstLinkFlags() & plAiNavMeshPolyFlags::Blocked) == 0, "N9: switching the blocker off opens the link again");
  }
} // namespace

void RunNavigationTests()
{
  TestDefaultNavMeshInitializes();
  TestSectorStatus();
  TestInvalidateDuringBuild();
  TestCrowdGridGrows();
  TestNavLinkIDs();
  TestUnknownNavmeshConfig();
  TestBlockersKeepGroundType();
  TestBlockerClosesNavLink();
}
