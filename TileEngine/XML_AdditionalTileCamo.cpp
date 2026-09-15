#include "builddefines.h"

#ifdef PRECOMPILEDHEADERS
	#include "TileEngine All.h"
#else
	#include <stdio.h>
	#include <string.h>
	#include "tiledef.h"
	#include "Debug.h"
	#include "FileMan.h"
	#include "Debug Control.h"
#endif

#include "expat.h"
#include "XML.h"

struct
{
	PARSE_STAGE curElement;
	CHAR8 szCharData[MAX_CHAR_DATA_LENGTH + 1];
	UINT32 currentDepth;
	UINT32 maxReadDepth;
}
typedef additionalTileCamoParseData;

static void XMLCALL additionalTileCamoStartElementHandle(void *userData, const XML_Char *name, const XML_Char **atts)
{
	additionalTileCamoParseData *pData = (additionalTileCamoParseData *)userData;

	if (pData->currentDepth <= pData->maxReadDepth)
	{
		if (strcmp(name, "ADDITIONALTILEPROPERTIES") == 0 && pData->curElement == ELEMENT_NONE)
		{
			pData->curElement = ELEMENT;
			memset(&zAdditionalTileCamoProperties, 0, sizeof(zAdditionalTileCamoProperties));
			pData->maxReadDepth++;
		}
		else if (pData->curElement == ELEMENT &&
			(strcmp(name, "bWoodCamoAffinity") == 0 ||
			 strcmp(name, "bDesertCamoAffinity") == 0 ||
			 strcmp(name, "bUrbanCamoAffinity") == 0 ||
			 strcmp(name, "bSnowCamoAffinity") == 0 ||
			 strcmp(name, "bCamoStanceModifer") == 0))
		{
			pData->curElement = ELEMENT_PROPERTY;
			pData->maxReadDepth++;
		}

		pData->szCharData[0] = '\0';
	}

	pData->currentDepth++;
}

static void XMLCALL additionalTileCamoCharacterDataHandle(void *userData, const XML_Char *str, int len)
{
	additionalTileCamoParseData *pData = (additionalTileCamoParseData *)userData;

	if (pData->currentDepth <= pData->maxReadDepth &&
		strlen(pData->szCharData) < MAX_CHAR_DATA_LENGTH)
	{
		strncat(pData->szCharData, str,
			__min((unsigned int)len, MAX_CHAR_DATA_LENGTH - strlen(pData->szCharData)));
	}
}

static void XMLCALL additionalTileCamoEndElementHandle(void *userData, const XML_Char *name)
{
	additionalTileCamoParseData *pData = (additionalTileCamoParseData *)userData;

	if (pData->currentDepth <= pData->maxReadDepth)
	{
		if (strcmp(name, "ADDITIONALTILEPROPERTIES") == 0)
		{
			pData->curElement = ELEMENT_NONE;
		}
		else if (strcmp(name, "bWoodCamoAffinity") == 0)
		{
			pData->curElement = ELEMENT;
			zAdditionalTileCamoProperties.bWoodCamoAffinity = (INT8)atol(pData->szCharData);
		}
		else if (strcmp(name, "bDesertCamoAffinity") == 0)
		{
			pData->curElement = ELEMENT;
			zAdditionalTileCamoProperties.bDesertCamoAffinity = (INT8)atol(pData->szCharData);
		}
		else if (strcmp(name, "bUrbanCamoAffinity") == 0)
		{
			pData->curElement = ELEMENT;
			zAdditionalTileCamoProperties.bUrbanCamoAffinity = (INT8)atol(pData->szCharData);
		}
		else if (strcmp(name, "bSnowCamoAffinity") == 0)
		{
			pData->curElement = ELEMENT;
			zAdditionalTileCamoProperties.bSnowCamoAffinity = (INT8)atol(pData->szCharData);
		}
		else if (strcmp(name, "bCamoStanceModifer") == 0)
		{
			pData->curElement = ELEMENT;
			zAdditionalTileCamoProperties.bCamoStanceModifer = (INT8)atol(pData->szCharData);
		}

		pData->maxReadDepth--;
	}

	pData->currentDepth--;
}

BOOLEAN ReadInAdditionalTileCamoProperties(STR fileName)
{
	HWFILE hFile;
	UINT32 uiBytesRead;
	UINT32 uiFSize;
	CHAR8 *lpcBuffer;
	XML_Parser parser = XML_ParserCreate(NULL);
	additionalTileCamoParseData pData;

	if (parser == NULL)
		return FALSE;

	hFile = FileOpen(fileName, FILE_ACCESS_READ, FALSE);
	if (!hFile)
	{
		XML_ParserFree(parser);
		return FALSE;
	}

	uiFSize = FileGetSize(hFile);
	lpcBuffer = (CHAR8 *)MemAlloc(uiFSize + 1);
	if (lpcBuffer == NULL)
	{
		FileClose(hFile);
		XML_ParserFree(parser);
		return FALSE;
	}

	if (!FileRead(hFile, lpcBuffer, uiFSize, &uiBytesRead))
	{
		FileClose(hFile);
		MemFree(lpcBuffer);
		XML_ParserFree(parser);
		return FALSE;
	}

	lpcBuffer[uiFSize] = 0;
	FileClose(hFile);

	XML_SetElementHandler(parser, additionalTileCamoStartElementHandle, additionalTileCamoEndElementHandle);
	XML_SetCharacterDataHandler(parser, additionalTileCamoCharacterDataHandle);

	memset(&pData, 0, sizeof(pData));
	XML_SetUserData(parser, &pData);

	if (!XML_Parse(parser, lpcBuffer, uiFSize, TRUE))
	{
		DebugMsg(TOPIC_JA2, DBG_LEVEL_0, String(
			"Additional tile camouflage XML error in %s: %s at line %d",
			fileName,
			XML_ErrorString(XML_GetErrorCode(parser)),
			XML_GetCurrentLineNumber(parser)));
		MemFree(lpcBuffer);
		XML_ParserFree(parser);
		return FALSE;
	}

	MemFree(lpcBuffer);
	XML_ParserFree(parser);
	return TRUE;
}
