#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiArchetypeAsset.h>
#include <EditorPluginAi/Assets/AiArchetypeAssetWindow.h>
#include <EditorPluginAi/Assets/AiArchetypeViews.h>
#include <EditorPluginAi/Assets/AiEditorHelpers.h>

#include <AiPlugin/UtilityAI/Decision/AiScoring.h>
#include <AiPlugin/UtilityAI/Decision/AiUtilityEvaluator.h>
#include <AiPlugin/UtilityAI/Perception/AiPerception.h>
#include <AiPlugin/UtilityAI/Perception/AiPerceptionRules.h>
#include <Core/Utils/Blackboard.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorUi/Surface.h>
#include <ToolsFoundation/CommandHistory/CommandHistory.h>

namespace
{
  constexpr float s_fRowHeight = 30.0f;
  constexpr float s_fBarWidth = 80.0f;
  constexpr float s_fPeekLine = 18.0f;
  constexpr float s_fPeekCurveRow = 28.0f;

  // settings: 24 px property rows in one or two columns
  constexpr float s_fMargin = 12.0f;
  constexpr float s_fFieldRow = 24.0f;
  constexpr float s_fFieldLabel = 132.0f;
  constexpr float s_fFieldWidth = 80.0f;
  constexpr float s_fFieldHeight = 20.0f;
  constexpr float s_fColumnGap = 18.0f;
  constexpr float s_fTwoColumnWidth = 520.0f;
  constexpr float s_fTabHeight = 26.0f;
  constexpr float s_fTextLine = 16.0f;

  // memory preview: one stimulus, or 1 s in view, simulated in 30 updates per second
  constexpr float s_fGraphHeight = 150.0f;
  constexpr double s_fChartSeconds = 15.0;
  constexpr double s_fInViewSeconds = 1.0;
  constexpr double s_fSimulationStep = 1.0 / 30.0;
  constexpr double s_fSimulationLimit = 3600.0;

  // migration preview: every consideration's input at 70 % of its range, as the Behavior window's score preview starts
  constexpr float s_fExampleInput = 0.7f;

  const char* s_szSenseNames[] = {"Sight", "Hearing", "Damage", "Custom"};

  const char* s_szSenseTooltips[] = {
    "Seeing targets through the sensor components",
    "Noises such as footsteps, gunshots and props",
    "Being hit; the attacker becomes a target",
    "Stimuli sent by game code",
  };

  plString Name(plStringView sPrefix, plUInt32 uiIndex, plStringView sSuffix)
  {
    plStringBuilder sName;
    sName.SetFormat("{}{}{}", sPrefix, uiIndex, sSuffix);
    return sName;
  }

  void Publish(plBlackboard& ref_board, const plUiDrawListWriter& writer, plStringView sEntry)
  {
    plHashedString sHashed;
    sHashed.Assign(sEntry);
    writer.Publish(ref_board, sHashed);
  }

  /// "4 considerations, target, cooldown 4.0s" (dot-separated), or the unresolved note.
  void GetTags(const plAiBehaviorPeek& peek, plStringBuilder& out_sTags)
  {
    out_sTags.Clear();

    if (!peek.m_bValid)
    {
      out_sTags = "unresolved - asset not found";
      return;
    }

    if (!peek.m_Considerations.IsEmpty())
    {
      out_sTags.SetFormat("{} considerations", peek.m_Considerations.GetCount());

      for (const auto& cons : peek.m_Considerations)
      {
        if (cons.m_bNeedsTarget)
        {
          out_sTags.Append(" \xC2\xB7 target");
          break;
        }
      }
    }

    if (peek.m_fCooldownSeconds > 0.0)
      out_sTags.AppendFormat(" \xC2\xB7 cooldown {}s", plArgF(peek.m_fCooldownSeconds, 1));
  }

  /// The band of a category in the ladder, or the unresolved band after them.
  plUInt32 BandForCategory(plInt32 iCategory)
  {
    const plArrayPtr<const plInt32> ladder = plAiEngineUi::GetCategoryLadder();

    for (plUInt32 i = 0; i < ladder.GetCount(); ++i)
    {
      if (ladder[i] == iCategory)
        return i;
    }

    return ladder.GetCount();
  }

  //////////////////////////////////////////////////////////////////////////
  // settings layout

  /// The text colour of disabled rows: the mockup's 38 % opacity.
  plColorGammaUB DisabledText()
  {
    return plAiEngineUi::Mix(plEditorUi::Surface(), plEditorUi::Muted(), 0.38f);
  }

  /// A transparent row of the stacked body.
  plUiPanel* BodyRow(plUiElement* pBody, float fHeight)
  {
    plUiPanel* pRow = plEditorUi::Panel(pBody);
    pRow->m_vOffsets.w = fHeight;
    pRow->m_Color = plColorGammaUB(0, 0, 0, 0);
    return pRow;
  }

  /// Lines a wrapped text needs at the given width, with room for the word breaks.
  plUInt32 CountLines(plStringView sText, float fWidth, plUInt32 uiFontSize)
  {
    const float fText = plAiEngineUi::TextWidth(sText, uiFontSize) * 1.15f;
    return plMath::Max<plUInt32>(1, static_cast<plUInt32>(plMath::Ceil(fText / plMath::Max(fWidth, 40.0f))));
  }

  /// A wrapped paragraph at rect, top aligned.
  plUiText* Paragraph(plUiElement* pParent, const plRectFloat& rect, plStringView sText, const plColorGammaUB& color, plUInt32 uiFontSize)
  {
    plUiText* pText = plAiEngineUi::Label(pParent, rect, sText, color, uiFontSize);
    pText->m_bWrap = true;
    pText->m_VAlign = plUiTextVAlign::Top;
    return pText;
  }

  /// A dim uppercase title with a hairline after it, between the groups of a section.
  void BuildSubHeader(plUiElement* pBody, float fWidth, plStringView sTitle)
  {
    using namespace plEditorUi;
    using namespace plAiEngineUi;

    plUiPanel* pRow = BodyRow(pBody, 24.0f);

    plStringBuilder sUpper(sTitle);
    sUpper.ToUpper();

    const float fTitle = TextWidth(sUpper, MinFontSize);
    Label(pRow, plRectFloat(s_fMargin, 9.0f, fTitle, 14.0f), sUpper, Dim(), MinFontSize)->m_Overflow = plUiTextOverflow::Overflow;

    auto* pLine = Panel(pRow);
    Place(pLine, plRectFloat(s_fMargin + fTitle + 8.0f, 16.0f, plMath::Max(fWidth - 2.0f * s_fMargin - fTitle - 8.0f, 0.0f), 1.0f));
    pLine->m_Color = Line();
  }

  /// A warning box: the mark in the warning colour, then the text.
  void BuildHint(plUiElement* pBody, float fWidth, plStringView sText)
  {
    using namespace plEditorUi;
    using namespace plAiEngineUi;

    plUiPanel* pRow = BodyRow(pBody, 34.0f);

    auto* pBox = Panel(pRow);
    Place(pBox, plRectFloat(s_fMargin, 2.0f, fWidth - 2.0f * s_fMargin, 24.0f));
    pBox->m_Color = Mix(Surface(), Warning(), 0.05f);
    pBox->m_fBorderWidth = 1.0f;
    pBox->m_BorderColor = Mix(Surface(), Warning(), 0.3f);
    pBox->m_vCornerRadius = plVec4(3);

    Label(pBox, plRectFloat(9.0f, 0, 10.0f, 24.0f), "!", Warning(), FontSize - 2, true);
    Label(pBox, plRectFloat(23.0f, 0, plMath::Max(fWidth - 2.0f * s_fMargin - 32.0f, 10.0f), 24.0f), sText, Muted(), FontSize - 2);
  }

  /// The selected look of a button: the selection fill with an accent edge.
  void MakePrimary(plUiButton* pButton)
  {
    using namespace plEditorUi;

    pButton->m_Color = Selection();
    pButton->m_HoverColor = plAiEngineUi::Mix(Selection(), Accent(), 0.12f);
    pButton->m_BorderColor = plAiEngineUi::Mix(Selection(), Accent(), 0.4f);
  }

  /// The cells of a field box: 24 px rows in two columns when there is room, otherwise one.
  struct FieldGrid
  {
    plUInt32 m_uiColumns = 1;
    float m_fCellWidth = 0.0f;

    plRectFloat Cell(plUInt32 uiIndex) const
    {
      const plUInt32 uiColumn = uiIndex % m_uiColumns;
      const plUInt32 uiRow = uiIndex / m_uiColumns;
      return plRectFloat(s_fMargin + uiColumn * (m_fCellWidth + s_fColumnGap), 2.0f + uiRow * s_fFieldRow, m_fCellWidth, s_fFieldRow);
    }

    float GetHeight(plUInt32 uiCells) const
    {
      const plUInt32 uiRows = (uiCells + m_uiColumns - 1) / m_uiColumns;
      return 2.0f + uiRows * s_fFieldRow + 6.0f;
    }
  };

  FieldGrid MakeGrid(float fWidth, bool bTwoColumns)
  {
    FieldGrid grid;
    grid.m_uiColumns = (bTwoColumns && fWidth >= s_fTwoColumnWidth) ? 2 : 1;
    grid.m_fCellWidth = (fWidth - 2.0f * s_fMargin - (grid.m_uiColumns - 1) * s_fColumnGap) / grid.m_uiColumns;
    return grid;
  }

  /// A number well that can't be edited: the value dimmed, as the mockup's disabled inputs.
  void DisabledField(plUiElement* pParent, const plRectFloat& rect, plStringView sText, plStringView sUnit)
  {
    using namespace plEditorUi;
    using namespace plAiEngineUi;

    auto* pWell = Panel(pParent);
    Place(pWell, rect);
    pWell->m_Color = Mix(Surface(), Field(), 0.5f);
    pWell->m_fBorderWidth = 1.0f;
    pWell->m_BorderColor = Mix(Surface(), Line(), 0.5f);
    pWell->m_vCornerRadius = plVec4(4);

    const float fUnit = sUnit.IsEmpty() ? 0.0f : CodeWidth(sUnit) + 4.0f;

    plUiText* pValue = Label(pWell, plRectFloat(6.0f, 0, rect.width - 12.0f - fUnit, rect.height), sText, DisabledText(), FontSize - 2, true);
    pValue->m_HAlign = plUiTextHAlign::Right;

    if (!sUnit.IsEmpty())
    {
      Label(pWell, plRectFloat(rect.width - 6.0f - fUnit + 4.0f, 0, fUnit, rect.height), sUnit, DisabledText(), FontSize - 2);
    }
  }

  //////////////////////////////////////////////////////////////////////////
  // settings edits

  using NumberAction = std::function<void(double)>;

  NumberAction RootFloat(plAiArchetypeAssetDocumentWindow* pWindow, const char* szProperty, const char* szDescription)
  {
    return [pWindow, szProperty, szDescription](double fValue)
    { pWindow->SetRootValue(szProperty, plVariant(static_cast<float>(fValue)), szDescription); };
  }

  NumberAction RootTime(plAiArchetypeAssetDocumentWindow* pWindow, const char* szProperty, const char* szDescription)
  {
    return [pWindow, szProperty, szDescription](double fValue)
    { pWindow->SetRootValue(szProperty, plVariant(plTime::MakeFromSeconds(fValue)), szDescription); };
  }

  NumberAction SenseFloat(plAiArchetypeAssetDocumentWindow* pWindow, const char* szSense, const char* szProperty, const char* szDescription)
  {
    return [pWindow, szSense, szProperty, szDescription](double fValue)
    { pWindow->SetSenseValue(szSense, szProperty, plVariant(static_cast<float>(fValue)), szDescription); };
  }

