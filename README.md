# Blacklight: Tango Down enhancements mod

A mod for the PC version of Blacklight: Tango Down (Steam, `BLTD.exe` 1.0).
It adds friendly bot support, brings the PS3-only map Crossover to the PC, adds custom in-game events
and multiple quality of life fixes.

## Download

Grab the [latest release](../../releases).

## Features

- Adds friendly bots to Black Ops co-op.
- Adds bots to the versus modes: Deathmatch, Team Deathmatch, Domination, Last Man Standing, Last Team Standing and
  Retrieval.
- Adds a navmesh to every versus map and fixes the Black Ops navmesh.
- Adds the PS3 map Crossover.
- Adds bot rows to the Black Ops, private match and Quick Match menus.
- Lowers the countdown of the public lobbies you host to 40 seconds.
- Adds crosshair options: static or dynamic, size and colour.
- Spawns players near their teammates and away from enemies.
- Lets players switch teams in public matches.
- Shows turrets in the HRV view.
- Marks the host in the lobby and on the scoreboard.
- Fixes the Aim Assistance option.
- Fixes the recoil.
- Lets only players with the mod join the matches you host.
- Fixes game crashes at map changes, during loading movies and at exit.
- Stops the memory check of Games for Windows - LIVE from crashing the host.
- Adds a Halloween theme on the 31st of October.

## Install

### dinput8.dll

Copy these files into `Binaries\Win32\`, next to `BLTD.exe`:

- `dinput8.dll`. The game loads it instead of the system's DirectInput library, and the mod forwards the calls to
  the system's copy.
- The `bltd-bots` folder. It holds the navmesh files, the menu movies, the Crossover map and the Halloween package.

If another mod already uses `dinput8.dll`, use Ultimate ASI Loader instead.

### Ultimate ASI Loader

Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) next to `BLTD.exe`. Use any of
its file names except `xlive.dll`. Then copy these files into `Binaries\Win32\scripts\`:

- `bltd-bots.asi`. It is the same file as `dinput8.dll`, with the plugin name.
- The `bltd-bots` folder.

The mod writes its files to `scripts\bltd-bots\`.

Install only one form at a time. A second copy of the mod sees that the game is hooked already, writes that to its
log and does nothing. For the same reason, remove `xlln-blacklight-tango-down.dll` of an earlier version from
`XLiveLessNess\modules`.

### Remove

Delete `dinput8.dll` and the `bltd-bots` folder.

## Build

Visual Studio 2022 with the C++ workload and CMake 3.15 or later:

```
cmake -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

The files in `bltd-bots/navcache`, `bltd-bots/ui` and `bltd-bots/maps` are data. The tools that make them are not
in this repository.

## License

[MIT](LICENSE).
