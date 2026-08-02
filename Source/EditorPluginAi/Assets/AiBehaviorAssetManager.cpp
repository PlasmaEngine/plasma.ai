#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiBehaviorAsset.h>
#include <EditorPluginAi/Assets/AiBehaviorAssetManager.h>
#include <EditorPluginAi/Assets/AiBehaviorAssetWindow.moc.h>
#include <GuiFoundation/UIServices/ImageCache.moc.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiBehaviorAssetDocumentManager, 1, plRTTIDefaultAllocator<plAiBehaviorAssetDocumentManager>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plAiBehaviorAssetDocumentManager::plAiBehaviorAssetDocumentManager()
{
  plDocumentManager::s_Events.AddEventHandler(plMakeDelegate(&plAiBehaviorAssetDocumentManager::OnDocumentManagerEvent, this));

  m_DocTypeDesc.m_sDocumentTypeName = "AiBehavior";
  m_DocTypeDesc.m_sFileExtension = "plAiBehaviorAsset";
  m_DocTypeDesc.m_sIcon = ":/AssetIcons/AiBehavior.svg";
  m_DocTypeDesc.m_sAssetCategory = "AI";
  m_DocTypeDesc.m_pDocumentType = plGetStaticRTTI<plAiBehaviorAssetDocument>();
  m_DocTypeDesc.m_pManager = this;
  m_DocTypeDesc.m_CompatibleTypes.PushBack("CompatibleAsset_AiBehavior");

  m_DocTypeDesc.m_sResourceFileExtension = "plAiBehavior";
  m_DocTypeDesc.m_AssetDocumentFlags = plAssetDocumentFlags::AutoTransformOnSave;

  plQtImageCache::GetSingleton()->RegisterTypeImage("AiBehavior", QPixmap(":/AssetIcons/AiBehavior.svg"));
}

plAiBehaviorAssetDocumentManager::~plAiBehaviorAssetDocumentManager()
{
  plDocumentManager::s_Events.RemoveEventHandler(plMakeDelegate(&plAiBehaviorAssetDocumentManager::OnDocumentManagerEvent, this));
}

void plAiBehaviorAssetDocumentManager::OnDocumentManagerEvent(const plDocumentManager::Event& e)
{
  switch (e.m_Type)
  {
    case plDocumentManager::Event::Type::DocumentWindowRequested:
    {
      if (e.m_pDocument->GetDynamicRTTI() == plGetStaticRTTI<plAiBehaviorAssetDocument>())
      {
        new plQtAiBehaviorAssetDocumentWindow(e.m_pDocument); // NOLINT: not a memory leak
      }
    }
    break;

    default:
      break;
  }
}

void plAiBehaviorAssetDocumentManager::InternalCreateDocument(plStringView sDocumentTypeName, plStringView sPath, bool bCreateNewDocument, plDocument*& out_pDocument, const plDocumentObject* pOpenContext)
{
  out_pDocument = new plAiBehaviorAssetDocument(sPath);
}

void plAiBehaviorAssetDocumentManager::InternalGetSupportedDocumentTypes(plDynamicArray<const plDocumentTypeDescriptor*>& inout_DocumentTypes) const
{
  inout_DocumentTypes.PushBack(&m_DocTypeDesc);
}
