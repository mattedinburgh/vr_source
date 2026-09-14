#ifdef JA2_PRECOMPILED_HEADERS
	#include "JA2 SGP ALL.H"
#elif defined( WIZ8_PRECOMPILED_HEADERS )
	#include "WIZ8 SGP ALL.H"
#else
	#include "builddefines.h"
	#include <math.h>
	#include <stdlib.h>
	#include "types.h"
	#include "string.h"
	#include "debug.h"
	#include "fileman.h"
	#include "himage.h"
	#include "impTGA.h"
	#include "pcx.h"
	#include "STCI.h"
	#include "PngLoader.h"
	#include "wcheck.h"
	#include "Compression.h"
	#include "vobject.h"
	#include "vobject_blitters.h"
#endif

#include "STIConvert.h"
#include <vector>

#include <vfs/Core/vfs.h>

const vfs::String::str_t CONST_DOTJPC(L".jpc.7z");



// This is the color substituted to keep a 24bpp->16bpp color
// from going transparent (0x0000) -- DB

#define BLACK_SUBSTITUTE	0x0001


UINT16 gusAlphaMask = 0;
UINT16 gusRedMask = 0;
UINT16 gusGreenMask = 0;
UINT16 gusBlueMask = 0;
INT16	gusRedShift = 0;
INT16	gusBlueShift = 0;
INT16	gusGreenShift = 0;


// this funky union is used for fast 16-bit pixel format conversions
typedef union
{
	struct
	{
		UINT16	usLower;
		UINT16	usHigher;
	};
	UINT32	uiValue;
} SplitUINT32;

namespace ImageFileType
{
	typedef std::map<vfs::String, int, vfs::String::Less> ExtMap_t;

	static int map(vfs::String const& ext)
	{
		static ExtMap_t _ext_map;
		static bool inited = false;
		if(!inited)
		{
			_ext_map["pcx"]    = PCX_FILE_READER;
			_ext_map["tga"]    = TGA_FILE_READER;
			_ext_map["sti"]    = STCI_FILE_READER;
			_ext_map["png"]    = PNG_FILE_READER;
			_ext_map["jpc.7z"] = JPC_FILE_READER;
			_ext_map["b1tc"]  = B1TC_FILE_READER;
			inited = true;
		}
		ExtMap_t::const_iterator cit = _ext_map.find(ext);
		if(cit != _ext_map.end())
		{
			return cit->second;
		}
		return UNKNOWN_FILE_READER;
	}

	static int getFileReaderType(std::string& filename, TestOrder order)
	{
		std::string::size_type pos = filename.find_last_of(".");
		std::string ext = filename.substr(pos+1, std::string::npos);
		if(ext.empty())
		{
			ext = "pcx";
			filename += ".pcx";
		}
		int reader_type = map(ext);

		// Optional true-colour sibling for a legacy STI. The STI name remains
		// the map/JSD identity while only the pixels are replaced.
		if(reader_type == STCI_FILE_READER)
		{
			vfs::String trueColorFile = filename.substr(0, pos+1).append("b1tc");
			if(getVFS()->fileExists(trueColorFile))
			{
				filename = trueColorFile.utf8();
				return B1TC_FILE_READER;
			}
		}

		/*
		 * if DEFAULT, then just check existance of file
		 * if not STI, then there is no different load order, just continue as usual
		 */
		if(order == DEFAULT || reader_type != STCI_FILE_READER)
		{
			return getVFS()->fileExists(filename) ? reader_type : UNKNOWN_FILE_READER;
		}
		/*
		 * file must have originally been an STI file, but should be treated as a JPC or a PNG file
		 */
		else if(order == JPC || order == PNG)
		{
			vfs::String file = filename.substr(0, pos+1).append(order == JPC ? "jpc.7z" : "png");
			if( getVFS()->fileExists(file) )
			{
				filename = file.utf8();
				return order == JPC ? JPC_FILE_READER : PNG_FILE_READER;
			}
			return UNKNOWN_FILE_READER;
		}
		/*
		 * file must have originally been an STI file, but should be treated as a JPC or a PNG file
		 * if the replacement filetypes don't exist, fall back to STI
		 */
		else if(order == JPC_FALLBACK || order == PNG_FALLBACK)
		{
			vfs::String file = filename.substr(0, pos+1).append(order == JPC_FALLBACK ? "jpc.7z" : "png");
			if( getVFS()->fileExists(file) )
			{
				filename = file.utf8();
				return order == JPC_FALLBACK ? JPC_FILE_READER : PNG_FILE_READER;
			}
			// fallback to original type
			return getVFS()->fileExists(filename) ? reader_type : UNKNOWN_FILE_READER;
		}
		return UNKNOWN_FILE_READER;
	}
};

HIMAGE CreateImage( SGPFILENAME ImageFile, UINT16 fContents, ImageFileType::TestOrder order )
{
	HIMAGE			hImage = NULL;	
	CHAR8			ExtensionSep[] = ".";	
	UINT32			iFileLoader;

#if 0
	SGPFILENAME	Extension;
	STR					StrPtr;

	// Depending on extension of filename, use different image readers
	// Get extension
	StrPtr = strstr( ImageFile, ExtensionSep );

	if ( StrPtr == NULL )
	{
		// No extension given, use default internal loader extension
		DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_2, "No extension given, using default" );
		strcat( ImageFile, ".PCX" );
		strcpy( Extension, ".PCX" );
	}
	else
	{
		strcpy( Extension, StrPtr+1 );
	}

	// Determine type from Extension
	do
	{
		iFileLoader = UNKNOWN_FILE_READER;

		if ( _stricmp( Extension, "PCX" ) == 0 )
		{
			iFileLoader = PCX_FILE_READER;
			break;
		}
		else if ( _stricmp( Extension, "TGA" ) == 0 )
		{
			iFileLoader = TGA_FILE_READER;
			break;
		}
		else if ( _stricmp( Extension, "STI" ) == 0 )
		{
#ifdef USE_VFS
			// see if there is a .jpc file first and when that fails, try .sti
			vfs::Path str(ImageFile);
			vfs::String::str_t const& findext = str.c_wcs();
			vfs::String::size_t dot = findext.find_last_of(vfs::Const::DOT());
			vfs::String fname = findext.substr(0,dot).append(CONST_DOTJPC);
			if(getVFS()->fileExists(fname))
			{
				iFileLoader = JPC_FILE_READER;
				strncpy(ImageFile, fname.utf8().c_str(), fname.length());
				ImageFile[fname.length()] = 0;
				break;
			}
#endif
			iFileLoader = STCI_FILE_READER;
			break;
		}
		else if ( _stricmp( Extension, "PNG" ) == 0 )
		{
			iFileLoader = PNG_FILE_READER;
			break;
		}
#ifdef USE_VFS
		else if ( vfs::StrCmp::Equal(Extension, L"jpc.7z") )
		{
			iFileLoader = JPC_FILE_READER;
			break;
		}
#endif
	} while ( FALSE );

	// Determine if resource exists before creating image structure
	if ( !FileExists( ImageFile ) )
	{
		//If in debig, make fatal!
#ifdef JA2
#ifdef _DEBUG
		//FatalError( "Resource file %s does not exist.", ImageFile );
#endif
#endif
		DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_2, String("Resource file %s does not exist.", ImageFile) );

		return( NULL );
	}
#else
	std::string filename(ImageFile);
	iFileLoader = ImageFileType::getFileReaderType(filename, order);
	if ( iFileLoader == UNKNOWN_FILE_READER )
	{
		//If in debug, make fatal!
#ifdef JA2
#ifdef _DEBUG
		//FatalError( "Resource file %s does not exist.", ImageFile );
#endif
#endif
		DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_2, String("Resource file %s does not exist.", ImageFile) );

		return( NULL );
	}

