# Vengeance LOBOT equipment graphics

This branch backports the JA2 1.13 Logical Body Types framework into Vengeance Reloaded without replacing the existing Vengeance merc renderer.

## First-pass visible layers

- helmet
- face gear
- vest / armour silhouette
- left and right leg rigs / holsters
- knee pads
- backpack
- right- and left-hand weapon overlays

The original Vengeance merc sprite is always rendered first. Missing LOBOT surfaces therefore fall back to the normal Vengeance appearance instead of making a soldier invisible.

## Asset pin

The deployment script uses JA2 1.13 commit:

`cd3fec5665825d7f6c05ba0093fd238891b17d45`

This is intentionally older than current 1.13: it contains the mature core equipment layers but predates several animation/filter extensions that Vengeance does not implement.

## Install

From the local source repository under `C:\VENGENCE\Jagged Alliance 2\00000`, run:

`DEPLOY_LOBOT_EQUIPMENT.bat`

The script assumes the game root is the parent directory and installs assets into `Data-Vengeance`. It does not require Git.