  NumberAction SenseTime(plAiArchetypeAssetDocumentWindow* pWindow, const char* szSense, const char* szProperty, const char* szDescription)
  {
    return [pWindow, szSense, szProperty, szDescription](double fValue)
    { pWindow->SetSenseValue(szSense, szProperty, plVariant(plTime::MakeFromSeconds(fValue)), szDescription); };
  }

  const plAiArchetypeSenseObject& GetSense(const plAiArchetypeAssetObject* pProps, plAiArchetypeSettingsView::Sense sense)
  {
    switch (sense)
    {
      case plAiArchetypeSettingsView::Sense::Damage:
        return pProps->m_Damage;
      case plAiArchetypeSettingsView::Sense::Custom:
        return pProps->m_Custom;
      default:
        return pProps->m_Hearing;
    }
  }

  //////////////////////////////////////////////////////////////////////////
  // memory preview

  /// One sense's memory preview: one stimulus at 0 s (sight: 1 s in view), run through the runtime's perception
  /// rules. Times are in seconds; negative means never.
  struct MemoryPreview
  {
    bool m_bEnabled = true;
    bool m_bSight = false;
    float m_fPeak = 0.0f;
    double m_fLastStimulus = 0.0;
    double m_fMemory = 0.0;
    double m_fZero = -1.0;
    double m_fForgotten = -1.0;
    plDynamicArray<plVec2> m_Samples; ///< (time, confidence) up to the end of the chart
  };

  /// One perception update in the order plAiBrainWorldModule::UpdatePerception runs it: decay with the settings of
  /// the sense that refreshed the record last, then the stimulus.
  void StepPerception(const plAiArchetypeResourceDescriptor& archetype, plUInt8 uiSense, bool bStimulus, plAiPerceivedTarget& ref_target, plTime now, plTime tDiff)
  {
    float fDecayPerSecond = 0.0f;
    plTime memory;
    plAiPerceptionRules::GetDecaySettings(archetype, ref_target.m_uiLastSense, fDecayPerSecond, memory);
    ref_target.m_fConfidence = plAiPerceptionRules::Decay(ref_target.m_fConfidence, fDecayPerSecond, tDiff);

    if (!bStimulus)
      return;

    if (uiSense == plAiStimulusType::Sight)
    {
      // GatherSight: in view, the decay of this update is added back with the gain
      const float fGain = (archetype.m_fConfidenceGainPerSecond + archetype.m_fConfidenceDecayPerSecond) * static_cast<float>(tDiff.GetSeconds());
      ref_target.m_fConfidence = plMath::Min(1.0f, ref_target.m_fConfidence + fGain);
      ref_target.m_LastSightTime = now;
    }
    else
    {
      // GatherStimuli: a stimulus of strength 1, scaled by the sense
      const float fStrength = archetype.GetSenseProfile(uiSense)->m_fStrengthScale;
      ref_target.m_fConfidence = plMath::Clamp(plMath::Max(ref_target.m_fConfidence, fStrength), 0.0f, 1.0f);
    }

    ref_target.m_LastStimulusTime = now;
    ref_target.m_uiLastSense = uiSense;
    ref_target.m_uiSenses |= 1 << uiSense;
  }

  /// Whether the record is forgotten now, with the memory of the sense that refreshed it last. Also returns that
  /// sense's decay rate.
  bool IsForgottenNow(const plAiArchetypeResourceDescriptor& archetype, const plAiPerceivedTarget& target, plTime now, float& out_fDecayPerSecond)
  {
    plTime memory;
    plAiPerceptionRules::GetDecaySettings(archetype, target.m_uiLastSense, out_fDecayPerSecond, memory);
    return plAiPerceptionRules::IsForgotten(target, now, memory);
  }

  MemoryPreview SimulateMemory(const plAiArchetypeResourceDescriptor& archetype, plUInt8 uiSense)
  {
    MemoryPreview preview;
    preview.m_bSight = uiSense == plAiStimulusType::Sight;

    const plAiSenseProfile* pSense = archetype.GetSenseProfile(uiSense);
    preview.m_bEnabled = pSense == nullptr || pSense->m_bEnabled;

    if (!preview.m_bEnabled)
      return preview;

    float fDecayPerSecond = 0.0f;
    plTime memory;
    plAiPerceptionRules::GetDecaySettings(archetype, uiSense, fDecayPerSecond, memory);
    preview.m_fMemory = memory.GetSeconds();

    const plUInt32 uiInViewSteps = static_cast<plUInt32>(s_fInViewSeconds / s_fSimulationStep + 0.5);
    const plUInt32 uiChartSteps = static_cast<plUInt32>(s_fChartSeconds / s_fSimulationStep + 0.5);
    const plUInt32 uiLastStep = static_cast<plUInt32>(s_fSimulationLimit / s_fSimulationStep);
    const plTime tDiff = plTime::MakeFromSeconds(s_fSimulationStep);

    plAiPerceivedTarget target;
    target.m_uiLastSense = uiSense;

    for (plUInt32 uiStep = 0; uiStep <= uiLastStep; ++uiStep)
    {
      const plTime now = plTime::MakeFromSeconds(uiStep * s_fSimulationStep);
      const bool bStimulus = preview.m_bSight ? (uiStep >= 1 && uiStep <= uiInViewSteps) : (uiStep == 0);
      const bool bAfterStimuli = preview.m_bSight ? (uiStep >= uiInViewSteps) : true;

      StepPerception(archetype, uiSense, bStimulus, target, now, tDiff);
      preview.m_fPeak = plMath::Max(preview.m_fPeak, target.m_fConfidence);

      if (uiStep <= uiChartSteps)
        preview.m_Samples.PushBack(plVec2(static_cast<float>(now.GetSeconds()), target.m_fConfidence));

      if (bAfterStimuli && preview.m_fZero < 0.0 && target.m_fConfidence <= 0.0f)
        preview.m_fZero = now.GetSeconds();

      float fRecordDecay = 0.0f;
      if (IsForgottenNow(archetype, target, now, fRecordDecay))
      {
        preview.m_fForgotten = now.GetSeconds();
        break;
      }

      // without decay the confidence never falls, so the record is never forgotten
      if (uiStep > uiChartSteps && target.m_fConfidence > 0.0f && fRecordDecay <= 0.0f)
        break;
    }

    preview.m_fLastStimulus = target.m_LastStimulusTime.GetSeconds();
    return preview;
  }

  void GetMemoryCaption(const plAiArchetypeResourceDescriptor& archetype, plUInt8 uiSense, const MemoryPreview& preview, plStringBuilder& out_sCaption)
  {
    if (!preview.m_bEnabled)
    {
      out_sCaption = "This sense is off. Its stimuli never create or refresh a target.";
      return;
    }

    const plAiSenseProfile* pSense = archetype.GetSenseProfile(uiSense);

    if (pSense == nullptr)
      out_sCaption.SetFormat("In view for 1 s, confidence rises at {}/s to {}. ", plArgF(archetype.m_fConfidenceGainPerSecond, 2), plArgF(preview.m_fPeak, 2));
    else
      out_sCaption.SetFormat("One stimulus at 0 s gives confidence {} (strength 1 \xC3\x97 {}). ", plArgF(preview.m_fPeak, 2), plArgF(pSense->m_fStrengthScale, 2));

    const double fMemoryEnd = preview.m_fLastStimulus + preview.m_fMemory;

    if (preview.m_fPeak <= 0.0f)
      out_sCaption.AppendFormat("It never gains confidence and is forgotten at {} s. ", plArgF(preview.m_fForgotten, 1));
    else if (preview.m_fZero < 0.0)
      out_sCaption.Append("Decay is 0, so it is never forgotten while confidence is above 0. ");
    else if (preview.m_fZero >= fMemoryEnd)
      out_sCaption.AppendFormat("It falls to 0 at {} s, after the memory time, so it is forgotten at once. ", plArgF(preview.m_fZero, 1));
    else
      out_sCaption.AppendFormat("It falls to 0 at {} s, is remembered until {} s, then forgotten. ", plArgF(preview.m_fZero, 1), plArgF(preview.m_fForgotten, 1));

    if (pSense != nullptr && pSense->m_bUseSightSettings)
      out_sCaption.Append("Decay and memory come from Sight.");
  }

  /// The plot area of the memory preview: 0 to 15 s across, confidence 0 to 1 up.
  struct GraphFrame
  {
    float m_fLeft = 34.0f;
    float m_fRight = 0.0f;
    float m_fTop = 22.0f;
    float m_fBottom = 0.0f;

    float X(double t) const
    {
      return m_fLeft + static_cast<float>(plMath::Clamp(t, 0.0, s_fChartSeconds) / s_fChartSeconds) * (m_fRight - m_fLeft);
    }

    float Y(double c) const
    {
      return m_fBottom - static_cast<float>(plMath::Clamp(c, 0.0, 1.0)) * (m_fBottom - m_fTop);
    }
  };