#endif

	// Create memory for image structure
	hImage = (HIMAGE)MemAlloc( sizeof( image_type ) );

	AssertMsg( hImage, "Failed to allocate memory for hImage in CreateImage");
	// Initialize some values
	memset( hImage, 0, sizeof( image_type ) );

	//hImage->fFlags = 0;
	// Set data pointers to NULL
	//hImage->pImageData = NULL;
	//hImage->pPalette	= NULL;
	//hImage->pui16BPPPalette = NULL;

	// Set filename and loader
	strncpy( hImage->ImageFile, /*ImageFile*/filename.c_str(), filename.length() );
	hImage->iFileLoader = iFileLoader;

	if ( !LoadImageData( hImage, fContents ) )
	{
		// B1TC is an optional pixel replacement. A malformed/incompatible sibling
		// must never make the authored STI unusable: retry the legacy asset.
		if ( iFileLoader == B1TC_FILE_READER )
		{
			std::string legacyFilename( hImage->ImageFile );
			const std::string::size_type dot = legacyFilename.find_last_of('.');
			if ( dot != std::string::npos )
			{
				legacyFilename = legacyFilename.substr( 0, dot + 1 ) + "sti";
				SGPFILENAME legacyImageFile;
				memset( legacyImageFile, 0, sizeof(legacyImageFile) );
				strncpy( legacyImageFile, legacyFilename.c_str(), sizeof(legacyImageFile) - 1 );
				if ( FileExists( legacyImageFile ) )
				{
					memset( hImage, 0, sizeof( image_type ) );
					strncpy( hImage->ImageFile, legacyImageFile, sizeof(hImage->ImageFile) - 1 );
					hImage->iFileLoader = STCI_FILE_READER;
					if ( LoadImageData( hImage, fContents ) )
						return hImage;
				}
			}
		}

		// Failed loaders are responsible for cleaning their partial payloads. Free
		// the HIMAGE shell here without assuming their flags/pointers are complete.
		MemFree( hImage );
		return( NULL );
	}

	// All is fine, image is loaded and allocated, return pointer
	return( hImage );

}

BOOLEAN DestroyImage( HIMAGE hImage )
{
	Assert( hImage != NULL );

	// First delete contents
	ReleaseImageData( hImage, IMAGE_ALLDATA );//hImage->fFlags );

	// Now free structure
	MemFree( hImage );

	return( TRUE );
}

static BOOLEAN VHDUnpackETRLERegion( HIMAGE hImage, UINT16 usIndex, std::vector<UINT8> &out )
{
	if ( hImage == NULL || hImage->pETRLEObject == NULL || hImage->pPixData8 == NULL ||
		 usIndex >= hImage->usNumberOfObjects )
		return FALSE;

	const ETRLEObject *pRegion = &hImage->pETRLEObject[ usIndex ];
	const UINT32 uiPixelCount = (UINT32)pRegion->usWidth * (UINT32)pRegion->usHeight;
	out.assign( uiPixelCount, 0 );

	const UINT8 *pSrc = hImage->pPixData8 + pRegion->uiDataOffset;
	const UINT8 *pEnd = pSrc + pRegion->uiDataLength;
	UINT32 uiPos = 0;

	while ( uiPos < uiPixelCount && pSrc < pEnd )
	{
		const UINT8 ubCode = *pSrc++;
		const UINT8 ubCount = ubCode & 0x7F;

		if ( ubCode & 0x80 )
		{
			if ( uiPos + ubCount > uiPixelCount )
				return FALSE;
			uiPos += ubCount;
		}
		else
		{
			// A zero-length opaque run is the ETRLE end-of-line marker.
			if ( ubCount == 0 )
				continue;
			if ( uiPos + ubCount > uiPixelCount || pSrc + ubCount > pEnd )
				return FALSE;
			memcpy( &out[ uiPos ], pSrc, ubCount );
			pSrc += ubCount;
			uiPos += ubCount;
		}
	}

	return uiPos == uiPixelCount;
}

BOOLEAN ScaleImageNearestForVHD( HIMAGE hImage, UINT8 ubScale )
{
	if ( hImage == NULL )
		return FALSE;
	if ( ubScale == 1 )
		return TRUE;
	if ( ubScale != 2 && ubScale != 4 )
		return FALSE;
	if ( !( hImage->fFlags & IMAGE_BITMAPDATA ) || hImage->usNumberOfObjects == 0 ||
		 hImage->pETRLEObject == NULL )
		return FALSE;

	const UINT16 usCount = hImage->usNumberOfObjects;
	std::vector<ETRLEObject> newRegions( usCount );
	UINT32 uiTotalBytes = 0;
	UINT16 usMaxWidth = 0;
	UINT16 usMaxHeight = 0;

	if ( hImage->ubBitDepth == 8 && ( hImage->fFlags & IMAGE_TRLECOMPRESSED ) )
	{
		std::vector< std::vector<UINT8> > compressed( usCount );

		for ( UINT16 i = 0; i < usCount; ++i )
		{
			const ETRLEObject &srcRegion = hImage->pETRLEObject[i];
			const UINT32 uiNewWidth32 = (UINT32)srcRegion.usWidth * ubScale;
			const UINT32 uiNewHeight32 = (UINT32)srcRegion.usHeight * ubScale;
			const INT32 iNewOffsetX = (INT32)srcRegion.sOffsetX * ubScale;
			const INT32 iNewOffsetY = (INT32)srcRegion.sOffsetY * ubScale;

			if ( uiNewWidth32 > 65535 || uiNewHeight32 > 65535 ||
				 iNewOffsetX < -32768 || iNewOffsetX > 32767 ||
				 iNewOffsetY < -32768 || iNewOffsetY > 32767 )
				return FALSE;

			std::vector<UINT8> srcPixels;
			if ( !VHDUnpackETRLERegion( hImage, i, srcPixels ) )
				return FALSE;

			const UINT16 usNewWidth = (UINT16)uiNewWidth32;
			const UINT16 usNewHeight = (UINT16)uiNewHeight32;
			std::vector<UINT8> scaled( uiNewWidth32 * uiNewHeight32, 0 );

			for ( UINT16 y = 0; y < usNewHeight; ++y )
			{
				const UINT16 srcY = (UINT16)( y / ubScale );
				for ( UINT16 x = 0; x < usNewWidth; ++x )
				{
					const UINT16 srcX = (UINT16)( x / ubScale );
					scaled[ (UINT32)y * usNewWidth + x ] =
						srcPixels[ (UINT32)srcY * srcRegion.usWidth + srcX ];
				}
			}

			// Existing STI code uses 3x raw size as a safe ETRLE work buffer.
			compressed[i].resize( scaled.size() * 3 + usNewHeight + 16, 0 );
			STCISubImage tempSub;
			memset( &tempSub, 0, sizeof(tempSub) );
			const UINT32 uiCompressed = ETRLECompressSubImage(
				&compressed[i][0], (UINT32)compressed[i].size(), &scaled[0],
				usNewWidth, usNewHeight, &tempSub );
			if ( uiCompressed == 0 )
				return FALSE;
			compressed[i].resize( uiCompressed );

			ETRLEObject &dstRegion = newRegions[i];
			memset( &dstRegion, 0, sizeof(dstRegion) );
			dstRegion.uiDataOffset = uiTotalBytes;
			dstRegion.uiDataLength = uiCompressed;
			dstRegion.sOffsetX = (INT16)iNewOffsetX;
			dstRegion.sOffsetY = (INT16)iNewOffsetY;
			dstRegion.usWidth = usNewWidth;
			dstRegion.usHeight = usNewHeight;

			uiTotalBytes += uiCompressed;
			usMaxWidth = __max( usMaxWidth, usNewWidth );
			usMaxHeight = __max( usMaxHeight, usNewHeight );
		}

		UINT8 *pNewData = (UINT8*)MemAlloc( uiTotalBytes );
		ETRLEObject *pNewRegions = (ETRLEObject*)MemAlloc( sizeof(ETRLEObject) * usCount );
		if ( pNewData == NULL || pNewRegions == NULL )
		{
			if ( pNewData ) MemFree( pNewData );
			if ( pNewRegions ) MemFree( pNewRegions );
			return FALSE;
		}

		for ( UINT16 i = 0; i < usCount; ++i )
		{
			memcpy( pNewData + newRegions[i].uiDataOffset, &compressed[i][0], compressed[i].size() );
			pNewRegions[i] = newRegions[i];
		}

		MemFree( hImage->pPixData8 );
		MemFree( hImage->pETRLEObject );
		hImage->pPixData8 = pNewData;
		hImage->pETRLEObject = pNewRegions;
		hImage->uiSizePixData = uiTotalBytes;
		hImage->usWidth = usMaxWidth;
		hImage->usHeight = usMaxHeight;
		return TRUE;
	}

	if ( hImage->ubBitDepth == 16 || hImage->ubBitDepth == 32 )
	{
		const UINT32 uiBytesPerPixel = ( hImage->ubBitDepth == 32 ) ? 4 : 2;
		const UINT8 *pOldData = (const UINT8*)hImage->pImageData;
		if ( pOldData == NULL )
			return FALSE;

		for ( UINT16 i = 0; i < usCount; ++i )
		{
			const ETRLEObject &srcRegion = hImage->pETRLEObject[i];
			const UINT32 uiNewWidth32 = (UINT32)srcRegion.usWidth * ubScale;
			const UINT32 uiNewHeight32 = (UINT32)srcRegion.usHeight * ubScale;
			const INT32 iNewOffsetX = (INT32)srcRegion.sOffsetX * ubScale;
			const INT32 iNewOffsetY = (INT32)srcRegion.sOffsetY * ubScale;

			if ( uiNewWidth32 > 65535 || uiNewHeight32 > 65535 ||
				 iNewOffsetX < -32768 || iNewOffsetX > 32767 ||
				 iNewOffsetY < -32768 || iNewOffsetY > 32767 )
				return FALSE;

			ETRLEObject &dstRegion = newRegions[i];
			memset( &dstRegion, 0, sizeof(dstRegion) );
			dstRegion.uiDataOffset = uiTotalBytes;
			dstRegion.uiDataLength = uiNewWidth32 * uiNewHeight32 * uiBytesPerPixel;
			dstRegion.sOffsetX = (INT16)iNewOffsetX;
			dstRegion.sOffsetY = (INT16)iNewOffsetY;
			dstRegion.usWidth = (UINT16)uiNewWidth32;
			dstRegion.usHeight = (UINT16)uiNewHeight32;

			if ( 0xFFFFFFFFu - uiTotalBytes < dstRegion.uiDataLength )
				return FALSE;
			uiTotalBytes += dstRegion.uiDataLength;
			usMaxWidth = __max( usMaxWidth, dstRegion.usWidth );
			usMaxHeight = __max( usMaxHeight, dstRegion.usHeight );
		}

		UINT8 *pNewData = (UINT8*)MemAlloc( uiTotalBytes );
		ETRLEObject *pNewRegions = (ETRLEObject*)MemAlloc( sizeof(ETRLEObject) * usCount );
		if ( pNewData == NULL || pNewRegions == NULL )
		{
			if ( pNewData ) MemFree( pNewData );
			if ( pNewRegions ) MemFree( pNewRegions );
			return FALSE;
		}

		for ( UINT16 i = 0; i < usCount; ++i )
		{
			const ETRLEObject &srcRegion = hImage->pETRLEObject[i];
			const ETRLEObject &dstRegion = newRegions[i];
			for ( UINT16 y = 0; y < dstRegion.usHeight; ++y )
			{
				const UINT16 srcY = (UINT16)( y / ubScale );
				for ( UINT16 x = 0; x < dstRegion.usWidth; ++x )
				{
					const UINT16 srcX = (UINT16)( x / ubScale );
					const UINT8 *pSrcPixel = pOldData + srcRegion.uiDataOffset +
						( ( (UINT32)srcY * srcRegion.usWidth + srcX ) * uiBytesPerPixel );
					UINT8 *pDstPixel = pNewData + dstRegion.uiDataOffset +
						( ( (UINT32)y * dstRegion.usWidth + x ) * uiBytesPerPixel );
					memcpy( pDstPixel, pSrcPixel, uiBytesPerPixel );
				}
			}
			pNewRegions[i] = dstRegion;
		}

		MemFree( hImage->pImageData );
		MemFree( hImage->pETRLEObject );
		hImage->pImageData = pNewData;
		hImage->pETRLEObject = pNewRegions;
		hImage->uiSizePixData = uiTotalBytes;
		hImage->usWidth = usMaxWidth;
		hImage->usHeight = usMaxHeight;
		return TRUE;
	}

	return FALSE;
}

