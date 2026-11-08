#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/NavMesh.h>
#include <RendererCore/Debug/DebugRenderer.h>

namespace
{
  plColor Rgb(plUInt8 r, plUInt8 g, plUInt8 b, plUInt8 a = 255)
  {
    return plColorGammaUB(r, g, b, a);
  }

  const plColor s_BorderColor = Rgb(0x4a, 0x4f, 0x57);
  const plColor s_HighlightColor = Rgb(0x4f, 0x8f, 0xe0);
  const plColor s_BuildingColor = Rgb(0xcd, 0x8c, 0x96);
  const plColor s_FailedColor = Rgb(0xd7, 0x82, 0x78);
  const plColor s_WarnColor = Rgb(0xdc, 0xc3, 0x6e);

  /// Fill color of a sector in a view; alpha 0 for a sector with no fill (queued).
  plColor GetFillColor(const plAiNavMeshSectorStatus& sector, plAiNavMeshVisualizeMode::Enum mode, plUInt32 uiPolyBudget)
  {
    switch (mode)
    {
      case plAiNavMeshVisualizeMode::SectorStatus:
        switch (sector.m_State)
        {
          case plAiNavMeshSectorState::Ready:
            return Rgb(0x3a, 0x40, 0x48, 178);
          case plAiNavMeshSectorState::Queued:
            return Rgb(0, 0, 0, 0);
          case plAiNavMeshSectorState::Building:
          case plAiNavMeshSectorState::Rebuilding:
            return plColor(s_BuildingColor.r, s_BuildingColor.g, s_BuildingColor.b, 0.35f);
          case plAiNavMeshSectorState::Failed:
            return plColor(s_FailedColor.r, s_FailedColor.g, s_FailedColor.b, 0.55f);
        }
        break;

      case plAiNavMeshVisualizeMode::RebuildReason:
        switch (sector.m_LastReason)
        {
          case plAiNavMeshRebuildReason::None:
            return Rgb(0x3a, 0x40, 0x48, 153);
          case plAiNavMeshRebuildReason::FirstBuild:
            return Rgb(0x4b, 0x50, 0x58, 153);
          case plAiNavMeshRebuildReason::Obstacle:
            return Rgb(0xcd, 0x8c, 0x96, 153);
          case plAiNavMeshRebuildReason::NavLink:
            return Rgb(0x6e, 0xb9, 0xd2, 153);
          case plAiNavMeshRebuildReason::Requested:
            return Rgb(0xdc, 0xc3, 0x6e, 153);
          case plAiNavMeshRebuildReason::Changed:
            return Rgb(0x91, 0x96, 0xa0, 153);
        }
        break;

      case plAiNavMeshVisualizeMode::PolygonDensity:
      {
        const float t = uiPolyBudget > 0 ? plMath::Min(1.0f, static_cast<float>(sector.m_uiPolyCount) / uiPolyBudget) : 0.0f;
        const plColor from = Rgb(0x3a, 0x40, 0x48);
        const plColor to = Rgb(0xdc, 0xc3, 0x6e);
        plColor color = plMath::Lerp(from, to, t);
        color.a = 0.75f;
        return color;
      }

      default:
        break;
    }

    return plColor::MakeZero();
  }

  void AddQuad(plDynamicArray<plDebugRenderer::Triangle>& ref_triangles, const plVec3& vMin, const plVec3& vMax, const plColor& color)
  {
    const plVec3 p00(vMin.x, vMin.y, vMin.z);
    const plVec3 p10(vMax.x, vMin.y, vMin.z);
    const plVec3 p11(vMax.x, vMax.y, vMin.z);
    const plVec3 p01(vMin.x, vMax.y, vMin.z);

    auto addTriangle = [&](const plVec3& a, const plVec3& b, const plVec3& c) {
      plDebugRenderer::Triangle& tri = ref_triangles.ExpandAndGetRef();
      tri.m_position[0] = a;
      tri.m_position[1] = b;
      tri.m_position[2] = c;
      tri.m_color = color;
    };

    // both windings, so the sector shows from above and below
    addTriangle(p00, p10, p11);
    addTriangle(p00, p11, p01);
    addTriangle(p00, p11, p10);
    addTriangle(p00, p01, p11);
  }

  void AddLine(plDynamicArray<plDebugRenderer::Line>& ref_lines, const plVec3& vStart, const plVec3& vEnd, const plColor& color)
  {
    plDebugRenderer::Line& line = ref_lines.ExpandAndGetRef();
    line.m_start = vStart;
    line.m_end = vEnd;
    line.m_startColor = color;
    line.m_endColor = color;
  }

  /// A dashed line along one axis: a sector that waits for a build slot.
  void AddDashedLine(plDynamicArray<plDebugRenderer::Line>& ref_lines, const plVec3& vStart, const plVec3& vEnd, const plColor& color)
  {
    constexpr float fDash = 1.0f;
    constexpr float fGap = 0.75f;

    const float fLength = (vEnd - vStart).GetLength();
    const plVec3 vDir = (vEnd - vStart) / plMath::Max(fLength, 0.001f);

    for (float f = 0.0f; f < fLength; f += fDash + fGap)
    {
      AddLine(ref_lines, vStart + vDir * f, vStart + vDir * plMath::Min(f + fDash, fLength), color);
    }
  }

