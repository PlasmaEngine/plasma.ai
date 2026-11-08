#include <AiPlugin/Eqs/EqsTest.h>
#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <AiPlugin/Tactical/ThreatMap.h>
#include <AiPlugin/Tactical/ThreatWorldModule.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>
#include <Foundation/Threading/ThreadUtils.h>
#include <Tests/AiTestCheck.h>
#include <Tests/AiTestPhysics.h>
#include <Tests/AiTestWorld.h>

/// Lets the tests make up the enemy positions a team remembers.
struct plAiThreatWorldModuleTestAccess
{
  static void SetSource(plAiThreatWorldModule& ref_module, plStringView sTeam, const plVec3& vPosition, float fConfidence)
  {
    plAiThreatWorldModule::InjectedSource& injected = ref_module.m_InjectedSources.ExpandAndGetRef();
    injected.m_sTeam.Assign(sTeam);
    injected.m_Source.m_vPosition = vPosition;
    injected.m_Source.m_fConfidence = fConfidence;
    injected.m_Source.m_uiTargetId = 1;
  }

  static void ClearSources(plAiThreatWorldModule& ref_module) { ref_module.m_InjectedSources.Clear(); }
};

namespace
{
  /// A flat 120 m floor, a wall across the middle of it, and the navmesh and threat map over it.
  struct ThreatWorld
  {
    ThreatWorld()
      : m_World("AiTest threat")
    {
      PL_LOCK(m_World.GetWorld().GetWriteMarker());

      plAiTestNavGeoWorldModule* pGeo = m_World.GetWorld().GetOrCreateModule<plAiTestNavGeoWorldModule>();
      pGeo->AddFloor(plVec2(-60, -60), plVec2(60, 60), 0.0f);

      m_pPhysics = m_World.GetWorld().GetOrCreateModule<plAiTestPhysicsWorldModule>();
      // 2 m thick, 12 m long, 4 m high: a position behind it is hidden, one 10 m to the side is not
      m_pPhysics->AddBox(plBoundingBox::MakeFromMinMax(plVec3(-1, -6, 0), plVec3(1, 6, 4)), plPhysicsShapeType::Static);

      m_World.GetWorld().GetOrCreateModule<plAiBrainWorldModule>();
      m_pNavModule = m_World.GetWorld().GetOrCreateModule<plAiNavMeshWorldModule>();
      m_pTactical = m_World.GetWorld().GetOrCreateModule<plAiTacticalWorldModule>();
      m_pThreat = m_World.GetWorld().GetOrCreateModule<plAiThreatWorldModule>();

      m_pNavMesh = m_pNavModule->GetNavMesh(m_pNavModule->GetConfig().m_NavmeshConfigs[0].m_sName);
    }

    /// Builds the sectors within 50 m of the origin.
    void BuildNavmesh()
    {
      const plTime deadline = plTime::Now() + plTime::MakeFromSeconds(15.0);

      while (plTime::Now() < deadline)
      {
        bool bReady = false;

        {
          PL_LOCK(m_World.GetWorld().GetWriteMarker());
          bReady = m_pNavMesh->RequestSector(plVec2(0, 0), plVec2(50, 50));
        }

        if (bReady)
          break;

        m_World.Step(1);
        plThreadUtils::Sleep(plTime::MakeFromMilliseconds(1));
      }
    }

    /// Steps until the team's published map shows a value above fMin at the position, or 4 s of world time passed.
    bool StepUntilThreat(plStringView sTeam, const plVec3& vPosition, float fMin)
    {
      for (plUInt32 i = 0; i < 240; ++i)
      {
        m_World.Step(1);

        PL_LOCK(m_World.GetWorld().GetReadMarker());
        if (m_pThreat->GetThreat(plTempHashedString(sTeam), vPosition) > fMin)
          return true;
      }

      return false;
    }

    float Threat(plStringView sTeam, const plVec3& vPosition)
    {
      PL_LOCK(m_World.GetWorld().GetReadMarker());
      return m_pThreat->GetThreat(plTempHashedString(sTeam), vPosition);
    }

    float Visibility(plStringView sTeam, const plVec3& vPosition)
    {
      PL_LOCK(m_World.GetWorld().GetReadMarker());
      return m_pThreat->GetVisibility(plTempHashedString(sTeam), vPosition);
    }

    float Danger(plStringView sTeam, const plVec3& vPosition)
    {
      PL_LOCK(m_World.GetWorld().GetReadMarker());
      return m_pThreat->GetDanger(plTempHashedString(sTeam), vPosition);
    }