  void DrawGraphGrid(plUiDrawListWriter& ref_writer, const GraphFrame& frame, float fWidth, float fHeight)
  {
    using namespace plEditorUi;
    using namespace plAiEngineUi;

    ref_writer.Rect(plRectFloat(0, 0, fWidth, fHeight), Field(), 3.0f);
    ref_writer.RectOutline(plRectFloat(0, 0, fWidth, fHeight), Line(), 1.0f, 3.0f);

    // a faint line every second, a stronger one every 5 s
    const plColorGammaUB minor = Mix(Field(), Line(), 0.35f);
    for (plUInt32 t = 1; t < 15; ++t)
    {
      if (t % 5 != 0)
        ref_writer.Line(plVec2(frame.X(t), frame.m_fTop), plVec2(frame.X(t), frame.m_fBottom), minor);
    }

    for (double t : {0.0, 5.0, 10.0, 15.0})
      ref_writer.Line(plVec2(frame.X(t), frame.m_fTop), plVec2(frame.X(t), frame.m_fBottom), Line());

    for (double c : {0.0, 0.5, 1.0})
      ref_writer.Line(plVec2(frame.m_fLeft, frame.Y(c)), plVec2(frame.m_fRight, frame.Y(c)), Line());

    // axis labels
    ref_writer.Text(plRectFloat(0, frame.Y(1.0) - 7.0f, frame.m_fLeft - 6.0f, 14.0f), "1.0", Dim(), MinFontSize, plUiTextHAlign::Right, plUiTextVAlign::Center, true);
    ref_writer.Text(plRectFloat(0, frame.Y(0.5) - 7.0f, frame.m_fLeft - 6.0f, 14.0f), "0.5", Dim(), MinFontSize, plUiTextHAlign::Right, plUiTextVAlign::Center, true);
    ref_writer.Text(plRectFloat(0, frame.Y(0.0) - 7.0f, frame.m_fLeft - 6.0f, 14.0f), "0", Dim(), MinFontSize, plUiTextHAlign::Right, plUiTextVAlign::Center, true);

    const float fLabelTop = frame.m_fBottom + 8.0f;
    ref_writer.Text(plRectFloat(frame.X(0.0), fLabelTop, 40.0f, 14.0f), "0 s", Dim(), MinFontSize, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
    ref_writer.Text(plRectFloat(frame.X(5.0) - 20.0f, fLabelTop, 40.0f, 14.0f), "5 s", Dim(), MinFontSize, plUiTextHAlign::Center, plUiTextVAlign::Center, true);
    ref_writer.Text(plRectFloat(frame.X(10.0) - 20.0f, fLabelTop, 40.0f, 14.0f), "10 s", Dim(), MinFontSize, plUiTextHAlign::Center, plUiTextVAlign::Center, true);
    ref_writer.Text(plRectFloat(frame.X(15.0) - 40.0f, fLabelTop, 40.0f, 14.0f), "15 s", Dim(), MinFontSize, plUiTextHAlign::Right, plUiTextVAlign::Center, true);
  }

  /// Sight: the second in view, shaded. Other senses: a marker at the stimulus.
  void DrawGraphStimulus(plUiDrawListWriter& ref_writer, const GraphFrame& frame, const MemoryPreview& preview)
  {
    using namespace plEditorUi;
    using namespace plAiEngineUi;

    if (preview.m_bSight)
    {
      // an opaque tint: translucent fills blend in linear space and come out much stronger
      ref_writer.Rect(plRectFloat(frame.X(0.0), frame.m_fTop, frame.X(s_fInViewSeconds) - frame.X(0.0), frame.m_fBottom - frame.m_fTop), Mix(Field(), Accent(), 0.08f));
      ref_writer.Text(plRectFloat(frame.X(0.0) + 2.0f, 4.0f, 60.0f, 14.0f), "in view", Dim(), MinFontSize);
      return;
    }

    const float x = frame.X(0.0);
    const plVec2 marker[] = {plVec2(x, frame.m_fTop - 8.0f), plVec2(x - 4.0f, frame.m_fTop - 14.0f), plVec2(x + 4.0f, frame.m_fTop - 14.0f)};
    ref_writer.Triangles(plMakeArrayPtr(marker), AiColor());
    ref_writer.Text(plRectFloat(x + 7.0f, 4.0f, 60.0f, 14.0f), "stimulus", Dim(), MinFontSize);
  }

  /// The confidence, up to where it reaches 0.
  void DrawGraphCurve(plUiDrawListWriter& ref_writer, const GraphFrame& frame, const MemoryPreview& preview)
  {
    const double fEnd = preview.m_fZero >= 0.0 ? plMath::Min(preview.m_fZero, s_fChartSeconds) : s_fChartSeconds;

    plDynamicArray<plVec2> points;
    for (const plVec2& sample : preview.m_Samples)
    {
      if (sample.x > fEnd + 0.001)
        break;

      points.PushBack(plVec2(frame.X(sample.x), frame.Y(sample.y)));
    }

    if (points.GetCount() >= 2)
      ref_writer.Polyline(points.GetArrayPtr(), plAiEngineUi::AiColor(), 2.0f);
  }

  /// The memory window, measured from the last stimulus.
  void DrawGraphMemoryLine(plUiDrawListWriter& ref_writer, const GraphFrame& frame, const MemoryPreview& preview)
  {
    using namespace plEditorUi;

    const double fMemoryEnd = preview.m_fLastStimulus + preview.m_fMemory;
    if (fMemoryEnd > s_fChartSeconds || preview.m_fZero < 0.0)
      return;

    const float x = frame.X(fMemoryEnd);
    ref_writer.Line(plVec2(x, frame.m_fTop), plVec2(x, frame.m_fBottom), Dim(), 1.0f, 3.0f, 3.0f);

    if (x - frame.X(0.0) <= 70.0f)
      return;

    plStringBuilder sText;
    sText.SetFormat("memory {} s", plArgF(preview.m_fMemory, 1));

    if (x > frame.m_fRight - 60.0f)
      ref_writer.Text(plRectFloat(x + 2.0f - 100.0f, 4.0f, 100.0f, 14.0f), sText, Dim(), MinFontSize, plUiTextHAlign::Right);
    else
      ref_writer.Text(plRectFloat(x - 50.0f, 4.0f, 100.0f, 14.0f), sText, Dim(), MinFontSize, plUiTextHAlign::Center);
  }

  /// Remembered at confidence 0 until the memory runs out.
  void DrawGraphRemembered(plUiDrawListWriter& ref_writer, const GraphFrame& frame, const MemoryPreview& preview)
  {
    using namespace plEditorUi;
    using namespace plAiEngineUi;

    const bool bLater = preview.m_fForgotten < 0.0 || preview.m_fForgotten > preview.m_fZero;
    if (preview.m_fZero >= s_fChartSeconds || !bLater)
      return;

    const float xa = frame.X(preview.m_fZero);
    const float xb = frame.X(preview.m_fForgotten < 0.0 ? s_fChartSeconds : preview.m_fForgotten);
    ref_writer.Line(plVec2(xa, frame.m_fBottom - 1.0f), plVec2(xb, frame.m_fBottom - 1.0f), AiColor(), 2.0f, 5.0f, 4.0f);

    if (xb - xa > 70.0f)
      ref_writer.Text(plRectFloat(xa, frame.m_fBottom - 18.0f, xb - xa, 14.0f), "remembered", AiColor(), MinFontSize, plUiTextHAlign::Center);
  }

  /// Where the record is forgotten, or an arrow to it when that is after the chart.
  void DrawGraphForgotten(plUiDrawListWriter& ref_writer, const GraphFrame& frame, const MemoryPreview& preview)
  {
    using namespace plEditorUi;

    plStringBuilder sText;
    const double fForgotten = preview.m_fForgotten;

    if (fForgotten >= 0.0 && fForgotten <= s_fChartSeconds)
    {
      const float x = frame.X(fForgotten);
      ref_writer.Line(plVec2(x, frame.m_fTop), plVec2(x, frame.m_fBottom), Danger(), 1.0f, 3.0f, 3.0f);
      ref_writer.Circle(plVec2(x, frame.m_fBottom - 1.0f), 3.0f, Danger());

      sText.SetFormat("forgotten {} s", plArgF(fForgotten, 1));

      if (x > frame.m_fRight - 90.0f)
        ref_writer.Text(plRectFloat(x - 5.0f - 120.0f, frame.m_fTop + 7.0f, 120.0f, 14.0f), sText, Danger(), MinFontSize, plUiTextHAlign::Right);
      else
        ref_writer.Text(plRectFloat(x + 5.0f, frame.m_fTop + 7.0f, 120.0f, 14.0f), sText, Danger(), MinFontSize);

      return;
    }

    if (fForgotten < 0.0)
      sText = "forgotten after more than an hour \xE2\x86\x92";
    else
      sText.SetFormat("forgotten at {} s \xE2\x86\x92", plArgF(fForgotten, 1));

    ref_writer.Text(plRectFloat(frame.m_fRight - 2.0f - 220.0f, frame.m_fTop + 7.0f, 220.0f, 14.0f), sText, Danger(), MinFontSize, plUiTextHAlign::Right);
  }

  void DrawMemoryGraph(plUiDrawListWriter& ref_writer, const MemoryPreview& preview, float fWidth, float fHeight)
  {
    using namespace plEditorUi;

    GraphFrame frame;
    frame.m_fRight = fWidth - 13.0f;
    frame.m_fBottom = fHeight - 24.0f;

    DrawGraphGrid(ref_writer, frame, fWidth, fHeight);

    if (!preview.m_bEnabled)
    {
      ref_writer.Text(plRectFloat(frame.m_fLeft, frame.m_fTop, frame.m_fRight - frame.m_fLeft, frame.m_fBottom - frame.m_fTop), "Sense off: stimuli of this type are ignored", Muted(), FontSize - 2, plUiTextHAlign::Center);
      return;
    }

    DrawGraphStimulus(ref_writer, frame, preview);
    DrawGraphMemoryLine(ref_writer, frame, preview);
    DrawGraphCurve(ref_writer, frame, preview);

    if (preview.m_fZero < 0.0)
    {
      ref_writer.Text(plRectFloat(frame.m_fLeft, frame.Y(preview.m_fPeak) + 8.0f, frame.m_fRight - frame.m_fLeft, 16.0f), "never forgotten while confidence > 0", Muted(), MinFontSize, plUiTextHAlign::Center);
      return;
    }

    DrawGraphRemembered(ref_writer, frame, preview);
    DrawGraphForgotten(ref_writer, frame, preview);
  }

  //////////////////////////////////////////////////////////////////////////
  // migration preview

  /// One behavior of the migration preview. Utilities are before the weights.
  struct MigrationRow
  {
    plString m_sName;
    bool m_bValid = false;
    plInt32 m_iCategory = 0;
    plUInt32 m_uiConsiderations = 0;
    float m_fUtilityV1 = 0.0f;
    float m_fUtilityV2 = 0.0f;
    float m_fWeight = 0.0f; ///< base weight times the archetype's scale
  };

  /// A consideration's example value: its input at 70 % of its range, through its curve.
  float ExampleValue(const plAiBehaviorPeekConsideration& cons)
  {
    const float fRaw = plMath::Lerp(cons.m_fInputMin, cons.m_fInputMax, s_fExampleInput);
    return plAiAssetUi::EvaluateConsideration(cons.m_Curve, fRaw, cons.m_fInputMin, cons.m_fInputMax, nullptr, cons.m_bLegacyCurveDomain);
  }

  MigrationRow GetMigrationRow(const plAiBehaviorPeek& peek, plStringView sReference, float fWeightScale)
  {
    MigrationRow row;
    row.m_bValid = peek.m_bValid;

    if (!peek.m_bValid)
    {
      row.m_sName = sReference.IsEmpty() ? plStringView("<no behavior set>") : sReference;
      return row;
    }

    row.m_sName = peek.m_sName;
    row.m_iCategory = peek.m_iCategory;
    row.m_fWeight = peek.m_fWeight * fWeightScale;

    // the runtime scores only considerations with an input
    float fProduct = 1.0f;
    for (const auto& cons : peek.m_Considerations)
    {
      if (!cons.m_bHasInput)
        continue;

      fProduct *= ExampleValue(cons);
      ++row.m_uiConsiderations;
    }

    fProduct = plMath::Max(fProduct, 0.0f);
    row.m_fUtilityV1 = plAiUtilityEvaluator::CombineConsiderations(fProduct, row.m_uiConsiderations, 1);
    row.m_fUtilityV2 = plAiUtilityEvaluator::CombineConsiderations(fProduct, row.m_uiConsiderations, 2);
    return row;
  }

  /// Whether two behaviors of a band swap places between the versions, once weight and scale apply.
  bool OrderSwaps(const MigrationRow& a, const MigrationRow& b)
  {
    auto score = [](const MigrationRow& row, float fUtility)
    { return plMath::Clamp(fUtility * row.m_fWeight, 0.0f, 1.0f); };

    const float fBefore = score(a, a.m_fUtilityV1) - score(b, b.m_fUtilityV1);
    const float fAfter = score(a, a.m_fUtilityV2) - score(b, b.m_fUtilityV2);
    return (fBefore > 0.0f && fAfter < 0.0f) || (fBefore < 0.0f && fAfter > 0.0f);
  }

  bool BandOrderChanges(plArrayPtr<const MigrationRow> rows, plInt32 iCategory)
  {
    for (plUInt32 i = 0; i < rows.GetCount(); ++i)
    {
      for (plUInt32 j = i + 1; j < rows.GetCount(); ++j)
      {
        const bool bBand = rows[i].m_bValid && rows[j].m_bValid && rows[i].m_iCategory == iCategory && rows[j].m_iCategory == iCategory;
        if (bBand && OrderSwaps(rows[i], rows[j]))
          return true;
      }
    }

    return false;
  }

  void GetOrderNote(plArrayPtr<const MigrationRow> rows, plStringBuilder& out_sNote)
  {
    plStringBuilder sBands;

    for (plInt32 iCategory : plAiEngineUi::GetCategoryLadder())
    {
      if (!BandOrderChanges(rows, iCategory))
        continue;

      if (!sBands.IsEmpty())
        sBands.Append(", ");

      sBands.Append(plAiAssetUi::CategoryName(iCategory));
    }

    if (sBands.IsEmpty())
      out_sNote = "Before weight. Order inside each band: unchanged.";
    else
      out_sNote.SetFormat("Before weight. Order changes in: {}.", sBands);
  }

  /// "+0.12" in the accent, "-0.12" in the danger colour, "0.00" dim.
  void FormatChange(float fChange, plStringBuilder& out_sText, plColorGammaUB& out_color)
  {
    using namespace plEditorUi;

    if (plMath::Abs(fChange) < 0.005f)
    {
      out_sText = "0.00";
      out_color = Dim();
      return;
    }

    out_sText.SetFormat("{}{}", fChange > 0.0f ? "+" : "-", plArgF(plMath::Abs(fChange), 2));
    out_color = fChange > 0.0f ? Accent() : Danger();
  }

  /// The x positions of the migration table's columns; the numbers are right aligned at their column's end.
  struct MigrationColumns
  {
    float m_fName = 12.0f;
    float m_fCount = 0.0f;
    float m_fV1 = 0.0f;
    float m_fV2 = 0.0f;
    float m_fChange = 0.0f;
    float m_fEnd = 0.0f;
  };

  MigrationColumns GetMigrationColumns(float fTable)
  {
    MigrationColumns columns;
    columns.m_fEnd = fTable - 12.0f;
    columns.m_fChange = columns.m_fEnd - 64.0f;
    columns.m_fV2 = columns.m_fChange - 76.0f;
    columns.m_fV1 = columns.m_fV2 - 76.0f;
    columns.m_fCount = columns.m_fV1 - 104.0f;
    return columns;
  }

  void BuildMigrationRow(plUiElement* pBox, float y, float fTable, const MigrationColumns& columns, const MigrationRow& row)
  {
    using namespace plEditorUi;
    using namespace plAiEngineUi;

    constexpr float fHeight = 24.0f;

    Label(pBox, plRectFloat(columns.m_fName, y, plMath::Max(columns.m_fCount - columns.m_fName - 8.0f, 10.0f), fHeight), row.m_sName, row.m_bValid ? Foreground() : Dim(), FontSize - 1);

    if (row.m_bValid)
    {
      plStringBuilder sChange;
      plColorGammaUB changeColor;
      FormatChange(row.m_fUtilityV2 - row.m_fUtilityV1, sChange, changeColor);

      Label(pBox, plRectFloat(columns.m_fCount, y, columns.m_fV1 - columns.m_fCount, fHeight), FormatNumber(row.m_uiConsiderations, 0), Muted(), FontSize - 2, true);
      Label(pBox, plRectFloat(columns.m_fV1, y, columns.m_fV2 - columns.m_fV1, fHeight), FormatNumber(row.m_fUtilityV1, 2), Muted(), FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;
      Label(pBox, plRectFloat(columns.m_fV2, y, columns.m_fChange - columns.m_fV2, fHeight), FormatNumber(row.m_fUtilityV2, 2), Muted(), FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;
      Label(pBox, plRectFloat(columns.m_fChange, y, columns.m_fEnd - columns.m_fChange, fHeight), sChange, changeColor, FontSize - 2, true)->m_HAlign = plUiTextHAlign::Right;
    }
    else
    {
      Label(pBox, plRectFloat(columns.m_fCount, y, columns.m_fEnd - columns.m_fCount, fHeight), "unresolved - asset not found", Dim(), FontSize - 2);
    }

    auto* pLine = Panel(pBox);
    Place(pLine, plRectFloat(0, y + fHeight - 1.0f, fTable, 1.0f));
    pLine->m_Color = Line();
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plAiArchetypeStackView

plAiArchetypeStackView::plAiArchetypeStackView(plAiArchetypeAssetDocumentWindow* pWindow)
  : plAiEngineView("AiArchetypeStack")
  , m_pWindow(pWindow)
{
}

plAiArchetypeStackView::~plAiArchetypeStackView() = default;

bool plAiArchetypeStackView::IsRebuildBlocked() const
{
  return m_pWindow->GetDocument()->GetCommandHistory()->IsInTransaction();
}

void plAiArchetypeStackView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const float fWidth = vSize.x;
  const float fTop = BuildHeader(pRoot, fWidth);

  plUiScrollView* pBody = AddScrollBody(pRoot, plRectFloat(0, fTop, fWidth, plMath::Max(vSize.y - fTop, 10.0f)), "Stack.Body", 0.0f, 0.0f);

  // the hint above the bands
  {
    plUiPanel* pHint = Panel(pBody);
    pHint->m_vOffsets.w = 28.0f;
    pHint->m_Color = plColorGammaUB(0, 0, 0, 0);
    Label(pHint, plRectFloat(10.0f, 0, fWidth - 20.0f, 28.0f), "grouped by the category, higher band always wins; inside a band the highest utility wins", Dim(), FontSize - 2);
  }

  // sort the rows into the ladder bands; the last band holds the unresolved references
  const auto& behaviors = m_pWindow->GetArchetypeDocument()->GetProperties()->m_Behaviors;
  const plArrayPtr<const plInt32> ladder = GetCategoryLadder();

  plDynamicArray<plUuid> guids;
  m_pWindow->GetBehaviorGuids(guids);

  plHybridArray<plHybridArray<plUInt32, 8>, 8> bands;
  bands.SetCount(ladder.GetCount() + 1);

  for (plUInt32 i = 0; i < guids.GetCount() && i < behaviors.GetCount(); ++i)
  {
    const plAiBehaviorPeek& peek = plAiBehaviorPeekCache::Get(behaviors[i].m_sBehavior);
    bands[peek.m_bValid ? BandForCategory(peek.m_iCategory) : ladder.GetCount()].PushBack(i);
  }

  for (plUInt32 uiBand = 0; uiBand < bands.GetCount(); ++uiBand)
  {
    const bool bUnresolved = uiBand == ladder.GetCount();
    const char* szTitle = bUnresolved ? "Unresolved" : plAiAssetUi::CategoryName(ladder[uiBand]);

    Section(pBody, szTitle, bUnresolved ? Border() : CategoryColor(ladder[uiBand]), FormatNumber(bands[uiBand].GetCount(), 0));

    for (plUInt32 i : bands[uiBand])
    {
      const plAiBehaviorPeek& peek = plAiBehaviorPeekCache::Get(behaviors[i].m_sBehavior);
      BuildRow(pBody, fWidth, i, guids[i], peek, behaviors[i].m_sBehavior, behaviors[i].m_fWeightScale);
    }

    if (bands[uiBand].IsEmpty())
    {
      plUiPanel* pEmpty = Panel(pBody);
      pEmpty->m_vOffsets.w = 26.0f;
      pEmpty->m_Color = plColorGammaUB(0, 0, 0, 0);
      Label(pEmpty, plRectFloat(12.0f, 0, fWidth - 24.0f, 24.0f), bUnresolved ? "all references resolve" : "no behaviors", Dim(), FontSize - 2);
    }
  }

  // the selected behavior, read-only
  Section(pBody, "Peek", AiColor());
  BuildPeek(pBody, fWidth);
}

float plAiArchetypeStackView::BuildHeader(plUiElement* pRoot, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();

  plUiPanel* pRow = Panel(pRoot);
  pRow->m_Color = Surface();

  plAiFlowLayout flow(6.0f, 3.0f, fWidth - 6.0f, ControlHeight, 2.0f);

  auto addLabel = [&](plStringView sText)
  {
    plUiText* pLabel = Label(pRow, flow.Take(TextWidth(sText) + 8.0f), sText, Dim());
    pLabel->m_HAlign = plUiTextHAlign::Center;
  };

  flow.Group(TextWidth("Team") + 10.0f + 130.0f);
  addLabel("Team");
  AddTextField(pRow, flow.Take(130.0f), "Team", pProps->m_sTeam, {}, "The team this archetype belongs to. Decides who is a friend and who is a foe.", [this](const plString& sText)
    { m_pWindow->SetRootValue("Team", plVariant(sText.GetData()), "Change Team"); });

  // the blackboard template: its asset name, browse
  {
    plStringBuilder sDisplay = pProps->m_sBlackboardTemplate;
    if (!pProps->m_sBlackboardTemplate.IsEmpty())
    {
      auto asset = plAssetCurator::GetSingleton()->FindSubAsset(pProps->m_sBlackboardTemplate);
      if (asset.isValid())
        sDisplay = asset->GetName();
    }

    flow.Group(TextWidth("Blackboard template") + 10.0f + 160.0f + 2.0f + 24.0f);
    addLabel("Blackboard template");

    const plRectFloat field = flow.Take(160.0f);
    auto* pWell = Panel(pRow);
    Place(pWell, field);
    pWell->m_Color = Field();
    pWell->m_fBorderWidth = 1.0f;
    pWell->m_BorderColor = Line();
    pWell->m_vCornerRadius = plVec4(4);

    Label(pWell, plRectFloat(8.0f, 0, field.width - 16.0f, field.height), sDisplay.IsEmpty() ? plStringView("<no blackboard template>") : sDisplay.GetView(), sDisplay.IsEmpty() ? Dim() : Muted());
    AddClickArea(pRow, field, "Blackboard", pProps->m_sBlackboardTemplate, {});

    AddIconButton(pRow, flow.Take(24.0f), "Blackboard.Browse", Icon::More, "Select a blackboard template asset", [this]()
      { Defer([this]()
          { m_pWindow->BrowseBlackboardTemplate(); }); });
  }

  const float fAdd = TextWidth("Add Behavior") + 38.0f;
  flow.AlignRight(fAdd);
  AddTextButton(pRow, flow.Take(fAdd), "AddBehavior", "Add Behavior", "Add a behavior asset to this archetype", [this]()
    { Defer([this]()
        { m_pWindow->AddBehavior(); }); },
    true);

  const float fHeight = flow.GetBottom() + 3.0f;
  Place(pRow, plRectFloat(0, 0, fWidth, fHeight));

  auto* pLine = Panel(pRoot);
  Place(pLine, plRectFloat(0, fHeight, fWidth, 1.0f));
  pLine->m_Color = Line();

  return fHeight + 1.0f;
}

void plAiArchetypeStackView::BuildRow(plUiElement* pBody, float fWidth, plUInt32 uiIndex, const plUuid& guid, const plAiBehaviorPeek& peek, plStringView sReference, float fWeightScale)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const bool bSelected = guid == m_pWindow->GetSelectedRow();

  plUiPanel* pRow = Panel(pBody);
  pRow->m_vOffsets.w = s_fRowHeight;
  pRow->m_Color = bSelected ? Selection() : plColorGammaUB(0, 0, 0, 0);

  // the row selects; its tooltip tells where the asset is
  const plStringBuilder sTooltip = peek.m_bValid ? plStringBuilder(peek.m_sAbsPath) : plStringBuilder("The referenced behavior asset could not be resolved.");
  plUiButton* pHit = AddClickArea(pRow, plRectFloat(0, 0, fWidth, s_fRowHeight), Name("Row.", uiIndex, ""), sTooltip, [this, guid]()
    { m_pWindow->SelectBehaviorRow(guid); });
  Fill(pHit);
  pHit->m_HoverColor = bSelected ? Selection() : Mix(Surface(), Raised(), 0.6f);

  const float fCenter = s_fRowHeight * 0.5f;
  float x = 12.0f;

  plUiImage* pIcon = SvgImage(pRow, ":/AssetIcons/AiBehavior.svg");
  Place(pIcon, plRectFloat(x, fCenter - 7.5f, 15.0f, 15.0f));
  pIcon->m_Color = AiColor();
  x += 15.0f + 10.0f;

  // name, base weight and this archetype's scale
  plStringBuilder sName;
  if (peek.m_bValid)
    sName = peek.m_sName;
  else
    sName = sReference.IsEmpty() ? plStringView("<no behavior set>") : sReference;

  Label(pRow, plRectFloat(x, 0, 140.0f, s_fRowHeight), sName, Foreground(), FontSize);
  x += 140.0f + 10.0f;

  plStringBuilder sWeight;
  if (peek.m_bValid)
    sWeight.SetFormat("weight {}", plArgF(peek.m_fWeight, 2));
  else
    sWeight = "weight ?";

  const float fWeight = CodeWidth(sWeight);
  Label(pRow, plRectFloat(x, 0, fWeight, s_fRowHeight), sWeight, Dim(), FontSize - 2, true);
  x += fWeight + 10.0f;

  const float fTimes = CodeWidth("\xC3\x97");
  Label(pRow, plRectFloat(x, 0, fTimes, s_fRowHeight), "\xC3\x97", Dim(), FontSize - 2, true);
  x += fTimes + 10.0f;

  AddNumberField(pRow, plRectFloat(x, fCenter - ControlHeight * 0.5f, 56.0f, ControlHeight), Name("Row.", uiIndex, ".Scale"), FormatNumber(fWeightScale, 2), {}, "This archetype's WeightScale for the behavior, from 0 to 1.", [this, guid](const plString& sText)
    {
    double fValue = 0.0;
    if (ParseClamped(sText, 0.0, 1.0, fValue))
      m_pWindow->SetWeightScale(guid, fValue);
    else
      Invalidate(); });
  x += 56.0f + 10.0f;

  // the effective weight: base weight times scale, full bar at 1 (utility is capped there)
  {
    const plRectFloat bar(x, fCenter - 3.0f, s_fBarWidth, 6.0f);

    auto* pTrack = Panel(pRow);
    Place(pTrack, bar);
    pTrack->m_Color = Field();
    pTrack->m_fBorderWidth = 1.0f;
    pTrack->m_BorderColor = Line();
    pTrack->m_vCornerRadius = plVec4(3);

    const float fFraction = peek.m_bValid ? plMath::Clamp(peek.m_fWeight * fWeightScale, 0.0f, 1.0f) : 0.0f;
    if (fFraction > 0.0f)
    {
      auto* pFill = Panel(pRow);
      Place(pFill, plRectFloat(bar.x + 1.0f, bar.y + 1.0f, (bar.width - 2.0f) * fFraction, bar.height - 2.0f));
      pFill->m_Color = Muted();
      pFill->m_vCornerRadius = plVec4(2);
    }

    AddClickArea(pRow, plRectFloat(bar.x, 0, bar.width, s_fRowHeight), Name("Row.", uiIndex, ".Weight"), "effective weight = base weight \xC3\x97 archetype scale (full bar = 1, where utility is capped)", [this, guid]()
      { m_pWindow->SelectBehaviorRow(guid); });

    x += s_fBarWidth + 10.0f;
  }

  // open and remove at the right end, the tags between
  const float fButtons = fWidth - 6.0f - 2.0f * SmallButtonSize - 2.0f;

  plStringBuilder sTags;
  GetTags(peek, sTags);
  Label(pRow, plRectFloat(x, 0, plMath::Max(fButtons - 8.0f - x, 10.0f), s_fRowHeight), sTags, Dim(), FontSize - 2, true);

  AddIconButton(pRow, plRectFloat(fButtons, fCenter - SmallButtonSize * 0.5f, SmallButtonSize, SmallButtonSize), Name("Row.", uiIndex, ".Open"), Icon::External, "Open the behavior asset", [this, guid]()
    { m_pWindow->OpenBehavior(guid); });

  AddIconButton(pRow, plRectFloat(fButtons + SmallButtonSize + 2.0f, fCenter - SmallButtonSize * 0.5f, SmallButtonSize, SmallButtonSize), Name("Row.", uiIndex, ".Remove"), Icon::Clear, "Remove this behavior from the archetype", [this, guid]()
    { m_pWindow->RemoveBehavior(guid); });
}

float plAiArchetypeStackView::BuildPeek(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  // the selected row's behavior reference
  plString sReference;
  bool bSelected = false;
  {
    plDynamicArray<plUuid> guids;
    m_pWindow->GetBehaviorGuids(guids);

    for (plUInt32 i = 0; i < guids.GetCount(); ++i)
    {
      if (guids[i] == m_pWindow->GetSelectedRow())
      {
        sReference = m_pWindow->GetBehaviorRef(i);
        bSelected = true;
      }
    }
  }

  const plAiBehaviorPeek* pPeek = bSelected ? &plAiBehaviorPeekCache::Get(sReference) : nullptr;
  const bool bValid = pPeek != nullptr && pPeek->m_bValid;
  const plUInt32 uiCurves = bValid ? pPeek->m_Considerations.GetCount() : 0;

  float fHeight = 6.0f + s_fPeekLine + 10.0f;
  if (bValid)
    fHeight += s_fPeekLine + plMath::Max<plUInt32>(uiCurves, 1) * s_fPeekCurveRow + s_fPeekLine;

  plUiPanel* pPanel = Panel(pBody);
  pPanel->m_vOffsets.w = fHeight;
  pPanel->m_Color = plColorGammaUB(0, 0, 0, 0);
  AddDrawList(pPanel, plRectFloat(0, 0, fWidth, fHeight), "Stack.Peek");

  plUiDrawListWriter writer;
  const float fTextWidth = fWidth - 20.0f;
  float y = 6.0f;

  if (!bSelected)
  {
    writer.Text(plRectFloat(10.0f, y, fTextWidth, s_fPeekLine), "select a behavior row to peek at its considerations", Dim(), FontSize - 2);
  }
  else if (!bValid)
  {
    writer.Text(plRectFloat(10.0f, y, fTextWidth, s_fPeekLine), "unresolved - asset not found", Dim(), FontSize - 2);
  }
  else
  {
    // the name and its tags, then category, weight, commit and logic
    const plStringBuilder sName(pPeek->m_sName);
    const float fName = TextWidth(sName);
    writer.Text(plRectFloat(10.0f, y, fName, s_fPeekLine), sName, Foreground(), FontSize - 1);

    plStringBuilder sTags;
    GetTags(*pPeek, sTags);
    if (!sTags.IsEmpty())
    {
      sTags.Prepend("\xC2\xB7 ");
      writer.Text(plRectFloat(10.0f + fName + 6.0f, y, plMath::Max(fTextWidth - fName - 6.0f, 10.0f), s_fPeekLine), sTags, Dim(), FontSize - 1);
    }

    y += s_fPeekLine;

    plStringBuilder sSummary;
    sSummary.SetFormat("{} \xC2\xB7 weight {} \xC2\xB7 commit {} \xC2\xB7 {}", plAiAssetUi::CategoryName(pPeek->m_iCategory), plArgF(pPeek->m_fWeight, 2), plArgF(pPeek->m_fCommitBonus, 2), pPeek->m_sLogic.IsEmpty() ? plStringView("no logic") : pPeek->m_sLogic.GetView());
    writer.Text(plRectFloat(10.0f, y, fTextWidth, s_fPeekLine), sSummary, CategoryColor(pPeek->m_iCategory), FontSize - 2, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
    y += s_fPeekLine;

    // each consideration with its curve
    for (const auto& cons : pPeek->m_Considerations)
    {
      DrawCurve(writer, plRectFloat(10.0f, y + 2.0f, 48.0f, 24.0f), &cons.m_Curve, cons.m_bLegacyCurveDomain, false);

      plStringBuilder sLabel;
      sLabel.SetFormat("{} [{} \xE2\x86\x92 {}]", cons.m_sLabel, cons.m_fInputMin, cons.m_fInputMax);
      writer.Text(plRectFloat(66.0f, y, fTextWidth - 56.0f, s_fPeekCurveRow), sLabel, Muted(), FontSize - 1);
      y += s_fPeekCurveRow;
    }

    if (pPeek->m_Considerations.IsEmpty())
    {
      writer.Text(plRectFloat(10.0f, y, fTextWidth, s_fPeekCurveRow), "no considerations - scores its raw weight", Dim(), FontSize - 2);
      y += s_fPeekCurveRow;
    }

    writer.Text(plRectFloat(10.0f, y, fTextWidth, s_fPeekLine), "read-only peek \xC2\xB7 use the row's open button to edit", Dim(), MinFontSize);
  }

  Publish(GetBoard(), writer, "Stack.Peek");
  return fHeight;
}

//////////////////////////////////////////////////////////////////////////
// plAiArchetypeSettingsView

plAiArchetypeSettingsView::plAiArchetypeSettingsView(plAiArchetypeAssetDocumentWindow* pWindow)
  : plAiEngineView("AiArchetypeSettings")
  , m_pWindow(pWindow)
{
}

plAiArchetypeSettingsView::~plAiArchetypeSettingsView() = default;

bool plAiArchetypeSettingsView::IsRebuildBlocked() const
{
  return m_pWindow->GetDocument()->GetCommandHistory()->IsInTransaction();
}

void plAiArchetypeSettingsView::BuildContent(plUiPanel* pRoot, const plVec2& vSize)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const float fWidth = vSize.x;
  const float fBodyHeight = plMath::Max(vSize.y - StatusRowHeight - 1.0f, 10.0f);

  plUiScrollView* pBody = AddScrollBody(pRoot, plRectFloat(0, 0, fWidth, fBodyHeight), "Settings.Body", 0.0f, 0.0f);

  BuildSectionHeader(pBody, fWidth, "Senses.Header", "Senses", "how stimuli become targets, and how long they are remembered", &m_bSensesOpen);
  if (m_bSensesOpen)
    BuildSenses(pBody, fWidth);

  BuildSectionHeader(pBody, fWidth, "Reactivity.Header", "Reactivity", "when to decide again, and how readily to switch", &m_bReactivityOpen);
  if (m_bReactivityOpen)
    BuildReactivity(pBody, fWidth);

  BuildSectionHeader(pBody, fWidth, "Scoring.Header", "Scoring", "how considerations combine into a utility", &m_bScoringOpen);
  if (m_bScoringOpen)
    BuildScoring(pBody, fWidth);

  StatusRow(pRoot, "Settings.Status", "Settings.StatusHint");
  UpdateStatus();
}

//////////////////////////////////////////////////////////////////////////
// controls

void plAiArchetypeSettingsView::BuildSectionHeader(plUiElement* pBody, float fWidth, plStringView sName, plStringView sTitle, plStringView sHint, bool* pOpen)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plStringBuilder sTitleWithChevron(*pOpen ? TreeExpanded : TreeCollapsed, sTitle);
  plUiPanel* pHeader = Section(pBody, sTitleWithChevron, AiColor(), {}, sHint);

  AddClickArea(pHeader, plRectFloat(0, 0, fWidth, SectionHeight), sName, *pOpen ? "Hide this section" : "Show this section", [this, pOpen]()
    {
    *pOpen = !*pOpen;
    Invalidate(); });
}

void plAiArchetypeSettingsView::AddRowLabel(plUiElement* pBox, const plRectFloat& cell, plStringView sName, plStringView sLabel, plStringView sTooltip, bool bEnabled)
{
  using namespace plAiEngineUi;

  const plRectFloat rect(cell.x, cell.y, s_fFieldLabel - 4.0f, cell.height);
  Label(pBox, rect, sLabel, bEnabled ? plEditorUi::Muted() : DisabledText());

  // hovering the label tells what the setting does
  const plStringBuilder sLabelName(sName, ".Label");
  AddClickArea(pBox, rect, sLabelName, sTooltip, {});
}

void plAiArchetypeSettingsView::AddNumberRow(plUiElement* pBox, const plRectFloat& cell, const NumberRow& row)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  AddRowLabel(pBox, cell, row.m_sName, row.m_sLabel, row.m_sTooltip, row.m_bEnabled);

  const plRectFloat field(cell.x + s_fFieldLabel, cell.y + (cell.height - s_fFieldHeight) * 0.5f, s_fFieldWidth, s_fFieldHeight);
  const plString sValue = FormatNumber(row.m_fValue, row.m_uiDecimals);

  if (row.m_bEnabled)
  {
    const NumberAction onValue = row.m_OnValue;
    const double fMin = row.m_fMin;
    const double fMax = row.m_fMax;

    AddNumberField(pBox, field, row.m_sName, sValue, row.m_sUnit, row.m_sTooltip, [this, onValue, fMin, fMax](const plString& sText)
      {
      double fNew = 0.0;
      if (ParseClamped(sText, fMin, fMax, fNew))
        onValue(fNew);
      else
        Invalidate(); });
  }
  else
  {
    DisabledField(pBox, field, sValue, row.m_sUnit);
    AddClickArea(pBox, field, row.m_sName, row.m_sTooltip, {});
  }

  if (!row.m_sAfter.IsEmpty())
  {
    const float x = field.x + field.width + 6.0f;
    Label(pBox, plRectFloat(x, cell.y, plMath::Max(cell.x + cell.width - x, 10.0f), cell.height), row.m_sAfter, row.m_bEnabled ? Dim() : DisabledText(), FontSize - 2);
  }
}

void plAiArchetypeSettingsView::AddSwitch(plUiElement* pParent, const plRectFloat& rect, plStringView sName, bool bOn, plStringView sTooltip, Action onToggle)
{
  using namespace plEditorUi;

  const plStringBuilder sOn(sName, ".SwitchOn");
  const plStringBuilder sOff(sName, ".SwitchOff");
  GetBoard().SetEntryValue(sOn.GetView(), bOn);
  GetBoard().SetEntryValue(sOff.GetView(), !bOn);

  plUiPanel* pSwitch = Switch(pParent, sName, sOn, sOff);
  plAiEngineUi::Place(pSwitch, rect);

  AddAction(sName, sTooltip, std::move(onToggle));
}

void plAiArchetypeSettingsView::AddSegment(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sLabel, bool bOn, plStringView sTooltip, Action onClick)
{
  using namespace plEditorUi;

  plUiButton* pButton = AddTextButton(pParent, rect, sName, sLabel, sTooltip, std::move(onClick));
  pButton->m_fBorderWidth = 0.0f;
  pButton->m_vCornerRadius = plVec4(0);
  pButton->m_Color = bOn ? Selection() : Plate();
  pButton->m_HoverColor = bOn ? Selection() : Raised();

  static_cast<plUiText*>(pButton->m_Children[0])->m_Color = bOn ? Foreground() : Muted();
}

//////////////////////////////////////////////////////////////////////////
// senses

void plAiArchetypeSettingsView::BuildSenses(plUiElement* pBody, float fWidth)
{
  BuildSenseTabs(pBody, fWidth);

  if (m_Tab == Sense::Sight)
    BuildSightFields(pBody, fWidth);
  else
    BuildSenseFields(pBody, fWidth);

  BuildMemoryPreview(pBody, fWidth);

  BuildSubHeader(pBody, fWidth, "All senses");
  BuildMaxTargets(pBody, fWidth);

  BuildSubHeader(pBody, fWidth, "Alert levels");
  BuildAlertLevels(pBody, fWidth);
}

void plAiArchetypeSettingsView::BuildSenseTabs(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();

  plUiPanel* pRow = BodyRow(pBody, s_fTabHeight);
  pRow->m_Color = Plate();

  auto* pLine = Panel(pRow);
  Place(pLine, plRectFloat(0, s_fTabHeight - 1.0f, fWidth, 1.0f));
  pLine->m_Color = Line();

  float x = 0.0f;

  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_szSenseNames); ++i)
  {
    const Sense sense = static_cast<Sense>(i);
    const bool bCurrent = sense == m_Tab;
    const bool bOff = sense != Sense::Sight && !GetSense(pProps, sense).m_bEnabled;

    plStringBuilder sUpper(s_szSenseNames[i]);
    sUpper.ToUpper();

    const float fText = TextWidth(sUpper, MinFontSize);
    const float fTab = 24.0f + fText + (bOff ? BadgeWidth("off") + 6.0f : 0.0f);

    Label(pRow, plRectFloat(x + 12.0f, 0, fText, s_fTabHeight), sUpper, bCurrent ? Foreground() : Dim(), MinFontSize)->m_Overflow = plUiTextOverflow::Overflow;

    if (bOff)
      Badge(pRow, x + 12.0f + fText + 6.0f, (s_fTabHeight - BadgeHeight) * 0.5f, "off", Dim());

    if (bCurrent)
    {
      auto* pUnderline = Panel(pRow);
      Place(pUnderline, plRectFloat(x, s_fTabHeight - 2.0f, fTab, 2.0f));
      pUnderline->m_Color = Accent();
    }

    AddClickArea(pRow, plRectFloat(x, 0, fTab, s_fTabHeight), Name("Senses.Tab.", i, ""), s_szSenseTooltips[i], [this, sense]()
      {
      m_Tab = sense;
      Invalidate(); });

    x += fTab;
  }

  // where the shown sense's decay and memory come from
  if (m_Tab == Sense::Sight)
    return;

  const plAiArchetypeSenseObject& shown = GetSense(pProps, m_Tab);
  if (!shown.m_bEnabled)
    return;

  plUiText* pSource = Label(pRow, plRectFloat(x + 8.0f, 0, plMath::Max(fWidth - x - 20.0f, 10.0f), s_fTabHeight), shown.m_bUseSightSettings ? "decay and memory from Sight" : "own decay and memory", Dim(), FontSize - 2);
  pSource->m_HAlign = plUiTextHAlign::Right;
}

