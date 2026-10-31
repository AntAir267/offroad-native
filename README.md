# Off-Road: Redneck Racing native

Off-Road: Redneck Racing (Rage / Interplay, 2001) recompiled for Linux on SDL3 and Vulkan. The game's
own OffRoad.exe is translated into C and built against a stand-in for Windows, DirectX 7, Miles and Bink.
Bring your own copy of the game: this repository holds only the tools and runtime.

    make GAME=<folder with OffRoad.exe>
    make install

See DESIGN.md for the state of the port.