    plAiTestWorld m_World;
    plAiTestPhysicsWorldModule* m_pPhysics = nullptr;
    plAiNavMeshWorldModule* m_pNavModule = nullptr;
    plAiTacticalWorldModule* m_pTactical = nullptr;
    plAiThreatWorldModule* m_pThreat = nullptr;
    plAiNavMesh* m_pNavMesh = nullptr;
  };

  void TestSnapshotGrid()
  {
    plAiThreatSnapshot snapshot;
    snapshot.Configure(32.0f, 64, 64, 2.0f);

    Check(snapshot.GetCellsPerSide() == 16 && snapshot.GetCellsPerChunk() == 256, "A2: a 32 m sector holds 16 x 16 cells of 2 m");

    const plUInt32 uiKey = snapshot.GetChunkKey(plVec3(1, 1, 0));
    Check(uiKey == 32u * 64u + 32u, "A2: the origin lies in the middle sector");
    Check(snapshot.GetChunkKey(plVec3(5000, 0, 0)) == plInvalidIndex, "A2: a position off the grid has no chunk");
    Check(snapshot.GetCellCenter(uiKey, 0).IsEqual(plVec2(1, 1), 0.01f), "A2: the first cell of a chunk is centered 1 m in");

    Check(snapshot.GetThreat(plVec3(1, 1, 0)) == 0.0f, "A2: a chunk that does not exist knows no threat");

    plAiThreatSnapshot::Chunk& chunk = snapshot.GetOrCreateChunk(uiKey);
    chunk.m_Valid[0] = 1;
    chunk.m_Threat[0] = 0.5f;
    chunk.m_Visible[0] = 1;
    chunk.m_Valid[1] = 0;
    chunk.m_Threat[1] = 0.9f;

    Check(plMath::IsEqual(snapshot.GetThreat(plVec3(0.5f, 0.5f, 0)), 0.5f, 0.001f), "A2: a cell reports its threat");
    Check(snapshot.GetVisibility(plVec3(0.5f, 0.5f, 0)) == 1.0f && snapshot.GetVisibility(plVec3(8, 8, 0)) == 0.0f, "A2: a cell reports whether an enemy sees it");
    Check(snapshot.GetThreat(plVec3(2.5f, 0.5f, 0)) == 0.0f, "A2: a cell without ground has no threat");

    plAiThreatDangerArea& area = snapshot.GetAreas().ExpandAndGetRef();
    area.m_vPosition = plVec3(10, 10, 0);
    area.m_fRadius = 4.0f;
    area.m_fFade = 1.0f;

    Check(plMath::IsEqual(snapshot.GetDanger(plVec3(10, 10, 0)), 1.0f, 0.001f), "A2: the center of a danger area is fully dangerous");
    Check(plMath::IsEqual(snapshot.GetDanger(plVec3(12, 10, 0)), 0.625f, 0.001f), "A2: danger falls off to 0.25 at the edge");
    Check(snapshot.GetDanger(plVec3(15, 10, 0)) == 0.0f, "A2: no danger outside the radius");

    area.m_fFade = 0.5f;
    Check(plMath::IsEqual(snapshot.GetDanger(plVec3(10, 10, 0)), 0.5f, 0.001f), "A2: a fading area is less dangerous");

    plAiThreatSnapshot copy;
    copy.CopyFrom(snapshot);
    chunk.m_Threat[0] = 0.0f;
    Check(plMath::IsEqual(copy.GetThreat(plVec3(0.5f, 0.5f, 0)), 0.5f, 0.001f), "A2: a copied snapshot does not change with the original");
  }

  void TestThreatFromLineOfSight()
  {
    ThreatWorld world;
    world.BuildNavmesh();

    const plVec3 vEnemy(-10, 0, 0);

    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      plAiThreatWorldModuleTestAccess::SetSource(*world.m_pThreat, "Blue", vEnemy, 1.0f);
    }

    Check(world.StepUntilThreat("Blue", plVec3(-4, 0, 0), 0.5f), "A2: a known enemy makes the cells in front of it dangerous");

    // let the pass finish: the cells behind the wall are computed too
    world.m_World.Step(60);

    Check(world.Visibility("Blue", plVec3(-4, 0, 0)) == 1.0f, "A2: a cell in the open is seen");
    Check(world.Threat("Blue", plVec3(-4, 0, 0)) > 0.5f, "A2: threat is high close to the enemy");
    Check(world.Threat("Blue", plVec3(10, 0, 0)) == 0.0f && world.Visibility("Blue", plVec3(10, 0, 0)) == 0.0f, "A2: a cell behind the wall is hidden from the enemy");