  void AddRectOutline(plDynamicArray<plDebugRenderer::Line>& ref_lines, const plVec3& vMin, const plVec3& vMax, const plColor& color, bool bDashed)
  {
    const plVec3 p00(vMin.x, vMin.y, vMin.z);
    const plVec3 p10(vMax.x, vMin.y, vMin.z);
    const plVec3 p11(vMax.x, vMax.y, vMin.z);
    const plVec3 p01(vMin.x, vMax.y, vMin.z);

    if (bDashed)
    {
      AddDashedLine(ref_lines, p00, p10, color);
      AddDashedLine(ref_lines, p10, p11, color);
      AddDashedLine(ref_lines, p11, p01, color);
      AddDashedLine(ref_lines, p01, p00, color);
    }
    else
    {
      AddLine(ref_lines, p00, p10, color);
      AddLine(ref_lines, p10, p11, color);
      AddLine(ref_lines, p11, p01, color);
      AddLine(ref_lines, p01, p00, color);
    }
  }
} // namespace

void plAiNavMesh::DebugDrawSectors(plDebugRendererContext context, plAiNavMeshVisualizeMode::Enum mode, plUInt32 uiPolyBudget, plVec2I32 vHighlight) const
{
  plDynamicArray<plAiNavMeshSectorStatus> sectors;
  GetSectorStatus(sectors);

  if (sectors.IsEmpty())
    return;

  // sectors without a tile (not built yet, failed) sit at the height of the built ones
  double fHeightSum = 0.0;
  plUInt32 uiWithBounds = 0;

  for (const plAiNavMeshSectorStatus& sector : sectors)
  {
    if (sector.m_bHasBounds)
    {
      fHeightSum += sector.m_fMinZ;
      ++uiWithBounds;
    }
  }

  const float fFallbackZ = uiWithBounds > 0 ? static_cast<float>(fHeightSum / uiWithBounds) : 0.0f;

  plDynamicArray<plDebugRenderer::Triangle> triangles;
  plDynamicArray<plDebugRenderer::Line> lines;
  plDynamicArray<plDebugRenderer::Line> highlightLines;

  constexpr float fLift = 0.1f; // above the floor, so the sector does not z-fight with it

  for (const plAiNavMeshSectorStatus& sector : sectors)
  {
    const plVec2 vOrigin = GetSectorPositionOffset(sector.m_vCoord);
    const float fZ = (sector.m_bHasBounds ? sector.m_fMinZ : fFallbackZ) + fLift;
    const plVec3 vMin(vOrigin.x, vOrigin.y, fZ);
    const plVec3 vMax(vOrigin.x + m_fSectorMetersXY, vOrigin.y + m_fSectorMetersXY, fZ);

    const plColor fill = GetFillColor(sector, mode, uiPolyBudget);
    if (fill.a > 0.0f)
    {
      AddQuad(triangles, vMin, vMax, fill);
    }

    const bool bQueued = sector.m_State == plAiNavMeshSectorState::Queued;
    AddRectOutline(lines, vMin, vMax, bQueued ? s_BuildingColor : s_BorderColor, bQueued);

    if (mode == plAiNavMeshVisualizeMode::SectorStatus)
    {
      if (sector.m_bBuildRunning)
      {
        // the bar of a running build, along the sector's low edge
        constexpr float fInset = 3.0f;
        constexpr float fBarHeight = 1.0f;
        const float fWidth = (m_fSectorMetersXY - 2.0f * fInset) * sector.m_fBuildProgress;
        const plVec3 vBarMin(vMin.x + fInset, vMin.y + fInset, fZ + 0.02f);
        AddQuad(triangles, vBarMin, plVec3(vBarMin.x + fWidth, vBarMin.y + fBarHeight, vBarMin.z), s_BuildingColor);
      }

      if (sector.m_State == plAiNavMeshSectorState::Failed)
      {
        constexpr float fInset = 3.0f;
        AddLine(lines, plVec3(vMin.x + fInset, vMin.y + fInset, fZ), plVec3(vMax.x - fInset, vMax.y - fInset, fZ), s_FailedColor);
        AddLine(lines, plVec3(vMax.x - fInset, vMin.y + fInset, fZ), plVec3(vMin.x + fInset, vMax.y - fInset, fZ), s_FailedColor);
      }
    }

    if (mode == plAiNavMeshVisualizeMode::PolygonDensity && IsNearPolyBudget(sector.m_uiPolyCount, uiPolyBudget))
    {
      plStringBuilder sWarning;
      sWarning.SetFormat("! {}", sector.m_uiPolyCount);
      plDebugRenderer::Draw3DText(context, sWarning, plVec3(vMin.x + m_fSectorMetersXY * 0.5f, vMin.y + m_fSectorMetersXY * 0.5f, fZ + 0.2f), s_WarnColor, 14);
    }

    if (sector.m_vCoord == vHighlight)
    {
      // an outline inside the border, so it is bold without leaving the sector
      for (float fOffset : {0.0f, 0.15f, 0.3f})
      {
        AddRectOutline(highlightLines, plVec3(vMin.x + fOffset, vMin.y + fOffset, fZ + 0.03f), plVec3(vMax.x - fOffset, vMax.y - fOffset, fZ + 0.03f), s_HighlightColor, false);
      }
    }
  }

  if (!triangles.IsEmpty())
  {
    plDebugRenderer::DrawSolidTriangles(context, triangles, plColor::White);
  }

  plDebugRenderer::DrawLines(context, lines, plColor::White);

  if (!highlightLines.IsEmpty())
  {
    plDebugRenderer::DrawLines(context, highlightLines, plColor::White);
  }
}