BOOLEAN ReleaseImageData( HIMAGE hImage, UINT16 fContents )
{

	Assert( hImage != NULL );

	if ( (fContents & IMAGE_PALETTE) && (hImage->fFlags & IMAGE_PALETTE) )
	{
		//Destroy palette
		if( hImage->pPalette != NULL )
		{
			MemFree( hImage->pPalette );
			hImage->pPalette = NULL;
		}

		if ( hImage->pui16BPPPalette != NULL )
		{
			MemFree( hImage->pui16BPPPalette );
			hImage->pui16BPPPalette = NULL;
		}

		// Remove contents flag
		hImage->fFlags = hImage->fFlags ^ IMAGE_PALETTE;
	}

	if ( (fContents & IMAGE_BITMAPDATA) && (hImage->fFlags & IMAGE_BITMAPDATA) )
	{
		//Destroy image data
		Assert( hImage->pImageData != NULL );
		MemFree( hImage->pImageData );
		hImage->pImageData = NULL;
		if (hImage->usNumberOfObjects > 0)
		{
			MemFree( hImage->pETRLEObject );
		}
		// Remove contents flag
		hImage->fFlags = hImage->fFlags ^ IMAGE_BITMAPDATA;
	}

	if ( (fContents & IMAGE_APPDATA) && (hImage->fFlags & IMAGE_APPDATA) )
	{
		// get rid of the APP DATA
		if ( hImage->pAppData != NULL )
		{
			MemFree( hImage->pAppData );
			hImage->fFlags &= (~IMAGE_APPDATA);
		}
	}

	return( TRUE );
}

static UINT16 B1TCReadU16( const UINT8 *pData )
{
	return (UINT16)( pData[0] | ((UINT16)pData[1] << 8) );
}

static UINT32 B1TCReadU32( const UINT8 *pData )
{
	return (UINT32)pData[0] |
		((UINT32)pData[1] << 8) |
		((UINT32)pData[2] << 16) |
		((UINT32)pData[3] << 24);
}

static void B1TCInheritSTIAppData( HIMAGE hImage, UINT16 fContents )
{
	if( !(fContents & IMAGE_APPDATA) )
		return;

	std::string legacyFilename( hImage->ImageFile );
	const std::string::size_type dot = legacyFilename.find_last_of('.');
	if( dot == std::string::npos )
		return;
	legacyFilename = legacyFilename.substr( 0, dot + 1 ) + "sti";

	image_type legacyImage;
	memset( &legacyImage, 0, sizeof(legacyImage) );
	strncpy( legacyImage.ImageFile, legacyFilename.c_str(), sizeof(legacyImage.ImageFile) - 1 );
	legacyImage.ImageFile[ sizeof(legacyImage.ImageFile) - 1 ] = 0;

	if( !FileExists( legacyImage.ImageFile ) )
		return;

	// B1TC replaces pixels only. Preserve any AuxObjectData/app-specific payload
	// carried by the authored STI so future true-colour conversions cannot
	// silently change animation or tile metadata.
	if( !LoadSTCIFileToImage( &legacyImage, IMAGE_APPDATA ) )
	{
		DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_2, String("Could not inherit STI appdata for true-colour sibling %s", hImage->ImageFile) );
		return;
	}

	if( legacyImage.pAppData != NULL && legacyImage.uiAppDataSize > 0 )
	{
		hImage->pAppData = legacyImage.pAppData;
		hImage->uiAppDataSize = legacyImage.uiAppDataSize;
		hImage->fFlags |= IMAGE_APPDATA;

		legacyImage.pAppData = NULL;
		legacyImage.uiAppDataSize = 0;
		legacyImage.fFlags &= ~IMAGE_APPDATA;
	}

	ReleaseImageData( &legacyImage, IMAGE_ALLDATA );
}


static UINT32 B1TCArtHash(UINT32 x)
{
	x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
	return x;
}
static CHAR8 B1TCArtLower(CHAR8 c) { return (c>='A'&&c<='Z')?(CHAR8)(c-'A'+'a'):c; }
static BOOLEAN B1TCArtHas(const CHAR8* s,const CHAR8* n)
{
	if(!s||!n||!*n) return FALSE;
	for(;*s;++s){const CHAR8*a=s,*b=n;while(*a&&*b&&B1TCArtLower(*a)==B1TCArtLower(*b)){++a;++b;}if(!*b)return TRUE;}
	return FALSE;
}
static void B1TCArtMix(UINT8* p,UINT8 r,UINT8 g,UINT8 b,UINT32 a)
{
	if(a>255)a=255;
	p[0]=(UINT8)(((UINT32)p[0]*(255-a)+(UINT32)r*a+127)/255);
	p[1]=(UINT8)(((UINT32)p[1]*(255-a)+(UINT32)g*a+127)/255);
	p[2]=(UINT8)(((UINT32)p[2]*(255-a)+(UINT32)b*a+127)/255);
}

