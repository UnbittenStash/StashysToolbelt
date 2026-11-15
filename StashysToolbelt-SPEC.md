# Stashy's Toolbelt — design notes (GPL-3.0-or-later)

Developer notes for version 1.2.2: what each feature does, how it is implemented, and what is
still unverified. For players, `README.md` is the overview. Mod reference `StashysToolbelt`.

Plain C++ mod (no assets, no Game Feature). SML ^3.12.0, GameVersion >=517554 (built against SML master headers, CL 517554).
Required on client and server; every player has it installed.

## Settings (per player)
SML Mod Settings screen, defined in C++ (`UToolbeltConfig`), stored in
`FactoryGame/Configs/StashysToolbelt.cfg`. Fallback if the C++-built screen doesn't draw: values
still load/save from that file; console commands can be added.
Server-side features use the *building player's* settings: clients send theirs through
`UToolbeltRCO` (polled every 0.5 s, first after 0.1 s); host/single player reads its own config directly.

| Key | Default | Feature |
|---|---|---|
| ZoopByDefault | on | 3 |
| ScrollToHeight | on | 1 |
| AimToHeight | off | 1 |
| WorkingArea | 75 m | 1, 4 (replaces GhostPreviewMaxDistance) |
| KeepColourOnUpgrade | on | 2 |
| RememberStackHeight | on | 1 |
| GhostPreview | on | 4 |
| InheritPowerSwatch | on | 2a |
| InheritBeltPipeSwatch | on | 2b |
| InheritStackSwatch | on | 2c |
| InheritAttachmentSwatch | on | 2d |
| PowerPoleTierFromBuilding | 2 (1–3, 1 = vanilla) | 5 |

## 1. Stackable poles remember height
- Applies to: Stackable Conveyor Pole, Stackable Pipeline Support. (Single adjustable Conveyor
  Pole / Pipeline Support: open question.)
- Remembered value = stack count of the last one actually **built** (per kind: belt vs pipe).
  Per session only — resets on load/join.
- New hologram starts at that count; adjustment is **relative** (aim up/down changes the count
  from the remembered start; never snaps to the crosshair).
- Building onto an existing stack: build up to the remembered **total**. If the existing stack is
  already at/above it, no change (vanilla behaviour).
- Only in zoop mode (feature 3 makes that the default). The base-placement step still shows one
  pole; the remembered height appears after the first click (zoop step).
- Implementation (piece 4, `ToolbeltStackHeight.cpp`, local player only):
  - after-hook `AFGStackablePoleHologram::SetHologramLocationAndRotation` (placement step):
    arm a fresh start, count existing levels in the snapped column;
  - after-hook `AFGStackablePoleHologram::SetZoopFromHitresult` (zoop step): first frame records
    start, aim and scroll offset; Z = start + aim change (if on) + scroll, clamped, via `SetZoopAmount`
    (see "Test round 1 feedback");
  - after-hook `UFGBuildGunStateBuild::Client_OnRecipeBuilt_Implementation` (server confirms
    the build, with `numConstructed`): remembered total = existing + built.
  - Levels ↔ zoop Z measured with the hologram's own `ConvertZoopToWorldLocation` and the pole's
    `GetStackHeight()`; existing levels counted from `AFGBuildableSubsystem` (same class,
    same XY column, contiguous by stack height).
  - ATs: `Accessor` `AFGBuildableHologram::mDesiredZoop`; `Friend` `UFGBuildGunStateBuild` →
    `FToolbeltStackHooks` (protected client RPC).
- Unverified: zoop axis is Z for stackables; `GetSnappedBuilding()` is the top of the column;
  `numConstructed` counts all levels (falls back to our own count); `Client_OnRecipeBuilt`
  fires for every stack build; the first zoop frame's raw Z ≈ 0.

## 2. Colour (swatch) inheritance — new placements only, never repaint existing, no chains
- 2a. Pole (all Mk), wall/ceiling outlets, power towers, built by dragging a power line from an
  existing pole/outlet/tower → copy that one's customization. Never from machines/buildings.
  Pole placed alone then wired later → nothing.
