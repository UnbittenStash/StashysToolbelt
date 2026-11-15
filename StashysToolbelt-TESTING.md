# Stashy's Toolbelt — in-game test procedure (v1.2.2)

**Status of 1.2.2** (Satisfactory 1.2.5.0, CL 517554, Windows client): passed the author's
single-player test (§1–7 and the single-player items of §9). **Not tested:** §8 — multiplayer
(host + clients) and dedicated servers. The boxes below are left unticked so the list can be
reused for the next version.

About 30–45 min single player, plus 15 min multiplayer. Tick each box; for any ✗, note what you
saw and grab the log lines (see 0.3).

## 0. Setup (once)

- [ ] **0.1 Launch options** (Steam → Satisfactory → Properties → Launch Options):
  `%command% -log -LogCmds="LogStashysToolbelt Verbose"`
  (Verbose shows the per-action lines used below.)
- [ ] **0.2 Test save:** a **new creative-ish save** is easiest — Advanced Game Settings → No Build
  Cost + all tiers/MAM unlocked. Keep one *normal* save too for 6.3 (pole tier unlock fallback).
- [ ] **0.3 Log file:** Windows `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log`;
  Linux (Proton) `…/steamapps/compatdata/526870/pfx/drive_c/users/steamuser/AppData/Local/FactoryGame/Saved/Logs/FactoryGame.log`.
  Watch it live (Linux): `tail -F "<that path>" | grep -i --line-buffered "toolbelt\|LogSML.*error\|Fatal"`

## 1. Loads cleanly (2 min)

- [ ] Game reaches the main menu (no SML "game version" / missing-dependency error).
- [ ] Log contains, once each:
  `Settings tree built (12 entries)` · `Swatch inheritance hooks installed` · `Zoop default hook installed` ·
  `Stack height hooks installed` · `Ghost preview hooks installed` · `Power pole tier hook installed`
- [ ] No `Could not load /SML/...BP_ConfigProperty...` warning (if present: settings UI will be missing → use console, 2.3).

## 2. Settings (3 min)