    const float fSide = world.Threat("Blue", plVec3(10, 20, 0));
    Check(fSide > 0.2f && fSide < world.Threat("Blue", plVec3(-4, 0, 0)), "A2: a cell past the end of the wall is seen, and the threat falls off with distance");
    Check(world.Threat("Blue", plVec3(-10, 45, 0)) == 0.0f, "A2: nothing is threatened beyond the range");

    Check(world.Threat("Green", plVec3(-4, 0, 0)) == 0.0f, "A2: a team that remembers no enemy sees no threat");

    {
      PL_LOCK(world.m_World.GetWorld().GetReadMarker());
      Check(world.m_pThreat->GetSnapshot(plTempHashedString("Green")) == nullptr, "A2: an unknown team has no map");
    }

    // a weaker memory of the same position threatens less
    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      plAiThreatWorldModuleTestAccess::ClearSources(*world.m_pThreat);
      plAiThreatWorldModuleTestAccess::SetSource(*world.m_pThreat, "Blue", vEnemy, 0.3f);
    }

    world.m_World.Step(120);
    const float fWeak = world.Threat("Blue", plVec3(-4, 0, 0));
    Check(fWeak > 0.0f && fWeak < 0.4f, "A2: an enemy the agents are less sure about threatens less");

    // no enemy is known any more: the map empties
    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      plAiThreatWorldModuleTestAccess::ClearSources(*world.m_pThreat);
    }

    world.m_World.Step(120);
    Check(world.Threat("Blue", plVec3(-4, 0, 0)) == 0.0f, "A2: the threat goes when the enemy is forgotten");
  }

  void TestDangerAreas()
  {
    ThreatWorld world;
    world.BuildNavmesh();

    // a team has a map once an enemy is known to it, or an agent belongs to it
    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      plAiThreatWorldModuleTestAccess::SetSource(*world.m_pThreat, "Blue", plVec3(-40, -40, 0), 0.0f);
      plAiThreatWorldModuleTestAccess::SetSource(*world.m_pThreat, "Red", plVec3(-40, -40, 0), 0.0f);
    }

    world.m_World.Step(30);

    const plVec3 vCenter(20, -20, 0);

    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      world.m_pThreat->AddDangerArea(vCenter, 5.0f, plTime::MakeFromSeconds(2.0), "Blue");
    }

    world.m_World.Step(2);

    Check(world.Danger("Blue", vCenter) > 0.9f, "A2: a danger area is dangerous at once");
    Check(world.Danger("Red", vCenter) == 0.0f, "A2: a danger area for one team is no danger to another");

    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      world.m_pThreat->AddDangerArea(plVec3(-20, -20, 0), 5.0f, plTime::MakeZero(), "");
    }

    world.m_World.Step(2);
    Check(world.Danger("Blue", plVec3(-20, -20, 0)) > 0.9f && world.Danger("Red", plVec3(-20, -20, 0)) > 0.9f, "A2: an area without a team is dangerous to every team");

    world.m_World.Step(30);
    const float fFading = world.Danger("Blue", vCenter);
    Check(fFading < 0.9f && fFading > 0.0f, "A2: a timed danger area fades while it lasts");

    world.m_World.Step(120);
    Check(world.Danger("Blue", vCenter) == 0.0f, "A2: a timed danger area is gone when its time is up");
    Check(world.Danger("Blue", plVec3(-20, -20, 0)) > 0.9f, "A2: an area without a duration stays");
    Check(world.m_pThreat->GetDangerAreaCount() == 1, "A2: the module counts the areas that remain");
  }

  void TestCoverQueryAvoidsDanger()
  {
    ThreatWorld world;
    world.BuildNavmesh();

    const plVec3 vCenter(20, -20, 0);

    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      plAiThreatWorldModuleTestAccess::SetSource(*world.m_pThreat, "Blue", plVec3(-40, -40, 0), 0.0f);
      world.m_pThreat->AddDangerArea(vCenter, 6.0f, plTime::MakeZero(), "Blue");
    }

    world.m_World.Step(30);

    plAiTacticalWorldModule::QueryID queryId = plAiTacticalWorldModule::InvalidQueryID;

    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());

      // the querier stands in the middle of the area: a ring of candidates around it, some inside
      plAiTacticalQueryDesc desc = plAiTacticalQueryDesc::MakeFromPreset(plAiTacticalQueryPreset::RandomNearSelf, vCenter, plVec3::MakeZero(), false, 1.0f, 12.0f);
      desc.m_uiCandidates = 32;
      desc.m_uiMaxResults = 16;
      desc.m_sTeam.Assign("Blue");
      queryId = world.m_pTactical->SubmitQuery(desc);
    }

    plAiTacticalQueryResult result;
    bool bDone = false;

    for (plUInt32 i = 0; i < 120 && !bDone; ++i)
    {
      world.m_World.Step(1);

      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      bDone = world.m_pTactical->TryGetResult(queryId, result) && result.m_Status != plAiTacticalQueryResult::Status::Pending;
    }

    Check(bDone && result.m_Status == plAiTacticalQueryResult::Status::Ready, "A2: the query runs and finds positions");

    bool bAllSafe = !result.m_TopN.IsEmpty();
    for (const auto& candidate : result.m_TopN)
    {
      const float fDanger = world.Danger("Blue", candidate.m_vPosition);
      bAllSafe &= fDanger < 0.5f && plMath::IsEqual(candidate.m_fDanger, fDanger, 0.01f);
    }

    Check(bAllSafe, "A2: no position of the danger area's core is picked, and the result reports its danger");

    // the same query for a team the area does not concern may pick them
    {
      PL_LOCK(world.m_World.GetWorld().GetWriteMarker());
      world.m_pTactical->ReleaseQuery(queryId);
    }
  }

  void TestEqsThreatMapTest()
  {
    plAiThreatSnapshot snapshot;
    snapshot.Configure(32.0f, 64, 64, 2.0f);

    plAiThreatSnapshot::Chunk& chunk = snapshot.GetOrCreateChunk(snapshot.GetChunkKey(plVec3(1, 1, 0)));
    chunk.m_Valid[0] = 1;
    chunk.m_Threat[0] = 0.75f;
    chunk.m_Visible[0] = 1;
    chunk.m_Valid[1] = 1; // x = 2..4
    plAiThreatDangerArea& area = snapshot.GetAreas().ExpandAndGetRef();
    area.m_vPosition = plVec3(3, 1, 0);
    area.m_fRadius = 1.5f;

    plAiEqsItem items[3];
    items[0].m_vPosition = plVec3(1, 1, 0);   // threat 0.75, seen
    items[1].m_vPosition = plVec3(3, 1, 0);   // in the danger area
    items[2].m_vPosition = plVec3(500, 0, 0); // off the grid

    plAiEqsEvalContext ctx;
    ctx.m_pThreat = &snapshot;

    plAiEqsTest_ThreatMap test;
    Check(test.m_Layer == plAiEqsThreatLayer::Safety && test.m_Purpose == plAiEqsTestPurpose::ScoreOnly, "A2: the test scores the safety of a position by default");

    test.Run(ctx, items, 0);
    Check(plMath::IsEqual(items[0].m_fRaw, 0.25f, 0.001f), "A2: safety is what the threat leaves");
    Check(items[1].m_fRaw < 0.01f, "A2: a position in the middle of a danger area is not safe");
    Check(items[2].m_fRaw == 1.0f, "A2: nothing is known off the grid, which is safe");

    test.m_Layer = plAiEqsThreatLayer::Threat;
    test.Run(ctx, items, 0);
    Check(plMath::IsEqual(items[0].m_fRaw, 0.75f, 0.001f), "A2: the threat layer reads the threat");

    test.m_Layer = plAiEqsThreatLayer::Visibility;
    test.Run(ctx, items, 0);
    Check(items[0].m_fRaw == 1.0f && items[1].m_fRaw == 0.0f, "A2: the visibility layer reads whether an enemy sees the position");

    test.m_Layer = plAiEqsThreatLayer::Danger;
    test.Run(ctx, items, 0);
    Check(items[1].m_fRaw > 0.9f && items[0].m_fRaw == 0.0f, "A2: the danger layer reads the areas");

    plAiEqsEvalContext noMap;
    plAiEqsItem item;
    test.Run(noMap, plArrayPtr<plAiEqsItem>(&item, 1), 0);
    Check(item.m_TestData == plAiEqsTestData::MissingContext, "A2: a querier whose team has no map uses the missing-data policy");
  }
} // namespace

void RunThreatTests()
{
  TestSnapshotGrid();
  TestThreatFromLineOfSight();
  TestDangerAreas();
  TestCoverQueryAvoidsDanger();
  TestEqsThreatMapTest();
}
