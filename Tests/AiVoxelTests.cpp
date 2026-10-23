#include <AiPlugin/Navigation/Components/VoxelNavigationComponent.h>
#include <AiPlugin/Navigation/Components/VoxelObstacleComponent.h>
#include <AiPlugin/Navigation/VoxelGrid.h>
#include <AiPlugin/Navigation/VoxelNavigation.h>
#include <AiPlugin/Navigation/VoxelWorldModule.h>
#include <Tests/AiTestCheck.h>
#include <Tests/AiTestPhysics.h>
#include <Tests/AiTestWorld.h>

namespace
{
  /// A 16 x 16 x 4 grid of 1 m voxels with its origin at (0, 0, 0): voxel (x, y, z) is centered at (x + 0.5, y + 0.5, z + 0.5).
  void InitUnitGrid(plVoxelGrid& ref_grid, plUInt32 uiDimX = 16, plUInt32 uiDimY = 16, plUInt32 uiDimZ = 4, float fVoxelSize = 1.0f)
  {
    ref_grid.Init(uiDimX, uiDimY, uiDimZ);
    ref_grid.SetWorldParameters(plVec3(uiDimX * 0.5f, uiDimY * 0.5f, uiDimZ * 0.5f) * fVoxelSize, fVoxelSize);
  }

  plVec3 Center(const plVoxelGrid& grid, plInt32 x, plInt32 y, plInt32 z)
  {
    return grid.CoordToWorld(plVec3I32(x, y, z));
  }

  void SetColumn(plVoxelGrid& ref_grid, plInt32 x, plInt32 y, bool bSolid = true)
  {
    for (plInt32 z = 0; z < static_cast<plInt32>(ref_grid.GetDimZ()); ++z)
    {
      ref_grid.SetVoxel(plVec3I32(x, y, z), bSolid);
    }
  }

  //////////////////////////////////////////////////////////////////////////

  void TestCornerCutting()
  {
    // two occupied columns touching at a corner, with the start and target on either side of it
    plVoxelGrid grid;
    InitUnitGrid(grid);
    SetColumn(grid, 5, 4);
    SetColumn(grid, 4, 5);

    Check(!grid.IsVisible(Center(grid, 4, 4, 1), Center(grid, 5, 5, 1)), "R3: line of sight doesn't slip between two occupied corners");

    plAiVoxelNavigation navigation;
    navigation.SetVoxelGrid(&grid);
    const auto result = navigation.FindPath(Center(grid, 4, 4, 1), Center(grid, 5, 5, 1));

    Check(result == plAiVoxelNavigation::State::PathFound, "R3: a path around the corner exists");
    Check(navigation.GetWaypoints().GetCount() >= 3, "R3: the path goes around the corner instead of through it");

    bool bSegmentsClear = true;
    for (plUInt32 i = 0; i + 1 < navigation.GetWaypoints().GetCount(); ++i)
    {
      bSegmentsClear &= grid.IsVisible(navigation.GetWaypoints()[i], navigation.GetWaypoints()[i + 1]);
    }
    Check(bSegmentsClear, "R3: every path segment has line of sight");
  }

  void TestClearanceValues()
  {
    plVoxelGrid grid;
    InitUnitGrid(grid, 16, 16, 8);
    grid.SetVoxel(plVec3I32(8, 8, 4), true);
    grid.BuildClearance();

    Check(grid.GetClearance(plVec3I32(8, 8, 4)) == 0, "R3: an occupied voxel has clearance 0");
    Check(grid.GetClearance(plVec3I32(9, 9, 5)) == 1, "R3: a diagonal neighbor has clearance 1");
    Check(grid.GetClearance(plVec3I32(10, 8, 6)) == 2, "R3: two voxels away is clearance 2");
    Check(grid.GetClearance(plVec3I32(0, 0, 0)) == plVoxelGrid::MaxClearance, "R3: clearance is capped");
  }

