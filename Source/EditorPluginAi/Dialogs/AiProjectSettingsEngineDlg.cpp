#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEngineView.h>
#include <EditorPluginAi/Dialogs/AiProjectSettingsEngineDlg.h>
#include <EditorUi/Menu.h>
#include <EditorUi/Surface.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GuiFoundation/EngineUi/EnginePrompt.h>
#include <GuiFoundation/UIServices/DynamicEnums.h>

void UpdateGroundTypeDynamicEnumValues();

namespace
{
  const char* s_szSettingsOk = "Ok";
  const char* s_szSettingsCancel = "Cancel";
  const char* s_szSettingsReset = "Reset";

  constexpr float s_fSettingsWidth = 760.0f;
  constexpr float s_fTabStripHeight = 30.0f;
  constexpr float s_fPageHeight = 470.0f;
  constexpr float s_fPagePadX = 14.0f;
  constexpr float s_fPagePadY = 12.0f;
  constexpr float s_fFormLabelWidth = 212.0f; ///< the mockup's 200 px label column and its 12 px gap
  constexpr float s_fConfigDropdownWidth = 200.0f;
  constexpr float s_fGroundTypeListHeight = 360.0f;
  constexpr float s_fRowStep = plEditorUi::ControlHeight + plEditorUiForm::RowGap;
  constexpr plUInt32 s_uiNavmeshRows = 16; ///< Collision Layer and 15 numbers

  /// The Path Search columns: Ground Type takes what Traversal Allowed and Traversal Cost leave. Each row has a cost
  /// well of 90 x 20 at the right, as in the mockup.
  constexpr float s_fAllowedColumnWidth = 160.0f;
  constexpr float s_fCostColumnWidth = 130.0f;
  constexpr float s_fCellPadding = 8.0f;
  constexpr float s_fTableHeaderHeight = 26.0f;
  constexpr float s_fCostWellWidth = 90.0f;
  constexpr float s_fCostWellHeight = 20.0f;

  /// Where a ground type row's check box is, from the row's left edge (the tree's icon slot).
  constexpr float s_fCheckBoxLeft = 16.0f;
  constexpr float s_fCheckBoxRight = 40.0f;

  /// The largest sector size: no navmesh distance or factor is meaningful beyond one sector, and path costs share it.
  constexpr double s_fNavmeshMax = 1024.0;

  const char* s_szCheckBoxOn = ":/AiPlugin/CheckBoxOn.svg";
  const char* s_szCheckBoxOff = ":/AiPlugin/CheckBoxOff.svg";
  const char* s_szAiKindIcon = ":/AiPlugin/AiKind.svg";
  const char* s_szFirstNavmesh = "<first navmesh>";

  const char* s_szGroundTypeHint = "Check a ground type to use it; double-click or F2 renames it. <None> and <Default> are fixed.";
  const char* s_szCollisionLayerTip = "From the Jolt collision layers; empty without the Jolt package";
  const char* s_szCoverNavmeshTip = "Which navmesh cover points are generated for. Probing uses this navmesh's collision layer.";

  const char* s_szNavConfig = "Ai.Nav.Config";
  const char* s_szNavAdd = "Ai.Nav.Add";
  const char* s_szNavRemove = "Ai.Nav.Remove";
  const char* s_szNavEmptyAdd = "Ai.Nav.EmptyAdd";
  const char* s_szNavLayer = "Ai.Nav.Layer";
  const char* s_szPathConfig = "Ai.Path.Config";
  const char* s_szPathAdd = "Ai.Path.Add";
  const char* s_szPathRemove = "Ai.Path.Remove";
  const char* s_szPathEmptyAdd = "Ai.Path.EmptyAdd";
  const char* s_szPathAllowed = "Ai.Path.Allowed."; ///< followed by the ground type index
  const char* s_szCoverNavmesh = "Ai.Tac.Navmesh";
  const char* s_szNavmeshPage = "Ai.Page.Navmesh";
  const char* s_szPathSearchPage = "Ai.Page.PathSearch";

  template <typename CONFIG>
  plInt32 FindConfig(const plDynamicArray<CONFIG>& configs, plStringView sName)
  {
    for (plUInt32 i = 0; i < configs.GetCount(); ++i)
    {
      if (configs[i].m_sName == sName)
        return static_cast<plInt32>(i);
    }

    return -1;
  }

  template <typename CONFIG>
  void SortConfigs(plDynamicArray<CONFIG>& ref_configs)
  {
    ref_configs.Sort([](const CONFIG& lhs, const CONFIG& rhs) -> bool
      { return lhs.m_sName < rhs.m_sName; });
  }

  plString FormatCount(plUInt32 uiCount)
  {
    plStringBuilder sText;
    sText.SetFormat("{}", uiCount);
    return sText;
  }

  /// True when the text is exactly one decimal number: an optional sign, digits with an optional fraction (or a
  /// fraction alone) and an optional exponent. "7abc", "0.5.5", "12m" and "." are not numbers.
  bool IsDecimalNumber(plStringView sText)
  {
    const char* szPos = sText.GetStartPointer();
    const char* szEnd = sText.GetEndPointer();

    auto skipSign = [&]()
    {
      if (szPos < szEnd && (*szPos == '+' || *szPos == '-'))
        ++szPos;
    };

    auto skipDigits = [&]() -> bool
    {
      const char* szStart = szPos;
      while (szPos < szEnd && *szPos >= '0' && *szPos <= '9')
        ++szPos;

      return szPos != szStart;
    };

    skipSign();
    bool bDigits = skipDigits();

    if (szPos < szEnd && *szPos == '.')
    {
      ++szPos;
      bDigits = skipDigits() || bDigits;
    }

    if (!bDigits)
      return false;

    if (szPos < szEnd && (*szPos == 'e' || *szPos == 'E'))
    {
      ++szPos;
      skipSign();

      if (!skipDigits())
        return false;
    }

    return szPos == szEnd;
  }

  /// Parses a number field's text; false unless the text without surrounding blanks is exactly one number.
  bool ParseNumberText(plStringView sText, double& out_fValue)
  {
    plStringBuilder sTrimmed(sText);
    sTrimmed.Trim(" \t");

    return IsDecimalNumber(sTrimmed) && plConversionUtils::StringToFloat(sTrimmed, out_fValue).Succeeded();
  }

  /// The ground types the Path Search page lists: <Default> and the used ones, as the Ground Types tab has them now.
  void GetPathSearchRows(const plAiNavigationConfig& config, plHybridArray<plUInt32, plAiNumGroundTypes>& out_rows)
  {
    out_rows.Clear();

    for (plUInt32 i = 1; i < plAiNumGroundTypes; ++i)
    {
      if (i == 1 || config.m_GroundTypes[i].m_bUsed)
        out_rows.PushBack(i);
    }
  }