- 2b. Belt/lift or pipe whose end connects to an existing belt/lift or pipe → copy it. Through a
  pole that's the belt/pipe on the pole's other side (poles only snap; belts connect to each
  other). Never the pole's colour, never from machines. Since 1.2.0 also from a splitter/merger/
  junction/pump/valve it starts or ends at (a line piece wins when both ends have a source).
- 2c. Stackable pole placed onto an existing stack → copy the stack's colour.
- 2d. Splitter/merger (incl. smart/programmable), pump, valve or pipe junction placed onto a belt
  or pipe → copy that line's colour (prefers a line that existed before; accepts the split halves
  vanilla rebuilds, which carry the original colour). Own toggle.
- 2b also covers hypertubes continued from hypertubes.
- Inheritance beats the vanilla group default swatch; with nothing to inherit, vanilla applies.
- Copies swatch + custom colour slot only (not pattern/material/skin). Skips upgrades and
  blueprint placement. Server-side, using the builder's settings.
- Implementation (piece 2, `ToolbeltSwatch.cpp`): after-hooks on `AFGWireHologram::Construct`
  (2a) and `AFGBuildableHologram::Construct` (2b/2c; belt, lift, pipe and stackable-pole holograms
  don't override it), plus a before/after pair on `AFGBlueprintHologram::Construct` as the
  blueprint guard. Unverified until tested: auto-built pole comes back in `out_children`;
  connections are made before `Construct` returns; `SetCustomizationData_Native` behaviour;
  stack zoop levels come back as children; `GetSnappedBuilding()` is the pole below.

## 3. Zoop by default
- Zoop-capable holograms start in zoop each session. A mode the player picks (e.g. vertical)
  sticks for the rest of the session (vanilla per-session memory untouched).
- Implementation (piece 3, `ToolbeltZoop.cpp`): after-hook on
  `UFGBuildGunStateBuild::TickState_Implementation`, local player only. When a new hologram
  appears and vanilla has picked its mode: if it supports zoop, isn't in zoop, and the player
  hasn't chosen a mode for its memory key this session → `SetCurrentBuildGunMode(zoop)`.
  A mode change while the same hologram stays up counts as the player's choice. Memory key =
  vanilla's grouping (Architecture / Pipe category, else the build class). Reset per world.
  Zoop descriptor via `Accessor` on `AFGBuildableHologram::mBuildModeZoop`.
- Unverified: changing build mode doesn't respawn the hologram (would hide the player's choice);
  `SetCurrentBuildGunMode` from the client reaches the server; one frame in the default mode
  before switching is invisible in practice.

## 4. Ghost belt/pipe preview
- **Extended (additions round):** also while placing a single Conveyor Pole / Pipeline Support.
  "Last pole" is per family (belt: Conveyor Pole + Stackable Conveyor Pole; pipe: Pipeline
  Support + Stackable Pipeline Support), whichever type was built last. Pairing: stack↔stack =
  bottom+top; single↔anything = one ghost to the level nearest in height. Yellow when the routed
  length is over 90% of the limit. Hypertube supports and wall/ceiling mounts: no ghost.
  Single-pole attachment point = a learned offset from a real pole of that class, shifted by the
  height difference (fallback: straight up by the pole height). Unverified: the hologram's
  `GetPoleHeight()` (current mesh variation height) matches where the belt attaches; the height
  step still calls `SetHologramLocationAndRotation`.
- While placing a stackable pole: preview belt (or pipe) from the last stackable pole of the
  same kind the player built, top level and bottom level → two ghosts.
- Only when that pole is within the WorkingArea setting. Distance only, no time limit.
- Uses the game's own belt/pipe hologram and vanilla's checks; shown yellow (never red) near/over the limit. Never a child of the
  pole hologram (children get built and charged). Local to the placing player.
- Belt tier: any tier (preview only) — the ghost uses Mk1 belt / Mk1 pipe; max length is shared
  (`mMaxSplineLength` 5600.1 uu = 56 m in both headers).
