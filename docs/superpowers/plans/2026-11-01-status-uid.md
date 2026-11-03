# Status UID Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Intended implementer: Strata (local coding model), working sequentially. This is a plan for later review, not permission to execute now.

**Goal:** Display the player's actual identity and resource pools in a bottom-center HUD, and other characters' own identity, resources, relationship, level and true death state overhead, implementing only locked decisions and stopping dependent work at explicit gates.

**Architecture:** Read existing `OpenCharacter` getters into disposable presentation snapshots; share a resource-bar control between the player and world views. Scene-owned controls live below existing popups and never consume pointer input. Exact partial-cover visibility is a blocking renderer/engineering gate, not a raycast approximation; the world overlay must not ship until that contract is approved and verified with actual rendering.

**Tech Stack:** Godot 4.7.2, GDScript SceneTree tests, existing GDExtension / godot-cpp `godot-4.5-stable`, C++20 `open_sim`, CTest. No new gameplay API, simulation rule or dependency is planned.

**Spec:** Live `AGENTS.md`; **Locked text only** in `docs/status_UID.md`; `docs/style.md` for existing visual/interaction constraints; `docs/combat_log.md` for the separate action chat. `docs/superpowers/plans/2026-10-31-combat-log.md` supplies the task/verification structure, not additional status requirements. Current paragraphs below are observations, not new authority.

## Global Constraints

- Planning creates this file only. No implementation, build, import, tests, commits, staging, stash/reset or access to `.superpowers/` during planning. No implementation or test result is claimed here.
- On later authorized execution preserve all pre-existing work, especially dirty `AGENTS.md`, `docs/README.md`, `scripts/main.gd`, untracked `docs/status_UID.md`, `scripts/tests/player_name_test.gd` and `.superpowers/brainstorm/`. Never access `.superpowers/`. Inspect only scoped file diffs; do not stage, commit, reset, stash or clean. Do not require an `AGENTS.md` edit. Work sequentially in this tree.
- Execution may implement **only Locked decisions**. Open and Proposed entries are not permission. At a gate stop the dependent task, report the missing decision, and continue an independent task only where safe. Do not turn a provisional fixture/style into a production decision. A partial delivery is not acceptance of the whole feature.
- C++ owns pools, maxima, identity, faction, level, death, costs and regeneration. GDScript owns presentation only; ephemeral snapshots are not independently editable state. Never tick regeneration, step combat, spend resources, damage actors or drain events from a status control.
- Bridge already exposes `is_configured`, `get_character_name`, `get_level`, `get_faction`, `is_alive`, `get_health` / `get_max_health`, `get_stamina` / `get_max_stamina`, `get_mana` / `get_max_mana`. Reuse these exact APIs. No bridge changes or UI-only simulation setters.
- Main HP is not head/torso/limb HP. Never infer death from the HP value, formatted text, fraction, movement or animation. Do not zero pools on death. The skull is for world characters; no player skull/death overlay was locked.
- HP / stamina / mana order and muted red / green / blue roles are locked in both views. Each fill uses its own current maximum and updates without a delayed drain/refill animation. Player values use nearest hundredth and exactly two decimals; world bars have neither resource labels nor numbers.
- The 50-Godot-unit camera-to-character maximum is locked for now, revisitable after playtesting; neither player distance nor horizontal distance is a substitute. Exact point and equality/boundary rules need approval (G2).
- Exact partial cover: **show if ANY PART of the character can be seen; hide only when COMPLETELY behind a solid obstacle**, subject to the other agreed visibility conditions. No single head/center ray, finite fixed ray samples, bounding-box visibility, frustum check or `VisibleOnScreenNotifier3D` alone proves this.
- Status ignores pointer input throughout its control tree and is not included in `_popup_covers`. Existing popups retain priority and input isolation; chat retains node-name wording, eight-entry history and its single event source through `CombatFeedback`. No status event consumer or new log entries.
- Use the current rustic/medieval direction; do not silently impose popup opacity/chrome on a status surface. Exact status sizes/shades/alignment and badge/skull styling are Open and gated. No new camera mode, lore, recruiting or gameplay system.
- Never commit `cpp/build/`, `extension/bin/`, `.godot/` or `*.so`. A later C++/extension change, if separately justified and authorized, requires failing simulation tests first, rebuild and fresh import; it is outside the expected scope here.

## Review Focus

1. A thin visible sliver can fall between every physics ray: Task 5 requires a real rendered visibility fixture, not a headless mock pass.
2. Dead head/torso with positive main HP and living tiny-positive HP must remain distinct: Tasks 1, 3 and 6 exercise real sheets.
3. A click over a status surface may hit floor, character or an open popup: Task 6 pushes actual viewport input and checks the resulting order/popup, not only mouse flags.
4. Window shrink/restore and populated chat may cause overlap or stale projection: Tasks 2, 4 and 6 test real resized viewports, while exact small-window policy remains gated.
5. Freed/replaced actor, repeated binding and scene reentry must not retain duplicate UI or stale identity: Tasks 3, 4 and 6 test ownership and teardown without inventing spawn/despawn gameplay.

---

## Live Baseline Inspected — Not Verification Results

