# Prey multiplayer bot characters

Date: 2026-08-02

Status: complete and verified in the working set. Full navigation remains an
explicitly separate future project, not an unmet gate for this character phase.

## Goal

Give Prey multiplayer a complete named bot roster with distinct play styles,
skill-scaled execution, and lore-conscious chat while preserving the shipped
Prey game contract. The implementation follows the useful parts of the
upstream openQ4 bot-character process—data-driven traits, separate mechanics
and voice files, strict content validation, and a server-side chat path—but it
does not copy Quake 4 assumptions that are incompatible with Prey.

The result must:

- keep `GAME_API_VERSION` at 7;
- use Prey's existing `hhArtificialPlayer` and ordinary multiplayer player
  rules rather than require an engine-owned bot client;
- use the exact retail-selectable multiplayer model slots 0 through 18;
- make skill describe execution and style describe intent;
- give every shipped character a substantial, safe, non-repetitive voice;
- package all authored data inside `basepr/pak0.pk4`; and
- state the current navigation limit honestly rather than claim full map
  routing.

## Constraints and decisions

### Preserve the Prey v7 boundary

The engine-side `OPENPREY_ENABLE_BOTS` path depends on callbacks and bot
metadata that are not part of Prey's game API v7. Enabling it would neither
identify the selected character to the game nor satisfy the rebase requirement
to preserve the ABI.

Prey already supplies the correct compatibility seam:

- retail declares `player_artificial_mp` with spawn class
  `hhArtificialPlayer`;
- `idGameLocal::SpawnArtificialPlayer` creates it in a player entity slot;
- `hhArtificialPlayer::Think` writes a normal `usercmd_t`;
- reliable userinfo plus snapshots carry its identity and command to clients;
- scoring, weapons, damage, team rules, respawn, and the scoreboard continue
  through the ordinary player code.

Character selection, resolved traits, movement, combat, and chat therefore stay
inside `OpenPrey-game`. No virtual is added to `idGame`, `idNetworkSystem`, or
the game export table, and `GAME_API_VERSION` remains 7.

### Adapt the upstream design instead of cloning it

| Upstream openQ4 assumption | Prey decision |
| --- | --- |
| Engine-owned `NA_BOT` client slots | Game-side `hhArtificialPlayer` entities |
| `playerModel` decl names and team-specific model keys | Retail `ui_modelNum` slots 0..18 |
| Quake 4 `weapon_*` classes | Prey `weaponobj_*` classes |
| Flat-world runtime grid routing | Gravity-aware visible-target steering and collision avoidance |
| Gauntlet humiliation event | Wrench humiliation event, `killWrench` |
| Quake 4 cast and dialogue | Prey retail model roster and evidence-bounded voices |

Retail Prey multiplayer maps do not ship bot AAS. The initial implementation
does not introduce a full navigation system: it projects aiming and movement
through the player's current gravity axis, pursues visible enemies and visible
items, abandons a visible item after bounded time without net progress, wanders
when idle, and uses a player-bounds trace for wall avoidance and occasional
jumping. It cannot promise deliberate multi-room, portal, wall-walk, or
objective routes. A future navigation project must model changing gravity and
portal connectivity before making that claim.

## Personality contract

Each spawn resolves three content layers into one `botTraits_t`:

1. A skill baseline from 1 through 5, with deterministic per-bot fractional
   variance.
2. One of six play styles: `rusher`, `sniper`, `roamer`, `hunter`,
   `ambusher`, or `skirmisher`.
3. A named character with its retail model slot, preferred skill band,
   personal modifiers, weapon biases, and voice.

Resolution order is baseline, style body, style skill block, character body,
then character skill block. `set`, `add`, and `scale` operations apply in file
order and clamp immediately. Named weapon preferences merge across layers.

The 48 scalar traits are:

- vision and reaction (8): `sightRange`, `fov`, `reactionMsec`,
  `reactionVarianceMsec`, `reacquireMsec`, `reacquireFraction`,
  `peripheralAngle`, `peripheralPenaltyMsec`;
- aim (10): `turnSpeed`, `turnAccel`, `turnDamping`, `aimTrackTimeConst`,
  `aimTremorDeg`, `aimTremorRateHz`, `aimTrackError`, `aimSettleMsec`,
  `aimLead`, `aimLeadError`;
- trigger (5): `fireConeDeg`, `holdFireTurnRate`, `burstMinMsec`,
  `burstMaxMsec`, `burstPauseMsec`;
- mistakes (2): `mistakeChance`, `mistakeMsec`;
- movement and engagement (10): `strafeChance`, `dodgeReactMsec`,
  `jumpChance`, `combatRange`, `rangeDiscipline`, `aggression`, `patience`,
  `retreatHealth`, `pursuit`, `itemFocus`;
- tactical personality (9): `initiative`, `targetStickiness`,
  `opportunism`, `vengefulness`, `suppressionMsec`, `strafeRhythmMsec`,
  `strafeRhythmVarianceMsec`, `weaponSwitchMsec`, `aimHeight`;
- decision quality (2): `weaponSkill`, `targetSelection`; and
- chat (2): `chatiness`, `chatDelayScale`.

The six styles deliberately change intent, not competence:

| Style | Intended read |
| --- | --- |
| `rusher` | Closes to brawling range, accepts imperfect shots, and prefers the wrench, Soul Leech, and autocannon. |
| `sniper` | Holds distance, waits for a clean shot, and prefers the rifle, Hider weapon, and bow. |
| `roamer` | Rotates through visible pickups and takes fights encountered on the route. |
| `hunter` | Selects one opponent, remembers it through brief cover, and pursues it over items. |
| `ambusher` | Holds a prepared line, reacts efficiently from that line, and moves less while engaged. |
| `skirmisher` | Trades in short bursts, changes its gravity-relative angle, and breaks off early. |

Supported weapon-bias names are `weaponobj_wrench`, `weaponobj_rifle`,
`weaponobj_crawlergrenade`, `weaponobj_soulstripper`,
`weaponobj_autocannon`, `weaponobj_hiderweapon`,
`weaponobj_rocketlauncher`, and `weaponobj_bow`.

## Retail roster and lore boundary

The shipped roster is exactly the models a retail player can select:

| Slot | Character | Style | Skill band | Evidence boundary |
| ---: | --- | --- | --- | --- |
| 0 | Tommy | hunter | 3-5 | Story identity; relentless but humane former soldier. |
| 1 | Mutilated Human | rusher | 1-3 | Retail role label; sparse, dignified voice with no invented former identity. |
| 2 | Chuck | rusher | 1-3 | Roadhouse story NPC; playful follower bravado without repeating the scene's abuse. |
| 3 | Dalton | rusher | 2-4 | Roadhouse story NPC; confrontational barroom-instigator swagger kept to the match. |
| 4 | Hider | ambusher | 2-4 | Hidden role label; cautious resistance coordination. |
| 5 | Grandfather | sniper | 2-4 | Story identity; patient teacher, without imitation folklore. |
| 6 | Abducted | roamer | 1-3 | Circumstance label; wary and self-directed, never helpless. |
| 7 | Teacher | roamer | 2-4 | Retail label; methodical observation only. |
| 8 | Edward | roamer | 1-3 | Retail named label; restrained, courteous regular. |
| 9 | Trent | skirmisher | 2-4 | Retail named label; concise, route-aware competitor. |
| 10 | Roy | ambusher | 2-4 | Retail named label; easygoing lane-holder and sportsman. |
| 11 | Mohawk Hider | skirmisher | 3-5 | Retail model label; bold, mobile Hidden scout, never a claim about a real-world community. |
| 12 | Victim | ambusher | 2-4 | Circumstance label; vigilant, agentic cover control rather than a joke or competence judgment. |
| 13 | Post-op | skirmisher | 3-5 | Circumstance label; controlled and focused, never treated as grotesque. |
| 14 | Becky | skirmisher | 3-5 | Retail named label; self-possessed marksman with no invented biography. |
| 15 | Elite Hunter | sniper | 4-5 | Enemy role; precise, sparse translated combat traffic. |
| 16 | Elhuit | ambusher | 3-5 | Story identity; formal Hidden resistance leadership. |
| 17 | Hunter | hunter | 2-4 | Enemy role; clipped functional squad traffic, no fake alien language. |
| 18 | Jen | skirmisher | 2-4 | Story identity; grounded, practical, and direct. |