- Implementation (piece 5, `ToolbeltGhost.cpp`): driven from feature 1's stackable-hologram
  hooks each frame; last column remembered when the server confirms a stack build.
  Ghosts are spawned with `AFGHologram::SpawnHologramFromRecipe` (child holograms off,
  collision off), routed with the belt/pipe hologram's own `AutoRouteSpline`, refreshed with
  `UpdateSplineComponent`, and coloured via `SetPlacementMaterialState` from vanilla checks:
  length ≤ max, `ValidateMinLength`, belts also `ValidateCurvature` + `ValidateIncline`.
  Connection points come from the real last-built poles' connection components; the same local
  offset is applied to the hologram's levels (`ConvertZoopToWorldLocation`). Removed when the
  build gun leaves build mode or switches hologram (local player only).
  ATs: `Friend` of `AFGConveyorBeltHologram`, `AFGPipelineHologram`, `AFGSplineHologram` →
  `FToolbeltGhost`.
- Unverified: `AutoRouteSpline` space (ghost sits at the world origin, so local == world either way); connection normal convention (start normal toward end, end normal toward start);
  pipe curvature isn't checked (only length/min length); spawning a hologram outside the build
  gun is harmless; the last-built poles have replicated to the client by the next placement.

## 5. Power pole tier when dragging a power line from a building
- Vanilla auto-builds a Mk1 pole (or wall outlet) at the end of a power line started from a
  building; dragging from a pole already copies that pole's type (unchanged).
- Setting picks the tier for the building case: 1–3, default 2. Wall outlets follow the same tier
  (proposed). If that tier isn't unlocked, use the highest unlocked tier below it (Mk2 not unlocked → Mk1). Agreed.
- Mechanism (piece 6, `ToolbeltPoleTier.cpp`): before-hook on `AFGWireHologram::BeginPlay`
  overwrites that instance's `mDefaultPowerPoleRecipe` / `mDefaultPowerPoleWallRecipe`
  (private → Accessors in AccessTransformers.ini), using the builder's setting (client and server
  both, so they agree). Unlock check via `AFGRecipeManager::IsRecipeAvailable`. Tier 1 touches
  nothing. Unverified: the auto-pole is created at/after BeginPlay; the construction instigator
  is set before BeginPlay (falls back to the actor instigator). Unverified: that vanilla's copy-from-source-pole logic still wins when
  dragging from a pole.
- Recipes (Starter Project paths): `/Game/FactoryGame/Recipes/Buildings/Recipe_PowerPoleMk{1,2,3}`,
  `Recipe_PowerPoleWall`, `Recipe_PowerPoleWallMk{2,3}`.

## Test round 1 feedback → changes (→ v1.2.0)
- **Top ghost ran toward the world origin** (always the same direction): `ConvertZoopToWorldLocation`
  was used as a world position; top = base + (Convert(Z) − Convert(0)) now.
- **Height control redesigned**: three independent toggles — `RememberStackHeight` (start at last
  built total), `ScrollToHeight` (wheel ±1 level in the zoop step; hooks `AFGHologram::Scroll`
  and `AFGBuildableHologram::ScrollRotate`, cancelled only for stack holograms in the zoop step,
  one step per frame), `AimToHeight` (default **off**; relative to the start). Height = start +
  aim change (if on) + scroll offset, clamped to vanilla's limit. Remember off + scroll off + aim on
  = untouched vanilla.
- **Remembered height is forgotten** when its stack no longer exists; it doesn't apply beyond the
  new shared **Working area** setting (replaces the ghost range; default 75 m) — starts at 1 then.
- **Ghost colours**: no red. Yellow from 90% to 120% of the length limit (or when vanilla would
  refuse the shape); normal otherwise.
- **Belts/pipes inherit from splitters/mergers/junctions/pumps/valves** (a line piece is preferred
  when both ends have a source). Machines still never.
- **Keep colour on upgrade** (new toggle, all upgrades): capture the old piece's swatch before
  `Construct` (base, power pole and wall power pole holograms), apply after; inheritance is
  skipped for that construct.
- **Settings UI**: section `WidgetType = CPS_Vertical` (one row per setting); number fields
  `CPF_/CPI_Spinbox` with real `MinValue/MaxValue` (0/0 made every value "invalid" = red outline).
  Classes `UCP_Section/UCP_Float/UCP_Integer` (SML `WidgetExtension/`), parents of the BP classes.