  void TestClearanceUpdate()
  {
    plVoxelGrid grid;
    InitUnitGrid(grid, 24, 24, 8);

    plUInt32 uiRandom = 12345;
    auto NextRandom = [&uiRandom]() { uiRandom = uiRandom * 1664525u + 1013904223u; return uiRandom >> 8; };

    for (plUInt32 i = 0; i < 60; ++i)
    {
      grid.SetVoxel(plVec3I32(NextRandom() % 24, NextRandom() % 24, NextRandom() % 8), true);
    }

    grid.BuildClearance();

    // change a block of voxels and update only around it
    const plBoundingBox changed = plBoundingBox::MakeFromMinMax(plVec3(9.1f, 9.1f, 2.1f), plVec3(12.9f, 12.9f, 4.9f));
    for (plInt32 z = 2; z <= 4; ++z)
      for (plInt32 y = 9; y <= 12; ++y)
        for (plInt32 x = 9; x <= 12; ++x)
          grid.SetVoxel(plVec3I32(x, y, z), (x + y + z) % 3 == 0);

    grid.UpdateClearance(changed);

    plVoxelGrid rebuilt = grid;
    rebuilt.BuildClearance();

    bool bSame = true;
    for (plInt32 z = 0; z < 8; ++z)
      for (plInt32 y = 0; y < 24; ++y)
        for (plInt32 x = 0; x < 24; ++x)
          bSame &= grid.GetClearance(plVec3I32(x, y, z)) == rebuilt.GetClearance(plVec3I32(x, y, z));

    Check(bSame, "R3: a local clearance update matches a full rebuild");
  }

  void TestAgentRadius()
  {
    // a wall at x = 8 with a one voxel wide slot at y = 8; voxels are 0.5 m
    plVoxelGrid grid;
    InitUnitGrid(grid, 16, 16, 4, 0.5f);

    for (plInt32 y = 0; y < 16; ++y)
    {
      if (y != 8)
      {
        SetColumn(grid, 8, y);
      }
    }

    grid.BuildClearance();

    plAiVoxelNavigation navigation;
    navigation.SetVoxelGrid(&grid);

    Check(navigation.FindPath(Center(grid, 3, 8, 1), Center(grid, 13, 8, 1), 10000, 0.0f) == plAiVoxelNavigation::State::PathFound, "R3: a point-sized agent fits through a one voxel slot");
    Check(navigation.FindPath(Center(grid, 3, 8, 1), Center(grid, 13, 8, 1), 10000, 0.6f) == plAiVoxelNavigation::State::NoPathFound, "R3: a 0.6 m agent doesn't fit through a 0.5 m slot");

    // widen the slot to three voxels: the middle one has clearance 2
    SetColumn(grid, 8, 7, false);
    SetColumn(grid, 8, 9, false);
    grid.BuildClearance();

    Check(navigation.FindPath(Center(grid, 3, 8, 1), Center(grid, 13, 8, 1), 10000, 0.6f) == plAiVoxelNavigation::State::PathFound, "R3: a 0.6 m agent fits through a 1.5 m slot");
  }

  void TestIncrementalSearch()
  {
    plVoxelGrid grid;
    InitUnitGrid(grid);
    SetColumn(grid, 5, 4);
    SetColumn(grid, 4, 5);

    plAiVoxelNavigation reference;
    reference.SetVoxelGrid(&grid);
    reference.FindPath(Center(grid, 1, 1, 1), Center(grid, 12, 12, 1));

    plAiVoxelNavigation navigation;
    navigation.SetVoxelGrid(&grid);
    navigation.StartPathSearch(Center(grid, 1, 1, 1), Center(grid, 12, 12, 1));

    plUInt32 uiFrames = 0;
    while (navigation.ContinuePathSearch(2) == plAiVoxelNavigation::State::Searching && uiFrames < 10000)
    {
      ++uiFrames;
    }

    Check(uiFrames > 1, "R9: a search with a small budget spans several steps");
    Check(navigation.GetState() == plAiVoxelNavigation::State::PathFound && navigation.GetWaypoints() == reference.GetWaypoints(), "R9: the incremental search finds the same path");
  }

