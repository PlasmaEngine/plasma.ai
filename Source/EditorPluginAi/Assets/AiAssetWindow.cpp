#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorPluginAi/Assets/AiArchetypeAsset.h>
#include <EditorPluginAi/Assets/AiAssetWindow.h>
#include <EditorPluginAi/Assets/AiBehaviorAsset.h>
#include <EditorPluginAi/Assets/AiEqsQueryAsset.h>
#include <EditorPluginAi/Validation/AiValidationView.h>
#include <Foundation/Utilities/AssetFileHeader.h>

plAiAssetDocumentWindow::plAiAssetDocumentWindow(plDocument* pDocument, const char* szUniqueName, const char* szResourceType)
  : plDocumentWindow(pDocument)
{
  m_sUniqueName = szUniqueName;
  m_sResourceType = szResourceType;

  GetDocument()->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plAiAssetDocumentWindow::PropertyEventHandler, this));
  GetDocument()->GetObjectManager()->m_StructureEvents.AddEventHandler(plMakeDelegate(&plAiAssetDocumentWindow::StructureEventHandler, this));

  plStringBuilder sMapping;

  sMapping.SetFormat("{}MenuBar", m_sUniqueName);
  SetMenuBarActions(sMapping);

  sMapping.SetFormat("{}ToolBar", m_sUniqueName);
  SetToolBarActions(sMapping);
}

plAiAssetDocumentWindow::~plAiAssetDocumentWindow()
{
  m_Timers.StopAll();

  GetDocument()->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plAiAssetDocumentWindow::PropertyEventHandler, this));
  GetDocument()->GetObjectManager()->m_StructureEvents.RemoveEventHandler(plMakeDelegate(&plAiAssetDocumentWindow::StructureEventHandler, this));

  RestoreResource();
}

void plAiAssetDocumentWindow::AddRawPropertiesPanel(plStringView sPanelId)
{
  plEditorUiDockPanelDesc desc;
  desc.m_sId = sPanelId;
  desc.m_sTitle = "Raw Properties";
  desc.m_Role = plEditorUiDockRole::Right;

  AddPropertiesPanel(desc);
}

void plAiAssetDocumentWindow::AddValidationPanel(plStringView sPanelId)
{
  m_pValidationView = AddPanel<plAiValidationView>({sPanelId, "Validation", plEditorUiDockRole::Bottom}, this);
}

const plDocumentObject* plAiAssetDocumentWindow::GetAssetPropertiesObject() const
{
  return GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
}

void plAiAssetDocumentWindow::RevealValidationElement(const plAiValidationElement& element)
{
  GetDocument()->GetSelectionManager()->SetSelection(GetAssetPropertiesObject());
}

void plAiAssetDocumentWindow::UpdatePreview()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plResourceUpdateMsgToEngine msg;
  msg.m_sResourceType = m_sResourceType;

  plStringBuilder tmp;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), tmp);

  plContiguousMemoryStreamStorage streamStorage;
  plMemoryStreamWriter memoryWriter(&streamStorage);

  plAssetDocument* pAssetDocument = static_cast<plAssetDocument*>(GetDocument());

  // Write Path
  plStringBuilder sAbsFilePath = GetDocument()->GetDocumentPath();
  sAbsFilePath.ChangeFileExtension(m_sResourceType);
  // Write Header
  memoryWriter << sAbsFilePath;
  const plUInt64 uiHash = plAssetCurator::GetSingleton()->GetAssetTransformHash(GetDocument()->GetGuid());
  plAssetFileHeader AssetHeader;
  AssetHeader.SetFileHashAndVersion(uiHash, pAssetDocument->GetAssetTypeVersion());
  AssetHeader.Write(memoryWriter).IgnoreResult();

  // Write Asset Data
  plStatus status(PL_FAILURE);

  if (auto pBehaviorDoc = plDynamicCast<plAiBehaviorAssetDocument*>(GetDocument()))
  {
    status = pBehaviorDoc->WriteAsset(memoryWriter, plAssetCurator::GetSingleton()->GetActiveAssetProfile());
  }
  else if (auto pArchetypeDoc = plDynamicCast<plAiArchetypeAssetDocument*>(GetDocument()))
  {
    status = pArchetypeDoc->WriteAsset(memoryWriter, plAssetCurator::GetSingleton()->GetActiveAssetProfile());
  }
  else if (auto pEqsDoc = plDynamicCast<plAiEqsQueryAssetDocument*>(GetDocument()))
  {
    status = pEqsDoc->WriteAsset(memoryWriter, plAssetCurator::GetSingleton()->GetActiveAssetProfile());
  }

  if (status.Succeeded())
  {
    msg.m_Data = plArrayPtr<const plUInt8>(streamStorage.GetData(), streamStorage.GetStorageSize32());

    plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
  }
}

void plAiAssetDocumentWindow::RestoreResource()
{
  plRestoreResourceMsgToEngine msg;
  msg.m_sResourceType = m_sResourceType;

  plStringBuilder tmp;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), tmp);

  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
}

void plAiAssetDocumentWindow::PropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  UpdatePreview();
  QueueUiRefresh(false);
}

void plAiAssetDocumentWindow::StructureEventHandler(const plDocumentObjectStructureEvent& e)
{
  switch (e.m_EventType)
  {
    case plDocumentObjectStructureEvent::Type::AfterObjectAdded:
    case plDocumentObjectStructureEvent::Type::AfterObjectRemoved:
    case plDocumentObjectStructureEvent::Type::AfterObjectMoved2:
    {
      if (e.m_EventType == plDocumentObjectStructureEvent::Type::AfterObjectRemoved)
      {
        UpdatePreview();
      }

      QueueUiRefresh(true);
      break;
    }

    default:
      break;
  }
}

void plAiAssetDocumentWindow::QueueUiRefresh(bool bStructure)
{
  m_bStructureChanged |= bStructure;

  if (m_bRefreshQueued)
    return;

  m_bRefreshQueued = true;
  m_Timers.Start(plTime::MakeZero(), [this]()
    { FlushUiRefresh(); });
}

void plAiAssetDocumentWindow::FlushUiRefresh()
{
  m_bRefreshQueued = false;
  const bool bStructure = m_bStructureChanged;
  m_bStructureChanged = false;

  if (bStructure)
  {
    OnAssetStructureChanged();
  }

  OnAssetPropertiesChanged();

  if (m_pValidationView != nullptr)
    m_pValidationView->Revalidate();
}
