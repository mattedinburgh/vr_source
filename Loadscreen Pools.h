#ifndef __LOADSCREEN_POOLS_H
#define __LOADSCREEN_POOLS_H

#include <vector>
#include <map>
#include <algorithm>

// Vengeance Reloaded numbered loadscreen pools.
//
// SectorLoadscreens.xml syntax:
//   POOL:Loadscreens\RealConflict\Town\Town|Loadscreens\LS_Shanty_street
//
// Pool files use sequential names:
//   Town_001_1920x1080.png
//   Town_002_1920x1080.png
//   ...
//
// The engine builds a shuffle bag so every available image is used once
// before any image in that pool repeats. A short global history also avoids
// immediate repeats when moving between different sector pools.

static std::map<std::string, std::vector<std::string> > gLoadscreenPoolAll;
static std::map<std::string, std::vector<std::string> > gLoadscreenPoolBag;
static std::vector<std::string> gRecentLoadscreens;
static const UINT32 LOADSCREEN_RECENT_HISTORY = 30;

static BOOLEAN LoadscreenWasRecent(const std::string& baseName)
{
	return std::find(gRecentLoadscreens.begin(), gRecentLoadscreens.end(), baseName) != gRecentLoadscreens.end();
}

static void RememberPooledLoadscreen(const std::string& baseName)
{
	gRecentLoadscreens.push_back(baseName);
	if (gRecentLoadscreens.size() > LOADSCREEN_RECENT_HISTORY)
		gRecentLoadscreens.erase(gRecentLoadscreens.begin());
}

static void DiscoverLoadscreenPool(const std::string& poolBase, SCREEN_RESOLUTION resolution)
{
	if (gLoadscreenPoolAll.find(poolBase) != gLoadscreenPoolAll.end())
		return;

	std::vector<std::string>& found = gLoadscreenPoolAll[poolBase];
	UINT32 consecutiveMisses = 0;
	BOOLEAN foundAny = FALSE;

	for (UINT32 i = 1; i <= 999; ++i)
	{
		CHAR8 number[8];
		sprintf(number, "_%03u", i);
		std::string candidateBase = poolBase + number;
		std::string candidateFile = FindBestFittingLoadscreenFilename(candidateBase, resolution);

		if (FileExists((CHAR8*)candidateFile.c_str()))
		{
			found.push_back(candidateBase);
			foundAny = TRUE;
			consecutiveMisses = 0;
		}
		else if (foundAny)
		{
			++consecutiveMisses;
			if (consecutiveMisses >= 12)
				break;
		}
	}
}

static std::string ResolveExternalLoadscreenFilename(const std::string& configuredPath, SCREEN_RESOLUTION resolution)
{
	if (configuredPath.compare(0, 5, "POOL:") != 0)
		return FindBestFittingLoadscreenFilename(configuredPath, resolution);

	size_t fallbackPos = configuredPath.find('|', 5);
	std::string poolBase = configuredPath.substr(
		5,
		fallbackPos == std::string::npos ? std::string::npos : fallbackPos - 5);

	std::string fallbackPath;
	if (fallbackPos != std::string::npos)
		fallbackPath = configuredPath.substr(fallbackPos + 1);

	DiscoverLoadscreenPool(poolBase, resolution);

	std::vector<std::string>& bag = gLoadscreenPoolBag[poolBase];
	if (bag.empty())
		bag = gLoadscreenPoolAll[poolBase];

	while (!bag.empty())
	{
		std::vector<UINT32> eligible;
		for (UINT32 i = 0; i < (UINT32)bag.size(); ++i)
		{
			if (!LoadscreenWasRecent(bag[i]))
				eligible.push_back(i);
		}

		UINT32 selectedIndex = eligible.empty()
			? Random((UINT32)bag.size())
			: eligible[Random((UINT32)eligible.size())];

		std::string selectedBase = bag[selectedIndex];
		bag.erase(bag.begin() + selectedIndex);

		std::string selectedFile = FindBestFittingLoadscreenFilename(selectedBase, resolution);
		if (FileExists((CHAR8*)selectedFile.c_str()))
		{
			RememberPooledLoadscreen(selectedBase);
			return selectedFile;
		}
	}

	if (!fallbackPath.empty())
		return FindBestFittingLoadscreenFilename(fallbackPath, resolution);

	return FindBestFittingLoadscreenFilename(poolBase, resolution);
}

#endif
