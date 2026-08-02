#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <AiPlugin/Tactical/Components/CoverPointComponent.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <RendererCore/Debug/DebugRenderer.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plAiCoverPointComponent, 2, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Quality", plAiCoverQuality, m_Quality)->AddAttributes(new plDefaultValueAttribute(plAiCoverQuality::High)),
    PL_MEMBER_PROPERTY("AutoProbe", m_bAutoProbe),
    PL_MEMBER_PROPERTY("Width", m_fWidth)->AddAttributes(new plClampValueAttribute(0.0f, 50.0f), new plSuffixAttribute(" m")),
    PL_MEMBER_PROPERTY("Height", m_fHeight)->AddAttributes(new plDefaultValueAttribute(2.0f), new plClampValueAttribute(0.1f, 5.0f), new plSuffixAttribute(" m")),
    PL_ACCESSOR_PROPERTY_READ_ONLY("VisSize", GetVisualizationSize)->AddAttributes(new plHiddenAttribute()),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("AI"),
    new plDirectionVisualizerAttribute(plBasisAxis::PositiveX, 0.5f, plColor::Cyan),
    // selected-state visualization: the authored cover extent as a wall slab, sitting on the
    // ground, pushed toward the wall side (+X)
    new plBoxVisualizerAttribute("VisSize", 1.0f, plColorScheme::LightUI(plColorScheme::Cyan), nullptr, plVisualizerAnchor::NegZ, plVec3(0.35f, 0, 0)),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

plAiCoverPointComponent::plAiCoverPointComponent()
{
  m_Quality = plAiCoverQuality::High; // the enum's Default is None, which every CoverQualityMin filter rejects
}

plAiCoverPointComponent::~plAiCoverPointComponent() = default;

void plAiCoverPointComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_Quality;
  s << m_bAutoProbe;
  s << m_fWidth;
  s << m_fHeight;
}

void plAiCoverPointComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& s = inout_stream.GetStream();

  s >> m_Quality;
  s >> m_bAutoProbe;

  if (uiVersion >= 2)
  {
    s >> m_fWidth;
    s >> m_fHeight;
  }
}

void plAiCoverPointComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  m_pTacticalModule = GetWorld()->GetOrCreateModule<plAiTacticalWorldModule>();
  m_ID = m_pTacticalModule->RegisterAuthoredCover(GetHandle());
}

void plAiCoverPointComponent::Update()
{
  // edit mode only - while simulating, AI.Tactical.VisualizeCover shows the REGISTERED points
  // (which is what agents actually use), so drawing here would just double up
  if (GetWorld()->GetWorldSimulationEnabled())
    return;

  plVec3 vWallDir = GetOwner()->GetGlobalDirForwards();
  vWallDir.z = 0.0f;
  vWallDir.NormalizeIfNotZero(plVec3(1, 0, 0)).IgnoreResult();

  const plVec3 vTangent(-vWallDir.y, vWallDir.x, 0);
  const plVec3 vCenter = GetOwner()->GetGlobalPosition();

  // the same strip layout MaintainAuthoredCover will register at simulation start
  float fSpacing = 1.0f;

  if (const auto* pNavMeshModule = GetWorld()->GetModuleReadOnly<plAiNavMeshWorldModule>())
  {
    fSpacing = plMath::Max(0.25f, pNavMeshModule->GetConfig().m_TacticalConfig.m_fCoverSpacing);
  }

  const float fWidth = plMath::Max(0.0f, m_fWidth);
  const plUInt32 uiPointCount = (fWidth < 0.01f) ? 1u : plMath::Clamp<plUInt32>(static_cast<plUInt32>(plMath::Round(fWidth / fSpacing)) + 1, 2u, 64u);

  const plColor color = (m_Quality == plAiCoverQuality::High) ? plColor::Cyan : plColor::LightSkyBlue;
  const float fHeight = plMath::Max(0.2f, m_fHeight);

  plHybridArray<plDebugRenderer::Line, 64> lines;
  plVec3 vPrev = plVec3::MakeZero();

  for (plUInt32 i = 0; i < uiPointCount; ++i)
  {
    const float fOffset = (uiPointCount == 1) ? 0.0f : (-fWidth * 0.5f + fWidth * static_cast<float>(i) / static_cast<float>(uiPointCount - 1));
    const plVec3 vPos = vCenter + vTangent * fOffset;

    // stand-position tick up to the authored wall top
    auto& tick = lines.ExpandAndGetRef();
    tick.m_start = vPos;
    tick.m_end = vPos + plVec3(0, 0, fHeight);
    tick.m_startColor = tick.m_endColor = color;

    // wall-facing dash
    auto& dash = lines.ExpandAndGetRef();
    dash.m_start = vPos + plVec3(0, 0, 0.2f);
    dash.m_end = dash.m_start + vWallDir * 0.35f;
    dash.m_startColor = dash.m_endColor = color;

    // wall band: ground + top rails to the previous point
    if (i > 0)
    {
      auto& ground = lines.ExpandAndGetRef();
      ground.m_start = vPrev + plVec3(0, 0, 0.05f);
      ground.m_end = vPos + plVec3(0, 0, 0.05f);
      ground.m_startColor = ground.m_endColor = color;

      auto& top = lines.ExpandAndGetRef();
      top.m_start = vPrev + plVec3(0, 0, fHeight);
      top.m_end = vPos + plVec3(0, 0, fHeight);
      top.m_startColor = top.m_endColor = color;
    }

    vPrev = vPos;
  }

  plDebugRenderer::DrawLines(GetWorld(), lines, plColor::White);
}

void plAiCoverPointComponent::OnDeactivated()
{
  if (m_pTacticalModule != nullptr && m_ID != plAiTacticalWorldModule::InvalidAuthoredCoverID)
  {
    m_pTacticalModule->UnregisterAuthoredCover(m_ID);
    m_ID = plAiTacticalWorldModule::InvalidAuthoredCoverID;
    m_pTacticalModule = nullptr;
  }

  SUPER::OnDeactivated();
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Components_CoverPointComponent);
