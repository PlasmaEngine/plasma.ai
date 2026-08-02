#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/Assets/AssetStatusIndicator.moc.h>
#include <EditorPluginAi/Assets/AiArchetypeAsset.h>
#include <EditorPluginAi/Assets/AiAssetWindow.moc.h>
#include <EditorPluginAi/Assets/AiBehaviorAsset.h>
#include <EditorPluginAi/Assets/AiEqsQueryAsset.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <GuiFoundation/ActionViews/MenuBarActionMapView.moc.h>
#include <GuiFoundation/ActionViews/ToolBarActionMapView.moc.h>
#include <GuiFoundation/PropertyGrid/PropertyGridWidget.moc.h>

#include <QBoxLayout>
#include <QTimer>

plQtAiAssetDocumentWindow::plQtAiAssetDocumentWindow(plDocument* pDocument, const char* szUniqueName, const char* szResourceType)
  : plQtDocumentWindow(pDocument)
{
  m_sUniqueName = szUniqueName;
  m_sResourceType = szResourceType;

  GetDocument()->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plQtAiAssetDocumentWindow::PropertyEventHandler, this));
  GetDocument()->GetObjectManager()->m_StructureEvents.AddEventHandler(plMakeDelegate(&plQtAiAssetDocumentWindow::StructureEventHandler, this));

  plStringBuilder sMapping;

  // Menu Bar
  {
    sMapping.SetFormat("{}MenuBar", m_sUniqueName);
    plQtMenuBarActionMapView* pMenuBar = static_cast<plQtMenuBarActionMapView*>(menuBar());
    plActionContext context;
    context.m_sMapping = sMapping;
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pMenuBar->SetActionContext(context);
  }

  // Tool Bar
  {
    sMapping.SetFormat("{}ToolBar", m_sUniqueName);
    plQtToolBarActionMapView* pToolBar = new plQtToolBarActionMapView("Toolbar", this);
    plActionContext context;
    context.m_sMapping = sMapping;
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pToolBar->SetActionContext(context);

    sMapping.SetFormat("{}WindowToolBar", m_sUniqueName);
    pToolBar->setObjectName(sMapping.GetData());
    addToolBar(pToolBar);
  }
}

plQtAiAssetDocumentWindow::~plQtAiAssetDocumentWindow()
{
  GetDocument()->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plQtAiAssetDocumentWindow::PropertyEventHandler, this));
  GetDocument()->GetObjectManager()->m_StructureEvents.RemoveEventHandler(plMakeDelegate(&plQtAiAssetDocumentWindow::StructureEventHandler, this));

  RestoreResource();
}

QWidget* plQtAiAssetDocumentWindow::CreateRawPropertiesWidget(QWidget* pParent)
{
  plQtPropertyGridWidget* pPropertyGrid = new plQtPropertyGridWidget(pParent, GetDocument());

  QWidget* pWidget = new QWidget(pParent);
  pWidget->setObjectName("Group");
  pWidget->setLayout(new QVBoxLayout());
  pWidget->setContentsMargins(0, 0, 0, 0);

  pWidget->layout()->setContentsMargins(0, 0, 0, 0);
  pWidget->layout()->addWidget(new plQtAssetStatusIndicator((plAssetDocument*)GetDocument()));
  pWidget->layout()->addWidget(pPropertyGrid);

  return pWidget;
}

void plQtAiAssetDocumentWindow::UpdatePreview()
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

void plQtAiAssetDocumentWindow::RestoreResource()
{
  plRestoreResourceMsgToEngine msg;
  msg.m_sResourceType = m_sResourceType;

  plStringBuilder tmp;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), tmp);

  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
}

void plQtAiAssetDocumentWindow::PropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  UpdatePreview();
  QueueUiRefresh(false);
}

void plQtAiAssetDocumentWindow::StructureEventHandler(const plDocumentObjectStructureEvent& e)
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

void plQtAiAssetDocumentWindow::QueueUiRefresh(bool bStructure)
{
  m_bStructureChanged |= bStructure;

  if (m_bRefreshQueued)
    return;

  m_bRefreshQueued = true;
  QTimer::singleShot(0, this, SLOT(FlushUiRefresh()));
}

void plQtAiAssetDocumentWindow::FlushUiRefresh()
{
  m_bRefreshQueued = false;
  const bool bStructure = m_bStructureChanged;
  m_bStructureChanged = false;

  if (bStructure)
  {
    OnAssetStructureChanged();
  }

  OnAssetPropertiesChanged();
}