void plAiArchetypeSettingsView::BuildSightFields(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();
  const FieldGrid grid = MakeGrid(fWidth, true);
  plUiPanel* pBox = BodyRow(pBody, grid.GetHeight(4));

  NumberRow gain;
  gain.m_sName = "Sight.Gain";
  gain.m_sLabel = "Gain per second";
  gain.m_sTooltip = "How fast confidence rises while a target is in view.";
  gain.m_sUnit = "/s";
  gain.m_fValue = pProps->m_fConfidenceGainPerSecond;
  gain.m_fMin = 0.0; // 0: sight never raises confidence, as the property allows
  gain.m_fMax = 100.0;
  gain.m_OnValue = RootFloat(m_pWindow, "ConfidenceGainPerSec", "Change Gain per Second");
  AddNumberRow(pBox, grid.Cell(0), gain);

  NumberRow decay;
  decay.m_sName = "Sight.Decay";
  decay.m_sLabel = "Decay per second";
  decay.m_sTooltip = "How fast confidence falls once nothing is perceived. 0 = never falls.";
  decay.m_sUnit = "/s";
  decay.m_fValue = pProps->m_fConfidenceDecayPerSecond;
  decay.m_fMax = 100.0;
  decay.m_OnValue = RootFloat(m_pWindow, "ConfidenceDecayPerSec", "Change Decay per Second");
  AddNumberRow(pBox, grid.Cell(1), decay);

  NumberRow memory;
  memory.m_sName = "Sight.Memory";
  memory.m_sLabel = "Memory";
  memory.m_sTooltip = "How long a target is kept after the last stimulus, once confidence is 0.";
  memory.m_sUnit = "s";
  memory.m_uiDecimals = 1;
  memory.m_fValue = pProps->m_TargetMemoryDuration.GetSeconds();
  memory.m_fMax = 600.0;
  memory.m_OnValue = RootTime(m_pWindow, "TargetMemoryDuration", "Change Memory");
  AddNumberRow(pBox, grid.Cell(2), memory);

  // the sensor object, by name
  const plRectFloat cell = grid.Cell(3);
  const char* szSensorTooltip = "Name of the child object that carries the sensor components. Empty = search all children.";
  AddRowLabel(pBox, cell, "Sight.Sensor", "Sensor object", szSensorTooltip, true);

  const plRectFloat field(cell.x + s_fFieldLabel, cell.y + (cell.height - s_fFieldHeight) * 0.5f, plMath::Min(150.0f, plMath::Max(cell.width - s_fFieldLabel, 40.0f)), s_fFieldHeight);
  AddTextField(pBox, field, "Sight.Sensor", pProps->m_sSensorObjectName, "all children", szSensorTooltip, [this](const plString& sText)
    { m_pWindow->SetRootValue("SensorObjectName", plVariant(sText.GetData()), "Change Sensor Object"); });
}