  //////////////////////////////////////////////////////////////////////////
  // world tests on the box physics

  struct VoxelWorld
  {
    explicit VoxelWorld(const char* szName, bool bWithPhysics = true)
      : m_World(szName)
    {
      PL_LOCK(m_World.GetWorld().GetWriteMarker());

      if (bWithPhysics)
      {
        m_pPhysics = m_World.GetWorld().GetOrCreateModule<plAiTestPhysicsWorldModule>();
      }

      m_pVoxels = m_World.GetWorld().GetOrCreateModule<plAiVoxelWorldModule>();
    }

    /// An object whose physics bounds are the box, registered as a voxel obstacle.
    plGameObjectHandle AddObstacle(const plBoundingBox& box)
    {
      plGameObjectHandle hObject;
      plGameObject* pObject = m_World.CreateObject("Obstacle", box.GetCenter(), &hObject);

      PL_LOCK(m_World.GetWorld().GetWriteMarker());
      m_pPhysics->AddBox(box, plPhysicsShapeType::Dynamic, hObject);

      plAiVoxelObstacleComponent* pObstacle = nullptr;
      plAiVoxelObstacleComponent::CreateComponent(pObject, pObstacle);
      return hObject;
    }

    bool IsSolid(const plVec3& vPosition)
    {
      plVec3I32 vCoord;
      return m_pVoxels->GetVoxelGrid()->WorldToCoord(vPosition, vCoord) && m_pVoxels->GetVoxelGrid()->CheckVoxel(vCoord);
    }

    plAiTestWorld m_World;
    plAiTestPhysicsWorldModule* m_pPhysics = nullptr;
    plAiVoxelWorldModule* m_pVoxels = nullptr;
  };

  void TestObstacleLayers()
  {
    VoxelWorld world("AiTest R2");
    world.m_pPhysics->AddBox(plBoundingBox::MakeFromMinMax(plVec3(2, -2, -2), plVec3(3, 2, 2)), plPhysicsShapeType::Static);

    // registered before the grid exists; applied when it is built
    const plGameObjectHandle hFirst = world.AddObstacle(plBoundingBox::MakeFromMinMax(plVec3(2.5f, -1, -1), plVec3(5, 1, 1)));
    world.AddObstacle(plBoundingBox::MakeFromMinMax(plVec3(4, -1, -1), plVec3(6, 1, 1)));

    world.m_World.Step(15);
    Check(world.m_pVoxels->IsReady(), "R2: the grid is built from the box physics");
    Check(world.IsSolid(plVec3(2.25f, 1.75f, 0)) && world.IsSolid(plVec3(3.75f, 0, 0)) && world.IsSolid(plVec3(5.75f, 0, 0)), "R2: static geometry and both obstacles are occupied");

    const plUInt32 uiGeneration = world.m_pVoxels->GetVoxelGrid()->GetGeneration();

    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      world.m_World.GetWorld().DeleteObjectNow(hFirst);
    }

