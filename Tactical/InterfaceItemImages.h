#ifndef _ITERFACEITEMIMAGES_H_
#define _ITERFACEITEMIMAGES_H_

#include "Types.h"
#include <vfs/Core/vfs_types.h>
#include <vfs/Core/vfs_path.h>
#include <map>

class MDItemVideoObjects
{
public:
	MDItemVideoObjects();

	UINT32	getVObjectForItem(UINT32 key);
	bool	hasItem(UINT32 key) const;
	void	registerItem(UINT32 key, vfs::Path const& sFileName);
	bool	registerItemsFromFilePattern(vfs::Path const& sFilePattern, bool optional = false);
	void	unRegisterAllItems();
private:
	std::map<UINT32,UINT32> m_mapVObjects;
};


extern bool					g_bUsePngItemImages;
const UINT8					MAX_PITEMS = 20;
// old item image handles
extern UINT32				guiGUNSM;
extern UINT32				guiPITEMS[MAX_PITEMS];

// new item image handles
extern MDItemVideoObjects	g_oGUNSM;
extern MDItemVideoObjects	g_oPITEMS[MAX_PITEMS];

// Sparse high-colour PNG overrides. These coexist with legacy STI sheets:
// only graphics present under Interface/ItemOverrides are replaced.
extern MDItemVideoObjects	g_oGUNSMOverrides;
extern MDItemVideoObjects	g_oPITEMSOverrides[MAX_PITEMS];

UINT16 GetInterfaceGraphicSubIndex(UINT8 ubGraphicType, UINT16 ubGraphicNum);

bool RegisterItemImages();

#endif // _ITERFACEITEMIMAGES_H_