Mother is excluded. Engine/game remnants mention a possible model 19 and some
partial assets exist, but retail `player.def` does not expose a complete
selectable `model_mp19` slot. The roster must not manufacture that missing
contract or alias another model to it.

Named story facts may guide a character. A retail name, visual role, or
multiplayer-only label may support only restrained gameplay inference. Content
must not invent biographies, relationships, slurs, fake alien languages, or
claims about real-world cultures. Generic names such as Hunter, Hider, and
Victim also make name-addressed reply matching broad, so their direct replies
remain low priority and restrained.

## Chat content contract

Each of the 19 shipped characters has exactly eight alternatives for each of
14 events: `entergame`, `levelstart`, `kill`, `killWrench`, `killStreak`,
`revenge`, `death`, `deathAccident`, `itemDenied`, `leadTaken`, `leadLost`,
`matchWin`, `matchLose`, and `farewell`. That is 2,128 event lines.

Each character also has nine reply categories—`help`, `goodGame`,
`challenge`, `greeting`, `thanks`, `praise`, `apology`, `farewell`, and
`direct`—with exactly eight triggers and four response lines per category.
Matching is normalized and whole-word based; direct rules require the
character to be addressed. One eligible bot answers, delayed replies retain
the incoming global/team route, and a generated reply cannot trigger another
reply.

Allowed substitutions are `$self`, `$other`, `$weapon`, `$map`, and `$item`,
subject to the data available for the event. Lines are capped at 160
characters, may not begin with `#`, and must remain useful when optional tokens
are unavailable. Chat files are raw authored text: retail `<PROFANITY>` markup
is not applied by this parser, so voices avoid strong profanity, slurs, and
invented derogatory language.

## Implementation sequence

1. **Personality parser**
   - Add `src/game/bots/BotCharacter.{h,cpp}` to the canonical GameLibs repo.
   - Load styles, characters, then chats through `idLexer` and
     `DECL_LEXER_FLAGS`.
   - Resolve inheritance after enumeration; warn and contain malformed
     content instead of terminating a server.
   - Initialise and shut down the manager with `idGameLocal`.

2. **Prey artificial-player runtime**
   - Bind a character and effective skill to `hhArtificialPlayer`.
   - Replace the retail test stub with trait-driven perception, targeting,
     weapon selection, aiming, burst discipline, gravity-aware movement,
     visible-item interest, wall avoidance, and normal respawn input.
   - Preserve the existing snapshot/user-command contract while publishing
     canonical character, skill, model, and team userinfo for spawn, reload,
     and late join.

3. **Roster lifecycle and operator controls**
   - Set `ui_name`, ready/play state, and `ui_modelNum` from the selected
     character.
   - Add named add/remove/list/reload commands and reconcile
     `g_artificialPlayerCount` both upward and downward.
   - Release character reservations when a bot is removed, reloaded, or
     displaced by a human connection.
   - Retire a displaced bot on existing clients before reusing its slot for a
     joining human, without changing reliable-message opcodes or API v7.

4. **Chat integration**
   - Hook authoritative kill/death and match-state events in
     `idMultiplayerGame`.
   - Observe accepted typed chat only after delivery routing is known.
   - Apply per-bot and global throttles, typing delay, team routing, and reply
     recursion protection.

