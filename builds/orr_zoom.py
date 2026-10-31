"""Per-build knowledge for Off-Road: Redneck Racing (Rage / Interplay, 2001), the ZOOM Platform
release: OffRoad.exe 1,798,144 bytes (the same file as OffRoadBump.exe), linked 2001-04-10.

Addresses only: no code or data from the game lives in this repository.
"""
NAME = 'orr-zoom-bump'
SHA256 = '15545ebd43a9b1cad09a7047a52c66005f4b5a52f7d8cdf3c2ce5166dc510a64'

# Functions reached only in ways discovery cannot see (found at run time, added here).
EXTRA_ENTRIES = [
    # functions Ghidra found that discovery reaches only by jumps, or not at all (2026-10-30)
    0x4233e0, 0x423400, 0x45d2f0, 0x46c2d0, 0x483ad0, 0x48e710, 0x4bb490, 0x4f70f0, 0x525100,
    0x527380, 0x558260, 0x56f851, 0x5710c4, 0x571106, 0x57119a, 0x571279, 0x5740a0, 0x5740de,
    0x57411b, 0x57429b, 0x5742ce, 0x578830, 0x578870,
]

# Addresses that look like code pointers but are not function entries.
NOT_ENTRIES = []

# Jump tables discovery cannot bound: jmp address -> (table address, entries)
JUMP_TABLES = {
    # [verified: disassembly, 2026-10-30]
    0x4e16f2: (0x4e1877, 4),    # cmp [ebp-0x34], 3
    # CRT memcpy (0x5716d0) and its second copy (0x576e50): byte tails with negative indexes,
    # read from their true starts (sub ecx, 4 / jb: -4..-1; neg ecx: -7..0), as in td6-native
    0x571724: (0x571818, 4),
    0x571892: (0x571944, 8),
    0x576ea4: (0x576f98, 4),
    0x577012: (0x5770c4, 8),
}

# Jumps into the middle of another function (exception handling, longjmp).
NONLOCAL_JUMPS = []

# Imports that never return.
NORETURN_IMPORTS = ['ExitProcess', 'ExitThread', 'TerminateProcess']

TABLES = []
# Functions the runtime may hook (see 1nsane-native / td6-native): hook_<name>, run at entry.
HOOKS = {
    # a camera's viewport set (ECX the camera, first argument 14 floats: focal x, y at +0, +4, width
    # at +0x24): widescreen.c widens the race's
    0x4f1350: 'viewport',
    # the renderer's clip rectangle set (ECX the renderer, two pointers to x, y: its +0x3ec..+0x3f8,
    # which every polygon is clipped to): widescreen.c lets the world past the 4:3 picture's sides
    0x4f6b20: 'clip_rect',
    # the camera's scale for flat drawings (ECX the camera: +0x1fc, +0x200 origin, +0x204, +0x208 scale
    # from 640 x 480): widescreen.c keeps them 4:3, so the HUD lays out as it always did
    0x4f17d0: 'camera_flat',
    # padgame.c: a control bound (ECX the input object; action name, input name, player); a line of
    # text drawn (ECX the font; x, y, text, ...): the controller's names and prompts
    0x4f9f20: 'bind',
    0x4b5540: 'text',
    # a race's controls set up (ECX the game: +0x10bc player 1, +0x4ec players)
    0x460970: 'race_controls',
    # the front end being loaded (at start and back from a race): every controller is player 1's again
    0x43a5a0: 'front_end',
    # padgame.c: a font's characters read (ECX the font, first argument the .dat's path); one character
    # drawn (ECX the glyph, arguments the batch and the colour): the button icons
    0x4b7a30: 'font_chars',
    0x4b68d0: 'glyph_draw',
    # menu.c: VIDEO's widgets being made (ECX the table), and its screen's handler (ECX the widget, EDX
    # the message)
    0x488330: 'video_rows',
    0x4874f0: 'video_screen',
    # game.c: cameras about to take the far plane from data (0x5d0e78): the draw distance option
    0x4a54b0: 'cameras_file',
    0x4d94b0: 'race_cameras',
}
CONSTS = {}
INLINE = {}
# Game data the runtime reads: emitted as GAME_<NAME> in gen/image.h.
DATA = {}
