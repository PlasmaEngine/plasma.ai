#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiEqsQueryAsset.h>
#include <EditorPluginAi/Assets/AiEqsQueryAssetManager.h>
#include <EditorPluginAi/Assets/AiEqsQueryAssetWindow.moc.h>
#include <GuiFoundation/UIServices/ImageCache.moc.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsQueryAssetDocumentManager, 1, plRTTIDefaultAllocator<plAiEqsQueryAssetDocumentManager>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plAiEqsQueryAssetDocumentManager::plAiEqsQueryAssetDocumentManager()
{
  plDocumentManager::s_Events.AddEventHandler(plMakeDelegate(&plAiEqsQueryAssetDocumentManager::OnDocumentManagerEvent, this));

  m_DocTypeDesc.m_sDocumentTypeName = "AiEqsQuery";
  m_DocTypeDesc.m_sFileExtension = "plAiEqsQueryAsset";
  m_DocTypeDesc.m_sIcon = ":/AssetIcons/AiEqsQuery.svg";
  m_DocTypeDesc.m_sAssetCategory = "AI";
  m_DocTypeDesc.m_pDocumentType = plGetStaticRTTI<plAiEqsQueryAssetDocument>();
  m_DocTypeDesc.m_pManager = this;
  m_DocTypeDesc.m_CompatibleTypes.PushBack("CompatibleAsset_AiEqsQuery");

  m_DocTypeDesc.m_sResourceFileExtension = "plAiEqsQuery";
  m_DocTypeDesc.m_AssetDocumentFlags = plAssetDocumentFlags::AutoTransformOnSave;

  plQtImageCache::GetSingleton()->RegisterTypeImage("AiEqsQuery", QPixmap(":/AssetIcons/AiEqsQuery.svg"));
}

plAiEqsQueryAssetDocumentManager::~plAiEqsQueryAssetDocumentManager()
{
  plDocumentManager::s_Events.RemoveEventHandler(plMakeDelegate(&plAiEqsQueryAssetDocumentManager::OnDocumentManagerEvent, this));
}

void plAiEqsQueryAssetDocumentManager::OnDocumentManagerEvent(const plDocumentManager::Event& e)
{
  switch (e.m_Type)
  {
    case plDocumentManager::Event::Type::DocumentWindowRequested:
    {
      if (e.m_pDocument->GetDynamicRTTI() == plGetStaticRTTI<plAiEqsQueryAssetDocument>())
      {
        new plQtAiEqsQueryAssetDocumentWindow(e.m_pDocument); // NOLINT: not a memory leak
      }
    }
    break;

    default:
      break;
  }
}

void plAiEqsQueryAssetDocumentManager::InternalCreateDocument(plStringView sDocumentTypeName, plStringView sPath, bool bCreateNewDocument, plDocument*& out_pDocument, const plDocumentObject* pOpenContext)
{
  out_pDocument = new plAiEqsQueryAssetDocument(sPath);
}

void plAiEqsQueryAssetDocumentManager::InternalGetSupportedDocumentTypes(plDynamicArray<const plDocumentTypeDescriptor*>& inout_DocumentTypes) const
{
  inout_DocumentTypes.PushBack(&m_DocTypeDesc);
}