- `scripts/main.gd::_ready` builds world, player, two dummies, combat, benches, camera, marker and HUD, in that order. `_physics_process` priority `-10` publishes positions and steps the only coordinator. `_process` follows the player with camera offset `(0, 9, 12)`; status projection must observe the resulting camera, not a stale position from before that callback.
- Player already has scene node name `Player` and sheet name `John Doe` (`_build_player`, lines 381–390). `scripts/tests/player_name_test.gd` exists and checks that real sheet name. Preserve it unchanged; do not plan another rename.
- `StandingDummy.setup(faction, level, id, seed, name = "")` configures its sheet and disables walking. Production `Friendly` and `Enemy` calls omit the name, so **both actual sheet names are empty**. Scene node names are not automatically status names.
- `scripts/standing_dummy.gd` currently creates a colored `Label3D` at local Y `2.15`, containing faction, level, rounded main HP and strength, and refreshes it every physics callback. This is duplicate/outdated output once the new overlay is integrated. `scripts/tests/character_visuals_test.gd:189–201` explicitly requires that label and HP text updates. Replace those assertions with equally strong current-sheet/faction/resource checks during the authorized migration, not by leaving two displays or deleting meaningful coverage.
- `extension/character_bridge.cpp:136–198, 273–395` binds and reads all status data, including authoritative death. Existing `apply_damage(part_id, amount)`, `power_up()` and `regenerate(seconds, in_combat)` are available for legitimate fixtures; none needs a new setter. `configure` / `configure_npc` are one-time configuration, not rename/relevel methods.
- Real head death: `apply_damage(0, 25.0)` on a fresh level-one sheet leaves main HP `75`; torso death: `apply_damage(1, 50.0)` leaves main HP `50`. Existing sheet/visual tests demonstrate these rules. `cpp/src/vitals.cpp` charges main HP and the named part independently and uses all three vital conditions for life.
- `_build_hud` creates an existing `CanvasLayer`, hint, chat, then six popups. Later siblings draw above chat. `_popup_covers` checks only the popups. Keep these semantics; add status before popups rather than adding another high-priority layer.
- `CombatLog` anchors across `0.42` of viewport width with 16-pixel margins, caps its height, wraps entries and ignores input. At small widths a centered HUD cannot simply assume the remaining space fits. Its text deliberately uses `Player` / `Friendly` / `Enemy`, not sheet names.
- `project.godot` uses 1280×720, canvas-items stretch, Forward Plus and 2× 3D MSAA. Existing log tests use an owned `SubViewport` for genuine resize because changing a headless root Window size alone is not reliable.
- Existing production geometry: 40-unit floor, four solid mesh/collision blocks, and benches with solid-looking meshes but pick `Area3D`s. Collision layer 1 is floor/world, 2 character bodies, 4 pick areas; collision membership alone is not a renderer solidity contract. Female models contain body and hair instances; character visuals/animation live under `Visual`. Any visibility contract must account for the actual animated render surfaces and self-collision.
- Scoped `assets/**/*skull*` search found no skull asset; scoped SVG search also found none. This does not establish that no usable artwork exists under another name. No arbitrary Unicode skull is guaranteed by the system serif fallback. G4 gates the choice.
- Fifteen current `scripts/tests/*_test.gd` scripts were discovered, including the user's player-name test. Recount during execution. Existing tests show guarded dynamic loading, failure/check counts, real input via `Viewport.push_input`, and real coordinator fixtures; follow those patterns.
- `docs/README.md` already links `status_UID.md`; no index edit is currently necessary. `AGENTS.md` says not to build merely Locked work: a later explicit implementation authorization is still required despite the present planning brief.

## Decision Gates — Visible Blockers, Not Defaults

Record an actual human decision in the spec (if later authorized) and amend this plan before implementing the dependent production behavior. Do not mark a gate complete based on this plan's candidate approach.

| Gate | Required decision / evidence | Blocks | Safe work meanwhile |
|---|---|---|---|
| G1 Exact cover visibility | Engineering/design approval of an exact realizable visibility contract for animated production characters and solid obstacles on Godot 4.7.2, with rendered evidence below | Task 5 and shipping world overlay in Tasks 4/6 | Snapshot, pool view, explicit-name world component tests, player-only work subject to its gates |
| G2 Distance/boundaries | Camera point versus which character point; Euclidean world-space distance; inclusion at exactly 50; behind-camera, screen-edge and near-plane behavior. Keep 50 revisitable, not a new gameplay constant elsewhere | Production distance filter and boundary/offscreen integration | Test mathematical distances 49 and 51 between explicit supplied points; no claim these choose the production measurement point |
| G3 Names/relationships | Exact empty-name fallback (or actual authored NPC sheet names), long-name policy, diamond side/shades, presentation of any other relationship | Production dummies' identity integration, legacy-label removal and associated test migration | Real explicitly named NPC fixtures, existing two faction codes without inventing a third; player `John Doe` |
| G4 Skull | Locate and approve a licensed asset, or approve an authored vector/procedural skull rendering strategy, its arrangement beside the diamond and styling; verify at target sizes on the renderer | Production death icon rendering and complete world acceptance | Test authoritative dead boolean, retain positive HP and resources, prepare a dead-icon slot in component fixtures only |
| G5 Status visual/layout | Exact dimensions/alignment/shades, empty/full appearance, player rounding ties, world head anchor, level-badge styling, player layout beside populated chat, long names, scale and small-window/overlap policy | Final player/world appearance, production layout and corresponding pixel/geometry assertions | Shared valid-data arithmetic, no-tie formatter cases, click-through control contract; explicitly provisional fixture layout |
| G6 Data/lifecycle behavior | Unavailable/unconfigured/invalid data, zero maximum, maximum-change policy and timing, appearance/removal with actors, eventual corpse removal, crowded displays | Unsupported input policies and dynamic production lifecycle beyond ownership safety | Fixed current valid roster, explicit unbind/free safety, immediate refresh from valid data; do not invent fade, last-known values or spawn system |

**Gate outcomes recorded 2026-11-01 (all by the human, full text in
`docs/status_UID.md` as "Locked 2026-11-01 (human)"): G2 resolved** (camera →
feet origin, true 3D Euclidean, ≤ 50 inclusive, hidden behind camera, no
screen-edge rule). **G3 resolved** (sheet name else scene node name; truncate
>20 to 19+`…`; diamond left of name cloning the bar shades; two-state only).
**G4 resolved** (authored `assets/ui/status_skull.svg`, trailing `◇ Name 💀`,
parchment cap-height). **G5 resolved** (solid plaque + double brass frame; deep
beveled palette; pixel set; half-up ties; ~2.3 anchor; plate badge; overlap
allowed). **G6 resolved** (hide invalid; zero max empty+truthful; live reread
each drawn frame post-camera; lifetime = actor node; overlap crowds). **G1
process resolved**: human chose *spike → rendered evidence → ratify-or-revise*;
the exact-cover lock stays as written; world overlay ships only after
ratification; the spike is throwaway and needs no ownership amendment.