// B1/Oronegro art direction: poor, humid Latin-American oil town.
// Pixel-only transformation: frame geometry, offsets, JSD/collision and map
// references remain exactly as authored.
static void B1TCApplyOronegroArtDirection(HIMAGE hImage)
{
	if(!hImage||!hImage->p32BPPData||!hImage->pETRLEObject)return;
	const CHAR8* name=hImage->ImageFile;
	const BOOLEAN roof=B1TCArtHas(name,"roof"), wall=B1TCArtHas(name,"build_");
	const BOOLEAN road=B1TCArtHas(name,"road"), floor=B1TCArtHas(name,"floor")||B1TCArtHas(name,"welflor");
	const BOOLEAN water=B1TCArtHas(name,"water"), grass=B1TCArtHas(name,"grass");
	const BOOLEAN ground=B1TCArtHas(name,"sand")||B1TCArtHas(name,"trail"), oil=B1TCArtHas(name,"oil_");
	if(!B1TCArtHas(name,"b1_")&&!oil)return;

	// These B1 families now carry bespoke painted pixels in vr_gamedir.
	// Do not run the earlier procedural weathering layer over them again.
	if(B1TCArtHas(name,"B1_BUILD_31") || B1TCArtHas(name,"B1_BUILD_35") ||
		B1TCArtHas(name,"B1_BUILD_36") || B1TCArtHas(name,"B1_BUILD_40") ||
		B1TCArtHas(name,"B1_W-ROOF2") || B1TCArtHas(name,"B1_Oil_OROOF") ||
		B1TCArtHas(name,"B1_Rooffan") || B1TCArtHas(name,"B1_ROADTLE2"))
		return;
	static const UINT8 fac[5][3]={{188,160,103},{88,147,145},{174,116,106},{118,145,102},{185,180,153}};
	UINT32 familySeed=2166136261U;
	for(const CHAR8* s=name;*s;++s) familySeed=(familySeed^(UINT8)B1TCArtLower(*s))*16777619U;
	const UINT8* familyFacade=fac[familySeed%5];

	for(UINT16 i=0;i<hImage->usNumberOfObjects;++i)
	{
		ETRLEObject*o=&hImage->pETRLEObject[i]; if(!o->usWidth||!o->usHeight)continue;
		UINT8*fr=(UINT8*)hImage->p32BPPData+o->uiDataOffset;
		UINT32 seed=B1TCArtHash(0xB10A7E00U^(UINT32)i*0x9e3779b9U^(UINT32)strlen(name)*131U);
		const UINT8*fc=familyFacade;
		INT32 pw=__max(3,(INT32)o->usWidth/5),ph=__max(2,(INT32)o->usHeight/7);
		INT32 px=o->usWidth>pw?(INT32)((seed>>8)%(o->usWidth-pw)):0,py=o->usHeight>ph?(INT32)((seed>>16)%(o->usHeight-ph)):0;
		INT32 rx=roof&&o->usWidth>=20?__max(2,(INT32)o->usWidth/12):0,ry=roof&&o->usHeight>=12?__max(1,(INT32)o->usHeight/12):0;
		INT32 hx=rx?rx+(INT32)((seed>>5)%__max(1,(INT32)o->usWidth-2*rx)):0,hy=ry?ry+(INT32)((seed>>13)%__max(1,(INT32)o->usHeight-2*ry)):0;
		BOOLEAN makeHole=roof&&rx&&ry&&(((seed>>3)&3U)==0U);
		INT32 orx=road?__max(5,(INT32)o->usWidth/6):0,ory=road?__max(3,(INT32)o->usHeight/7):0;
		INT32 ox=road?(INT32)((seed>>7)%__max(1,(INT32)o->usWidth)):0,oy=road?(INT32)((seed>>15)%__max(1,(INT32)o->usHeight)):0;

		for(UINT16 y=0;y<o->usHeight;++y)for(UINT16 x=0;x<o->usWidth;++x)
		{
			UINT8*p=fr+(((UINT32)y*o->usWidth+x)*4); if(!p[3])continue;
			UINT32 n=B1TCArtHash(seed^(UINT32)(x/3)*73856093U^(UINT32)(y/3)*19349663U);
			UINT32 q=B1TCArtHash(seed^(UINT32)x*83492791U^(UINT32)y*2654435761U);
			INT32 mx=__max((INT32)p[0],__max((INT32)p[1],(INT32)p[2])),mn=__min((INT32)p[0],__min((INT32)p[1],(INT32)p[2]));
			INT32 lum=((INT32)p[0]+p[1]+p[2])/3,sat=mx-mn;
			if(lum>40)B1TCArtMix(p,191,176,143,8+((n>>24)&15));

			if(wall)
			{
				if(lum>48&&sat<105)B1TCArtMix(p,fc[0],fc[1],fc[2],36+((n>>20)&31));
				UINT32 col=B1TCArtHash(seed^(UINT32)x*2246822519U);
				if((col&31U)<4U&&y>o->usHeight/5)B1TCArtMix(p,62,72,54,20+(UINT32)y*30/__max(1,(INT32)o->usHeight));
				if(y>o->usHeight*3/4)B1TCArtMix(p,70,61,47,22);
				if((n&255U)<13U&&lum>55)B1TCArtMix(p,130,124,105,80);
			}
			if(roof)
			{
				if((n&255U)<52U&&lum>30)B1TCArtMix(p,146,65,33,78+((n>>8)&47));
				if(((x+(seed&7U))%9U)<=1U)B1TCArtMix(p,72,64,55,24);
				if(x>=px&&x<px+pw&&y>=py&&y<py+ph){UINT32 k=(seed>>23)%3;if(k==0)B1TCArtMix(p,92,105,103,115);else if(k==1)B1TCArtMix(p,113,82,61,120);else B1TCArtMix(p,83,109,112,105);}
				if(makeHole){INT32 dx=(INT32)x-hx,dy=(INT32)y-hy,lhs=dx*dx*ry*ry+dy*dy*rx*rx,rr=rx*rx*ry*ry;if(lhs<rr*2/5&&p[3]>=200){p[3]=0;continue;}if(lhs<rr)B1TCArtMix(p,71,38,26,150);}
			}
			if(road)
			{
				INT32 dx=(INT32)x-ox,dy=(INT32)y-oy;if(orx&&ory&&dx*dx*ory*ory+dy*dy*orx*orx<orx*orx*ory*ory)B1TCArtMix(p,37,35,30,45+((q>>24)&31));
				INT32 cx=((INT32)((seed>>2)%__max(1,(INT32)o->usWidth))+((INT32)y*(3+(INT32)((seed>>12)&3U)))/7)%__max(1,(INT32)o->usWidth);
				if((INT32)x-cx<=1&&(INT32)x-cx>=-1&&(q&7U))B1TCArtMix(p,42,38,33,110);
			}
			if(floor){if((n&255U)<30U)B1TCArtMix(p,88,75,58,40);if((q&511U)<8U)B1TCArtMix(p,48,43,36,75);}
			if(ground){if((n&255U)<80U)B1TCArtMix(p,132,112,78,20+((n>>8)&23));if((n&1023U)<18U)B1TCArtMix(p,69,65,49,55);}
			if(grass){if((n&3U)==0U)B1TCArtMix(p,93,111,57,28);else if((n&7U)==1U)B1TCArtMix(p,151,133,70,24);}
			if(water){B1TCArtMix(p,66,82,67,18);if((n&511U)<16U&&lum>50){p[0]=(UINT8)__min(255,(INT32)p[0]+10);p[2]=(UINT8)__min(255,(INT32)p[2]+7);}}
			if(oil){if((n&255U)<72U)B1TCArtMix(p,119,61,36,70);if((q&255U)<32U)B1TCArtMix(p,42,39,34,65);}
		}
	}
}

