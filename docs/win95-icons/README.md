# Win95 icon catalogue

Contact sheets of every `RT_ICON` in the real Windows 95 modules Debian95 can pull
icons from at runtime. `tools/icon_catalog.py` regenerates them from the corpus.

| Module | Kind | Icons | Notable |
|--------|------|-------|---------|
| SHELL32.DLL  | PE | 136 | My Computer (36), Network (40), Recycle (69), Win flag (80), folders, drives, docs |
| MORICONS.DLL | NE | 106 | classic MS-DOS application icons |
| PROGMAN.EXE  | NE |  92 | Program Manager group/item icons |
| MMSYS.CPL    | PE |  51 | multimedia: speaker, CD, MIDI, mic |
| PIFMGR.DLL   | NE |  39 | the "change icon" set: MS-DOS, apple, beach ball, race car, cards |
| USER.EXE     | NE |  19 | message-box icons — warning (7), question (9), error (11), info (13); exit door (18) |
| MAIN.CPL     | PE |  16 | Control Panel: mouse, keyboard, printers, fonts, regional |
| EXPLORER.EXE | PE |  14 | shell/taskbar icons |
| WINFILE.EXE  | NE |   8 | File Manager |

The loader (`subsystems/win32/icons.c`) reads any of these with
`icon_get_from("MODULE.EXT", id, size, bg, out)` (`icon_get(id,...)` = SHELL32).
Modules are staged into the desktop data dir by `package.sh` and the Buildroot
`debian95-desktop.mk`.
