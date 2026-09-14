# Cold UI Source Audit — Vengeance vs current 1.13

This branch accompanies `vr_gamedir:art/cold-ui-pilot`. It is preparation only; no deployment or launch changes are included.

## Cross-reference result

The checked Vengeance and current 1.13 source retain the same broad UI architecture in the key areas reviewed:

- tactical inventory/interface panels load named assets from `INTERFACE\\...`;
- map UI loads named assets from `INTERFACE\\...`;
- laptop/Bobby Ray loads named assets from `LAPTOP\\...`;
- Bobby Ray layout/text constants are substantially aligned.

That is good for the cold-theme work: the visual layer can be changed without redesigning inventory/shop logic.

## Vengeance source areas that load the active loose UI

### Tactical/Interface Panels.cpp

Loads, among others:

- `inventory_bottom_panel_1024x768.STI`
- `inventory_gold_front.sti`
- `inv_frn.sti`
- `bottom_bar_1024x768.sti`

It also contains font-colour constants and stat-bar rendering.

### Tactical/Interface Items.cpp

Loads inventory figures, info-box art, attachment-slot art and item-description controls.

Important warm hard-coded colours found:

```cpp
#define STATUS_BAR_SHADOW FROMRGB(140, 136, 119)
#define STATUS_BAR        FROMRGB(201, 172, 133)
```

These are clear candidates for the cold-theme source pass, but should be tuned against the generated UI asset pilot rather than changed blindly.

### Strategic/Map Screen Interface Bottom.cpp

Loads:

- `map_screen_bottom_1024x768.sti`
- `map_border_buttons.sti`
- `map_screen_bottom_arrows.sti`

Some map-screen text uses fixed palette indices such as 183 and legacy green/white blinking states.

### Laptop/BobbyRGuns.cpp

Vengeance and current 1.13 use the same core layout constants in the checked header section, including:

```cpp
#define BOBBYR_ORDER_TEXT_COLOR  75
#define BOBBYR_STATIC_TEXT_COLOR 75
```

and load:

- `LAPTOP\\gunbackground.sti`
- `LAPTOP\\gunsgrid.sti`
- catalogue/navigation button sheets.

### Laptop/laptop.cpp

Loads the laptop shell and common laptop chrome, including:

- `LAPTOP\\laptop3.png` when present,
- `taskbar.sti`,
- `programtitlebar.sti`,
- `lights.sti`,
- laptop button sheets.

## Important GitHub-only limitation discovered

Several source-referenced UI sheets are not present as loose files in either current `vr_gamedir` or the GitHub mirror checked. They are inherited from base SLF/VFS data. Examples include:

- `map_border_buttons.sti`
- `map_screen_bottom_arrows.sti`
- `inventory_buttons.sti`
- `Bars.sti`
- `taskbar.sti`
- `programtitlebar.sti`
- several laptop catalogue/button sheets

Therefore the complete theme will eventually require a bridge that extracts those original assets from the installed base data and places cold replacements into the override layer.

GitHub remains the source of truth; the bridge is only for obtaining binary source art that is not stored loose in Git.

## Planned source pass after asset review

Do not apply until the cold asset pilot is visually reviewed:

1. Convert warm inventory status bars to steel/cyan values.
2. Harmonise selected-state and title text colours.
3. Keep semantic colours (health red, warning orange/yellow, positive green) distinct.
4. Avoid changing gameplay feedback colours merely for aesthetics.
5. Keep all source changes isolated on `art/cold-ui-pilot` until explicitly approved.


## Current 1.13 cross-check — concrete matches

A second pass against the current public 1.13 source confirms the key Vengeance constants and asset-loading model are not Vengeance-specific accidents:

- `Tactical/Interface Items.cpp` in current 1.13 still uses the same warm status-bar pair:
  - `STATUS_BAR_SHADOW FROMRGB(140, 136, 119)`
  - `STATUS_BAR FROMRGB(201, 172, 133)`
- `Laptop/BobbyRGuns.cpp` still uses `BOBBYR_ORDER_TEXT_COLOR 75` and `BOBBYR_STATIC_TEXT_COLOR 75`, and loads `gunbackground.sti` plus `gunsgrid.sti`.
- `Tactical/Interface Panels.cpp` still loads `inventory_bottom_panel_1024x768.STI`, `inventory_gold_front.sti`, `inv_frn.sti`, `Bars.sti`, `bottom_bar_1024x768.sti` and `gold_front.sti`.
- `Strategic/Map Screen Interface Bottom.cpp` still loads `map_screen_bottom_1024x768.sti` and retains the legacy map-bottom font colour logic.

The useful difference is that current 1.13 has additional widescreen-specific UI handling (for example a 1280x720 map-bottom asset). Vengeance does not currently mirror all of that handling, so this cold pilot deliberately themes the assets Vengeance actually requests rather than importing the newer 1.13 layout wholesale.

This reinforces the chosen approach: preserve JA2/1.13 layout semantics and modernise the Vengeance presentation layer rather than redesigning the UI architecture.


## Source pilot now staged

The branch now contains one deliberately narrow source-side visual change:

- `Tactical/Interface Items.cpp`
  - item-condition/status-bar shadow: warm grey-brown -> dark steel blue
  - item-condition/status-bar foreground: tan -> restrained cold steel/cyan

No health, damage, warning, morale or other semantic gameplay colours were changed.

Map-screen palette-index text and Bobby Ray palette-index text remain untouched until the real asset preview sheets are reviewed, because changing those indices without seeing the themed backgrounds would be guesswork.