void plAiArchetypeSettingsView::BuildSenseFields(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();
  const plAiArchetypeSenseObject& sense = GetSense(pProps, m_Tab);
  const char* szSense = s_szSenseNames[static_cast<plUInt32>(m_Tab)];

  const bool bOn = sense.m_bEnabled;
  const bool bUseSight = sense.m_bUseSightSettings;

  const FieldGrid grid = MakeGrid(fWidth, true);
  plUiPanel* pBox = BodyRow(pBody, grid.GetHeight(6));

  // enabled: a switch
  {
    const plRectFloat cell = grid.Cell(0);
    const plStringBuilder sName(szSense, ".Enabled");
    const char* szTooltip = "Turn this sense on or off. When off, stimuli of this type are ignored.";

    AddRowLabel(pBox, cell, sName, "Enabled", szTooltip, true);
    AddSwitch(pBox, plRectFloat(cell.x + s_fFieldLabel, cell.y + (cell.height - SwitchHeight) * 0.5f, SwitchWidth, SwitchHeight), sName, bOn, szTooltip, [this, szSense, bOn]()
      { m_pWindow->SetSenseValue(szSense, "Enabled", plVariant(!bOn), bOn ? "Turn Sense Off" : "Turn Sense On"); });
  }

  // use sight settings: a check box
  {
    const plRectFloat cell = grid.Cell(1);
    const plStringBuilder sName(szSense, ".UseSight");
    const char* szTooltip = "Use the decay and memory from Sight instead of this sense's own.";

    AddRowLabel(pBox, cell, sName, "Use sight settings", szTooltip, bOn);
    AddCheckBox(pBox, plRectFloat(cell.x + s_fFieldLabel, cell.y, CheckBoxSize + 6.0f, cell.height), sName, {}, bUseSight, szTooltip, [this, szSense, bUseSight]()
      { m_pWindow->SetSenseValue(szSense, "UseSightSettings", plVariant(!bUseSight), "Toggle Use Sight Settings"); },
      bOn);
  }

  const plStringBuilder sRange(szSense, ".Range");
  NumberRow range;
  range.m_sName = sRange;
  range.m_sLabel = "Range scale";
  range.m_sTooltip = "Scales the radius of the stimuli this sense receives. 2 = hears twice as far.";
  range.m_sUnit = "\xC3\x97";
  range.m_fValue = sense.m_fRangeScale;
  range.m_fMax = 10.0;
  range.m_bEnabled = bOn;
  range.m_OnValue = SenseFloat(m_pWindow, szSense, "RangeScale", "Change Range Scale");
  AddNumberRow(pBox, grid.Cell(2), range);

  const plStringBuilder sStrength(szSense, ".Strength");
  NumberRow strength;
  strength.m_sName = sStrength;
  strength.m_sLabel = "Strength scale";
  strength.m_sTooltip = "Scales the confidence a stimulus gives. Confidence is capped at 1.";
  strength.m_sUnit = "\xC3\x97";
  strength.m_fValue = sense.m_fStrengthScale;
  strength.m_fMax = 10.0;
  strength.m_bEnabled = bOn;
  strength.m_OnValue = SenseFloat(m_pWindow, szSense, "StrengthScale", "Change Strength Scale");
  AddNumberRow(pBox, grid.Cell(3), strength);

  // decay and memory show the sight values while the sense uses them
  const plStringBuilder sDecay(szSense, ".Decay");
  NumberRow decay;
  decay.m_sName = sDecay;
  decay.m_sLabel = "Decay per second";
  decay.m_sTooltip = "How fast confidence falls after a stimulus. Only used when Use sight settings is off.";
  decay.m_sUnit = "/s";
  decay.m_fValue = bUseSight ? pProps->m_fConfidenceDecayPerSecond : sense.m_fDecayPerSecond;
  decay.m_fMax = 100.0;
  decay.m_bEnabled = bOn && !bUseSight;
  decay.m_OnValue = SenseFloat(m_pWindow, szSense, "DecayPerSec", "Change Decay per Second");
  AddNumberRow(pBox, grid.Cell(4), decay);

  const plStringBuilder sMemory(szSense, ".Memory");
  NumberRow memory;
  memory.m_sName = sMemory;
  memory.m_sLabel = "Memory";
  memory.m_sTooltip = "How long the target is kept after the last stimulus, once confidence is 0. Only used when Use sight settings is off.";
  memory.m_sUnit = "s";
  memory.m_uiDecimals = 1;
  memory.m_fValue = bUseSight ? pProps->m_TargetMemoryDuration.GetSeconds() : sense.m_MemoryDuration.GetSeconds();
  memory.m_fMax = 600.0;
  memory.m_bEnabled = bOn && !bUseSight;
  memory.m_OnValue = SenseTime(m_pWindow, szSense, "MemoryDuration", "Change Memory");
  AddNumberRow(pBox, grid.Cell(5), memory);
}