**G1 engineering decision details:** No verified exact renderer contract was established during planning. Therefore Task 5 is explicitly blocked, not an instruction to guess a convenient algorithm. A candidate to investigate *after separate authorization* is an actor-ID/depth visibility pass covering every rendered character fragment against the agreed solid-scene depth, with an any-visible-fragment reduction per actor and overlay composition from that result. It must share the real camera, skinned pose, projection, viewport/MSAA coverage, relevant material discard/opacity, depth and transform state; exclude the overlay, pick areas and self-blocking collision proxies. A low-resolution mask or simplified capsule is not exact. Godot's built-in occlusion culling can be conservative and is not an acceptance oracle for this lock. No renderer API or synchronous readback capability is asserted here.

Before unblocking, demonstrate the Godot integration API, supported rendering method, pass ordering, coverage semantics, identity attachment, occluder classification, and latency/performance on production models. Decide whether visible rendered coverage is the agreed meaning of “any part” at subpixel/MSAA boundaries; if the lock is geometric rather than raster visibility, a finite image test does not prove it. If no workable exact contract exists, obtain a deliberate revised design from the human; **do not silently weaken the current lock**. Renderer extension/native changes, shaders or project renderer configuration need an amended file ownership/test plan first; they are not authorized by the candidate paragraph.

G2 must also settle uncertain obstacle semantics discovered by G1 (opaque visual mesh without collider, invisible collider, transparent/non-solid surface, other characters/equipment as cover). Do not infer “solid” from a pick mask or make the whole floor an unconditional blocker.

**Technical defaults independent of those choices:** Use plain `Label` text, deterministic named child nodes, recursively ignored pointer filters, idempotent construction/binding, scene ownership and safe object-validity checks. Refresh valid-sheet presentation once per drawn frame after world/camera updates, with an explicit `refresh()` seam for deterministic tests; it never runs simulation. These are proposed execution mechanics, not decisions about invalid-data display, smoothing, removal, names, visibility or layout. Reading the live maximum on every refresh supports the locked current-maximum ratio without deciding recalculation game rules or the Open transition policy.

## File Ownership and Sequential Dependencies

Names below are new presentation APIs, not claims that they exist today. Do not modify files outside the active row. Dependencies may be satisfied by a gated component in a test viewport, but never by a placeholder shipped as complete.

| Task | Depends on | Exact files owned |
|---|---|---|
| 0. Approval/preflight | later explicit execution authorization | This plan; `docs/status_UID.md` only for human-approved decisions, if authorized |
| 1. Valid-sheet snapshot / formatting | 0, tie behavior remains G5 | Create `scripts/status_presenter.gd`, `scripts/tests/status_presenter_test.gd` |
| 2. Shared bar and player component | 1; production visual/layout requires G5 | Create `scripts/status_bar.gd`, `scripts/player_status.gd`, `scripts/tests/player_status_test.gd` |
| 3. World component | 1/2; production identity/style/icon requires G3/G4/G5 | Create `scripts/world_status.gd`, `scripts/tests/world_status_test.gd`; create `assets/ui/status_skull.svg` **only if this exact authored-vector strategy is approved in G4** |
| 4. Scene ownership / player wiring | 2/3; production player layout requires G5 | Create `scripts/status_overlay.gd`, `scripts/tests/status_integration_test.gd`; modify `scripts/main.gd::_build_hud` and player-status binding only; world ownership exercised in isolated fixtures |
| 5. Exact visibility/distance | G1/G2, 3/4 | Create `scripts/tests/status_visibility_render_test.gd` after approved contract; implementation files **must first be enumerated in an approved amendment**, not guessed now |
| 6. Final world wiring / acceptance | 4/5, all relevant gates | `scripts/main.gd` world-status binding only, `scripts/standing_dummy.gd` legacy-label methods/call sites only, `scripts/tests/character_visuals_test.gd` legacy-label assertions only; `scripts/tests/status_integration_test.gd`, `scripts/tests/world_status_test.gd`, `scripts/tests/player_status_test.gd`, `scripts/tests/status_visibility_render_test.gd`; fixes only in already owned presentation files |
| 7. Evidence/current-state handoff | implemented subset verified; whole-feature acceptance requires all gates | `docs/status_UID.md` Current only; `docs/README.md` only if a real stale index entry is found; this plan's evidence/checklist |

`scripts/player.gd`, `scripts/combat_log.gd`, `scripts/combat_feedback.gd`, `extension/character_bridge.cpp`, `cpp/`, `project.godot`, model scenes and `scripts/tests/player_name_test.gd` are read-only for this plan's implementation. No production geometry is added merely to make authored cover fixtures pass. If a verified contract needs additional files, stop and amend ownership with approval.

## Task 0 — Obtain Authorization and Resolve Only Necessary Gates

**Consumes:** this brief/plan and live design locks. **Produces:** explicit execution scope and recorded gate outcomes, not code.

- [x] Confirm later implementation authorization; re-read the live docs and named files. Capture scoped baseline diffs for allowed files and note the known user-owned paths without accessing `.superpowers/`. If the requested plan path now has user changes, preserve them.
- [x] Present G1–G6 where they block the next task. An unanswered gate remains visibly blocked. Do not add a fallback name, choose exact-50 inclusion, mandate a skull emoji or claim exact cover from sampled rays.
- [x] Track this plan's checkboxes as the execution checklist. Do not create another tracking file. Keep independent valid-input work separate from blocked production integration, so an incomplete world feature cannot accidentally replace the current label.

## Task 1 — Snapshot and Format Valid Sheets Without Owning State

**Interfaces (`scripts/status_presenter.gd`, static helper, no node/clock):**

