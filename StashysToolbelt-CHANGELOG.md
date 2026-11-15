# Stashy's Toolbelt — changelog

Versioning: SemVer (`.uplugin` `SemVersion` == `VersionName`, `Version` = major).
**patch** = fixes only · **minor** = new features/settings · **major** = breaking (for example a
setting removed in a way that loses saved values). Every build gets its own number.

## 1.2.2 — first public release (Windows client)
- For Satisfactory **1.2.5.0** (game build CL 517554) and newer; SML ^3.12.0. Built against SML
  `master` headers at CL 517554 (SML's own code there is unchanged from 3.12.0); `GameVersion`
  `>=517554`. The rebuild was needed because the game's buildable classes changed layout in
  1.2.5.0.
- Released for the **Windows client only**. No dedicated-server build in this release.
- Tested in single player. Multiplayer (host + clients) and dedicated servers are not tested.
- Performance: finding the poles in a column (stack height, ghost preview) now only looks at poles
  of that kind instead of every building in the world, so the cost depends on how many such poles
  there are, not on the size of the factory.
- Fixed: `Toolbelt.Set` clamps numbers to the settings screen's range before saving (for example
  `PowerPoleTierFromBuilding 7` → 3) and says so. Before, the out-of-range value was saved and the
  settings screen showed the field as invalid (the mod itself already clamped it when used).
- `.uplugin`: `CanContainContent` false (the mod has no content folder).

---

**Development history** (builds before the first public release):

## 1.2.1 — licensing (no code changes)
- Licensed GPL-3.0-or-later: `LICENSE` (in the bundle root and the mod folder), SPDX header in
  every source file, `README.md` with credits.

## 1.2.0 — test round 1 fixes + height-control redesign
- Fixed: top ghost belt/pipe ran toward the world origin.
- New: stack height control split into three toggles — remember height, scroll wheel sets height,
  aim sets height (off by default).
- New: remembered height is forgotten when its stack is gone, and doesn't apply outside the new
  **Working area** setting (replaces the ghost preview range; shared by both).
- Changed: ghost colours — no red; yellow from 90% to 120% of the length limit.
- New: belts/pipes inherit colour from splitters/mergers/junctions/pumps/valves they start at.
- New: **Keep colour on upgrade** (all upgrades), toggle.
- Fixed: settings screen showed everything in one row; number fields had a red outline (no range).
- Tooling: log redaction script also removes identity blobs in travel URLs.

## 1.1.0 — additions (the build tested in round 1; its .uplugin still said 1.0.0)
- New: console commands `Toolbelt.Status`, `Toolbelt.Set`, `Toolbelt.Reset`.
- New: ghost preview also for single Conveyor Poles / Pipeline Supports (per belt/pipe family).
- New: ghost turns yellow near the length limit.
- New: colour inheritance for splitters/mergers/pumps/valves/junctions placed onto a line, and
  hypertube → hypertube.

## 1.0.2 — second (independent) review fixes (not built): snap frames, height going down, ghost
local/world space, remote players before settings sync, listen-host ghost flicker, caching.
## 1.0.1 — first review fixes (not built): unity build, MP latency snapshot at the build click,
zoop settle ticks, zoop limit, ghost scan caching.
## 1.0.0 — first complete source (not built): settings + RCO, colour inheritance, zoop default,
stack height memory, ghost preview, power pole tier from buildings.