  /// A vertical scroll view filling rect; its one child panel holds the content and sets the content height.
  plUiScrollView* AddScrollView(plUiElement* pParent, const char* szName, const plRectFloat& rect)
  {
    plUiScrollView* pScroll = PL_DEFAULT_NEW(plUiScrollView);
    pParent->m_Children.PushBack(pScroll);
    pScroll->m_sName = szName;
    plEditorUi::SetRect(pScroll, rect);
    pScroll->m_Direction = plUiStackDirection::Vertical;
    pScroll->m_Alignment = plUiStackAlignment::Stretch;
    pScroll->m_Color = plColorGammaUB(0, 0, 0, 0);
    pScroll->m_bClipChildren = true;

    return pScroll;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// Ground types: the 32 slots, a check box per custom slot, F2 or a double-click renames

class plAiProjectSettingsEngineDlg::GroundTypeAdapter : public plEditorUiTreeAdapter
{
public:
  GroundTypeAdapter(plAiProjectSettingsEngineDlg& ref_dialog)
    : m_Dialog(ref_dialog)
  {
  }

  static plUInt64 ToItem(plUInt32 uiIndex) { return static_cast<plUInt64>(uiIndex) + 1; }
  static plUInt32 ToIndex(plUInt64 uiItem) { return static_cast<plUInt32>(uiItem - 1); }

  /// <None> and <Default> cannot be renamed or switched off.
  static bool IsFixed(plUInt32 uiIndex) { return uiIndex <= 1; }

  void GetChildren(plUInt64 uiParent, plDynamicArray<plUInt64>& out_children) const override
  {
    if (uiParent != 0)
      return;

    for (plUInt32 i = 0; i < plAiNumGroundTypes; ++i)
      out_children.PushBack(ToItem(i));
  }

  void GetRow(plUInt64 uiItem, plEditorUiTreeRow& out_row) const override
  {
    const plUInt32 uiIndex = ToIndex(uiItem);
    const plAiNavigationConfig::GroundType& groundType = m_Dialog.m_Config.m_GroundTypes[uiIndex];

    out_row.m_sText = groundType.m_sName;
    out_row.m_sCount = FormatCount(uiIndex);

    if (IsFixed(uiIndex))
    {
      out_row.m_TextStyle = plEditorUiTreeTextStyle::Dimmed;
      return;
    }

    out_row.m_TextStyle = groundType.m_bUsed ? plEditorUiTreeTextStyle::Normal : plEditorUiTreeTextStyle::Muted;
    out_row.m_sIcon = groundType.m_bUsed ? s_szCheckBoxOn : s_szCheckBoxOff;
    out_row.m_IconColor = groundType.m_bUsed ? plEditorUi::Accent() : plEditorUi::Border();
  }

  plUInt64 GetParent(plUInt64 uiItem) const override
  {
    PL_IGNORE_UNUSED(uiItem);
    return 0;
  }

  bool HasDecorations() const override { return true; }

  bool CanRename(plUInt64 uiItem) const override { return !IsFixed(ToIndex(uiItem)); }

  plStatus Rename(plUInt64 uiItem, plStringView sText) override
  {
    m_Dialog.m_Config.m_GroundTypes[ToIndex(uiItem)].m_sName = sText;
    return plStatus(PL_SUCCESS);
  }

  /// A double-click renames (Enter presses OK before the tree sees it). A double-click on the check box toggles it
  /// twice and does not rename.
  void Activate(plUInt64 uiItem) override
  {
    if (!m_Dialog.m_bToggledOnPress)
      m_Dialog.m_pGroundTypes->BeginRename(uiItem);
  }

private:
  plAiProjectSettingsEngineDlg& m_Dialog;
};

//////////////////////////////////////////////////////////////////////////
// plAiProjectSettingsEngineDlg

plAiProjectSettingsEngineDlg::plAiProjectSettingsEngineDlg()
  : plEngineDialog("AiProjectSettings", "Ai Project Settings")
{
  SetIcon(":/AiPlugin/plAiPlugin.svg");
  EnsureConfigFileExists();

  for (auto it : plDynamicEnum::GetDynamicEnum("PhysicsCollisionLayer").GetAllValidValues())
    m_CollisionLayers.Insert(it.Key(), it.Value());

  LoadConfig();

  if (!IsReady())
    return;

  plEditorUiDialogButton buttons[3];
  buttons[0].m_sId = s_szSettingsReset;
  buttons[0].m_sLabel = "Reset";
  buttons[0].m_Role = plEditorUiDialogButton::Role::Left;
  buttons[0].m_sToolTip = "Reload from the saved file (not factory defaults)";
  buttons[1].m_sId = s_szSettingsOk;
  buttons[1].m_sLabel = "OK";
  buttons[1].m_Role = plEditorUiDialogButton::Role::Accept;
  buttons[2].m_sId = s_szSettingsCancel;
  buttons[2].m_sLabel = "Cancel";
  buttons[2].m_Role = plEditorUiDialogButton::Role::Reject;
  GetHost().SetButtons(buttons);
  GetHost().SetFooterHint("RuntimeConfigs/AiPluginConfig.cfg");

  plEditorUiSurface& surface = *GetSurface();

  // subscribed before the tree exists, so Space toggles a check box before the tree sees the key
  m_KeySubscription = surface.m_KeyEvents.AddEventHandler(plMakeDelegate(&plAiProjectSettingsEngineDlg::OnKey, this));
  m_PointerSubscription = surface.m_PointerEvents.AddEventHandler(plMakeDelegate(&plAiProjectSettingsEngineDlg::OnPointer, this));

  m_pTabs = PL_DEFAULT_NEW(plEditorUiTabs, surface, "Ai.Tabs");
  m_TabSubscription = m_pTabs->m_CurrentChanged.AddEventHandler(plMakeDelegate(&plAiProjectSettingsEngineDlg::OnTabChanged, this));

  m_pGroundTypeAdapter = PL_DEFAULT_NEW(GroundTypeAdapter, *this);
  m_pGroundTypes = PL_DEFAULT_NEW(plEditorUiTree, surface, "Ai.GroundTypes", *m_pGroundTypeAdapter);

  UpdateTabs();
  ShowTab(Tab::GroundTypes);
}

plAiProjectSettingsEngineDlg::~plAiProjectSettingsEngineDlg()
{
  if (!IsReady())
    return;

  m_pTabs->m_CurrentChanged.RemoveEventHandler(m_TabSubscription);
  GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);
  GetSurface()->m_KeyEvents.RemoveEventHandler(m_KeySubscription);
}

bool plAiProjectSettingsEngineDlg::Open()
{
  if (!plEngineDialog::IsEngineEnabled())
    return false;

  plAiProjectSettingsEngineDlg dlg;
  return dlg.Exec() == plEngineDialog::Accepted;
}

void plAiProjectSettingsEngineDlg::EnsureConfigFileExists()
{
  plAiNavigationConfig cfg;

  if (cfg.Load().Failed())
  {
    cfg.Save().IgnoreResult();
  }
}

plVec2 plAiProjectSettingsEngineDlg::GetPreferredSize() const
{
  return plVec2(s_fSettingsWidth, s_fTabStripHeight + 1.0f + s_fPageHeight + plEditorUiDialogHost::FooterHeight);
}

// Data

void plAiProjectSettingsEngineDlg::LoadConfig()
{
  // a fresh config, so nothing of the edited one survives a Reset
  m_Config = plAiNavigationConfig();
  m_Config.Load().IgnoreResult();

  // a cover navmesh that is not configured means the first navmesh
  plString& sCoverNavmesh = m_Config.m_TacticalConfig.m_sCoverNavmesh;
  if (!sCoverNavmesh.IsEmpty() && FindConfig(m_Config.m_NavmeshConfigs, sCoverNavmesh) < 0)
    sCoverNavmesh.Clear();

  m_iNavmesh = m_Config.m_NavmeshConfigs.IsEmpty() ? -1 : 0;
  m_iPathSearch = m_Config.m_PathSearchConfigs.IsEmpty() ? -1 : 0;
}

void plAiProjectSettingsEngineDlg::SaveConfig()
{
  m_Config.Save().IgnoreResult();
  UpdateGroundTypeDynamicEnumValues();
}

void plAiProjectSettingsEngineDlg::SelectNavmesh(plInt32 iIndex)
{
  m_iNavmesh = iIndex;

  if (m_Tab == Tab::Navmesh)
  {
    CollectFields();
    WriteFields();
  }

  Rebuild();
}

void plAiProjectSettingsEngineDlg::SelectPathSearch(plInt32 iIndex)
{
  m_iPathSearch = iIndex;

  if (m_Tab == Tab::PathSearch)
  {
    CollectFields();
    WriteFields();
  }

  Rebuild();
}

plUInt32 plAiProjectSettingsEngineDlg::CountUsedGroundTypes() const
{
  plUInt32 uiCount = 0;
  for (plUInt32 i = 2; i < plAiNumGroundTypes; ++i)
  {
    if (m_Config.m_GroundTypes[i].m_bUsed)
      ++uiCount;
  }

  return uiCount;
}

void plAiProjectSettingsEngineDlg::ToggleGroundType(plUInt32 uiIndex)
{
  if (GroundTypeAdapter::IsFixed(uiIndex))
    return;

  bool& bUsed = m_Config.m_GroundTypes[uiIndex].m_bUsed;
  bUsed = !bUsed;

  m_pGroundTypes->Refresh();
  UpdateTabs();
  Rebuild();
}

void plAiProjectSettingsEngineDlg::ToggleTraversalAllowed(plUInt32 uiGroundType)
{
  if (m_iPathSearch < 0 || uiGroundType >= plAiNumGroundTypes)
    return;

  bool& bAllowed = m_Config.m_PathSearchConfigs[m_iPathSearch].m_bGroundTypeAllowed[uiGroundType];
  bAllowed = !bAllowed;

  Rebuild();
}

// Number fields

void plAiProjectSettingsEngineDlg::AddField(plStringView sKey, const char* szLabel, NumberType type, void* pValue, double fMin, double fMax, const char* szUnit, float fWidth, const char* szToolTip)
{
  NumberSetting& field = m_Fields.ExpandAndGetRef();
  field.m_sKey = sKey;
  field.m_szLabel = szLabel;
  field.m_szUnit = szUnit;
  field.m_szToolTip = szToolTip;
  field.m_Type = type;
  field.m_pValue = pValue;
  field.m_fMin = fMin;
  field.m_fMax = fMax;
  field.m_fWidth = fWidth;
}

void plAiProjectSettingsEngineDlg::ClearFields()
{
  m_Fields.Clear();
  m_PathRows.Clear();
}

void plAiProjectSettingsEngineDlg::CollectFields()
{
  ClearFields();

  const char* szMeters = "m";
  const char* szDegree = "\xC2\xB0";

  if (m_Tab == Tab::Navmesh && m_iNavmesh >= 0)
  {
    plAiNavmeshConfig& nav = m_Config.m_NavmeshConfigs[m_iNavmesh];

    AddField("NumSectorsX", "Num Sectors X", NumberType::UInt16, &nav.m_uiNumSectorsX, 16, 256, "", 90.0f);
    AddField("NumSectorsY", "Num Sectors Y", NumberType::UInt16, &nav.m_uiNumSectorsY, 16, 256, "", 90.0f);
    AddField("SectorSize", "Sector Size in Meters (XY)", NumberType::Float, &nav.m_fSectorSize, 8, 1024, szMeters);
    // the navmesh generator divides by the cell size and height
    AddField("CellSize", "Cell Size (XY)", NumberType::Float, &nav.m_fCellSize, 0.01, s_fNavmeshMax, szMeters);
    AddField("CellHeight", "Cell Height (Z)", NumberType::Float, &nav.m_fCellHeight, 0.01, s_fNavmeshMax, szMeters);
    AddField("AgentRadius", "Agent Radius", NumberType::Float, &nav.m_fAgentRadius, 0, s_fNavmeshMax, szMeters);
    AddField("AgentHeight", "Agent Height", NumberType::Float, &nav.m_fAgentHeight, 0, s_fNavmeshMax, szMeters);
    AddField("AgentStep", "Agent Step Height", NumberType::Float, &nav.m_fAgentStepHeight, 0, s_fNavmeshMax, szMeters);
    AddField("AgentSlope", "Agent Walkable Slope", NumberType::Degree, &nav.m_WalkableSlope, 0, 90, szDegree);
    AddField("MaxEdgeLength", "Max Edge Length", NumberType::Float, &nav.m_fMaxEdgeLength, 0, s_fNavmeshMax, szMeters);
    AddField("SimplificationError", "Max Simplification Error", NumberType::Float, &nav.m_fMaxSimplificationError, 0, s_fNavmeshMax);
    AddField("MinRegionSize", "Min Region Size", NumberType::Float, &nav.m_fMinRegionSize, 0, s_fNavmeshMax);
    AddField("RegionMergeSize", "Region Merge Size", NumberType::Float, &nav.m_fRegionMergeSize, 0, s_fNavmeshMax);
    AddField("DetailSampleDistance", "Detail Sample Distance", NumberType::Float, &nav.m_fDetailMeshSampleDistanceFactor, 0, s_fNavmeshMax);
    AddField("DetailSampleError", "Detail Sample Error", NumberType::Float, &nav.m_fDetailMeshSampleErrorFactor, 0, s_fNavmeshMax);
  }
  else if (m_Tab == Tab::PathSearch && m_iPathSearch >= 0)
  {
    plAiPathSearchConfig& path = m_Config.m_PathSearchConfigs[m_iPathSearch];

    GetPathSearchRows(m_Config, m_PathRows);

    for (plUInt32 uiGroundType : m_PathRows)
    {
      plStringBuilder sKey;
      sKey.SetFormat("Cost{}", uiGroundType);
      AddField(sKey, "Traversal Cost", NumberType::Float, &path.m_fGroundTypeCost[uiGroundType], 0, s_fNavmeshMax, "", s_fCostWellWidth);
    }
  }
  else if (m_Tab == Tab::Tactical)
  {
    plAiTacticalConfig& tac = m_Config.m_TacticalConfig;

    AddField("CoverSpacing", "Cover Point Spacing", NumberType::Float, &tac.m_fCoverSpacing, 0.25, 10, szMeters);
    AddField("ProbeDistance", "Wall Probe Distance", NumberType::Float, &tac.m_fCoverProbeDistance, 0.1, 5, szMeters);
    AddField("CrouchHeight", "Crouch Cover Height", NumberType::Float, &tac.m_fCoverCrouchHeight, 0.1, 3, szMeters);
    AddField("StandHeight", "Stand Cover Height", NumberType::Float, &tac.m_fCoverStandHeight, 0.2, 3, szMeters);
    AddField("MaxCoverPoints", "Max Cover Points Per Sector", NumberType::UInt16, &tac.m_uiMaxCoverPointsPerSector, 1, 1024);
  }
  else if (m_Tab == Tab::Squads)
  {
    plAiSquadConfig& squad = m_Config.m_SquadConfig;

    AddField("AttackTokens", "Attack Tokens", NumberType::UInt8, &squad.m_uiAttackTokens, 0, 16, "", 110.0f, "How many squad members may actively fire at the same time.");
    AddField("ShareConfidence", "Share Confidence", NumberType::Float, &squad.m_fShareConfidence, 0.05, 1, "", 110.0f, "A member's target confidence needed before its target is shared squad-wide.");
    AddField("ShareStrength", "Share Strength", NumberType::Float, &squad.m_fShareStrength, 0.05, 1, "", 110.0f, "Confidence squadmates receive from a shared target (below 1, so shared knowledge stays weaker than first-hand sight).");
    AddField("ShareInterval", "Share Interval", NumberType::Float, &squad.m_fShareInterval, 0.05, 5, "s", 110.0f, "Minimum seconds between target shares per squad.");
  }
}

void plAiProjectSettingsEngineDlg::WriteFields()
{
  plBlackboard& board = GetSurface()->GetBlackboard();

  for (NumberSetting& field : m_Fields)
  {
    field.m_sShownText = FormatValue(field, GetValue(field));
    field.m_bInvalid = false;

    const plStringBuilder sBinding("Ai.F.", field.m_sKey, ".Text");
    GetSurface()->GetCanvas().SetTextBinding(sBinding, field.m_sShownText);
    board.SetEntryValue(sBinding, field.m_sShownText);
  }
}

plString plAiProjectSettingsEngineDlg::GetFieldText(const NumberSetting& field) const
{
  const plStringBuilder sBinding("Ai.F.", field.m_sKey, ".Text");
  const plVariant value = GetSurface()->GetBlackboard().GetEntryValue(plTempHashedString(sBinding));
  return value.IsValid() ? value.ConvertTo<plString>() : field.m_sShownText;
}

void plAiProjectSettingsEngineDlg::ReadFields()
{
  bool bValidityChanged = false;

  for (NumberSetting& field : m_Fields)
  {
    const plString sText = GetFieldText(field);
    if (sText == field.m_sShownText)
      continue;

    field.m_sShownText = sText;

    // text that is not a number or a value out of range is not applied: the stored value stays until the text is valid
    const bool bWhole = field.m_Type == NumberType::UInt8 || field.m_Type == NumberType::UInt16;
    double fValue = 0.0;
    const bool bValid = ParseNumberText(sText, fValue) && fValue >= field.m_fMin && fValue <= field.m_fMax && (!bWhole || plMath::Floor(fValue) == fValue);

    if (bValid)
      SetValue(field, fValue);

    if (field.m_bInvalid == bValid)
    {
      field.m_bInvalid = !bValid;
      bValidityChanged = true;
    }
  }

  // the field's border shows its state
  if (bValidityChanged)
    Rebuild();
}

double plAiProjectSettingsEngineDlg::GetValue(const NumberSetting& field)
{
  switch (field.m_Type)
  {
    case NumberType::Float:
      return *static_cast<const float*>(field.m_pValue);
    case NumberType::Degree:
      return static_cast<const plAngle*>(field.m_pValue)->GetDegree();
    case NumberType::UInt8:
      return *static_cast<const plUInt8*>(field.m_pValue);
    case NumberType::UInt16:
      return *static_cast<const plUInt16*>(field.m_pValue);
  }

  return 0.0;
}

void plAiProjectSettingsEngineDlg::SetValue(const NumberSetting& field, double fValue)
{
  switch (field.m_Type)
  {
    case NumberType::Float:
      *static_cast<float*>(field.m_pValue) = static_cast<float>(fValue);
      break;
    case NumberType::Degree:
      *static_cast<plAngle*>(field.m_pValue) = plAngle::MakeFromDegree(static_cast<float>(fValue));
      break;
    case NumberType::UInt8:
      *static_cast<plUInt8*>(field.m_pValue) = static_cast<plUInt8>(fValue);
      break;
    case NumberType::UInt16:
      *static_cast<plUInt16*>(field.m_pValue) = static_cast<plUInt16>(fValue);
      break;
  }
}

plString plAiProjectSettingsEngineDlg::FormatValue(const NumberSetting& field, double fValue)
{
  if (field.m_Type == NumberType::UInt8 || field.m_Type == NumberType::UInt16)
    return FormatCount(static_cast<plUInt32>(fValue));

  return plAiEngineUi::FormatNumber(fValue, 2);
}

plString plAiProjectSettingsEngineDlg::GetRangeMessage(plStringView sLabel, double fMin, double fMax, bool bWhole)
{
  plStringBuilder sMessage;

  if (bWhole)
    sMessage.SetFormat("{}: enter a whole number from {} to {}.", sLabel, static_cast<plInt32>(fMin), static_cast<plInt32>(fMax));
  else
    sMessage.SetFormat("{}: enter a number from {} to {}.", sLabel, plArgF(fMin, 2), plArgF(fMax, 2));

  return sMessage;
}

void plAiProjectSettingsEngineDlg::UpdateValidation()
{
  plString sMessage;

  for (const NumberSetting& field : m_Fields)
  {
    if (field.m_bInvalid)
    {
      sMessage = GetRangeMessage(field.m_szLabel, field.m_fMin, field.m_fMax, field.m_Type == NumberType::UInt8 || field.m_Type == NumberType::UInt16);
      break;
    }
  }

  GetHost().SetValidation(plEditorUiSeverity::Error, sMessage);
  GetHost().SetButtonEnabled(s_szSettingsOk, sMessage.IsEmpty());
}

void plAiProjectSettingsEngineDlg::OnUiUpdate()
{
  ReadFields();
  UpdateValidation();
}

// Document

void plAiProjectSettingsEngineDlg::UpdateTabs()
{
  plEditorUiTab tabs[5];
  tabs[0].m_sLabel = "Ground Types";
  tabs[0].m_sCount = FormatCount(CountUsedGroundTypes());
  tabs[1].m_sLabel = "Navmesh";
  tabs[1].m_sCount = FormatCount(m_Config.m_NavmeshConfigs.GetCount());
  tabs[2].m_sLabel = "Path Search";
  tabs[2].m_sCount = FormatCount(m_Config.m_PathSearchConfigs.GetCount());
  tabs[3].m_sLabel = "Tactical";
  tabs[4].m_sLabel = "Squads";

  m_pTabs->SetTabs(tabs);
}

void plAiProjectSettingsEngineDlg::ShowTab(Tab tab)
{
  m_Tab = tab;
  m_pTabs->SetCurrent(static_cast<plUInt32>(tab));

  // the Path Search rows follow the ground types as they are now
  CollectFields();
  WriteFields();
  Rebuild();
}

void plAiProjectSettingsEngineDlg::OnTabChanged(plUInt32 uiTab)
{
  if (static_cast<Tab>(uiTab) != m_Tab)
    ShowTab(static_cast<Tab>(uiTab));
}

void plAiProjectSettingsEngineDlg::BuildBody(plUiElement* pBody, const plVec2& vSize)
{
  using namespace plEditorUi;

  m_DropdownAnchors.Clear();

  BuildTabRow(pBody, vSize.x);

  const float fTop = s_fTabStripHeight + 1.0f;
  m_PageRect = plRectFloat(0, fTop, vSize.x, plMath::Max(vSize.y - fTop, 1.0f));

  plUiPanel* pPage = Panel(pBody);
  SetRect(pPage, m_PageRect);
  pPage->m_Color = plColorGammaUB(0, 0, 0, 0);
  pPage->m_bClipChildren = true;

  const plVec2 vPage(m_PageRect.width, m_PageRect.height);

  switch (m_Tab)
  {
    case Tab::GroundTypes:
      BuildGroundTypes(pPage, vPage);
      break;
    case Tab::Navmesh:
      BuildNavmesh(pPage, vPage);
      break;
    case Tab::PathSearch:
      BuildPathSearch(pPage, vPage);
      break;
    case Tab::Tactical:
      BuildTactical(pPage, vPage);
      break;
    case Tab::Squads:
      BuildSquads(pPage, vPage);
      break;
  }
}

void plAiProjectSettingsEngineDlg::BuildTabRow(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;

  plUiPanel* pStrip = Panel(pBody);
  SetRect(pStrip, plRectFloat(0, 0, fWidth, s_fTabStripHeight));
  pStrip->m_Color = Background();

  plUiPanel* pTabs = m_pTabs->Build(pStrip, fWidth - 12.0f);
  SetRect(pTabs, plRectFloat(6.0f, s_fTabStripHeight - plEditorUiTabs::Height, fWidth - 12.0f, plEditorUiTabs::Height));

  plUiPanel* pLine = Panel(pBody);
  SetRect(pLine, plRectFloat(0, s_fTabStripHeight, fWidth, 1.0f));
  pLine->m_Color = Line();
}

void plAiProjectSettingsEngineDlg::BuildGroundTypes(plUiElement* pPage, const plVec2& vSize)
{
  using namespace plEditorUi;

  const float fWidth = vSize.x - 2.0f * s_fPagePadX;

  float y = s_fPagePadY;
  y += WrappedText(pPage, s_szGroundTypeHint, plRectFloat(s_fPagePadX, y, fWidth, 0.0f), Dim(), FontSize - 2, 16.0f) + 10.0f;

  const float fListHeight = plMath::Clamp(vSize.y - y - s_fPagePadY, 60.0f, s_fGroundTypeListHeight);

  plUiPanel* pFrame = Panel(pPage);
  SetRect(pFrame, plRectFloat(s_fPagePadX, y, fWidth, fListHeight));
  pFrame->m_Color = plColorGammaUB(0, 0, 0, 0);
  pFrame->m_fBorderWidth = 1.0f;
  pFrame->m_BorderColor = Line();
  pFrame->m_vCornerRadius = plVec4(4);

  plUiPanel* pList = Panel(pFrame);
  SetRect(pList, plRectFloat(1.0f, 1.0f, fWidth - 2.0f, fListHeight - 2.0f));
  pList->m_Color = plColorGammaUB(0, 0, 0, 0);
  pList->m_bClipChildren = true;

  m_pGroundTypes->Build(pList);
}

float plAiProjectSettingsEngineDlg::BuildConfigBar(plUiElement* pPage, float y, const char* szLabel, plStringView sCurrent, bool bHasConfigs, const char* szPrefix, const char* szScrollView)
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();