static BOOLEAN LoadB1TCFileToImage( HIMAGE hImage, UINT16 fContents )
{
	HWFILE hFile = FileOpen( hImage->ImageFile, FILE_ACCESS_READ );
	if( !hFile )
		return FALSE;

	const UINT32 uiFileSize = FileGetSize( hFile );
	if( uiFileSize < 8 )
	{
		FileClose( hFile );
		return FALSE;
	}

	UINT8 *pFileData = (UINT8*)MemAlloc( uiFileSize );
	if( pFileData == NULL )
	{
		FileClose( hFile );
		return FALSE;
	}

	UINT32 uiBytesRead = 0;
	const BOOLEAN fRead = FileRead( hFile, pFileData, uiFileSize, &uiBytesRead );
	FileClose( hFile );
	if( !fRead || uiBytesRead != uiFileSize )
	{
		MemFree( pFileData );
		return FALSE;
	}

	if( memcmp( pFileData, "B1TC", 4 ) != 0 )
	{
		MemFree( pFileData );
		return FALSE;
	}

	const UINT16 usVersion = B1TCReadU16( pFileData + 4 );
	const UINT16 usFrameCount = B1TCReadU16( pFileData + 6 );
	if( usVersion != 1 || usFrameCount == 0 )
	{
		MemFree( pFileData );
		return FALSE;
	}

	const UINT32 uiDirectorySize = 8 + (UINT32)usFrameCount * 16;
	if( uiDirectorySize > uiFileSize )
	{
		MemFree( pFileData );
		return FALSE;
	}

	hImage->pETRLEObject = (ETRLEObject*)MemAlloc( sizeof(ETRLEObject) * usFrameCount );
	if( hImage->pETRLEObject == NULL )
	{
		MemFree( pFileData );
		return FALSE;
	}
	memset( hImage->pETRLEObject, 0, sizeof(ETRLEObject) * usFrameCount );

	UINT32 uiTotalPixelBytes = 0;
	UINT16 usMaxWidth = 0;
	UINT16 usMaxHeight = 0;

	for( UINT16 i = 0; i < usFrameCount; ++i )
	{
		const UINT8 *pEntry = pFileData + 8 + (UINT32)i * 16;
		const INT16 sOffsetX = (INT16)B1TCReadU16( pEntry + 0 );
		const INT16 sOffsetY = (INT16)B1TCReadU16( pEntry + 2 );
		const UINT16 usWidth = B1TCReadU16( pEntry + 4 );
		const UINT16 usHeight = B1TCReadU16( pEntry + 6 );
		const UINT32 uiSourceOffset = B1TCReadU32( pEntry + 8 );
		const UINT32 uiDataLength = B1TCReadU32( pEntry + 12 );
		const UINT32 uiExpectedLength = (UINT32)usWidth * (UINT32)usHeight * 4;

		if( usWidth == 0 || usHeight == 0 || uiDataLength != uiExpectedLength ||
			uiSourceOffset < uiDirectorySize || uiSourceOffset > uiFileSize ||
			uiDataLength > uiFileSize - uiSourceOffset )
		{
			MemFree( hImage->pETRLEObject );
			hImage->pETRLEObject = NULL;
			MemFree( pFileData );
			return FALSE;
		}

		ETRLEObject *pObject = &hImage->pETRLEObject[i];
		pObject->sOffsetX = sOffsetX;
		pObject->sOffsetY = sOffsetY;
		pObject->usWidth = usWidth;
		pObject->usHeight = usHeight;
		pObject->uiDataOffset = uiTotalPixelBytes;
		pObject->uiDataLength = uiDataLength;

		if( usWidth > usMaxWidth ) usMaxWidth = usWidth;
		if( usHeight > usMaxHeight ) usMaxHeight = usHeight;
		if( uiTotalPixelBytes > 0xFFFFFFFFu - uiDataLength )
		{
			MemFree( hImage->pETRLEObject );
			hImage->pETRLEObject = NULL;
			MemFree( pFileData );
			return FALSE;
		}
		uiTotalPixelBytes += uiDataLength;
	}

	hImage->p32BPPData = (UINT32*)MemAlloc( uiTotalPixelBytes );
	if( hImage->p32BPPData == NULL )
	{
		MemFree( hImage->pETRLEObject );
		hImage->pETRLEObject = NULL;
		MemFree( pFileData );
		return FALSE;
	}

	for( UINT16 i = 0; i < usFrameCount; ++i )
	{
		const UINT8 *pEntry = pFileData + 8 + (UINT32)i * 16;
		const UINT32 uiSourceOffset = B1TCReadU32( pEntry + 8 );
		const UINT32 uiDataLength = B1TCReadU32( pEntry + 12 );
		memcpy( (UINT8*)hImage->p32BPPData + hImage->pETRLEObject[i].uiDataOffset,
			pFileData + uiSourceOffset, uiDataLength );
	}

	MemFree( pFileData );
	hImage->usNumberOfObjects = usFrameCount;
	hImage->usWidth = usMaxWidth;
	hImage->usHeight = usMaxHeight;
	hImage->ubBitDepth = 32;
	hImage->uiSizePixData = uiTotalPixelBytes;
	hImage->pPalette = NULL;
	hImage->pui16BPPPalette = NULL;
	hImage->fFlags |= IMAGE_BITMAPDATA;
	B1TCInheritSTIAppData( hImage, fContents );
	return TRUE;
}


BOOLEAN LoadImageData( HIMAGE hImage, UINT16 fContents )
{
	BOOLEAN fReturnVal = FALSE;

	Assert( hImage != NULL );

	// Switch on file loader
	switch( hImage->iFileLoader )
	{
		case TGA_FILE_READER:

			fReturnVal = LoadTGAFileToImage( hImage, fContents );
			break;

		case PCX_FILE_READER:

			fReturnVal = LoadPCXFileToImage( hImage, fContents );
			break;

		case STCI_FILE_READER:
			fReturnVal = LoadSTCIFileToImage( hImage, fContents );
			break;

		case PNG_FILE_READER:
			fReturnVal = LoadPNGFileToImage( hImage, fContents );
			break;

		case JPC_FILE_READER:
			fReturnVal = LoadJPCFileToImage( hImage, fContents );
			break;

		case B1TC_FILE_READER:
			fReturnVal = LoadB1TCFileToImage( hImage, fContents );
			break;
		
		default:

			DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_2, "Unknown image loader was specified." );

	}

	if ( !fReturnVal )
	{
		DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_2, "Error occured while reading image data." );
	}

	return( fReturnVal );

}

BOOLEAN CopyImageToBuffer( HIMAGE hImage, UINT32 fBufferType, BYTE *pDestBuf, UINT16 usDestWidth, UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect *srcRect )
{
	// Use blitter based on type of image
	Assert( hImage != NULL );

	if ( hImage->ubBitDepth == 8 && fBufferType == BUFFER_8BPP )
	{
		#ifndef NO_ZLIB_COMPRESSION
			if ( hImage->fFlags & IMAGE_COMPRESSED )
			{
				DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_2, "Copying Compressed 8 BPP Imagery." );
				return( Copy8BPPCompressedImageTo8BPPBuffer( hImage, pDestBuf, usDestWidth, usDestHeight, usX, usY, srcRect ) );
			}
		#endif

		// Default do here
		DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_2, "Copying 8 BPP Imagery." );
		return ( Copy8BPPImageTo8BPPBuffer( hImage, pDestBuf, usDestWidth, usDestHeight, usX, usY, srcRect ) );

	}

	if ( hImage->ubBitDepth == 8 && fBufferType == BUFFER_16BPP )
	{
		#ifndef NO_ZLIB_COMPRESSION
			if ( hImage->fFlags & IMAGE_COMPRESSED )
			{
				DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "Copying Compressed 8 BPP Imagery to 16BPP Buffer." );
				return ( Copy8BPPCompressedImageTo16BPPBuffer( hImage, pDestBuf, usDestWidth, usDestHeight, usX, usY, srcRect ) );
			}
		#endif

		// Default do here
		DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "Copying 8 BPP Imagery to 16BPP Buffer." );
		return ( Copy8BPPImageTo16BPPBuffer( hImage, pDestBuf, usDestWidth, usDestHeight, usX, usY, srcRect ) );

	}


	if ( hImage->ubBitDepth == 16 && fBufferType == BUFFER_16BPP )
	{
		#ifndef NO_ZLIB_COMPRESSION
			if ( hImage->fFlags & IMAGE_COMPRESSED )
			{
				DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "Automatically Copying Compressed 16 BPP Imagery." );
				return( Copy16BPPCompressedImageTo16BPPBuffer( hImage, pDestBuf, usDestWidth, usDestHeight, usX, usY, srcRect ) );
			}
		#endif

			DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "Automatically Copying 16 BPP Imagery." );
		return( Copy16BPPImageTo16BPPBuffer( hImage, pDestBuf, usDestWidth, usDestHeight, usX, usY, srcRect ) );
	}

	if ( hImage->ubBitDepth == 24 && fBufferType == BUFFER_16BPP )
	{
		DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "Copying 24 BPP Imagery to 16BPP Buffer." );
        AssertMsg(false,"not yet implemented");
		return( FALSE );
	}

	if ( hImage->ubBitDepth == 32 && fBufferType == BUFFER_16BPP )
	{
		DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "Copying 32 BPP Imagery to 16BPP Buffer." );
		return Blt32BPPTo16BPPTrans((UINT16*)pDestBuf, usDestWidth * sizeof(UINT16), hImage->p32BPPData, usDestWidth*sizeof(UINT32), 0,0,0,0,usDestWidth, usDestHeight);
	}

	return( FALSE );

}


#ifndef NO_ZLIB_COMPRESSION