- `read_sheet(sheet: OpenCharacter) -> Dictionary`, precondition: configured sheet with valid finite values and positive maxima. Return exactly `character_id: int`, `character_name: String`, `level: int`, `faction: int`, `alive: bool`, and `hp`, `stamina`, `mana`, each a dictionary `{current: float, maximum: float}`. These are freshly read values, never writable authority.
- `pool_fraction(current: float, maximum: float) -> float`, valid-domain precondition `0 <= current <= maximum`, `maximum > 0`; return `current / maximum`, not rounded current divided by a fixed 100.
- `pool_text(label: String, current: float, maximum: float) -> String` returns e.g. `HP 99.96 / 100.00`. Do not reuse `CombatText`'s “never positive as zero” rule: that rule intentionally differs. Exact ties await G5; an engine formatter's default tie outcome is not an approved design.

- [x] **RED:** create guarded-loading `status_presenter_test.gd`, with `failures`, `checks` and per-case completion checks as in `combat_log_test.gd`. Missing implementation must be a counted assertion failure. Assert named real configured player/NPC snapshots match *every* getter and separate instances have independent IDs, names, factions and levels (including levels 1 and 7). Explicit fixture names are test data, not production NPC names.
- [x] **RED:** pin fill `50/100 == 0.5`, `25/200 == 0.125`, `30/60 == 0.5`, fractional/non-100 maxima, zero current with positive maximum, full value and a tiny positive value. Pin non-ties `pool_text("HP", 99.96, 100.0) == "HP 99.96 / 100.00"`, stamina `85.0 -> "Stamina 85.00 / 100.00"`, mana `12.344 -> "Mana 12.34 / 100.00"`, `12.346 -> 12.35`, `0.001 -> 0.00`. These are display-only, not pool mutation. Add exact tie cases only after G5 is locked.
- [x] **RED:** real `configure` sheets + `apply_damage(0,25)` and `apply_damage(1,50)` yield `alive == false`, HP 75/50 respectively. A separate real sheet takes `apply_damage(2,40)`, `apply_damage(3,40)`, `apply_damage(4,19.999)`: assert getter HP `>0` and `<0.005`, `is_alive()`, text `0.00`, and fraction `>0`. Read the actual float rather than assuming decimal subtraction is exact. HP-zero death is a separate fixture. Snapshot calls leave getters/positions/orders untouched.
- [x] Run `godot --path . --headless --script res://scripts/tests/status_presenter_test.gd`; observe counted RED, then implement only the valid-domain helper. Do not invent invalid/zero-maximum behavior; production use of such data is gated G6.
- [x] Import and rerun the focused test GREEN; rerun `character_sheet_test.gd` and existing `player_name_test.gd`. Test a changed maximum by reading a real sheet before/after existing `power_up()` and comparing the new snapshot to getters (not assuming a UI-owned max). More complex policy is G6.

## Task 2 — Shared Bar and Player Status Component

**Interfaces:** `StatusBar extends Control` in `status_bar.gd`: `present(current: float, maximum: float, text: String) -> void`; stores only render values, fills immediately by Task 1 fraction. `PlayerStatus extends Control` in `player_status.gd`: `bind_sheet(sheet: OpenCharacter) -> void`, `refresh() -> void`. Consumes Task 1; child names `Identity`, `CharacterName`, `Level`, `HP`, `Stamina`, `Mana`; resource rows are `StatusBar`s. No automatic simulation calls. Root and every child use `MOUSE_FILTER_IGNORE`. Player has labels/numbers; world caller will pass empty text.

- [x] **RED:** guarded `player_status_test.gd` instantiates in an owned `SubViewport`/`CanvasLayer`, binds a real `John Doe` sheet, and checks identity above exactly three horizontal bars ordered HP/stamina/mana. Assert main HP and three exact formatted labels, semantic red/green/blue roles, no XP gauge. Exact shades and dimensions require G5; do not lock invented pixels in tests.
- [x] **RED:** after real damage call `refresh()` once and assert label/fill already equal the getters with no intermediate drain value, Tween or timed drain. A tiny-positive fixture prints `0.00` while keeping nonzero fractional data. Dead player still reads its actual main HP; do not add unrequested skull/zeroing. Bind a second real sheet, refresh, and assert name, level, maxima and fills all switch to it, never a mix.
- [x] Run `godot --path . --headless --script res://scripts/tests/player_status_test.gd` for RED. Implement the two controls using Task 1; plain text and idempotent construction. Any temporary sizes/colors needed to instantiate tests must be labeled fixture/provisional, not wired into the game before G5.
- [x] After G5 approval add exact accepted layout/color/tie assertions, then implement bottom-center anchoring and approved chat separation at 1280×720. For 720×420 and return to 1280×720 change `SubViewport.size`, await layout frames, and assert actual visible rect changed; validate the approved smaller-window policy and preserved values. If there is not enough room under the approved policy, stop; do not hide chat or shrink it without permission.
- [x] Import; focused test GREEN. Assert all descendant filters ignore input, including containers, labels, progress controls and any decorative children. This is structural coverage only; actual input is Task 6.

## Task 3 — World Status Component and Truthful Death Indicator

**Interfaces:** `WorldStatus extends Control` in `world_status.gd`: `bind_actor(actor: CharacterBody3D) -> void`, `refresh() -> void`. Resolve the actor's **current** `sheet` on refresh (not permanently cached initial sheet), consume Task 1 and reuse `StatusBar`. Children `CharacterName`, `RelationshipDiamond`, `DeathIcon`, `BarStack/HP`, `BarStack/Stamina`, `BarStack/Mana`, `LevelBadge`. `LevelBadge` is right of the stack, reads `Lv N` and is not progress. The control does not decide world visibility; Task 5 controls final eligibility. Positioning is supplied by overlay after the approved head-anchor decision.