  float x = s_fPagePadX;
  const float fLabel = plMath::Ceil(MeasureText(szLabel, FontSize - 1)) + 2.0f;
  TextAt(pPage, szLabel, plRectFloat(x, y, fLabel, ControlHeight), Muted());
  x += fLabel + 6.0f;

  const plStringBuilder sConfig(szPrefix, ".Config");
  const plStringBuilder sAdd(szPrefix, ".Add");
  const plStringBuilder sRemove(szPrefix, ".Remove");

  const plRectFloat dropdown(x, y, s_fConfigDropdownWidth, ControlHeight);
  Dropdown(pPage, board, dropdown, sCurrent, sConfig, bHasConfigs);
  AddDropdownAnchor(sConfig, dropdown, szScrollView);
  x += s_fConfigDropdownWidth + 6.0f;

  IconPushButton(pPage, board, plRectFloat(x, y, ControlHeight, ControlHeight), IconPath(Icon::Add), sAdd);
  SetToolTip(board, sAdd, "Add");
  x += ControlHeight + 6.0f;

  IconPushButton(pPage, board, plRectFloat(x, y, ControlHeight, ControlHeight), IconPath(Icon::Remove), sRemove, bHasConfigs);
  SetToolTip(board, sRemove, "Remove the selected config");

  return y + ControlHeight;
}