- [ ] **2.1** Main menu → Mods → Stashy's Toolbelt: 12 options, **one per row**, tooltips; the two number fields are spin boxes **without** a red outline (range 0–1000 m, 1–3).
- [ ] **2.2** Toggle one off, restart game → still off (file `FactoryGame/Configs/StashysToolbelt.cfg` exists).
- [ ] **2.3** In a save, open console (`` ` `` / `~`): `Toolbelt.Status` prints the settings and state.
  `Toolbelt.Set ZoopByDefault off` → Status shows `off`; `Toolbelt.Reset` → back to defaults.
- [ ] **2.4** `Toolbelt.Set PowerPoleTierFromBuilding 7` → prints `= 3 (clamped …)`; settings screen shows 3 with no red outline. Same for `WorkingArea 5000` → 1000.
- Leave everything **on** (defaults) for the tests below.

## 3. Zoop by default (3 min)  — **test this first in a save**

- [ ] **3.1** Fresh load, pick Foundation → mode is **Zoop** immediately. Log (Verbose): `Zoop default applied`.
  ✗ *If not, and no log line:* the build gun's tick hook isn't firing (biggest known risk) — report it; it also affects ghost cleanup.
- [ ] **3.2** Switch Foundation to **Vertical**; build one; pick Wall → still not zoop (same "Architecture" group); pick Foundation again → Vertical.
- [ ] **3.3** Pick something with zoop in another group (e.g. stackable pole) → Zoop.
- [ ] **3.4** Save & reload → Foundation is Zoop again (memory is per session).
- [ ] **3.5** `Toolbelt.Set ZoopByDefault off`, reload → vanilla default mode. Turn back on.

## 4. Stackable pole height (5 min) — defaults: remember on, scroll on, aim off

Stackable **Conveyor** Pole, zoop mode:
- [ ] **4.1** Place base, **scroll** up to **5** (aim doesn't change it), build. Log: `Remembered stack height 5`.
- [ ] **4.2** Next one: after the first click it **starts at 5**; looking up/down does nothing; scroll down → 4, 3.
- [ ] **4.3** Build a **3**-high; next starts at 3 (cancel one at 8 with right-click → next still 3).
- [ ] **4.4** Onto existing stack: build 2 on the ground, then on top of it → starts so the **total is 3** (adds 1).
- [ ] **4.5** Onto a stack already ≥ remembered → starts at 1 new level; scroll still works.
- [ ] **4.6** Dismantle every stack of that kind → next one starts at **1**.
- [ ] **4.7** Walk beyond the working area (75 m) from the last stack → starts at 1; come back → remembered again.
- [ ] **4.8** Pipeline / hypertube stackable supports remember separately.
- [ ] **4.9** `Toolbelt.Set AimToHeight on` → looking up/down also changes height (relative to the start), scroll still adds/removes.
- [ ] **4.10** `RememberStackHeight off`, `ScrollToHeight off`, `AimToHeight on` → plain vanilla. Reset after.
- [ ] **4.11** Never exceeds vanilla's max stack height. In the base-placement step, scroll still rotates as vanilla.

## 5. Ghost belt/pipe preview (7 min)

- [ ] **5.1** Build a stackable conveyor pole 3 high. Walk ~20 m away, place another → ghost belts
  **bottom↔bottom and top↔top**, normal colour, both sitting exactly on the poles' belt points
  (the top one used to run off toward the map origin).
  ✗ *Ghost far away / at map origin / wrong height:* note where — routing-space or offset issue.
  ✗ *No ghost at all:* note whether the log shows errors; try `Toolbelt.Status` ("Ghost Belt: last pole …").
- [ ] **5.2** Move away: **yellow** from ~50 m (90% of 56 m) until ~67 m (120%), then normal colour again. Never red.
- [ ] **5.3** Beyond the working area (75 m) → ghost disappears. Right next to the last pole → none.
- [ ] **5.4** Single **Conveyor Pole**: ghost from the last pole; adjust its height → ghost end follows the attach point.
- [ ] **5.5** Mixed: last = stack, now single → **one** ghost to the stack level nearest in height.
- [ ] **5.6** Pipes: same with Pipeline Support / stackable support → ghost **pipe**.
- [ ] **5.7** Cleanup: switch to another building, leave build mode, open the menu → ghosts vanish.
- [ ] **5.8** The ghost is never built: build the pole → only the pole is built and charged (check inventory in a normal save).
- [ ] **5.9** Building a belt that auto-places a pole at its end → **no** ghost during that.

## 6. Power: colour + pole tier (6 min)

Paint a **Mk1 pole red** first (Customizer).
- [ ] **6.1** Drag a power line from the red pole into empty space → auto-built pole is **red**.
- [ ] **6.2** Drag a power line from a **machine** into empty space → auto pole is **Mk2** and default colour.
- [ ] **6.3** Normal save without Mk2 unlocked → **Mk1** (fallback). `Toolbelt.Set PowerPoleTierFromBuilding 3` with Mk3 unlocked → Mk3; set `1` → vanilla.
- [ ] **6.4** Drag from an existing **Mk1** pole into empty space → still copies **Mk1** (vanilla copy behaviour kept).
- [ ] **6.5** Drag from a machine onto a **wall** → wall outlet tier follows the setting.
- [ ] **6.6** Wire two **existing** poles of different colours → neither changes colour.
- [ ] **6.7** Red **tower** → pole built from it is red; red pole → wall outlet from it is red.

## 7. Belts, pipes, attachments, stacks: colour (6 min)

Paint an existing belt **blue** and a pipe **green**.
- [ ] **7.1** Start a new belt from the blue belt's end → new belt **blue**. Same for a lift.
- [ ] **7.2** Belt ends on a pole; start a new belt from that pole → **blue** (inherits through the pole, never the pole's colour).
- [ ] **7.3** Belt from a **machine output** → default colour (no inheritance from machines).
- [ ] **7.4** Pipe from the green pipe → **green**. Hypertube from a painted hypertube → same colour.
- [ ] **7.5** Splitter / merger / smart splitter placed **onto** the blue belt → **blue**; pump / valve / junction onto the green pipe → **green**.
- [ ] **7.6** Painted stackable pole stack; place a stackable pole **onto** it (zoop several) → **all new levels** take the stack's colour.
- [ ] **7.7** **Upgrade** a blue Mk1 belt to Mk3 → stays blue (log: `Upgrade kept swatch`); also a painted pipe, lift and power pole. `KeepColourOnUpgrade off` → vanilla.
- [ ] **7.7b** New belt/pipe started **from** a coloured splitter/merger/junction/pump/valve → takes its colour.
- [ ] **7.8** Place a **blueprint** containing coloured belts next to a differently coloured belt → blueprint keeps its own colours.
- [ ] **7.9** Each toggle off (`Toolbelt.Set InheritBeltPipeSwatch off`, etc.) → that case reverts to vanilla.

## 8. Multiplayer (15 min) — host + one client, then ideally a dedicated server

- [ ] **8.1** Client joins; log on host/server: `Received settings from …` within ~1 s.
- [ ] **8.2** Client: repeat 3.1, 4.1–4.2, 5.1, 6.1–6.2, 7.1 — all work for the client.
- [ ] **8.3** Per player: client sets `Toolbelt.Set InheritBeltPipeSwatch off` → client's belts don't inherit, host's still do.
- [ ] **8.4** Client sets pole tier **1**, host **3** → client's auto pole is **Mk1** in preview *and* when built (no Mk1-preview/Mk2-built mismatch).
- [ ] **8.5** Client builds a stack quickly twice → remembered height is the second one; ghost anchors on the real last stack (not where the client was aiming).
- [ ] **8.6** Host's ghosts don't flicker when the client enters/leaves build mode.
- [ ] **8.7** Dedicated server: loads with the mod (log lines from §1 on the server), no crash; colour + tier work for a connected client.
- [ ] **8.8** Client does **not** see the host's ghost belts/pipes (ghosts are holograms, which replicate; vanilla's relevancy should keep them to the host).
- [ ] **8.9** Client with `PowerPoleTierFromBuilding 3` and Mk3 unlocked: preview and built pole agree (server reads the client's setting from the RCO).

## 9. Sanity

- [ ] No hitch when placing poles in a large factory — especially sweeping the cursor across a field of existing stacks (each new stack hovered does a column lookup).
- [ ] 10+ minutes of normal building, no crash. Save/reload OK.
- [ ] Optional: with Universal Swatch Slots installed — colours still inherit correctly.
- [ ] With other build-gun mods you use (e.g. HigherConveyorPoles, VanillaUpgrades): 4.x stack height and 7.7 upgrades still behave.
- [ ] Pipes on 1.2.5.0: vanilla's pipe hologram now applies customization itself (`ApplyCustomizationData`, new in CL 517554) — re-check 7.4 / 7.7b, and that a new pipe from a plain pipe still gets vanilla's default.

## Reporting a failure

Test number · what you did · what happened vs. expected · single player / host / client / dedicated ·
the `LogStashysToolbelt` lines and any `Error`/`Fatal` lines around it · `Toolbelt.Status` output.
Run the log through `redact_log.py` (repository root) before sharing it; it removes player names,
IDs and IP addresses.
