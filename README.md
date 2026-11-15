# Stashy's Toolbelt

Build gun quality-of-life mod for Satisfactory 1.2. Every feature can be switched on or off per
player in **Mods → Stashy's Toolbelt** (main or pause menu) or with the console.

Mod reference: `StashysToolbelt` · Version 1.2.2 · License: GPL-3.0-or-later

## Features

- **Zoop by default.** Buildings that can zoop start in zoop mode each session. If you pick
  another build mode for a category, that choice sticks for the rest of the session.
- **Stackable pole height.** Stackable conveyor poles, pipeline supports and hypertube supports
  start at the height of the last stack you built this session. After placing the base, the
  scroll wheel adds or removes one level; optionally, aiming sets the height (vanilla-style). The
  remembered height is forgotten when that stack is gone or you're outside the working area.
- **Ghost belt/pipe preview.** After building a pole (stackable or single), a ghost belt or pipe
  shows the run from the last pole you built to where you're aiming. It turns yellow near the
  length limit. Only shown within the working area.
- **Colour inheritance.** New pieces take the colour (swatch and custom colour, not material or
  pattern) of what they're built from:
  - power poles, wall outlets and towers built by dragging a power line from a pole/outlet/tower
  - belts, lifts, pipes and hypertubes continued from a belt/lift, pipe or hypertube, including
    through a pole, and belts/pipes started from a splitter, merger, junction, pump or valve
  - splitters, mergers, pumps, valves and junctions placed onto a belt or pipe
  - stackable poles placed onto an existing stack

  Never from machines, never repaints anything that already exists, and blueprints keep their
  own colours.
- **Keep colour on upgrade.** Upgrading anything (belts, lifts, pipes, power poles, …) keeps its
  colour.
- **Power pole tier from buildings.** A power line dragged from a building ends in the pole or
  wall outlet tier you choose (Mk1–Mk3; default Mk2). If that tier isn't unlocked yet, the
  highest unlocked tier below it is used.

## Settings

| Setting (console key) | Default | What it does |
|---|---|---|
| `ZoopByDefault` | on | zoop as the default build mode |
| `RememberStackHeight` | on | stacks remember the last height |
| `ScrollToHeight` | on | scroll wheel changes stack height by one level |
| `AimToHeight` | off | aiming up/down changes stack height |
| `WorkingArea` | 75 m (0–1000) | range for remembered height and the ghost preview |
| `GhostPreview` | on | ghost belt/pipe from the last pole |
| `InheritPowerSwatch` | on | colour for power poles/outlets/towers |
| `InheritBeltPipeSwatch` | on | colour for belts, lifts, pipes, hypertubes |
| `InheritStackSwatch` | on | colour for poles placed onto a stack |
| `InheritAttachmentSwatch` | on | colour for splitters/mergers/pumps/valves/junctions |
| `KeepColourOnUpgrade` | on | upgrades keep their colour |
| `PowerPoleTierFromBuilding` | 2 (1–3) | pole tier at the end of a line from a building; 1 = vanilla |

Settings are saved to `FactoryGame/Configs/StashysToolbelt.cfg`.

**Console** (open with `` ` `` or `~` in a save):
`Toolbelt.Status` (settings and current state) · `Toolbelt.Set <Key> <Value>` (for example
`Toolbelt.Set ZoopByDefault off`; numbers are clamped to the ranges above) · `Toolbelt.Reset`
(back to defaults).

## Requirements and compatibility

- Satisfactory **1.2.5.0 or newer** (game build CL 517554+), SML **3.12.0** or newer 3.x.
- **Windows game client only.** This release has no dedicated-server build.
- **Single player: tested.** Multiplayer with a player host and clients: **not tested yet.** In
  multiplayer every player needs the mod (each player's settings are sent to the host).
- **Dedicated servers: not supported in 1.2.2.** The server can't install it, and SML won't let a
  client with the mod join a server without it.
- Untested together, possible overlaps: other zoop mods (Infinite Zoop, Oops! All Zoops,
  Blueprint Zooper), CustomSwatches (likely conflict), Universal Swatch Slots and Automatic
  Pipeline Color (expected fine), Construction Preferences.
- The mod adds no items, recipes or buildings and stores nothing in save files, so it can be
  removed from a save at any time.

## Install

With the [Satisfactory Mod Manager](https://ficsit.app) — search for *Stashy's Toolbelt*.

## Reporting a problem

Include what you did, what happened, single player or multiplayer, and your log:
`%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log` (lines with `LogStashysToolbelt`,
`LogSML` and any `Error`/`Fatal`), plus `Toolbelt.Status` output. For more detail, launch with
`-LogCmds="LogStashysToolbelt Verbose"`.

## Building from source

A plain C++ mod (no assets) for SML 3.12. 1.2.2 was compiled against SML `master` headers at game
CL 517554 (SML's own code there is unchanged from 3.12.0). Put `StashysToolbelt/` into your SML
project's `Mods/` folder and build it with Alpakit. `Config/Alpakit.ini` lists all three targets;
only the Windows client has been released and tested.

## Repository

| Path | What |
|---|---|
| `StashysToolbelt/` | the mod (plugin source) |
| `StashysToolbelt-CHANGELOG.md` | version history |
| `StashysToolbelt-SPEC.md` | design notes: how each feature works, what's still unverified |
| `StashysToolbelt-TESTING.md` | in-game test procedure |

## License

GNU General Public License v3.0 or later — see `LICENSE`. Every source file carries
`SPDX-License-Identifier: GPL-3.0-or-later`.

## Credits

- Written by Stashy with Claude (Anthropic): disclosed as generative-AI assisted on ficsit.app.
- Built on the Satisfactory Mod Loader (SML, GPLv3) and the Satisfactory Modding community's
  headers and documentation. No code or assets copied from other mods; vanilla assets are only
  referenced by path, never redistributed.
- Satisfactory is a trademark of Coffee Stain Studios; this mod is unofficial.
