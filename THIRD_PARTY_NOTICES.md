# Dependency provenance

Source dependencies are fetched into ignored build directories, never copied from a game ROM.

| Dependency | Tested source revision | License / upstream |
| --- | --- | --- |
| SDL 3.2.28 | `7f3ae3d57459e59943a4ecfefc8f6277ec6bf540` | zlib, https://github.com/libsdl-org/SDL/blob/release-3.2.28/LICENSE.txt |
| Catch2 3.8.1 | `56809e5282f104c5c8b570e7c2996cdc352d94f1` | BSL-1.0, https://github.com/catchorg/Catch2/blob/v3.8.1/LICENSE.txt |

SDL debug text is supplied by SDL itself; no external font/art/audio assets are included. Copy applicable upstream license texts with any future binary distribution. The vcpkg manifest pins its registry baseline independently; the default tested FetchContent build does not activate vcpkg or imply its package versions match the above source pins.