- [x] **RED:** `world_status_test.gd` uses explicit-name real `StandingDummy` fixtures with differing node names, seeded NPC sheets, factions 0/1 and levels 1/7. Check status uses exact sheet names/levels and own pool getters, diamond green/red independent of resource colors, three ordered unlabeled/non-numerical bars, numeric medieval-badge slot right of the stack, no strength text. Do not copy player's values. An attack order against the ally does not recolor its faction diamond; current bridge faction, not hostility inferred from combat, is the source.
- [x] **RED:** apply real main/part damage and refresh: all changed fills update immediately and other actors remain unchanged. Repeat both head-25/HP-75 and torso-50/HP-50 death fixtures from Task 1. Assert status is retained, relationship diamond still present and dead slot enabled based only on `not sheet.is_alive()`. Living tiny-positive HP shows no dead indicator. Repeat refresh to ensure no duplicated bars/icons.
- [x] Run `godot --path . --headless --script res://scripts/tests/world_status_test.gd` for RED; implement the valid, explicitly named component. Before G4 use only a non-shipping test slot/boolean contract, **not a substitute glyph claimed as a skull**. After G4, add failing icon-resource/render assertions and implement the approved asset or drawing in the agreed exact file. Check license/provenance if sourced; do not generate/download art without approval.
- [x] After G3/G5 approval add explicit tests for approved empty/long-name behavior and diamond/badge arrangement, then implement them. Preserve `StandingDummy.setup`'s empty default unless human explicitly authorizes authored sheet names. Never change the sheet from UI to make the name look nicer. The log keeps scene names regardless of status identity policy.
- [x] Test `bind_actor` twice, rebind to a different actor, and replace an actor's sheet with another valid configured fixture sheet: the next refresh must read the current identity/faction/resources and leave the old sheet unchanged. Validity guards prevent dereferencing a freed actor. Unbound/invalid visible behavior itself awaits G6; test only no exception/authoritative mutation until it is decided. Import and run focused test GREEN.

## Task 4 — Own One Overlay per Scene and Wire the Player

**Interfaces:** `StatusOverlay extends Control` in `status_overlay.gd`: `bind_scene(player: CharacterBody3D, actors: Array, camera: Camera3D) -> void`, `refresh() -> void`, `unbind_scene() -> void`. `PlayerStatus` child named `PlayerStatus`; world children named `WorldStatus_<character_id>` for live valid current roster entries other than the passed player. Ownership keyed internally by actor instance, not ID alone; rebind/replacement must not reuse another actor's stale identity. No player overhead status was requested.

**Runtime approach:** add one named `StatusOverlay` to the existing HUD layer before popups. Bind during `_build_hud`, after sheets/camera exist. Have the overlay process after the main camera `_process` (explicit later `process_priority`, verified in test), then refresh valid-sheet children and approved projection/filtering once per rendered frame. No duplicate physics clock. `refresh()` is a test seam; normal-play tests must verify the automatic callback too. Attach all controls to the scene tree so freeing the scene frees them and disconnects viewport callbacks.

- [x] **RED:** create `status_integration_test.gd` using real `scenes/main.tscn`. Assert exactly one player view bound to current `Player.sheet` (`John Doe`), world views only for the two other actors when their gates permit, correct independent sheets, same HUD layer and both status/chat behind every popup. Check the first visible update and subsequent real damage without manually refreshing the control. Test sheet replacement on a fixture actor through the explicit binding path, not a new game mechanic.
- [x] Run `godot --path . --headless --script res://scripts/tests/status_integration_test.gd` for RED; implement scene-owned overlay binding only for the approved scope. It is safe to integrate the player independently when its gates are closed; **do not attach an always-visible or raycast-only world overlay as an interim production implementation**. If world gates remain open, retain the legacy dummy label and report the migration blocked.
- [x] Keep production world-status attachment disabled and the legacy labels/tests intact in this task. Exercise world-view ownership in isolated component/overlay fixtures only. Final world attachment and label/test migration belong to Task 6 after Task 5; this avoids a dependency cycle or a partially correct world display in normal play.
- [x] **Lifecycle RED/GREEN:** bind the same roster twice and assert counts stable; `unbind_scene()` releases references/children and repeat calls are safe; bind again and verify single UI copy. Free one fixture actor and refresh safely, then remove it from passed roster and rebind; no orphan status. Reuse its numeric ID on a new node/sheet and verify fresh identity. Exact automatic appearance/despawn policy awaits G6; do not add a production spawn API. A corpse is not a freed actor: keep its view until any approved removal behavior actually occurs.
- [x] Free the real world, await deferred frees, instantiate it again, and verify one overlay/player view, correct world counts and no stale signals/old-camera projections. No global singleton status state. Import; integration, world, player and existing character-visuals tests GREEN. Preserve the user's existing `main.gd` hunks and player-name test.

## Task 5 — Exact Cover and True Camera-Distance Contract (Blocked on G1/G2)

**Consumes:** approved G1 renderer contract, G2 measurement/boundary rules, current camera and actor visuals. **Produces:** approved presentation visibility adapter. Its external interface must be `bind_scene(actors: Array, camera: Camera3D) -> void`, `refresh() -> void`, `status_visible(actor: CharacterBody3D) -> bool`, `unbind_scene() -> void`; exact class/file/backend and frame semantics must be added to this plan on approval. No current API is fabricated by this proposed interface.

- [x] **STOP before production code** until G1 has a workable verified contract and G2 has explicit point/boundary choices. If unresolved, record “world visibility blocked; world feature not accepted” and continue only safe player/component work. Do not count a mocked boolean, finite rays or a notifier test as proving cover.

**Recorded 2026-11-01:** G2 is decided and pinned by `status_integration_test.gd`
(49/50/51/diagonal/behind fixtures). **G1 exact cover remains blocked; the
world feature is not accepted.** Production world overlay stays unattached
(main.gd binds an empty roster); legacy dummy labels and their tests remain.
A human-authorized throwaway spike ran (then deleted) to give the ratification
decision real data:

**G1 spike evidence (2026-11-01, RX 6800 / Vulkan Forward+ / Godot 4.7.2,
1280×720, 2× MSAA, real animated female model + production block geometry,
evidence PNGs under `/tmp/opencode/g1-evidence/`):**
- Mechanism: render the world a second time with every character fragment
  tagged a unique unshaded ID colour and all obstacles neutral; a character
  is visible iff ≥1 pixel of its ID survives the ordinary depth-tested
  raster pass. This is fragment-level, not sampled: the 30 cm sliver case
  (the exact failure mode of ray fans) reported 257 solid ID pixels as
  visible; complete cover reported 0; head-only 580; turned body 562; and
  an actor partly hidden behind another actor was correctly classified with
  its 570-pixel peer occluder in frame.