void plAiProjectSettingsEngineDlg::BuildEmptyState(plUiElement* pPage, const plRectFloat& rect, const char* szLine, const char* szAction, const char* szActionId)
{
  using namespace plEditorUi;

  // the AI kind icon, one line and the action that adds the first config
  const float fTop = rect.y + 26.0f;

  plUiImage* pIcon = SvgImage(pPage, s_szAiKindIcon);
  SetRect(pIcon, plRectFloat(rect.x + rect.width * 0.5f - 11.0f, fTop, 22.0f, 22.0f));
  pIcon->m_Color = Dim();

  TextAt(pPage, szLine, plRectFloat(rect.x, fTop + 30.0f, rect.width, 18.0f), Muted(), FontSize)->m_HAlign = plUiTextHAlign::Center;

  const float fButton = PushButtonWidth(szAction);
  PushButton(pPage, GetSurface()->GetBlackboard(), plRectFloat(rect.x + (rect.width - fButton) * 0.5f, fTop + 56.0f, fButton, ControlHeight), szAction, szActionId);
}

void plAiProjectSettingsEngineDlg::BuildNumberRow(plEditorUiForm& ref_form, plUiElement* pParent, const NumberSetting& field)
{
  const plRectFloat row = ref_form.Row(field.m_szLabel, plEditorUi::ControlHeight, field.m_szToolTip);
  BuildNumberWell(pParent, plRectFloat(row.x, row.y, plMath::Min(field.m_fWidth, row.width), plEditorUi::ControlHeight), field);
}