void plAiArchetypeSettingsView::BuildMemoryPreview(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  // the document's settings as the runtime reads them
  plAiArchetypeResourceDescriptor archetype;
  m_pWindow->GetArchetypeDocument()->GetSettings(archetype);

  const plUInt8 uiSense = static_cast<plUInt8>(m_Tab);
  const MemoryPreview preview = SimulateMemory(archetype, uiSense);

  plStringBuilder sCaption;
  GetMemoryCaption(archetype, uiSense, preview, sCaption);

  const float fGraphWidth = plMath::Max(fWidth - 2.0f * s_fMargin, 160.0f);
  const plUInt32 uiLines = plMath::Max<plUInt32>(2, CountLines(sCaption, fGraphWidth, FontSize - 2));
  const float fCaptionTop = 6.0f + s_fGraphHeight + 5.0f;

  plUiPanel* pPanel = BodyRow(pBody, fCaptionTop + uiLines * s_fTextLine + 4.0f);

  const plRectFloat graph(s_fMargin, 6.0f, fGraphWidth, s_fGraphHeight);
  AddDrawList(pPanel, graph, "Senses.Memory");
  AddClickArea(pPanel, graph, "Senses.Memory.Hover", "Confidence over 15 s after one stimulus. Updates as you edit.", {});

  Paragraph(pPanel, plRectFloat(s_fMargin, fCaptionTop, fGraphWidth, uiLines * s_fTextLine), sCaption, Dim(), FontSize - 2);

  plUiDrawListWriter writer;
  DrawMemoryGraph(writer, preview, fGraphWidth, s_fGraphHeight);
  Publish(GetBoard(), writer, "Senses.Memory");
}