- Cost per frame: extra full-scene pass + readback/RGBA8-convert 5–9 ms +
  exact-ID count scan 50 ms in naive GDScript (must move into the C++
  extension / RenderingDevice for production: <1 ms is the realistic floor).
  The per-pixel `get_pixel` reference loop (1.8 s) shows only what not to do.
- Open contract questions the spike cannot answer alone, for the human to
  settle at ratification: MSAA/alpha-blended edge pixels do not carry the
  exact ID signature (sub-pixel slivers can read hidden — decide the
  mask-resolution + edge-semantics rule); the spike shared one world with
  material overrides while production would use a compositor effect or a
  dedicated overlay world (an implementation choice the lock should not
  pretend to make); and production cost needs a separately authorized
  C++/extension change, which this run deliberately did not attempt.
- [ ] After approval, enumerate every exact renderer/shader/adapter/fixture file in the ownership table and write an executable rendering RED in `scripts/tests/status_visibility_render_test.gd`. Use actual Godot rendering, original production rendering method and camera configuration. Wait for completed render frames according to the agreed latency; measure visible coverage/overlay state using the approved observable, not solely the implementation's own boolean.
- [ ] Authored fixtures: opaque full wall; narrow vertical slit/sliver exposing a part between usual sample rays; only head visible; only torso visible with head blocked; full character hidden; no cover; self capsule/body collisions; pick `Area3D` and agreed non-solid obstacle; character rotation/animation; camera pans/translates from fully hidden through sliver to clear and back. Assert all partial cases show status, full solid cover hides it, self-collision does not hide it and non-solid geometry follows the approved rule. Move camera while actor remains fixed; test resized render target and changed aspect ratio. No minimum visible-area threshold unless explicitly approved as a revised lock.
- [ ] Run the rendered test without `--headless` and observe a real RED for the deficient visibility implementation; missing backend setup must fail distinctly rather than skip cases. Implement only the approved exact algorithm, then rendered GREEN. Record actual backend, viewport sizes, coverage semantics and frame latency/performance; dummy renderer/headless results cannot stand in for these.
- [x] Distance fixtures must stand genuinely 49 and 51 units from the camera using the approved character point and 3D Euclidean length. Freeze the follow callback in the test so it cannot move the camera back. Explicitly use a vertical/diagonal separation with horizontal gap below 50 but true 3D distance above 50; also place player near a distant NPC while camera is far (and converse) to catch player-based filtering. Test exactly 50 and immediately either side **only after** G2 decides inclusion/point/precision policy. No arbitrary `<=` or epsilon added by the implementer. Keep threshold a single presentation constant `MAX_VIEW_DISTANCE := 50.0`, easy to revisit.
- [ ] Combine death with visibility: corpse retains diamond/skull/actual positive HP fill when eligible, still respects distance/complete cover. Death must not bypass the renderer gate or imply zero HP. Add screen-edge/behind-camera cases with approved G2 policy, not an invented clamp/hide rule.
- [ ] Separate production evidence: repeat the cover path with actual female body/hair and animations, original four blocks, floor and bench meshes. Original blocks are not guaranteed to give a full wall or sliver at initial camera angles. Authored test walls prove fixtures only; do not add them to `_build_world` or claim the main scene already contains a complete-cover fixture. Verify the approved solidity/identity wiring actually includes production meshes, not only specially tagged test boxes.

## Task 6 — Actual Input, Resize, Live Data and Popup/Log Regressions

**Consumes:** Tasks 2–5 and approved gate policies. **Produces:** runtime acceptance evidence, not another status architecture. For a player-only authorized slice, run its subset and explicitly report world rows blocked.

- [x] **World integration RED/GREEN, only after Task 5 passes:** enable real-scene world assertions in `status_integration_test.gd` and observe RED for missing production world views. Wire the approved visibility adapter and world-status binding in `main.gd`. Remove `_label`, `_build_label`, `_label_text`, `_refresh_label` and only their call sites in `standing_dummy.gd`. Preserve collision/Visual creation, setup arguments, `set_can_move(false)`, coordinator/fallback regeneration and movement. No replacement numeric HP/strength label.
- [x] In that same migration replace `character_visuals_test.gd:189–201`'s legacy `Label3D` assertions with equally strong current-sheet checks: one ally status, sheet identity/relationship/level, full initial HP fraction, real torso damage of 10 changes fill to getter ratio (`90/100` in fixture), other resources persist and C++ agrees. Assert no obsolete faction/HP/strength label remains, without requiring forbidden world resource text. Keep all existing model, animation, limb, movement and death coverage. Import and run integration/visual suites GREEN before proceeding.
- [x] **RED:** extend `status_integration_test.gd` with actual mouse motion/down/up via `Viewport.push_input`, following `melee_combat_test.gd::_click_at`; never call `_unhandled_input` directly as the only proof. Find a real physics floor hit inside the visible HUD rect, send click and assert C++ movement/order and marker, then actual player displacement. If original floor does not cover that projection, fail the search explicitly; an extended-floor input fixture can test the input mechanism separately, but label it authored geometry and still verify an actual production-world reachable point. Do not mistake a sky miss for swallowed input.
- [x] Add overhead click-through: find a floor hit inside the visible overhead rect and verify walk; separately align a valid living character collision beneath an overhead point and check its action popup opens without floor order or immediate attack. An overhead is not itself a character hit target. Click the body's normal projected point too, for both ally and enemy, and verify unchanged Attack confirmation behavior. Dead-body click remains inert, not a walk through to the floor. Fixture repositioning is presentation setup, not a change to picking rules.
- [x] Place an existing popup over a status coordinate with its normal open/move mechanisms. Click its interactive region and assert it consumes input and sends only its existing action, not a floor move; click uncovered HUD/log and assert the world still receives input. Verify all six popups remain above status/chat; retain existing drag/close/shortcut behavior. Do not add status to `_popup_covers` or change `_try_character_or_floor_click`.
- [ ] Populate the log with eight actual reported swings (use real coordinator setup/tick and listen to `swing_reported`, never add a new production drain). Verify status refresh does not add tick lines, rename log actors to sheet names, mutate event dictionaries, duplicate signal connections or suppress floating damage/swing animation. Keep the existing chat-specific synthetic formatting tests too.
- [x] Real runtime data: order an in-range fight and compare player and NPC stamina bars to live getters after a performed swing and regeneration; assert different actors are not copied from player. Use controlled physics stepping as existing melee fixtures do for precise assertions, then a normal callback case for integration. Read mana/current max faithfully even if mana remains full in current play; do not add a spell/stamina/mana setter to force UI coverage. Task 1/2 valid numeric fixtures cover independent pool/max values; actual max change via existing `power_up()` covers bridge reread. If a dynamic mana scenario cannot be produced by a current command, record that production scenario unavailable, not falsely exercised.
- [x] Resize an owned `SubViewport` containing the real scene 1280×720 → 720×420 → 1280×720, await layout, and verify camera projection and viewport size really changed. Check bottom-center player anchoring, bottom-left populated chat remains separate under the approved G5 policy, unchanged resource data, projected head placement and popup priority. Exact layout/long-name/crowding/edge behavior beyond approved policies remains gated. Repeat ordinary desktop resize separately.
- [ ] Import; run focused tests below GREEN and existing melee/combat-feedback/combat-log/popup/player-name suites. A case not completed or an engine runtime/script error is failure even if process exits zero. Do not weaken old tests to accommodate duplicate views, wrong identity or unimplemented locks.