void plAiProjectSettingsEngineDlg::BuildNumberWell(plUiElement* pParent, const plRectFloat& rect, const NumberSetting& field)
{
  using namespace plEditorUi;

  const plStringBuilder sName("Ai.F.", field.m_sKey);
  const plStringBuilder sBinding(sName, ".Text");
  NumberField(pParent, rect, sName, sBinding, {}, {}, field.m_szUnit);

  // the well NumberField added last
  if (field.m_bInvalid)
    static_cast<plUiPanel*>(pParent->m_Children.PeekBack())->m_BorderColor = Danger();

  if (field.m_szToolTip[0] != '\0')
    SetToolTip(GetSurface()->GetBlackboard(), sName, field.m_szToolTip);
}

float plAiProjectSettingsEngineDlg::GetNavmeshContentHeight() const
{
  const float fBar = s_fPagePadY + plEditorUi::ControlHeight + 10.0f;

  if (m_iNavmesh < 0)
    return fBar + 26.0f + 56.0f + plEditorUi::ControlHeight + s_fPagePadY;

  const float fCard = 1.0f + plEditorUi::SectionHeaderHeight + 8.0f + s_uiNavmeshRows * s_fRowStep - plEditorUiForm::RowGap + 8.0f + 1.0f;
  return fBar + fCard + s_fPagePadY;
}