5. **Content, packaging, and validation**
   - Author six styles, 19 mechanics files, and 19 voice banks under
     `content/basepr/pak0/botfiles/`.
   - Package them in `pak0.pk4`; do not stage loose botfiles in `.install`.
   - Port and activate `tools/tests/mp_bot_characters.py` for the Prey schema,
     v7 lifecycle, exact roster, all content cardinalities, and chat safety.
   - Keep `mp_bot_navigation.py` explicitly deferred until openPREY has an
     honest Prey-aware navigation contract.

## Acceptance gates

- `GAME_API_VERSION` remains 7 and engine bot extensions remain disabled.
- All six styles load and every one is used by the roster.
- All 48 trait names agree between the struct, field table, baseline curve,
  style files, and character files.
- The roster contains exactly slots 0..18 above, with no Mother/model 19.
- Every style/character weapon name belongs to the eight supported Prey
  multiplayer classes.
- Every shipped voice has 14 × 8 event lines and 9 reply rules with 8 triggers
  and 4 responses.
- Parser errors are warnings, every file list is freed, and reload safely
  rebinds live bots.
- Character content is present in the produced `basepr/pak0.pk4` and absent as
  loose staged runtime data.
- The active character contract test passes against both repositories.
- A windowed multiplayer smoke test confirms named models, skill/style
  differences, scoring, respawn, add/remove/reload, delayed chat, and clean
  shutdown logs. Full navigation is not an acceptance claim for this phase.

## Verification results

Completed on 2026-08-02:

- `python tools/tests/mp_bot_characters.py`, Python syntax checking,
  `packaging_safety.py`, and `docs_link_integrity.py` pass. The active PR
  validation dry run includes the bot-character contract.
- A serial Meson build completed after the final game-network changes,
  including a full 304-step game-module rebuild after the client-slot state
  addition. `meson install -C builddir --no-rebuild --skip-subprojects`
  staged the result successfully.
- The staged `basepr/pak0.pk4` has MD5
  `8fe151e9c4b354d09430f309b13840eb` and contains exactly 44 bot assets: six
  `.style`, 19 `.bot`, and 19 `.chat` files. No loose staged botfiles or import
  libraries remain in `.install`.
- The direct-build dedicated smoke at
  `.tmp/mp-bot-final-builddir-20260802-123036/basepr/logs/openprey.log` loaded
  six styles and 19 characters, exercised four distinct named bots and skill
  levels, emitted authored chat, reloaded live content, removed bots, and shut
  down without a bot parser or identity error.
- The final packaged Team DM late-join run at
  `.tmp/mp-bot-final-latejoin-20260802-125729/` forced the client to 800x600
  windowed mode. Tommy, Hunter, and Jen entered at skills 5, 3, and 4; a real
  client claimed Tommy's slot; Hunter and Jen remained live through GAMEON and
  `botreload`; `removebots` removed exactly two; and both processes exited 0.
  The client received the authoritative artificial-player snapshot type.
- The packaged pending-connection race replay at
  `.tmp/mp-bot-final-pending-race-20260802-130112/` reproduced the critical
  ordering—client 0 connected, bots were created, then client 0 began. The
  pending slot stayed reserved, so Tommy, Hunter, and Jen occupied slots 1, 2,
  and 3; no duplicate entity was created and both processes exited 0.
- A no-bot control at `.tmp/mp-baseline-latejoin-20260802-125233/` reproduced
  the stock `object_glowportal_3` stale-sequence warning. The bot-specific,
  intentional player-placeholder upgrade is debug output rather than a
  warning; unexpected client-slot recycling remains a warning.

These results validate the character, lifecycle, chat, packaging, and current
gravity-aware combat fallback. They do not broaden the stated navigation claim.

The living subsystem and authoring reference is
[`docs/dev/mp-bots.md`](../mp-bots.md).