## Verification Commands — Later Execution Only

Run from `/home/akira/Projects/Open`. All commands below are planned, **not run during planning**. Record exit status and error output, and distinguish a blocked new case from a pass. No builds/imports before implementation authorization.

**Full C++ + fresh extension/import before Godot regression** (also required after any separately authorized extension change):

```bash
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
godot --path . --headless --import --quit
```

Use the pinned tag or a verified matching `GODOT_CPP_DIR`, not a different API version. Optional simulation-only baseline, never substitute this for the extension/import needed by Godot:

```bash
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=OFF
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
```

Reconfigure ON and rebuild before Godot if using the same build directory afterward. There are no planned C++ changes; full CTest protects the simulation boundary.

**Focused RED/GREEN commands** (only once each referenced test file exists; import new `class_name`s first):

```bash
godot --path . --headless --script res://scripts/tests/status_presenter_test.gd
godot --path . --headless --script res://scripts/tests/player_status_test.gd
godot --path . --headless --script res://scripts/tests/world_status_test.gd
godot --path . --headless --script res://scripts/tests/status_integration_test.gd
godot --path . --headless --script res://scripts/tests/player_name_test.gd
godot --path . --headless --script res://scripts/tests/character_sheet_test.gd
godot --path . --headless --script res://scripts/tests/character_visuals_test.gd
godot --path . --headless --script res://scripts/tests/melee_combat_test.gd
godot --path . --headless --script res://scripts/tests/combat_feedback_test.gd
godot --path . --headless --script res://scripts/tests/combat_log_test.gd
godot --path . --headless --script res://scripts/tests/character_popup_test.gd
```

**Every current headless Godot regression**, discovering rather than hard-coding the old count. The explicit rendered suite must refuse headless certification and is run separately; exclude only that exact path:

```bash
count=0
for test in scripts/tests/*_test.gd; do
  if [ "$test" = "scripts/tests/status_visibility_render_test.gd" ]; then
    continue
  fi
  godot --path . --headless --script "res://$test" || exit 1
  count=$((count + 1))
done
printf 'Headless test scripts executed: %s\n' "$count"
```

This includes equipment, technique, weapon, workbench, model/animation, bridge and user player-name regressions; no fixed 15-script assumption after adding tests. Audit logs for script/runtime errors and case completion as well as command exit. New suite runtime errors must increment failure or fail completion guards, not print a misleading pass.

**Rendered visibility suite**, after G1/G2 approval and creation (desktop/display and actual production rendering backend required):

```bash
godot --path . --script res://scripts/tests/status_visibility_render_test.gd
```

Do not silently replace this with `--headless`, a dummy renderer or a mock. No display/backend means “not run / blocked,” never GREEN. Report automated rendered evidence separately from the following human visual pass.

## Task 7 — Separate Desktop Verification and Evidence-Based Docs

**Execution evidence (2026-11-01, this run):**

- Build: `cmake -DOPEN_BUILD_GDEXTENSION=ON` + full build exit 0; `ctest`: **24/24
  passed**; fresh `--import` exit 0. No C++/extension sources were touched.
- Headless discovery sweep (`scripts/tests/*_test.gd`, all 19 — 15 baseline plus
  `status_presenter_test`, `player_status_test`, `world_status_test`,
  `status_integration_test`): **19 executed, 0 failures, 0 hidden script/parse
  errors** in the logs. No rendered suite exists yet (`status_visibility_render_test.gd`
  is gated on G1 ratification and was not written), so nothing was excluded from
  the sweep and no rendered certification is claimed.
- Gate state at handoff: **player HUD shipped and accepted** by its test evidence;
  **world overlay NOT shipped, NOT accepted** — G1 blocked pending the human's
  ratification of the spike evidence above; legacy dummy labels and their
  `character_visuals_test` assertions deliberately intact.
- Desktop visual verification: **NOT YET RUN** — it requires the human's eyes;
  the game launches with `godot --path .` (the plaque is visible immediately).
  This is a gap in whole-feature acceptance, not inferred from headless runs.