void plAiProjectSettingsEngineDlg::BuildNavmesh(plUiElement* pPage, const plVec2& vSize)
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();

  // the page scrolls: the config card is taller than the page
  plUiScrollView* pScroll = AddScrollView(pPage, s_szNavmeshPage, plRectFloat(0, 0, vSize.x, vSize.y));

  plUiPanel* pContent = Panel(pScroll);
  pContent->m_vOffsets.w = GetNavmeshContentHeight();
  pContent->m_Color = plColorGammaUB(0, 0, 0, 0);

  const bool bHasConfig = m_iNavmesh >= 0;
  const plString sName = bHasConfig ? m_Config.m_NavmeshConfigs[m_iNavmesh].m_sName : plString();
  const float fWidth = vSize.x - 2.0f * s_fPagePadX;

  const float y = BuildConfigBar(pContent, s_fPagePadY, "Navmesh Configs", sName, bHasConfig, "Ai.Nav", s_szNavmeshPage) + 10.0f;

  if (!bHasConfig)
  {
    BuildEmptyState(pContent, plRectFloat(s_fPagePadX, y, fWidth, 0.0f), "No navmesh configs yet.", "Add Navmesh Config", s_szNavEmptyAdd);
    return;
  }

  const plAiNavmeshConfig& nav = m_Config.m_NavmeshConfigs[m_iNavmesh];
  const float fCardHeight = GetNavmeshContentHeight() - y - s_fPagePadY;

  plUiPanel* pCard = Panel(pContent);
  SetRect(pCard, plRectFloat(s_fPagePadX, y, fWidth, fCardHeight));
  pCard->m_Color = Surface();
  pCard->m_fBorderWidth = 1.0f;
  pCard->m_BorderColor = Line();
  pCard->m_vCornerRadius = plVec4(6);
  pCard->m_bClipChildren = true;

  plUiPanel* pHeader = Panel(pCard);
  SetRect(pHeader, plRectFloat(1.0f, 1.0f, fWidth - 2.0f, SectionHeaderHeight));
  pHeader->m_Color = plColorGammaUB(0, 0, 0, 0);
  SectionHeader(pHeader, sName, plAiEngineUi::AiColor());

  plEditorUiForm form(pCard, plRectFloat(11.0f, 1.0f + SectionHeaderHeight + 8.0f, fWidth - 22.0f, fCardHeight), s_fFormLabelWidth);

  // a layer that is not in the list shows its number
  const auto itLayer = m_CollisionLayers.Find(nav.m_uiCollisionLayer);
  const plString sLayer = itLayer.IsValid() ? itLayer.Value() : FormatCount(nav.m_uiCollisionLayer);

  const plRectFloat layerRect = form.Row("Collision Layer");
  Dropdown(pCard, board, layerRect, sLayer, s_szNavLayer, !m_CollisionLayers.IsEmpty());
  SetToolTip(board, s_szNavLayer, s_szCollisionLayerTip);
  AddDropdownAnchor(s_szNavLayer, plRectFloat(s_fPagePadX + layerRect.x, y + layerRect.y, layerRect.width, layerRect.height), s_szNavmeshPage);

  for (const NumberSetting& field : m_Fields)
    BuildNumberRow(form, pCard, field);
}

void plAiProjectSettingsEngineDlg::BuildPathSearch(plUiElement* pPage, const plVec2& vSize)
{
  using namespace plEditorUi;

  const bool bHasConfig = m_iPathSearch >= 0;
  const plString sName = bHasConfig ? m_Config.m_PathSearchConfigs[m_iPathSearch].m_sName : plString();
  const float fWidth = vSize.x - 2.0f * s_fPagePadX;

  float y = BuildConfigBar(pPage, s_fPagePadY, "Search Configs", sName, bHasConfig, "Ai.Path") + 10.0f;

  if (!bHasConfig)
  {
    BuildEmptyState(pPage, plRectFloat(s_fPagePadX, y, fWidth, 0.0f), "No path search configs yet.", "Add Path Search Config", s_szPathEmptyAdd);
    return;
  }

  BuildPathSearchHeader(pPage, plRectFloat(s_fPagePadX, y, fWidth, s_fTableHeaderHeight));
  y += s_fTableHeaderHeight;

  // the rows scroll under the header
  plUiScrollView* pScroll = AddScrollView(pPage, s_szPathSearchPage, plRectFloat(s_fPagePadX, y, fWidth, plMath::Max(vSize.y - y - s_fPagePadY, RowHeight)));

  plUiPanel* pRows = Panel(pScroll);
  pRows->m_vOffsets.w = m_PathRows.GetCount() * RowHeight;
  pRows->m_Color = plColorGammaUB(0, 0, 0, 0);

  for (plUInt32 i = 0; i < m_PathRows.GetCount(); ++i)
    BuildPathSearchRow(pRows, i * RowHeight, fWidth, m_PathRows[i], m_Fields[i]);
}

void plAiProjectSettingsEngineDlg::BuildPathSearchHeader(plUiElement* pPage, const plRectFloat& rect)
{
  using namespace plEditorUi;

  plUiPanel* pHeader = Panel(pPage);
  SetRect(pHeader, rect);
  pHeader->m_Color = Surface();

  // the uppercase column titles of every table
  auto title = [&](const char* szTitle, float x, float fWidth, bool bRight)
  {
    plStringBuilder sTitle(szTitle);
    sTitle.ToUpper();

    plUiText* pTitle = TextAt(pHeader, sTitle, plRectFloat(x + s_fCellPadding, 0, fWidth - 2.0f * s_fCellPadding, rect.height), Dim(), MinFontSize);
    pTitle->m_HAlign = bRight ? plUiTextHAlign::Right : plUiTextHAlign::Left;
  };

  const float fAllowedX = rect.width - s_fCostColumnWidth - s_fAllowedColumnWidth;
  title("Ground Type", 0.0f, fAllowedX, false);
  title("Traversal Allowed", fAllowedX, s_fAllowedColumnWidth, false);
  title("Traversal Cost", rect.width - s_fCostColumnWidth, s_fCostColumnWidth, true);

  plUiPanel* pRule = Panel(pHeader);
  SetRect(pRule, plRectFloat(0, rect.height - 1.0f, rect.width, 1.0f));
  pRule->m_Color = Line();
}

void plAiProjectSettingsEngineDlg::BuildPathSearchRow(plUiElement* pRows, float y, float fWidth, plUInt32 uiGroundType, const NumberSetting& cost)
{
  using namespace plEditorUi;

  const plAiPathSearchConfig& path = m_Config.m_PathSearchConfigs[m_iPathSearch];
  const float fAllowedX = fWidth - s_fCostColumnWidth - s_fAllowedColumnWidth;

  TextAt(pRows, m_Config.m_GroundTypes[uiGroundType].m_sName, plRectFloat(s_fCellPadding, y, fAllowedX - 2.0f * s_fCellPadding, RowHeight), Foreground());

  // CheckButton draws its box 2 px into its rect, so the box lines up with the column title
  plStringBuilder sAllowed;
  sAllowed.SetFormat("{}{}", s_szPathAllowed, uiGroundType);
  CheckButton(pRows, GetSurface()->GetBlackboard(), plRectFloat(fAllowedX + s_fCellPadding - 2.0f, y, RowHeight, RowHeight), {}, sAllowed, path.m_bGroundTypeAllowed[uiGroundType]);

  BuildNumberWell(pRows, plRectFloat(fWidth - s_fCellPadding - s_fCostWellWidth, y + (RowHeight - s_fCostWellHeight) * 0.5f, s_fCostWellWidth, s_fCostWellHeight), cost);

  // the hairline under each row: the line colour at 60 %
  plUiPanel* pRule = Panel(pRows);
  SetRect(pRule, plRectFloat(0, y + RowHeight - 1.0f, fWidth, 1.0f));
  pRule->m_Color = plColorGammaUB(Line().r, Line().g, Line().b, 153);
}