BOOLEAN Copy8BPPCompressedImageTo8BPPBuffer( HIMAGE hImage, BYTE *pDestBuf, UINT16 usDestWidth, UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect *srcRect )
{
	UINT32	uiNumLines;
	UINT32	uiLineSize;
	UINT32		uiCnt;

	UINT8 *	pDest;
	UINT32	uiDestStart;

	UINT8 *	pScanLine;

	PTR			pDecompPtr;
	UINT32	uiDecompressed;

	// Assertions
	Assert( hImage != NULL );
	Assert( hImage->pCompressedImageData != NULL );

	// Validations
	CHECKF( usX >= 0 );
	CHECKF( usX < usDestWidth );
	CHECKF( usY >= 0 );
	CHECKF( usY < usDestHeight );
	CHECKF( srcRect->iRight > srcRect->iLeft );
	CHECKF( srcRect->iBottom > srcRect->iTop );

	DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "8BPP to 8BPP Compressed Blitter Called!" );
	// determine where to start Copying and rectangle size
	uiDestStart = usY * usDestWidth + usX;
	uiNumLines = srcRect->iBottom - srcRect->iTop;
	uiLineSize = srcRect->iRight - srcRect->iLeft;

	Assert( usDestWidth >= uiLineSize );
	Assert( usDestHeight >= uiNumLines );

	pDest = (UINT8 *) pDestBuf + uiDestStart;

	// Copying a portion of a compressed image is rather messy
	// because we have to decompress past all the data we want
	// to skip.

	// To keep memory requirements small and regular, we will
	// decompress one scanline at a time even if none of the data will
	// be blitted (but stop when the bottom line of the rectangle
	// to blit has been done).

	// initialize the decompression routines
	pDecompPtr = DecompressInit( hImage->pCompressedImageData, hImage->usWidth * hImage->usHeight );
	CHECKF( pDecompPtr );

	// Allocate memory for one scanline
	pScanLine = (UINT8*) MemAlloc( hImage->usWidth );
	CHECKF( pScanLine );
	memset( pScanLine, 0, hImage->usWidth );

	// go past all the scanlines we don't need to process
	for (uiCnt = 0; uiCnt < (UINT32) srcRect->iTop; uiCnt++)
	{
		uiDecompressed = Decompress( pDecompPtr, pScanLine, hImage->usWidth );
		Assert( uiDecompressed == hImage->usWidth );
	}

	// now we start Copying
	for (uiCnt = 0; uiCnt < uiNumLines - 1; uiCnt++)
	{
		// decompress a scanline
		uiDecompressed = Decompress( pDecompPtr, pScanLine, hImage->usWidth );
		Assert( uiDecompressed == hImage->usWidth );
		// and blit
//		memcpy( pDest, pScanLine + srcRect->iLeft, uiLineSize );
		pDest += usDestWidth;
	}
	// decompress the last scanline and blit
	uiDecompressed = Decompress( pDecompPtr, pScanLine, hImage->usWidth );
	Assert( uiDecompressed == hImage->usWidth );
//	memcpy( pDest, pScanLine + srcRect->iLeft, uiLineSize );

	DecompressFini( pDecompPtr );
	return( TRUE );
}

BOOLEAN Copy8BPPCompressedImageTo16BPPBuffer( HIMAGE hImage, BYTE *pDestBuf, UINT16 usDestWidth, UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect *srcRect )
{
	UINT32		uiNumLines;
	UINT32		uiLineSize;
	UINT32		uiLine;
	UINT32		uiCol;

	UINT16 *	pDest;
	UINT16 *	pDestTemp;
	UINT32		uiDestStart;

	UINT8 *		pScanLine;
	UINT8 *		pScanLineTemp;

	PTR				pDecompPtr;
	UINT32		uiDecompressed;

	UINT16 *	p16BPPPalette;

	// Assertions
	Assert( hImage != NULL );
	Assert( hImage->pCompressedImageData != NULL );
	DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "Start check" );
	// Validations
	CHECKF( usX >= 0 );
	CHECKF( usX < usDestWidth );
	CHECKF( usY >= 0 );
	CHECKF( usY < usDestHeight );
	CHECKF( srcRect->iRight > srcRect->iLeft );
	CHECKF( srcRect->iBottom > srcRect->iTop );
	DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "End check" );
	p16BPPPalette = hImage->pui16BPPPalette;

	// determine where to start Copying and rectangle size
	uiDestStart = usY * usDestWidth + usX;
	uiNumLines = srcRect->iBottom - srcRect->iTop;
	uiLineSize = srcRect->iRight - srcRect->iLeft;

	Assert( usDestWidth >= uiLineSize );
	Assert( usDestHeight >= uiNumLines );

	pDest = (UINT16 *) pDestBuf;
	pDest += uiDestStart;
	DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, String( "Start Copying at %p", pDest ) );

	// Copying a portion of a compressed image is rather messy
	// because we have to decompress past all the data we want
	// to skip.

	// To keep memory requirements small and regular, we will
	// decompress one scanline at a time even if none of the data will
	// be blitted (but stop when the bottom line of the rectangle
	// to blit has been done).

	// initialize the decompression routines
	pDecompPtr = DecompressInit( hImage->pCompressedImageData, hImage->usWidth * hImage->usHeight );
	CHECKF( pDecompPtr );

	// Allocate memory for one scanline
	pScanLine = (UINT8*) MemAlloc( hImage->usWidth );
	CHECKF( pScanLine );
	memset( pScanLine, 0, hImage->usWidth );

	// go past all the scanlines we don't need to process
	for (uiLine = 0; uiLine < (UINT32) srcRect->iTop; uiLine++)
	{
		DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "Skipping scanline" );
		uiDecompressed = Decompress( pDecompPtr, pScanLine, hImage->usWidth );
		Assert( uiDecompressed == hImage->usWidth );
	}

	DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, "Actually Copying" );
	// now we start Copying
	for (uiLine = 0; uiLine < uiNumLines - 1; uiLine++)
	{
		// decompress a scanline
		uiDecompressed = Decompress( pDecompPtr, pScanLine, hImage->usWidth );
		Assert( uiDecompressed == hImage->usWidth );

		// set pointers and blit
		pDestTemp = pDest;
		pScanLineTemp = pScanLine + srcRect->iLeft;
		for (uiCol = 0; uiCol < uiLineSize; uiCol++ )
		{
			*pDestTemp = p16BPPPalette[ *pScanLineTemp ];
			pDestTemp++;
			pScanLineTemp++;
		}
		pDest += usDestWidth;
	}

	DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, String( "End Copying at %p", pDest ) );

	DecompressFini( pDecompPtr );
	return( TRUE );
}

BOOLEAN Copy16BPPCompressedImageTo16BPPBuffer( HIMAGE hImage, BYTE *pDestBuf, UINT16 usDestWidth, UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect *srcRect )
{
	// 16BPP Compressed image has not been implemented yet
	DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_2, "16BPP Compressed imagery blitter has not been implemented yet." );
	return( FALSE );
}
#endif //NO_ZLIB_COMPRESSION


BOOLEAN Copy8BPPImageTo8BPPBuffer( HIMAGE hImage, BYTE *pDestBuf, UINT16 usDestWidth, UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect *srcRect )
{
	UINT32 uiSrcStart, uiDestStart, uiNumLines, uiLineSize;
	UINT32 cnt;
	UINT8 *pDest, *pSrc;

	// Assertions
	Assert( hImage != NULL );
	Assert( hImage->p16BPPData != NULL );

	// Validations
	CHECKF( usX >= 0 );
	CHECKF( usX < usDestWidth );
	CHECKF( usY >= 0 );
	CHECKF( usY < usDestHeight );
	CHECKF( srcRect->iRight > srcRect->iLeft );
	CHECKF( srcRect->iBottom > srcRect->iTop );

	// Determine memcopy coordinates
	uiSrcStart = srcRect->iTop * hImage->usWidth + srcRect->iLeft;
	uiDestStart = usY * usDestWidth + usX;
	uiNumLines = ( srcRect->iBottom - srcRect->iTop ) + 1;
	uiLineSize = ( srcRect->iRight - srcRect->iLeft ) + 1;

	Assert( usDestWidth >= uiLineSize );
	Assert( usDestHeight >= uiNumLines );

	// Copy line by line
	pDest = ( UINT8*)pDestBuf + uiDestStart;
	pSrc =	hImage->p8BPPData + uiSrcStart;

	for( cnt = 0; cnt < uiNumLines-1; cnt++ )
	{
		memcpy( pDest, pSrc, uiLineSize );
		pDest += usDestWidth;
		pSrc	+= hImage->usWidth;
	}
	// Do last line
	memcpy( pDest, pSrc, uiLineSize );

	return( TRUE );

}