- `docs/status_UID.md` **Current** rewritten to exactly the shipped subset (this
  run); locks unchanged; no Proposed/Open text promoted. `docs/README.md`
  re-checked — its status link was already correct, so it stands untouched.
  `AGENTS.md`, the user's other dirty files, and `player_name_test.gd` were
  preserved; nothing was staged, committed, reset or stashed.

- [x] Run full extension build, full CTest, import, every discovered headless script and the approved rendered suite. Record counts, actual results, blocked gates and any regression failures; no success claim from a subset. *(No approved rendered suite exists; the absence is recorded, not papered over.)*
- [ ] Launch `godot --path .` on a real desktop. At 1280×720 and a smaller live-resized window inspect readable rustic status, approved muted colors/order/fill, identity over bars, right-side level badge, distinct relationship diamond and a recognizable non-missing-glyph skull. Check no numeric/label text on world bars and no old HP/strength label. Validate contrast against light/dark world backgrounds; verify player HUD/chat separation under the agreed layout, and all popups remain readable/usable on top. *(Blocked on the human; world-overlay rows additionally remain blocked on G1 ratification and cannot be verified yet.)*
- [x] Only after actual implementation/evidence update `docs/status_UID.md` **Current** to describe exactly what shipped and verified, including any player-only delivery and gated world work. Do not promote undecided Open/Proposed text or claim missing visibility is implemented. Preserve locks and human decisions. Gated decisions may change only via explicit human approval recorded earlier, not as documentation cleanup.
- [x] Reconcile `docs/README.md` only if now genuinely inaccurate: it already has the correct status link. Do not edit the user's dirty `AGENTS.md`. Review scoped diffs against the execution baseline; report implementation, headless results, rendered results, visual results and unresolved gates separately. No staging or commit.

## Acceptance Matrix — Current Versus Target

“Current” below is inspected baseline, not a tested pass. “Gated” remains **not accepted** until decisions and evidence exist; no checkbox in an independent task can waive it.

| Area / Locked target | Current inspected behavior | Target proof / owner | Gate |
|---|---|---|---|
| Player actual name and main level | `John Doe` already set; name test exists; no fixed HUD | One bottom-center identity area reading current sheet, levels 1/7 fixtures and preserved name test; T1/2/4/6 | G5 layout |
| Player three labeled horizontal resources | No status HUD | HP/stamina/mana, labeled current/max, nearest hundredth/two places, no-ties and tiny-positive `0.00`; T1/2 | G5 ties/style |
| Shared order/colors | No shared status bars; old dummy label faction-colored | HP red, stamina green, mana blue, muted, in that order in both views; T2/3 + desktop | G5 shades |
| Fill / immediate updates | Bridge has current/max; old dummy label HP integer only | Per-pool own current max, no fixed 100, `50/100`, differing maxima/actors, live damage/regen/max reread, no smoothing; T1–3/6 | G6 invalid/zero-max/transition policies |
| World own identity and relationship | Empty NPC sheet names; label says Friendly/Enemy and strength | Name above bars, independent sheet level/pools, separate green/red diamond, no strength/old numeric HP; T3/4 | G3 empty/long names; G5 |
| World layout/level | `Label3D` text only | Three unlabeled/non-numeric bars, level badge on right, current main level `Lv N`, no XP; T3 + rendered/visual evidence | G5 anchor/style |
| Normal ally/enemy visibility | Old labels built unconditionally | Both statuses shown without hover/combat, no faction/attack-order gate; T3/5/6 | G1/G2 |
| Any visible part / complete solid cover | No exact status visibility implementation inspected | Rendered thin sliver/head-only/torso-only remain visible, fully hidden hides; self/non-solid/camera/animation fixtures and production setup; T5 | **G1 core engineering blocker** |
| True camera range 50 | No status distance filter | 49/51 and diagonal/vertical camera distance, player-distance negative controls, approved exact-50/point; T5 | **G2 point/boundary blocker** |
| Death retention and skull alongside diamond | Bridge `is_alive`; no status skull asset identified | Alive tiny HP no skull; dead head HP75 and torso HP50 retain actual fills/diamond and show recognizable skull while eligible; T1/3/5/6 | G4 icon; G1/G2 eligibility; G6 eventual removal |
| Player/world clicks pass through | Chat ignores input; popups consume it | Actual input over HUD/overheads yields correct floor movement or character popup; over popup remains consumed; T6 | G5 final placement |
| Popup/log compatibility | Chat under popups, single shared swing source, node names | Status also below popups, no second drain/log tick lines/renames, existing motions/damage remain; T4/6 | G5 small-window overlap |
| Resize / projection | Canvas stretch, log resize tests; no status | Real SubViewport size change and desktop resize, camera-follow current projection, approved chat separation/edge policy; T2/4/5/6 | G2 edges; G5 size/overlap |
| Actor lifecycle / reentry | Fixed roster and per-scene HUD | No duplicates, stale sheet/ID/camera or freed accesses on bind/unbind/reentry; corpse distinct from removal; T3/4/6 | G6 dynamic appearance/removal |
| State boundary and regression | C++ sim / bridge / Godot presentation split | No new sim setters/state/rules or event drain, full CTest + all headless scripts + separate rendered and desktop results; T7 | All relevant gates for whole-feature acceptance |

## Planner Self-Review / Handoff

- [x] Read live required docs, scene/actor/bridge/log code, representative sheet/visual/popup/melee/log/name tests, model setup, current pool implementation and prior log plan.
- [x] Map every Locked player/world/shared target to a task and acceptance row; distinguish baseline from target and from evidence not yet collected.
- [x] Verify proposed interfaces agree across tasks; label new APIs as planned, reuse actual bridge getters and existing legitimate fixture commands only.
- [x] Keep exact partial occlusion visibly blocked without a verified renderer contract; require actual rendered evidence and production hookup rather than finite samples or authored geometry alone.
- [x] Keep measurement point, exact boundary, fallback names, skull sourcing, exact styling/layout, invalid data and lifecycle policies as explicit gates; no invented canon, removal behavior or tiny-HP/death inference.
- [x] Limit this planning change to this file. Implementation, builds, tests and desktop verification are all future work; execution must await separate authorization.
