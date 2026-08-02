#pragma once

#include <AiPlugin/UtilityAI/Decision/AiBehaviorLogic.h>
#include <AiPlugin/UtilityAI/Decision/AiInput.h>
#include <Core/ResourceManager/Resource.h>
#include <Foundation/Tracks/Curve1D.h>

using plAiBehaviorResourceHandle = plTypedResourceHandle<class plAiBehaviorResource>;

/// \brief One consideration of a utility behavior: an input, a normalization range and a response curve.
///
/// Scoring: raw = input->Evaluate(), x = saturate((raw - min) / (max - min)), utility = curve(x).
/// An empty curve means identity (utility = x).
struct PL_AIPLUGIN_DLL plAiConsiderationDesc
{
  plUniquePtr<plAiInput> m_pInput;
  float m_fInputMin = 0.0f;
  float m_fInputMax = 1.0f;
  plCurve1D m_ResponseCurve;

  /// \brief Sorts control points and builds the linear approximation. Must be called once after filling the curve.
  void PrepareCurve();

  /// \brief Evaluates this consideration for the given context. Returns a score in [0,1].
  float Evaluate(const plAiScoringContext& context) const;
};

/// \brief Everything that defines one reusable utility AI behavior.
struct PL_AIPLUGIN_DLL plAiBehaviorResourceDescriptor
{
  plAiBehaviorResourceDescriptor();
  ~plAiBehaviorResourceDescriptor();
  plAiBehaviorResourceDescriptor(plAiBehaviorResourceDescriptor&&) = default;
  plAiBehaviorResourceDescriptor& operator=(plAiBehaviorResourceDescriptor&&) = default;

  plHashedString m_sName;
  plEnum<plAiBehaviorCategory> m_Category;
  float m_fWeight = 1.0f;      ///< scales the final utility within the category
  float m_fCommitBonus = 0.2f; ///< how much better a candidate must score to replace this behavior while it is active
  plTime m_CooldownDuration;   ///< after deactivation, the behavior scores zero for this long

  plDynamicArray<plAiConsiderationDesc> m_Considerations;

  /// Prototype of the execution logic. Cloned per agent on activation.
  plUniquePtr<plAiBehaviorLogic> m_pLogic;

  /// \brief True if any consideration input requires a perceived target.
  bool NeedsTarget() const;

  /// \brief Collects all blackboard entries read by consideration inputs (for async-scoring snapshots).
  void CollectBlackboardEntries(plDynamicArray<plHashedString>& out_entries) const;

  plResult Serialize(plStreamWriter& inout_stream) const;
  plResult Deserialize(plStreamReader& inout_stream);
};

/// \brief A data-driven utility AI behavior, authored through the 'AI Behavior' asset.
class PL_AIPLUGIN_DLL plAiBehaviorResource : public plResource
{
  PL_ADD_DYNAMIC_REFLECTION(plAiBehaviorResource, plResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plAiBehaviorResource);
  PL_RESOURCE_DECLARE_CREATEABLE(plAiBehaviorResource, plAiBehaviorResourceDescriptor);

public:
  plAiBehaviorResource();
  ~plAiBehaviorResource();

  const plAiBehaviorResourceDescriptor& GetDescriptor() const { return m_Descriptor; }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* Stream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage) override;

  plAiBehaviorResourceDescriptor m_Descriptor;
};