void plAiArchetypeSettingsView::BuildMaxTargets(plUiElement* pBody, float fWidth)
{
  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();
  const FieldGrid grid = MakeGrid(fWidth, false);
  plUiPanel* pBox = BodyRow(pBody, grid.GetHeight(1));

  NumberRow maxTargets;
  maxTargets.m_sName = "Senses.MaxTargets";
  maxTargets.m_sLabel = "Max targets";
  maxTargets.m_sTooltip = "How many perceived targets an agent keeps. When full, the least confident one is dropped.";
  maxTargets.m_sAfter = "the least confident target is dropped";
  maxTargets.m_uiDecimals = 0;
  maxTargets.m_fValue = pProps->m_uiMaxTargets;
  maxTargets.m_fMin = 1.0;
  maxTargets.m_fMax = 64.0;
  maxTargets.m_OnValue = [this](double fValue)
  { m_pWindow->SetRootValue("MaxTargets", plVariant(static_cast<plUInt8>(fValue + 0.5)), "Change Max Targets"); };

  AddNumberRow(pBox, grid.Cell(0), maxTargets);
}

void plAiArchetypeSettingsView::BuildAlertLevels(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  struct Level
  {
    const char* m_szName;
    const char* m_szText;
    const char* m_szTooltip;
    plColorGammaUB m_Color;
  };

  const Level levels[] = {
    {"Unaware", "No remembered targets.", "No targets are remembered.", CategoryColor(plAiBehaviorCategory::Idle)},
    {"Suspicious", "Targets were only heard.", "Every remembered target was only heard.", CategoryColor(plAiBehaviorCategory::Investigate)},
    {"Searching", "Seen, did damage or reported by an ally, but not just now.", "A target was seen, did damage or was reported by an ally, but not in the last second.", CategoryColor(plAiBehaviorCategory::Command)},
    {"Alert", "Seen or did damage in the last second.", "A target was seen or did damage in the last second.", Danger()},
  };

  const float fStrip = fWidth - 2.0f * s_fMargin;
  const float fCell = fStrip / PL_ARRAY_SIZE(levels);
  const float fText = fCell - 18.0f;

  plUInt32 uiLines = 1;
  for (const Level& level : levels)
    uiLines = plMath::Max(uiLines, CountLines(level.m_szText, fText, FontSize - 2));

  const float fCellHeight = 26.0f + uiLines * 15.0f + 8.0f;
  plUiPanel* pRow = BodyRow(pBody, 4.0f + fCellHeight + 6.0f);

  auto* pStrip = Panel(pRow);
  Place(pStrip, plRectFloat(s_fMargin, 4.0f, fStrip, fCellHeight));
  pStrip->m_Color = Plate();
  pStrip->m_fBorderWidth = 1.0f;
  pStrip->m_BorderColor = Border();
  pStrip->m_vCornerRadius = plVec4(4);
  pStrip->m_bClipChildren = true;

  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(levels); ++i)
  {
    const float x = i * fCell;

    if (i > 0)
    {
      auto* pDivider = Panel(pStrip);
      Place(pDivider, plRectFloat(x, 0, 1.0f, fCellHeight));
      pDivider->m_Color = Line();
    }

    auto* pBar = Panel(pStrip);
    Place(pBar, plRectFloat(x, 0, fCell, 2.0f));
    pBar->m_Color = levels[i].m_Color;

    plStringBuilder sUpper(levels[i].m_szName);
    sUpper.ToUpper();

    Label(pStrip, plRectFloat(x + 9.0f, 8.0f, fText, 14.0f), sUpper, Foreground(), MinFontSize);
    Label(pStrip, plRectFloat(x + 9.0f, 8.0f, fText, 14.0f), FormatNumber(i, 0), Dim(), MinFontSize, true)->m_HAlign = plUiTextHAlign::Right;
    Paragraph(pStrip, plRectFloat(x + 9.0f, 26.0f, fText, uiLines * 15.0f), levels[i].m_szText, Muted(), FontSize - 2);

    AddClickArea(pStrip, plRectFloat(x, 0, fCell, fCellHeight), Name("Senses.Alert.", i, ""), levels[i].m_szTooltip, {});
  }

  const char* szNote = "Worked out from the remembered targets after every perception update. Behaviors can score on it with the Alert Level input (0-3). It is also on the blackboard as Ai_AlertLevel.";
  const plUInt32 uiNoteLines = CountLines(szNote, fStrip, FontSize - 2);

  plUiPanel* pNote = BodyRow(pBody, uiNoteLines * s_fTextLine + 10.0f);
  Paragraph(pNote, plRectFloat(s_fMargin, 2.0f, fStrip, uiNoteLines * s_fTextLine), szNote, Dim(), FontSize - 2);
}

//////////////////////////////////////////////////////////////////////////
// reactivity

void plAiArchetypeSettingsView::BuildReactivity(plUiElement* pBody, float fWidth)
{
  BuildSubHeader(pBody, fWidth, "Decide early on");
  BuildWakeFields(pBody, fWidth);

  BuildSubHeader(pBody, fWidth, "Target switching");
  BuildSwitchingFields(pBody, fWidth);

  BuildSubHeader(pBody, fWidth, "Behavior lock");
  BuildLockFields(pBody, fWidth);

  if (!m_pWindow->GetArchetypeDocument()->GetProperties()->m_MaxLockDuration.IsPositive())
    BuildHint(pBody, fWidth, "A behavior that keeps reporting Locked holds the agent forever.");
}

void plAiArchetypeSettingsView::BuildWakeFields(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;

  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();
  const FieldGrid grid = MakeGrid(fWidth, true);
  plUiPanel* pBox = BodyRow(pBody, grid.GetHeight(3));

  const bool bStimulus = pProps->m_bWakeOnStimulus;
  const bool bEnd = pProps->m_bWakeOnBehaviorEnd;

  AddCheckBox(pBox, grid.Cell(0), "Reactivity.WakeOnStimulus", "Perceived stimulus", bStimulus, "Decide again straight away when the agent perceives a stimulus, instead of waiting for the next scheduled decision.", [this, bStimulus]()
    { m_pWindow->SetRootValue("WakeOnStimulus", plVariant(!bStimulus), "Toggle Decide Early on Stimulus"); },
    true, Foreground());

  AddCheckBox(pBox, grid.Cell(1), "Reactivity.WakeOnEnd", "Behavior finished", bEnd, "Decide again straight away when the active behavior reports Done or Failed.", [this, bEnd]()
    { m_pWindow->SetRootValue("WakeOnBehaviorEnd", plVariant(!bEnd), "Toggle Decide Early on Behavior End"); },
    true, Foreground());

  NumberRow spacing;
  spacing.m_sName = "Reactivity.WakeSpacing";
  spacing.m_sLabel = "Minimum wake spacing";
  spacing.m_sTooltip = "Early decisions are at least this far apart, so a burst of stimuli can't flood the agent.";
  spacing.m_sUnit = "s";
  spacing.m_fValue = pProps->m_MinWakeSpacing.GetSeconds();
  spacing.m_fMax = 5.0;
  spacing.m_bEnabled = bStimulus || bEnd;
  spacing.m_OnValue = RootTime(m_pWindow, "MinWakeSpacing", "Change Minimum Wake Spacing");
  AddNumberRow(pBox, grid.Cell(2), spacing);
}

void plAiArchetypeSettingsView::BuildSwitchingFields(plUiElement* pBody, float fWidth)
{
  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();
  const FieldGrid grid = MakeGrid(fWidth, true);
  plUiPanel* pBox = BodyRow(pBody, grid.GetHeight(2));

  NumberRow margin;
  margin.m_sName = "Reactivity.SwitchMargin";
  margin.m_sLabel = "Switch margin";
  margin.m_sTooltip = "A new target must score at least this much higher than the current one before the agent switches.";
  margin.m_fValue = pProps->m_fTargetSwitchMargin;
  margin.m_fMax = 1.0;
  margin.m_OnValue = RootFloat(m_pWindow, "TargetSwitchMargin", "Change Switch Margin");
  AddNumberRow(pBox, grid.Cell(0), margin);

  NumberRow hold;
  hold.m_sName = "Reactivity.MinHold";
  hold.m_sLabel = "Minimum hold";
  hold.m_sTooltip = "The current target must have been held at least this long before the agent can switch.";
  hold.m_sUnit = "s";
  hold.m_fValue = pProps->m_TargetMinHold.GetSeconds();
  hold.m_fMax = 30.0;
  hold.m_OnValue = RootTime(m_pWindow, "TargetMinHold", "Change Minimum Hold");
  AddNumberRow(pBox, grid.Cell(1), hold);
}