- Confirmed working in test 1: all hooks install; zoop default (foundations; belts don't zoop in
  vanilla); vertical choice sticks; power pole/tower colours; belt/pipe colours incl. modded
  PioneerPipelines pipes/crosses and modded mergers; colour from coloured machines stays default.
- User's mod list includes HigherConveyorPoles and VanillaUpgrades — retest stacks/upgrades with
  them in mind (no shared native hooks seen in the log).

## Console commands (additions round)
`Toolbelt.Status` (settings + remembered heights + ghost/zoop state), `Toolbelt.Set <Key> <Value>`
(saved like the settings screen), `Toolbelt.Reset`. Also the fallback if the settings screen
doesn't draw. File: `ToolbeltConsole.cpp`.

## Backlog (discussed, not scheduled)
- **Line upgrade** (belts; pipes too): upgrade the whole connected line in one go. Scope as
  decided: conveyor **belts only** (no lifts, never across belt↔lift), never through splitters/
  mergers/anything else; pipes the same (pipe↔pipe only). Skip pieces already ≥ target tier.
  Idea: extra segments as child upgrade holograms so vanilla's HUD shows/charges the total.
  Open: how it's switched on (build mode vs setting vs fixed key). Bulk Upgrade / Mass Upgrade
  were found broken on 1.2.
- **Belts that randomly don't connect**: diagnose first (read-only `Toolbelt.CheckBelts`:
  free belt ends overlapping another free, opposite-facing end), auto-fix only if that finds
  real cases. Shelved — may be ends snapped to a pole/ground rather than a bug.
- **Taller limits** (what HigherConveyorPoles used to give; target 2–2.5× vanilla): per-player
  "Height limit multiplier". Stackable poles (conveyor/pipe/hypertube) via the public
  `AFGBuildableHologram::SetMaxZoopAmount`; conveyor lifts via `Friend` on the private
  `AFGConveyorLiftHologram::mMaximumHeight`; server applies the builder's value (RCO prefs).
  Single Conveyor Pole / Pipeline Support would need stretched vanilla meshes — probably skip.
  Open: default/max multiplier, stretched single poles yes/no, lifts confirmed in scope.

## Decided against
Remembering stackable pole rotation; auto-disabling zoop features when a conflicting zoop mod
is installed.

## Known / untested mod overlaps
Infinite Zoop, Oops! All Zoops, Blueprint Zooper (zoop); CustomSwatches (likely conflict —
category auto-swatches); Universal Swatch Slots (used alongside during development; expected fine);
Automatic Pipeline Color (own pipe class; expected fine); Construction Preferences (default pole tiers, if still maintained). All untested.

## Build order
Write all pieces, review, then test once at the end.
1. Skeleton + settings + RCO ✅ written
2. Colour inheritance ✅ written
3. Zoop default ✅ written
4. Remembered stack height ✅ written
5. Ghost preview ✅ written
6. Power pole tier from buildings ✅ written

All pieces written → reviewed (below) → build and test.

## Review (whole-mod pass) — changes made
- **Unity builds**: `bUseUnity = false` in Build.cs. Several .cpp files had same-named
  helpers in anonymous namespaces (`GSession`, `XYTolerance`), which collide when UBT merges
  files. Globals also renamed (`GZoopSession`, `GStackSession`, `GGhostSession`, …).
- **Multiplayer latency (stack height + ghost)**: what gets remembered is now frozen at the build
  click (after-hook `AFGBuildableHologram::DoMultiStepPlacement` returning true) and committed when
  the server confirms. Before, frames during the RPC round-trip could overwrite it with the *next*
  hologram's state (wrong height, ghost anchored at the cursor).
- **Zoop default**: waits 2 ticks for vanilla to settle the new hologram's mode before deciding,
  so a vanilla mode applied one tick late can't undo it or be mistaken for the player's choice.
- **Stack height**: never exceeds the hologram's own `GetMaxZoopAmount()`; if it reports none, the
  feature doesn't steer (previously a 200 fallback could exceed vanilla's limit).