void plAiProjectSettingsEngineDlg::BuildTactical(plUiElement* pPage, const plVec2& vSize)
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();

  plEditorUiForm form(pPage, plRectFloat(s_fPagePadX, s_fPagePadY, vSize.x - 2.0f * s_fPagePadX, vSize.y - 2.0f * s_fPagePadY), s_fFormLabelWidth);

  const plString& sCover = m_Config.m_TacticalConfig.m_sCoverNavmesh;
  const plRectFloat coverRect = form.Row("Cover Navmesh", ControlHeight, s_szCoverNavmeshTip);
  Dropdown(pPage, board, coverRect, sCover.IsEmpty() ? plStringView(s_szFirstNavmesh) : sCover.GetView(), s_szCoverNavmesh);
  SetToolTip(board, s_szCoverNavmesh, s_szCoverNavmeshTip);
  AddDropdownAnchor(s_szCoverNavmesh, coverRect);

  for (const NumberSetting& field : m_Fields)
    BuildNumberRow(form, pPage, field);

  AddPageToolTips(form);
}

void plAiProjectSettingsEngineDlg::BuildSquads(plUiElement* pPage, const plVec2& vSize)
{
  plEditorUiForm form(pPage, plRectFloat(s_fPagePadX, s_fPagePadY, vSize.x - 2.0f * s_fPagePadX, vSize.y - 2.0f * s_fPagePadY), s_fFormLabelWidth);

  for (const NumberSetting& field : m_Fields)
    BuildNumberRow(form, pPage, field);

  AddPageToolTips(form);
}

void plAiProjectSettingsEngineDlg::AddPageToolTips(const plEditorUiForm& form)
{
  // the form's rects are in page units, the dialog's in body units
  plHybridArray<plEditorUiForm::ToolTip, 8> toolTips;
  for (const plEditorUiForm::ToolTip& toolTip : form.GetToolTips())
  {
    plEditorUiForm::ToolTip& moved = toolTips.ExpandAndGetRef();
    moved.m_Rect = plRectFloat(toolTip.m_Rect.x + m_PageRect.x, toolTip.m_Rect.y + m_PageRect.y, toolTip.m_Rect.width, toolTip.m_Rect.height);
    moved.m_sText = toolTip.m_sText;
  }

  AddBodyToolTips(toolTips);
}

void plAiProjectSettingsEngineDlg::AddDropdownAnchor(plStringView sElement, const plRectFloat& rect, const char* szScrollView)
{
  // rect is in page units (in the scroll view's content when there is one), the anchor in surface units
  DropdownAnchor& anchor = m_DropdownAnchors[sElement];
  anchor.m_Rect = plRectFloat(GetBodyRect().x + m_PageRect.x + rect.x, GetBodyRect().y + m_PageRect.y + rect.y, rect.width, rect.height);
  anchor.m_szScrollView = szScrollView;
}

float plAiProjectSettingsEngineDlg::GetScrollOffset(const char* szScrollView) const
{
  plHashTable<plString, float> positions;
  GetSurface()->GetCanvas().CaptureScrollPositions(positions);

  float fOffset = 0.0f;
  positions.TryGetValue(plString(szScrollView), fOffset);
  return fOffset;
}

// Interaction

void plAiProjectSettingsEngineDlg::ShowMenu(plStringView sElement, const plEditorUiMenu& menu, plDelegate<void(plUInt32)> onPick)
{
  // under the clicked dropdown; from the keyboard under where it was built, moved by its page's scrolling
  plRectFloat anchor = m_PressedRect;
  if (m_sPressedElement != sElement)
  {
    auto it = m_DropdownAnchors.Find(sElement);
    if (!it.IsValid())
      return;

    anchor = it.Value().m_Rect;

    if (it.Value().m_szScrollView != nullptr)
      anchor.y -= GetScrollOffset(it.Value().m_szScrollView);
  }

  plEditorUiMenuView::Show(*GetSurface(), plVec2(anchor.x, anchor.y + anchor.height), menu, onPick);
}

void plAiProjectSettingsEngineDlg::ShowConfigMenu(bool bNavmesh)
{
  plEditorUiMenu menu;
  menu.m_fMinWidth = s_fConfigDropdownWidth;

  const plInt32 iCurrent = bNavmesh ? m_iNavmesh : m_iPathSearch;
  const plUInt32 uiCount = bNavmesh ? m_Config.m_NavmeshConfigs.GetCount() : m_Config.m_PathSearchConfigs.GetCount();

  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    const plString& sName = bNavmesh ? m_Config.m_NavmeshConfigs[i].m_sName : m_Config.m_PathSearchConfigs[i].m_sName;
    plEditorUiMenuItem& item = menu.AddAction(sName, i);
    item.m_bCheckable = true;
    item.m_bChecked = static_cast<plInt32>(i) == iCurrent;
  }

  ShowMenu(bNavmesh ? s_szNavConfig : s_szPathConfig, menu, [this, bNavmesh](plUInt32 uiPicked)
    {
      if (bNavmesh)
      {
        if (uiPicked < m_Config.m_NavmeshConfigs.GetCount())
          SelectNavmesh(static_cast<plInt32>(uiPicked));
      }
      else if (uiPicked < m_Config.m_PathSearchConfigs.GetCount())
      {
        SelectPathSearch(static_cast<plInt32>(uiPicked));
      } });
}

void plAiProjectSettingsEngineDlg::ShowCollisionLayerMenu()
{
  if (m_iNavmesh < 0)
    return;

  const plUInt8 uiCurrent = m_Config.m_NavmeshConfigs[m_iNavmesh].m_uiCollisionLayer;

  plEditorUiMenu menu;
  for (auto it : m_CollisionLayers)
  {
    plEditorUiMenuItem& item = menu.AddAction(it.Value(), static_cast<plUInt32>(it.Key()));
    item.m_bCheckable = true;
    item.m_bChecked = it.Key() == uiCurrent;
  }

  ShowMenu(s_szNavLayer, menu, [this](plUInt32 uiPicked)
    {
      if (m_iNavmesh < 0)
        return;

      m_Config.m_NavmeshConfigs[m_iNavmesh].m_uiCollisionLayer = static_cast<plUInt8>(uiPicked);
      Rebuild(); });
}

void plAiProjectSettingsEngineDlg::ShowCoverNavmeshMenu()
{
  const plString& sCurrent = m_Config.m_TacticalConfig.m_sCoverNavmesh;

  plEditorUiMenu menu;

  plEditorUiMenuItem& first = menu.AddAction(s_szFirstNavmesh, 0);
  first.m_bCheckable = true;
  first.m_bChecked = sCurrent.IsEmpty();

  for (plUInt32 i = 0; i < m_Config.m_NavmeshConfigs.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(m_Config.m_NavmeshConfigs[i].m_sName, i + 1);
    item.m_bCheckable = true;
    item.m_bChecked = m_Config.m_NavmeshConfigs[i].m_sName == sCurrent;
  }

  ShowMenu(s_szCoverNavmesh, menu, [this](plUInt32 uiPicked)
    {
      plString& sCover = m_Config.m_TacticalConfig.m_sCoverNavmesh;

      if (uiPicked == 0)
        sCover.Clear();
      else if (uiPicked <= m_Config.m_NavmeshConfigs.GetCount())
        sCover = m_Config.m_NavmeshConfigs[uiPicked - 1].m_sName;

      Rebuild(); });
}

