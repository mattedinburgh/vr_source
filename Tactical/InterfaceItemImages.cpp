#include "InterfaceItemImages.h"

#include "DEBUG.H"
#include "vobject.h"
#include "utilities.h"
#include "GameSettings.h"
#include <vfs/Core/vfs.h>

extern void WriteMessageToFile( const STR16 pString );

/******************************************************************************/

bool					g_bUsePngItemImages = false;
// old item image handles
UINT32					guiGUNSM;
UINT32					guiPITEMS[MAX_PITEMS];

// new item image handles
MDItemVideoObjects		g_oGUNSM;
MDItemVideoObjects		g_oPITEMS[MAX_PITEMS];

// Optional per-graphic PNG overrides. They deliberately do not require
// USE_PNG_ITEM_IMAGES, so a mod can replace a handful of icons without
// providing the entire item-art catalogue in PNG form.
MDItemVideoObjects		g_oGUNSMOverrides;
MDItemVideoObjects		g_oPITEMSOverrides[MAX_PITEMS];

/******************************************************************************/

MDItemVideoObjects::MDItemVideoObjects()
{}

UINT32 MDItemVideoObjects::getVObjectForItem(UINT32 key)
{
	std::map<UINT32,UINT32>::iterator it = m_mapVObjects.find(key);
	if(it != m_mapVObjects.end())
	{
		return it->second;
	}
	SGP_THROW(_BS(L"Item key not registered : ") << key << _BS::wget);	
}

bool MDItemVideoObjects::hasItem(UINT32 key) const
{
	std::map<UINT32,UINT32>::const_iterator it = m_mapVObjects.find(key);
	return it != m_mapVObjects.end();
}

void MDItemVideoObjects::registerItem(UINT32 key, vfs::Path const& sFileName)
{
	std::map<UINT32,UINT32>::iterator it = m_mapVObjects.find(key);
	if(it != m_mapVObjects.end())
	{
		SGP_THROW(_BS(L"Item image already registered : ") << key << _BS::wget);
	}
	// LOAD INTERFACE GUN PICTURES
	UINT32 uiVObject;
	VOBJECT_DESC VObjectDesc;
	VObjectDesc.fCreateFlags = VOBJECT_CREATE_FROMFILE;
	FilenameForBPP(const_cast<STR>(sFileName.to_string().c_str()), VObjectDesc.ImageFile);
	if(! AddVideoObject( &VObjectDesc, &uiVObject )) 
	{
		SGP_THROW(_BS(L"Could not add video object for file \"") << sFileName << _BS::wget);
	}
	m_mapVObjects.insert(std::make_pair(key,uiVObject));
}

bool MDItemVideoObjects::registerItemsFromFilePattern(vfs::Path const& sFilePattern, bool optional)
{
	std::wstringstream wss;
	int item = 0;
	vfs::CVirtualFileSystem::Iterator it = getVFS()->begin(sFilePattern);
	if(it.end())
	{
		return optional;
	}
	for(; !it.end(); it.next())
	{
		wss.str(it.value()->getName().c_wcs());
		if( !(wss >> item) )
		{
			std::wstring err = _BS(L"Could not extract item number from file \"") << wss.str() << L"\"" << _BS::wget;
			WriteMessageToFile( const_cast<STR16>(err.c_str()) );
			continue;
		}
		try
		{
			this->registerItem(item, it.value()->getPath());
		}
		catch(std::exception& ex)
		{
			SGP_RETHROW( _BS(L"Registering item from file \"") << wss.str() << L"\" failed" << _BS::wget, ex );
		}
	}
	return true;
}


void MDItemVideoObjects::unRegisterAllItems()
{
	std::map<UINT32,UINT32>::iterator it = m_mapVObjects.begin();
	for(; it != m_mapVObjects.end(); ++it)
	{
		// later
	}
}

/******************************************************************************/

bool RegisterItemImages()
{
	VOBJECT_DESC	VObjectDesc;

	//// LOAD INTERFACE GUN PICTURES
	if(!g_bUsePngItemImages)
	{
		VObjectDesc.fCreateFlags = VOBJECT_CREATE_FROMFILE;
		FilenameForBPP("INTERFACE\\mdguns.sti", VObjectDesc.ImageFile);
		if( !AddVideoObject( &VObjectDesc, &guiGUNSM ) )
			AssertMsg(0, "Missing INTERFACE\\mdguns.sti" );
	}
	else if(!g_oGUNSM.registerItemsFromFilePattern(L"INTERFACE/mdguns/*.png"))
	{
		return false;
	}

	for (UINT8 ubLoop = 0; ubLoop < gGameExternalOptions.ubNumPItems; ubLoop++)
	{
		// LOAD INTERFACE ITEM PICTURES
		if(!g_bUsePngItemImages)
		{
			VObjectDesc.fCreateFlags = VOBJECT_CREATE_FROMFILE;
			FilenameForBPP(String("INTERFACE\\mdp%ditems.sti",ubLoop+1), VObjectDesc.ImageFile);
			if( !AddVideoObject( &VObjectDesc, &guiPITEMS[ubLoop] ) )
				AssertMsg(0, String("Missing INTERFACE\\mdp%ditems.sti",ubLoop+1) );
		}
		else if(!g_oPITEMS[ubLoop].registerItemsFromFilePattern(String("INTERFACE/MDP%dITEMS/*.png",ubLoop+1)))
		{
			return false;
		}

	}

	// Sparse PNG override layer. Missing override folders are expected and harmless.
	if(!g_oGUNSMOverrides.registerItemsFromFilePattern(L"INTERFACE/ItemOverrides/GUN/*.png", true))
		return false;

	for (UINT8 ubLoop = 0; ubLoop < gGameExternalOptions.ubNumPItems; ubLoop++)
	{
		if(!g_oPITEMSOverrides[ubLoop].registerItemsFromFilePattern(
			String("INTERFACE/ItemOverrides/P%d/*.png",ubLoop+1), true))
		{
			return false;
		}
	}

	return true;
}