BOOLEAN Copy16BPPImageTo16BPPBuffer( HIMAGE hImage, BYTE *pDestBuf, UINT16 usDestWidth, UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect *srcRect )
{
	UINT32 uiSrcStart, uiDestStart, uiNumLines, uiLineSize;
	UINT32 cnt;
	UINT16 *pDest, *pSrc;

	Assert( hImage != NULL );
	Assert( hImage->p16BPPData != NULL );

	// Validations
	CHECKF( usX >= 0 );
	CHECKF( usX < hImage->usWidth );
	CHECKF( usY >= 0 );
	CHECKF( usY < hImage->usHeight );
	CHECKF( srcRect->iRight > srcRect->iLeft );
	CHECKF( srcRect->iBottom > srcRect->iTop );

	// Determine memcopy coordinates
	uiSrcStart = srcRect->iTop * hImage->usWidth + srcRect->iLeft;
	uiDestStart = usY * usDestWidth + usX;
	uiNumLines = ( srcRect->iBottom - srcRect->iTop ) + 1;
	uiLineSize = ( srcRect->iRight - srcRect->iLeft ) + 1;

	CHECKF( usDestWidth >= uiLineSize );
	CHECKF( usDestHeight >= uiNumLines );

	// Copy line by line
	pDest = ( UINT16*)pDestBuf + uiDestStart;
	pSrc =	hImage->p16BPPData + uiSrcStart;

	for( cnt = 0; cnt < uiNumLines-1; cnt++ )
	{
		memcpy( pDest, pSrc, uiLineSize * 2 );
		pDest += usDestWidth;
		pSrc	+= hImage->usWidth;
	}
	// Do last line
	memcpy( pDest, pSrc, uiLineSize * 2 );

	return( TRUE );

}

BOOLEAN Extract8BPPCompressedImageToBuffer( HIMAGE hImage, BYTE *pDestBuf )
{

	return( FALSE );
}

BOOLEAN Extract16BPPCompressedImageToBuffer( HIMAGE hImage, BYTE *pDestBuf )
{

	return( FALSE );
}


BOOLEAN Copy8BPPImageTo16BPPBuffer( HIMAGE hImage, BYTE *pDestBuf, UINT16 usDestWidth, UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect *srcRect )
{
	UINT32 uiSrcStart, uiDestStart, uiNumLines, uiLineSize;
	UINT32 rows, cols;
	UINT8	*pSrc, *pSrcTemp;
	UINT16 *pDest, *pDestTemp;
	UINT16 *p16BPPPalette;


	p16BPPPalette = hImage->pui16BPPPalette;

	// Assertions
	Assert( p16BPPPalette != NULL );
	Assert( hImage != NULL );

	// Validations
	CHECKF( hImage->p16BPPData != NULL );
	CHECKF( usX >= 0 );
	CHECKF( usX < usDestWidth );
	CHECKF( usY >= 0 );
	CHECKF( usY < usDestHeight );
	CHECKF( srcRect->iRight > srcRect->iLeft );
	CHECKF( srcRect->iBottom > srcRect->iTop );

	// Determine memcopy coordinates
	uiSrcStart = srcRect->iTop * hImage->usWidth + srcRect->iLeft;
	uiDestStart = usY * usDestWidth + usX;
	uiNumLines = ( srcRect->iBottom - srcRect->iTop );
	uiLineSize = ( srcRect->iRight - srcRect->iLeft );

	CHECKF( usDestWidth >= uiLineSize );
	CHECKF( usDestHeight >= uiNumLines );

	// Convert to Pixel specification
	pDest = ( UINT16*)pDestBuf + uiDestStart;
	pSrc =	hImage->p8BPPData + uiSrcStart;
	DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, String( "Start Copying at %p", pDest ) );

	// For every entry, look up into 16BPP palette
	for( rows = 0; rows < uiNumLines-1; rows++ )
	{
		pDestTemp = pDest;
		pSrcTemp = pSrc;

		for ( cols = 0; cols < uiLineSize; cols++ )
		{
			*pDestTemp = p16BPPPalette[ *pSrcTemp ];
			pDestTemp++;
			pSrcTemp++;
		}

		pDest += usDestWidth;
		pSrc	+= hImage->usWidth;
	}
	// Do last line
	DbgMessage( TOPIC_HIMAGE, DBG_LEVEL_3, String( "End Copying at %p", pDest ) );

	return( TRUE );

}

UINT16 *Create16BPPPalette( SGPPaletteEntry *pPalette )
{
	UINT16 *p16BPPPalette, r16, g16, b16, usColor;
	UINT32 cnt;
	UINT8	r,g,b;

	Assert( pPalette != NULL );

	p16BPPPalette = (UINT16 *) MemAlloc( sizeof( UINT16 ) * 256 );
	memset( p16BPPPalette, 0, sizeof( UINT16 ) * 256 );

	for ( cnt = 0; cnt < 256; cnt++ )
	{
		r = pPalette[ cnt ].peRed;
		g = pPalette[ cnt ].peGreen;
		b = pPalette[ cnt ].peBlue;

		if(gusRedShift < 0)
			r16=((UINT16)r>>abs(gusRedShift));
		else
			r16=((UINT16)r<<gusRedShift);

		if(gusGreenShift < 0)
			g16=((UINT16)g>>abs(gusGreenShift));
		else
			g16=((UINT16)g<<gusGreenShift);


		if(gusBlueShift < 0)
			b16=((UINT16)b>>abs(gusBlueShift));
		else
			b16=((UINT16)b<<gusBlueShift);

		usColor = (r16&gusRedMask)|(g16&gusGreenMask)|(b16&gusBlueMask);

		if(usColor==0)
		{
			if((r+g+b)!=0)
				usColor=BLACK_SUBSTITUTE | gusAlphaMask;
		}
		else
			usColor |= gusAlphaMask;

		p16BPPPalette[ cnt ] = usColor;
	}

	return( p16BPPPalette );
}

/**********************************************************************************************
 Create16BPPPaletteShaded

	Creates an 8 bit to 16 bit palette table, and modifies the colors as it builds.

	Parameters:
		rscale, gscale, bscale:
				Color mode: Percentages (255=100%) of color to translate into destination palette.
				Mono mode:	Color for monochrome palette.
		mono:
				TRUE or FALSE to create a monochrome palette. In mono mode, Luminance values for
				colors are calculated, and the RGB color is shaded according to each pixel's brightness.

	This can be used in several ways:

	1) To "brighten" a palette, pass down RGB values that are higher than 100% ( > 255) for all
			three. mono=FALSE.
	2) To "darken" a palette, do the same with less than 100% ( < 255) values. mono=FALSE.

	3) To create a "glow" palette, select mono=TRUE, and pass the color in the RGB parameters.

	4) For gamma correction, pass in weighted values for each color.

**********************************************************************************************/
UINT16 *Create16BPPPaletteShaded( SGPPaletteEntry *pPalette, UINT32 rscale, UINT32 gscale, UINT32 bscale, BOOLEAN mono)
{
	UINT16 *p16BPPPalette, r16, g16, b16, usColor;
	UINT32 cnt, lumin;
	UINT32 rmod, gmod, bmod;
	UINT8	r,g,b;

	Assert( pPalette != NULL );

	p16BPPPalette = (UINT16 *) MemAlloc( sizeof( UINT16 ) * 256 );
	memset( p16BPPPalette, 0, sizeof( UINT16 ) * 256 );

	for ( cnt = 0; cnt < 256; cnt++ )
	{
		if(mono)
		{
			lumin=(pPalette[ cnt ].peRed*299/1000)+ (pPalette[ cnt ].peGreen*587/1000)+(pPalette[ cnt ].peBlue*114/1000);
			rmod=(rscale*lumin)/256;
			gmod=(gscale*lumin)/256;
			bmod=(bscale*lumin)/256;
		}
		else
		{
			rmod = (rscale*pPalette[ cnt ].peRed/256);
			gmod = (gscale*pPalette[ cnt ].peGreen/256);
			bmod = (bscale*pPalette[ cnt ].peBlue/256);
		}

		r = (UINT8)__min(rmod, 255);
		g = (UINT8)__min(gmod, 255);
		b = (UINT8)__min(bmod, 255);

		if(gusRedShift < 0)
			r16=((UINT16)r>>(-gusRedShift));
		else
			r16=((UINT16)r<<gusRedShift);

		if(gusGreenShift < 0)
			g16=((UINT16)g>>(-gusGreenShift));
		else
			g16=((UINT16)g<<gusGreenShift);


		if(gusBlueShift < 0)
			b16=((UINT16)b>>(-gusBlueShift));
		else
			b16=((UINT16)b<<gusBlueShift);

		// Prevent creation of pure black color
		usColor	= (r16&gusRedMask)|(g16&gusGreenMask)|(b16&gusBlueMask);

		if(usColor==0)
		{
			if((r+g+b)!=0)
				usColor=BLACK_SUBSTITUTE | gusAlphaMask;
		}
		else
			usColor |= gusAlphaMask;

		p16BPPPalette[ cnt ] = usColor;
	}
	return( p16BPPPalette );
}