    Check(!world.IsSolid(plVec3(3.75f, 0, 0)), "R2: removing an obstacle frees the voxels only it occupied");
    Check(world.IsSolid(plVec3(2.75f, 0, 0)), "R2: removing an obstacle keeps static geometry");
    Check(world.IsSolid(plVec3(4.75f, 0, 0)), "R2: removing an obstacle keeps another obstacle's voxels");
    Check(world.m_pVoxels->GetVoxelGrid()->GetGeneration() != uiGeneration, "R9: obstacle changes advance the grid generation");
  }

  plGameObjectHandle SpawnFlyer(VoxelWorld& ref_world, const plVec3& vPosition, plComponentHandle* out_pNavigation)
  {
    plGameObjectHandle hObject;
    plGameObject* pObject = ref_world.m_World.CreateObject("Flyer", vPosition, &hObject);

    PL_LOCK(ref_world.m_World.GetWorld().GetWriteMarker());
    pObject->MakeDynamic(); // the component moves its owner

    plAiVoxelNavigationComponent* pNavigation = nullptr;
    *out_pNavigation = plAiVoxelNavigationComponent::CreateComponent(pObject, pNavigation);
    pNavigation->m_fReachedDistance = 0.3f;
    return hObject;
  }

  template <typename Function>
  void WithFlyer(VoxelWorld& ref_world, const plComponentHandle& hNavigation, Function function)
  {
    PL_LOCK(ref_world.m_World.GetWorld().GetWriteMarker());

    plAiVoxelNavigationComponent* pNavigation = nullptr;
    if (ref_world.m_World.GetWorld().TryGetComponent(hNavigation, pNavigation))
    {
      function(*pNavigation);
    }
  }

  plVec3 GetPosition(VoxelWorld& ref_world, const plGameObjectHandle& hObject)
  {
    PL_LOCK(ref_world.m_World.GetWorld().GetReadMarker());
    const plGameObject* pObject = nullptr;
    return ref_world.m_World.GetWorld().TryGetObject(hObject, pObject) ? pObject->GetGlobalPosition() : plVec3::MakeZero();
  }

  void TestNoPhysics()
  {
    VoxelWorld world("AiTest R4 no physics", false);
    plComponentHandle hNavigation;
    SpawnFlyer(world, plVec3::MakeZero(), &hNavigation);

    world.m_World.Step(5);
    WithFlyer(world, hNavigation, [](plAiVoxelNavigationComponent& ref_navigation) { ref_navigation.SetDestination(plVec3(3, 0, 0)); });
    world.m_World.Step(30);

    Check(!world.m_pVoxels->IsReady(), "R4: without physics the grid doesn't report ready");
    WithFlyer(world, hNavigation, [](plAiVoxelNavigationComponent& ref_navigation) { Check(ref_navigation.GetState() == plAiVoxelNavigationComponentState::Searching, "R4: a destination waits for the grid instead of being dropped or failing"); });
  }

  void TestEarlyDestinationAndReplan()
  {
    VoxelWorld world("AiTest R4 R9");
    plComponentHandle hNavigation;
    const plGameObjectHandle hFlyer = SpawnFlyer(world, plVec3(-5, 0, 0), &hNavigation);

    // set before the grid is built (it builds after 10 updates)
    world.m_World.Step(2);
    WithFlyer(world, hNavigation, [](plAiVoxelNavigationComponent& ref_navigation) { ref_navigation.SetDestination(plVec3(5, 0, 0)); });

    world.m_World.Step(20);
    WithFlyer(world, hNavigation, [](plAiVoxelNavigationComponent& ref_navigation) { Check(ref_navigation.GetState() == plAiVoxelNavigationComponentState::Moving, "R4: a destination set before the grid was ready is followed once it is"); });

    // block the straight line while the flyer is on its way
    const plBoundingBox block = plBoundingBox::MakeFromMinMax(plVec3(-1, -1, -1), plVec3(1, 1, 1));
    world.AddObstacle(block);

    plBoundingBox inner = block;
    inner.Grow(plVec3(-0.1f));

    bool bEnteredBlock = false;
    for (plUInt32 i = 0; i < 400; ++i)
    {
      world.m_World.Step(1);
      bEnteredBlock |= inner.Contains(GetPosition(world, hFlyer));
    }

    Check(!bEnteredBlock, "R9: an obstacle added on the path makes the flyer go around it");
    Check((GetPosition(world, hFlyer) - plVec3(5, 0, 0)).GetLength() < 0.5f, "R9: the flyer still arrives");
    WithFlyer(world, hNavigation, [](plAiVoxelNavigationComponent& ref_navigation) { Check(ref_navigation.GetState() == plAiVoxelNavigationComponentState::Idle, "R9: the flyer is idle after arriving"); });
  }
} // namespace

void RunVoxelTests()
{
  TestCornerCutting();
  TestClearanceValues();
  TestClearanceUpdate();
  TestAgentRadius();
  TestIncrementalSearch();
  TestObstacleLayers();
  TestNoPhysics();
  TestEarlyDestinationAndReplan();
}