void plAiProjectSettingsEngineDlg::AddConfig(bool bNavmesh)
{
  bool bOk = false;
  plStringBuilder sName;

  if (bNavmesh)
  {
    sName = plEnginePrompt::GetText("Add Navmesh Config", "Name:", "New Navmesh", &bOk, [this](plStringView sText)
      { return plAiProjectSettings::ValidateConfigName(sText, m_Config.m_NavmeshConfigs, "Navmesh"); });
  }
  else
  {
    sName = plEnginePrompt::GetText("Add Path Search Config", "Name:", "NewPathSearch", &bOk, [this](plStringView sText)
      { return plAiProjectSettings::ValidateConfigName(sText, m_Config.m_PathSearchConfigs, "Path Search"); });
  }

  if (!bOk)
    return;

  sName.Trim(" \t\r\n");
  const plString sNewName = sName;

  // the field pointers go into the config arrays, which grow
  ClearFields();

  if (bNavmesh)
  {
    m_Config.m_NavmeshConfigs.ExpandAndGetRef().m_sName = sNewName;
    SortConfigs(m_Config.m_NavmeshConfigs);
    SelectNavmesh(FindConfig(m_Config.m_NavmeshConfigs, sNewName));
  }
  else
  {
    m_Config.m_PathSearchConfigs.ExpandAndGetRef().m_sName = sNewName;
    SortConfigs(m_Config.m_PathSearchConfigs);
    SelectPathSearch(FindConfig(m_Config.m_PathSearchConfigs, sNewName));
  }

  UpdateTabs();
}

void plAiProjectSettingsEngineDlg::RemoveConfig(bool bNavmesh)
{
  const plInt32 iIndex = bNavmesh ? m_iNavmesh : m_iPathSearch;
  const plInt32 iCount = static_cast<plInt32>(bNavmesh ? m_Config.m_NavmeshConfigs.GetCount() : m_Config.m_PathSearchConfigs.GetCount());

  if (iIndex < 0 || iIndex >= iCount)
    return;

  const char* szQuestion = bNavmesh ? "Remove the current Navmesh Config?" : "Remove the current Path Search Config?";
  if (!plEngineDialog::AskYesNo(this, "Ai Project Settings", szQuestion, false))
    return;

  // the next config takes the place of the removed one; none is selected when it was the last
  const plInt32 iNext = plMath::Min(iIndex, iCount - 2);

  // the field pointers go into the config arrays, which shrink
  ClearFields();

  if (bNavmesh)
  {
    // a removed cover navmesh falls back to the first navmesh
    plString& sCover = m_Config.m_TacticalConfig.m_sCoverNavmesh;
    if (sCover == m_Config.m_NavmeshConfigs[iIndex].m_sName)
      sCover.Clear();

    m_Config.m_NavmeshConfigs.RemoveAtAndCopy(iIndex);
    SelectNavmesh(iNext);
  }
  else
  {
    m_Config.m_PathSearchConfigs.RemoveAtAndCopy(iIndex);
    SelectPathSearch(iNext);
  }

  UpdateTabs();
}

void plAiProjectSettingsEngineDlg::Reset()
{
  LoadConfig();

  m_pGroundTypes->Refresh();

  CollectFields();
  WriteFields();
  UpdateTabs();
  Rebuild();
}

void plAiProjectSettingsEngineDlg::OnPointer(const plEditorUiPointerEvent& e)
{
  if (e.m_Type != plEditorUiPointerEvent::Type::Press)
    return;

  m_sPressedElement = e.m_sElement;
  m_PressedRect = e.m_ElementRect;
  m_bToggledOnPress = false;

  // a click on a ground type's check box toggles it
  if (m_Tab != Tab::GroundTypes || e.m_Button != plEditorUiPointerEvent::Button::Left || e.m_sElement != "Ai.GroundTypes.row")
    return;

  if (e.m_iItemIndex < 0 || e.m_iItemIndex >= static_cast<plInt32>(m_pGroundTypes->GetVisibleRowCount()))
    return;

  const float fX = e.m_vPosition.x - e.m_ElementRect.x;
  if (fX < s_fCheckBoxLeft || fX >= s_fCheckBoxRight)
    return;

  const plUInt32 uiIndex = GroundTypeAdapter::ToIndex(m_pGroundTypes->GetVisibleRow(static_cast<plUInt32>(e.m_iItemIndex)));
  if (GroundTypeAdapter::IsFixed(uiIndex))
    return;

  ToggleGroundType(uiIndex);
  m_bToggledOnPress = true;
}

void plAiProjectSettingsEngineDlg::OnKey(const plEditorUiKeyEvent& e)
{
  m_bToggledOnPress = false;
  m_sPressedElement.Clear();

  if (e.m_bHandled || m_Tab != Tab::GroundTypes || !m_pGroundTypes->HasFocus() || m_pGroundTypes->IsRenaming())
    return;

  // Enter in the ground type list presses OK before the tree takes it as a rename
  if (e.m_Key == plEditorUiKeyEvent::Key::Enter && e.m_uiModifiers == 0)
  {
    GetHost().PressButton(s_szSettingsOk);
    e.m_bHandled = true;
    return;
  }

  // Space toggles the current ground type's check box
  if (e.m_Key != plEditorUiKeyEvent::Key::Space || m_pGroundTypes->GetCurrent() == 0)
    return;

  const plUInt32 uiIndex = GroundTypeAdapter::ToIndex(m_pGroundTypes->GetCurrent());
  if (GroundTypeAdapter::IsFixed(uiIndex))
    return;

  ToggleGroundType(uiIndex);
  e.m_bHandled = true;
}

void plAiProjectSettingsEngineDlg::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  // prompts and questions run after the current UI dispatch
  auto defer = [this](void (plAiProjectSettingsEngineDlg::*pAction)(bool), bool bNavmesh)
  {
    m_Timers.Start(plTime::MakeZero(), [this, pAction, bNavmesh]()
      { (this->*pAction)(bNavmesh); });
  };

  if (sId == s_szNavConfig)
    ShowConfigMenu(true);
  else if (sId == s_szPathConfig)
    ShowConfigMenu(false);
  else if (sId == s_szNavAdd || sId == s_szNavEmptyAdd)
    defer(&plAiProjectSettingsEngineDlg::AddConfig, true);
  else if (sId == s_szPathAdd || sId == s_szPathEmptyAdd)
    defer(&plAiProjectSettingsEngineDlg::AddConfig, false);
  else if (sId == s_szNavRemove)
    defer(&plAiProjectSettingsEngineDlg::RemoveConfig, true);
  else if (sId == s_szPathRemove)
    defer(&plAiProjectSettingsEngineDlg::RemoveConfig, false);
  else if (sId == s_szNavLayer)
    ShowCollisionLayerMenu();
  else if (sId == s_szCoverNavmesh)
    ShowCoverNavmeshMenu();
  else if (sId.StartsWith(s_szPathAllowed))
  {
    // the check box of a Path Search row: its ground type follows the prefix
    const plStringView sGroundType(sId.GetStartPointer() + plStringUtils::GetStringElementCount(s_szPathAllowed), sId.GetEndPointer());

    plUInt32 uiGroundType = 0;
    if (plConversionUtils::StringToUInt(sGroundType, uiGroundType).Succeeded())
      ToggleTraversalAllowed(uiGroundType);
  }
}

void plAiProjectSettingsEngineDlg::OnButton(plStringView sId)
{
  if (sId == s_szSettingsReset)
  {
    Reset();
    return;
  }

  if (sId == s_szSettingsOk)
  {
    // the last key can arrive in the same frame as Enter
    ReadFields();
    UpdateValidation();
    if (!GetHost().IsButtonEnabled(s_szSettingsOk))
      return;

    SaveConfig();
  }

  plEngineDialog::OnButton(sId);
}