// Convert from RGB to 16 bit value
UINT16 Get16BPPColor( UINT32 RGBValue )
{
	UINT16 r16, g16, b16, usColor = 0;
	UINT8	r,g,b;

	r = SGPGetRValue( RGBValue );
	g = SGPGetGValue( RGBValue );
	b = SGPGetBValue( RGBValue );

	if(gusRedShift < 0)
		r16=((UINT16)r>>abs(gusRedShift));
	else
		r16=((UINT16)r<<gusRedShift);

	if(gusGreenShift < 0)
		g16=((UINT16)g>>abs(gusGreenShift));
	else
		g16=((UINT16)g<<gusGreenShift);


	if(gusBlueShift < 0)
		b16=((UINT16)b>>abs(gusBlueShift));
	else
		b16=((UINT16)b<<gusBlueShift);

	usColor=(r16&gusRedMask)|(g16&gusGreenMask)|(b16&gusBlueMask);

	// if our color worked out to absolute black, and the original wasn't
	// absolute black, convert it to a VERY dark grey to avoid transparency
	// problems

	if(usColor==0)
	{
		if(RGBValue!=0)
			usColor=BLACK_SUBSTITUTE | gusAlphaMask;
	}
	else
		usColor	|=	gusAlphaMask;

	return(usColor);
}


// Convert from 16 BPP to RGBvalue
UINT32 GetRGBColor( UINT16 Value16BPP )
{
	UINT16 r16, g16, b16;
	UINT32 r,g,b,val;

	r16 = Value16BPP & gusRedMask;
	g16 = Value16BPP & gusGreenMask;
	b16 = Value16BPP & gusBlueMask;

	if(gusRedShift < 0)
		r=((UINT32)r16<<abs(gusRedShift));
	else
		r=((UINT32)r16>>gusRedShift);

	if(gusGreenShift < 0)
		g=((UINT32)g16<<abs(gusGreenShift));
	else
		g=((UINT32)g16>>gusGreenShift);

	if(gusBlueShift < 0)
		b=((UINT32)b16<<abs(gusBlueShift));
	else
		b=((UINT32)b16>>gusBlueShift);

	r &= 0x000000ff;
	g &= 0x000000ff;
	b &= 0x000000ff;

	val = FROMRGB(r,g,b);

	return(val);
}

//*****************************************************************************
//
// ConvertToPaletteEntry
//
// Parameter List : Converts from RGB to SGPPaletteEntry
//
// Return Value	pointer to the SGPPaletteEntry
//
// Modification History :
// Dec 15th 1996->modified for use by Wizardry
//
//*****************************************************************************

SGPPaletteEntry *ConvertRGBToPaletteEntry(UINT8 sbStart, UINT8 sbEnd, UINT8 *pOldPalette)
{
	UINT16 Index;
	SGPPaletteEntry *pPalEntry;
	SGPPaletteEntry *pInitEntry;

	pPalEntry = (SGPPaletteEntry *)MemAlloc(sizeof(SGPPaletteEntry) * 256);
	memset( pPalEntry, 0, sizeof(SGPPaletteEntry) * 256 );
	pInitEntry = pPalEntry;

	DbgMessage(TOPIC_HIMAGE, DBG_LEVEL_0, "Converting RGB palette to SGPPaletteEntry");

	for(Index=0; Index <= (sbEnd-sbStart);Index++)
	{
		pPalEntry->peRed = *(pOldPalette + (Index*3));
		pPalEntry->peGreen = *(pOldPalette + (Index*3) + 1);
		pPalEntry->peBlue = *(pOldPalette + (Index*3) + 2);
		pPalEntry->peFlags = 0;
		pPalEntry++;
	}
	return pInitEntry;
}

BOOLEAN GetETRLEImageData( HIMAGE hImage, ETRLEData *pBuffer )
{
	// Assertions
	Assert( hImage != NULL );
	Assert( pBuffer != NULL );

	// Create memory for data
	pBuffer->usNumberOfObjects = hImage->usNumberOfObjects;

	// Create buffer for objects
	pBuffer->pETRLEObject = (ETRLEObject *) MemAlloc( sizeof( ETRLEObject ) * pBuffer->usNumberOfObjects );
	if(!pBuffer->pETRLEObject)
	{
		return false;
	}
	CHECKF( pBuffer->pETRLEObject != NULL );
	memset( pBuffer->pETRLEObject, 0, sizeof( ETRLEObject ) * pBuffer->usNumberOfObjects );

	// Copy into buffer
	memcpy( pBuffer->pETRLEObject, hImage->pETRLEObject, sizeof( ETRLEObject ) * pBuffer->usNumberOfObjects );

	// Allocate memory for pixel data
	pBuffer->pPixData = MemAlloc( hImage->uiSizePixData );
	if(!pBuffer->pPixData)
	{
		return false;
	}
	CHECKF( pBuffer->pPixData != NULL );
	memset( pBuffer->pPixData, 0, hImage->uiSizePixData );

	pBuffer->uiSizePixData = hImage->uiSizePixData;

	// Copy into buffer
	memcpy( pBuffer->pPixData, hImage->pPixData8, pBuffer->uiSizePixData );

	return( TRUE );
}

void ConvertRGBDistribution565To555( UINT16 * p16BPPData, UINT32 uiNumberOfPixels )
{
	UINT16 *	pPixel;
	UINT32		uiLoop;

	SplitUINT32		Pixel;

	pPixel = p16BPPData;
	for (uiLoop = 0; uiLoop < uiNumberOfPixels; uiLoop++)
	{
		// If the pixel is completely black, don't bother converting it -- DB
		if(*pPixel!=0)
		{
			// we put the 16 pixel bits in the UPPER word of uiPixel, so that we can
			// right shift the blue value (at the bottom) into the LOWER word to protect it
			Pixel.usHigher = *pPixel;
			Pixel.uiValue >>= 5;
			// get rid of the least significant bit of green
			Pixel.usHigher >>= 1;
			// now shift back into the upper word
			Pixel.uiValue <<= 5;
			// and copy back
			*pPixel = Pixel.usHigher | gusAlphaMask;
		}
		pPixel++;
	}
}

void ConvertRGBDistribution565To655( UINT16 * p16BPPData, UINT32 uiNumberOfPixels )
{
	UINT16 *	pPixel;
	UINT32		uiLoop;

	SplitUINT32		Pixel;

	pPixel = p16BPPData;
	for (uiLoop = 0; uiLoop < uiNumberOfPixels; uiLoop++)
	{
		// we put the 16 pixel bits in the UPPER word of uiPixel, so that we can
		// right shift the blue value (at the bottom) into the LOWER word to protect it
		Pixel.usHigher = *pPixel;
		Pixel.uiValue >>= 5;
		// get rid of the least significant bit of green
		Pixel.usHigher >>= 1;
		// shift to the right some more...
		Pixel.uiValue >>= 5;
		// so we can left-shift the red value alone to give it an extra bit
		Pixel.usHigher <<= 1;
		// now shift back and copy
		Pixel.uiValue <<= 10;
		*pPixel = Pixel.usHigher;
		pPixel++;
	}
}

void ConvertRGBDistribution565To556( UINT16 * p16BPPData, UINT32 uiNumberOfPixels )
{
	UINT16 *	pPixel;
	UINT32		uiLoop;

	SplitUINT32		Pixel;

	pPixel = p16BPPData;
	for (uiLoop = 0; uiLoop < uiNumberOfPixels; uiLoop++)
	{
		// we put the 16 pixel bits in the UPPER word of uiPixel, so that we can
		// right shift the blue value (at the bottom) into the LOWER word to protect it
		Pixel.usHigher = *pPixel;
		Pixel.uiValue >>= 5;
		// get rid of the least significant bit of green
		Pixel.usHigher >>= 1;
		// shift back into the upper word
		Pixel.uiValue <<= 5;
		// give blue an extra bit (blank in the least significant spot)
		Pixel.usHigher <<= 1;
		// copy back
		*pPixel = Pixel.usHigher;
		pPixel++;
	}
}

void ConvertRGBDistribution565ToAny( UINT16 * p16BPPData, UINT32 uiNumberOfPixels )
{
	UINT16 *	pPixel;
	UINT32		uiRed, uiGreen, uiBlue, uiTemp, uiLoop;

	pPixel = p16BPPData;
	for (uiLoop = 0; uiLoop < uiNumberOfPixels; uiLoop++)
	{
		// put the 565 RGB 16-bit value into a 32-bit RGB value
		uiRed = (*pPixel) >> 11;
		uiGreen = (*pPixel & 0x07E0) >> 5;
		uiBlue = (*pPixel & 0x001F);
		uiTemp = FROMRGB(uiRed,uiGreen,uiBlue);
		// then convert the 32-bit RGB value to whatever 16 bit format is used
		*pPixel = Get16BPPColor( uiTemp );
		pPixel++;
	}
}