void plAiArchetypeSettingsView::BuildLockFields(plUiElement* pBody, float fWidth)
{
  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();
  const FieldGrid grid = MakeGrid(fWidth, false);
  plUiPanel* pBox = BodyRow(pBody, grid.GetHeight(1));

  NumberRow lock;
  lock.m_sName = "Reactivity.LockTimeout";
  lock.m_sLabel = "Lock timeout";
  lock.m_sTooltip = "A behavior lock is ignored after this long. 0 = no limit.";
  lock.m_sUnit = "s";
  lock.m_sAfter = "0 = no limit";
  lock.m_uiDecimals = 1;
  lock.m_fValue = pProps->m_MaxLockDuration.GetSeconds();
  lock.m_fMax = 600.0;
  lock.m_OnValue = RootTime(m_pWindow, "MaxLockDuration", "Change Lock Timeout");
  AddNumberRow(pBox, grid.Cell(0), lock);
}

//////////////////////////////////////////////////////////////////////////
// scoring

void plAiArchetypeSettingsView::BuildScoring(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const bool bLegacy = m_pWindow->GetArchetypeDocument()->GetProperties()->m_uiScoringVersion < 2;

  // the preview only compares against version 2 from version 1
  if (!bLegacy)
    m_bMigrationOpen = false;

  BuildVersionRow(pBody, fWidth);

  // what the version does, and its formula
  {
    const char* szText = bLegacy ? "Multiplies the considerations, then applies the compensation once. More considerations score lower." : "Takes the geometric mean of the considerations. The number of considerations doesn't change the score.";
    const char* szFormula = bLegacy ? "p = product; if n > 1: p + (1 - p) \xC3\x97 p \xC3\x97 (1 - 1/n)" : "p = product; p ^ (1/n)";

    const float fBox = fWidth - 2.0f * s_fMargin;
    const float fInner = fBox - 18.0f;
    const plUInt32 uiLines = CountLines(szText, fInner, FontSize - 2);
    const float fHeight = 6.0f + uiLines * s_fTextLine + 16.0f + 6.0f;

    plUiPanel* pRow = BodyRow(pBody, fHeight + 6.0f);

    auto* pDesc = Panel(pRow);
    Place(pDesc, plRectFloat(s_fMargin, 0, fBox, fHeight));
    pDesc->m_Color = Plate();
    pDesc->m_fBorderWidth = 1.0f;
    pDesc->m_BorderColor = Line();
    pDesc->m_vCornerRadius = plVec4(3);

    Paragraph(pDesc, plRectFloat(9.0f, 6.0f, fInner, uiLines * s_fTextLine), szText, Muted(), FontSize - 2);
    Label(pDesc, plRectFloat(9.0f, 6.0f + uiLines * s_fTextLine, fInner, 16.0f), szFormula, Dim(), MinFontSize, true);
  }

  // what it means for this asset
  {
    plUiPanel* pRow = BodyRow(pBody, 22.0f);
    Label(pRow, plRectFloat(s_fMargin, 0, fWidth - 2.0f * s_fMargin, 18.0f), bLegacy ? "Existing assets stay on version 1 until migrated." : "This archetype uses version 2. Behaviors may need their weights re-tuned.", Dim(), FontSize - 2);
  }

  if (m_bMigrationOpen)
    BuildMigration(pBody, fWidth);
}

void plAiArchetypeSettingsView::BuildVersionRow(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  const bool bLegacy = m_pWindow->GetArchetypeDocument()->GetProperties()->m_uiScoringVersion < 2;
  const char* szTooltip = "Which formula combines a behavior's considerations into one utility.";

  plUiPanel* pRow = BodyRow(pBody, 30.0f);
  AddRowLabel(pRow, plRectFloat(s_fMargin, 0, s_fFieldLabel, 30.0f), "Scoring.Version", "Scoring version", szTooltip, true);

  // the version as a segmented control: two buttons in one bordered well
  const float fV1 = TextWidth("1 (legacy)") + 24.0f;
  const float fV2 = TextWidth("2") + 24.0f;

  auto* pWell = Panel(pRow);
  Place(pWell, plRectFloat(s_fMargin + s_fFieldLabel, 5.0f, fV1 + fV2 + 3.0f, 20.0f));
  pWell->m_Color = Border();
  pWell->m_vCornerRadius = plVec4(4);
  pWell->m_bClipChildren = true;

  AddSegment(pWell, plRectFloat(1.0f, 1.0f, fV1, 18.0f), "Scoring.V1", "1 (legacy)", bLegacy, szTooltip, [this, bLegacy]()
    {
    if (bLegacy)
      return;

    m_pWindow->SetScoringVersion(1); });

  AddSegment(pWell, plRectFloat(2.0f + fV1, 1.0f, fV2, 18.0f), "Scoring.V2", "2", !bLegacy, szTooltip, [this, bLegacy]()
    {
    if (!bLegacy)
      return;

    m_bMigrationOpen = false;
    m_pWindow->SetScoringVersion(2); });

  // the migration preview opens below
  const float fPreview = TextWidth("Preview migration") + 20.0f;
  const plRectFloat preview(fWidth - s_fMargin - fPreview, 4.0f, fPreview, 22.0f);
  const char* szPreviewTooltip = bLegacy ? "Show how every behavior's utility changes under version 2" : "Already on version 2";

  plUiButton* pPreview = AddTextButton(pRow, preview, "Scoring.Preview", "Preview migration", szPreviewTooltip, [this]()
    {
    m_bMigrationOpen = !m_bMigrationOpen;
    Invalidate(); },
    false, bLegacy);

  if (m_bMigrationOpen)
    MakePrimary(pPreview);
}

void plAiArchetypeSettingsView::BuildMigration(plUiElement* pBody, float fWidth)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  // every behavior of the archetype, before and after
  const auto& behaviors = m_pWindow->GetArchetypeDocument()->GetProperties()->m_Behaviors;

  plHybridArray<MigrationRow, 16> rows;
  for (const auto& behavior : behaviors)
    rows.PushBack(GetMigrationRow(plAiBehaviorPeekCache::Get(behavior.m_sBehavior), behavior.m_sBehavior, behavior.m_fWeightScale));

  constexpr float fHeader = 26.0f;
  constexpr float fRowHeight = 24.0f;
  constexpr float fFooter = 36.0f;

  const float fTable = fWidth - 2.0f * s_fMargin;
  const plUInt32 uiShownRows = plMath::Max<plUInt32>(rows.GetCount(), 1);
  const float fHeight = fHeader + uiShownRows * fRowHeight + fFooter;

  plUiPanel* pRow = BodyRow(pBody, 4.0f + fHeight + 12.0f);

  auto* pBox = Panel(pRow);
  Place(pBox, plRectFloat(s_fMargin, 4.0f, fTable, fHeight));
  pBox->m_Color = Plate();
  pBox->m_fBorderWidth = 1.0f;
  pBox->m_BorderColor = Border();
  pBox->m_vCornerRadius = plVec4(4);
  pBox->m_bClipChildren = true;

  // the column titles
  const MigrationColumns columns = GetMigrationColumns(fTable);

  auto columnTitle = [&](float x, float fColumn, plStringView sName, plStringView sTitle, plStringView sTooltip, bool bRight)
  {
    plStringBuilder sUpper(sTitle);
    sUpper.ToUpper();

    plUiText* pTitle = Label(pBox, plRectFloat(x, 0, fColumn, fHeader), sUpper, Dim(), MinFontSize);
    pTitle->m_HAlign = bRight ? plUiTextHAlign::Right : plUiTextHAlign::Left;

    if (!sTooltip.IsEmpty())
      AddClickArea(pBox, plRectFloat(x, 0, fColumn, fHeader), sName, sTooltip, {});
  };

  columnTitle(columns.m_fName, columns.m_fCount - columns.m_fName, "Migration.Behavior", "Behavior", {}, false);
  columnTitle(columns.m_fCount, columns.m_fV1 - columns.m_fCount, "Migration.Count", "Considerations", "Considerations with an input", false);
  columnTitle(columns.m_fV1, columns.m_fV2 - columns.m_fV1, "Migration.V1", "Utility v1", "Utility before weight, version 1", true);
  columnTitle(columns.m_fV2, columns.m_fChange - columns.m_fV2, "Migration.V2", "Utility v2", "Utility before weight, version 2", true);
  columnTitle(columns.m_fChange, columns.m_fEnd - columns.m_fChange, "Migration.Change", "Change", "Utility v2 minus utility v1", true);

  auto* pLine = Panel(pBox);
  Place(pLine, plRectFloat(0, fHeader - 1.0f, fTable, 1.0f));
  pLine->m_Color = Line();

  // one row per behavior
  float y = fHeader;
  for (const MigrationRow& row : rows)
  {
    BuildMigrationRow(pBox, y, fTable, columns, row);
    y += fRowHeight;
  }

  if (rows.IsEmpty())
  {
    Label(pBox, plRectFloat(columns.m_fName, y, fTable - 24.0f, fRowHeight), "no behaviors", Dim(), FontSize - 2);
    y += fRowHeight;
  }

  plStringBuilder sOrder;
  GetOrderNote(rows.GetArrayPtr(), sOrder);
  BuildMigrationFooter(pBox, y, fTable, sOrder);
}

void plAiArchetypeSettingsView::BuildMigrationFooter(plUiElement* pBox, float fTop, float fWidth, plStringView sOrder)
{
  using namespace plEditorUi;
  using namespace plAiEngineUi;

  constexpr float fFooter = 36.0f;

  auto* pFooter = Panel(pBox);
  Place(pFooter, plRectFloat(0, fTop, fWidth, fFooter));
  pFooter->m_Color = Background();

  const float fApply = TextWidth("Switch to version 2") + 20.0f;
  const float fCancel = TextWidth("Cancel") + 20.0f;
  const float fApplyX = fWidth - 12.0f - fApply;
  const float fCancelX = fApplyX - 8.0f - fCancel;

  Label(pFooter, plRectFloat(12.0f, 0, plMath::Max(fCancelX - 20.0f, 10.0f), fFooter), sOrder, Dim(), FontSize - 2);

  AddTextButton(pFooter, plRectFloat(fCancelX, 7.0f, fCancel, 22.0f), "Migration.Cancel", "Cancel", "Close the preview without changing the version", [this]()
    {
    m_bMigrationOpen = false;
    Invalidate(); });

  plUiButton* pApply = AddTextButton(pFooter, plRectFloat(fApplyX, 7.0f, fApply, 22.0f), "Migration.Apply", "Switch to version 2", "Set this archetype to scoring version 2", [this]()
    {
    m_bMigrationOpen = false;
    m_pWindow->SetScoringVersion(2); });

  MakePrimary(pApply);
}

void plAiArchetypeSettingsView::UpdateStatus()
{
  const plAiArchetypeAssetObject* pProps = m_pWindow->GetArchetypeDocument()->GetProperties();

  plUInt32 uiSensesOn = 1;
  for (const plAiArchetypeSenseObject* pSense : {&pProps->m_Hearing, &pProps->m_Damage, &pProps->m_Custom})
  {
    if (pSense->m_bEnabled)
      ++uiSensesOn;
  }

  plStringBuilder sStatus;
  sStatus.SetFormat("Behaviors {} \xC2\xB7 Senses on {} / 4 \xC2\xB7 Max targets {} \xC2\xB7 Scoring version {}", pProps->m_Behaviors.GetCount(), uiSensesOn, static_cast<plUInt32>(pProps->m_uiMaxTargets), static_cast<plUInt32>(pProps->m_uiScoringVersion));

  GetBoard().SetEntryValue("Settings.Status", plString(sStatus));
  GetBoard().SetEntryValue("Settings.StatusHint", plString("Hover a label for what it does"));
}
