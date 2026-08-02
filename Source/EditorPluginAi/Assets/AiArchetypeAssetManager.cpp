#include <EditorPluginAi/EditorPluginAiPCH.h>

#include <EditorPluginAi/Assets/AiArchetypeAsset.h>
#include <EditorPluginAi/Assets/AiArchetypeAssetManager.h>
#include <EditorPluginAi/Assets/AiArchetypeAssetWindow.moc.h>
#include <GuiFoundation/UIServices/ImageCache.moc.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiArchetypeAssetDocumentManager, 1, plRTTIDefaultAllocator<plAiArchetypeAssetDocumentManager>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plAiArchetypeAssetDocumentManager::plAiArchetypeAssetDocumentManager()
{
  plDocumentManager::s_Events.AddEventHandler(plMakeDelegate(&plAiArchetypeAssetDocumentManager::OnDocumentManagerEvent, this));

  m_DocTypeDesc.m_sDocumentTypeName = "AiArchetype";
  m_DocTypeDesc.m_sFileExtension = "plAiArchetypeAsset";
  m_DocTypeDesc.m_sIcon = ":/AssetIcons/AiArchetype.svg";
  m_DocTypeDesc.m_sAssetCategory = "AI";
  m_DocTypeDesc.m_pDocumentType = plGetStaticRTTI<plAiArchetypeAssetDocument>();
  m_DocTypeDesc.m_pManager = this;
  m_DocTypeDesc.m_CompatibleTypes.PushBack("CompatibleAsset_AiArchetype");

  m_DocTypeDesc.m_sResourceFileExtension = "plAiArchetype";
  m_DocTypeDesc.m_AssetDocumentFlags = plAssetDocumentFlags::AutoTransformOnSave;

  plQtImageCache::GetSingleton()->RegisterTypeImage("AiArchetype", QPixmap(":/AssetIcons/AiArchetype.svg"));
}

plAiArchetypeAssetDocumentManager::~plAiArchetypeAssetDocumentManager()
{
  plDocumentManager::s_Events.RemoveEventHandler(plMakeDelegate(&plAiArchetypeAssetDocumentManager::OnDocumentManagerEvent, this));
}

void plAiArchetypeAssetDocumentManager::OnDocumentManagerEvent(const plDocumentManager::Event& e)
{
  switch (e.m_Type)
  {
    case plDocumentManager::Event::Type::DocumentWindowRequested:
    {
      if (e.m_pDocument->GetDynamicRTTI() == plGetStaticRTTI<plAiArchetypeAssetDocument>())
      {
        new plQtAiArchetypeAssetDocumentWindow(e.m_pDocument); // NOLINT: not a memory leak
      }
    }
    break;

    default:
      break;
  }
}

void plAiArchetypeAssetDocumentManager::InternalCreateDocument(plStringView sDocumentTypeName, plStringView sPath, bool bCreateNewDocument, plDocument*& out_pDocument, const plDocumentObject* pOpenContext)
{
  out_pDocument = new plAiArchetypeAssetDocument(sPath);
}

void plAiArchetypeAssetDocumentManager::InternalGetSupportedDocumentTypes(plDynamicArray<const plDocumentTypeDescriptor*>& inout_DocumentTypes) const
{
  inout_DocumentTypes.PushBack(&m_DocTypeDesc);
}
