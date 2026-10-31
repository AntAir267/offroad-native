# Hard Truck 2: Recompiled

Hard Truck 2: King of the Road, translated from its Windows code into C and rebuilt as a
native game for Linux and Windows: Vulkan graphics, SDL3 input and sound, controller support with rumble, and a
MORE OPTIONS page in the game's own options menu (resolution, widescreen, anti-aliasing, texture
filtering, vsync, frame limit, dead zone, renderer). The original look is always one option away.

None of the game is included. You need your own copy from GOG (Hard Truck 2 - King of the Road,
the 1.3 release).

## Playing

1. Download the AppImage for Linux and make it executable (Windows: see below).
2. Run it. On first run it asks for the GOG installer (setup_hard_truck_2_...exe) or a folder
   where the game is installed, finds it on its own if it is in Downloads or a usual games
   folder, checks every file, then translates and compiles the game on your computer (a few
   minutes, once).
3. After that it starts the game straight away.

Your settings and saves live in ~/.local/share/hardtruck2-native.

On Windows: unzip the download anywhere and run hardtruck2-recompiled.exe; the same steps follow.
It needs a graphics driver with Vulkan 1.3. Settings and saves live in %APPDATA%\hardtruck2-native.

Known gaps: the instrument bar stays 4:3 in widescreen; multiplayer is not supported; the game's
control profiles are keyboard-only (the controller has its own layout).

## Licence

Public domain (LICENSE). CREDITS.md lists the libraries and their licences.
Building from source: BUILDING.md.