- **Ghost performance**: `AFGBuildableSubsystem::GetTypedBuildable` walks every buildable in the
  world; the last column's and the snapped column's poles are now cached (re-resolve at most
  every 0.5 s while not found) instead of scanned every frame. **1.2.2:** column lookups use
  `TActorIterator` over the pole class instead, so each lookup only visits poles of that kind.
- uplugin: `CreatedBy` set, description lists all features.

## Review — checked, no change needed
- `UFGBuildGunState` overrides `GetWorld()` (FGBuildGun.h), so world/config lookups via the build
  state work.
- Stackable/factory-building holograms don't override `DoMultiStepPlacement`; the base hook covers them.
- Every game/SML `#include` resolves at `v3.12.0`; no UObject pointer UPROPERTYs need `TObjectPtr`
  (statics use weak pointers).
- Features with settings off do nothing; dedicated servers run only the server-side parts
  (swatch, pole tier, RCO) — client hooks bail on non-local players.
- Swatch can never repaint an existing actor: only `Ret`/`out_children` are written.
- Pole tier sets the same recipe on client and server from the same per-player value.

## Review 2 (independent reviewer, findings verified against headers) — changes made
- **Stack height + ghost on existing stacks**: snapping onto a stack replaces
  `SetHologramLocationAndRotation` for that frame (FGHologram.h: "Will only be called if we have
  a valid hit result and did not snap"). Added an after-hook on `TrySnapToActor` (when it snapped)
  and re-arm after each build click; the placement handler ignores calls during the zoop step
  (`Accessor` `mBuildableHologramBuildStep`).
- **Stack could never be built shorter than remembered**: vanilla's raw zoop Z can't go below the
  first frame's value. Adjustment now uses the aim ray's height on the column (closest point
  between the camera ray and the vertical line through the base) → levels up *and* down.
  Falls back to raw Z if the trace data is unusable.
- **Ghost position**: spline data is actor-local (FGSplineHologram.h), so the old "place at start"
  hedge was wrong (would draw at ~2×start if local). Ghost now sits at the world origin, where
  local == world. Also explicitly un-hidden after spawn.
- **Remote players before their settings arrive**: server now treats them as vanilla (no
  inheritance, tier 1) instead of "all on, Mk2"; sync polls every 0.5 s (first after 0.1 s).
- Ghost `EndState` cleanup now local-player only (listen host flicker); recipe classes cached
  instead of `LoadClass` per frame / per wire; ghost re-resolves the last column for 5 s after a
  build (levels may replicate late); quick repeated stack builds use a small FIFO of clicks.
- Swatch server check is `GetNetMode() != NM_Client` (a client-spawned hologram has authority
  on that client); zoop default also respects vanilla's own remembered mode
  (`GetLastBuildModeForCategory`) when it's neither zoop nor default; `class UWorld;` forward decl.

## Review — remaining risks (test these first)
Status for 1.2.2: the single-player items below behaved in the author's single-player test of
1.2.2; the multiplayer and dedicated-server items are untested.

- **`TickState` / `EndState` are BlueprintNativeEvents.** If the game's build-state Blueprint
  overrides them without calling the parent, the `_Implementation` hooks never fire: zoop
  default and ghost cleanup would silently do nothing. First test: Verbose log
  "Zoop default applied".
- Ghost validation (`ValidateIncline`/`ValidateCurvature`) with child holograms off might touch
  null child/snap pointers — first ghost test under a debugger or with `-log`.
- Pole tier: whether the server's rebuilt hologram takes the auto-pole recipe from the client's
  serialized data or from its own BeginPlay override (test remote client tier 1 vs host tier 3).
- Stackable hypertube supports (if they use `AFGStackablePoleHologram`) get height memory and
  stack colour inheritance — intended; no ghost for them.
- Clients have the replicated pole actors (stack counting, ghost columns iterate the actors present on the client).
- `AFGRecipeManager::IsRecipeAvailable` agrees on client and server (otherwise preview Mk1 vs built Mk2).
- Remote players are treated as vanilla until their first sync (~0.1–0.5 s after their RCO exists).
- Everything listed as "Unverified" in the sections above.
