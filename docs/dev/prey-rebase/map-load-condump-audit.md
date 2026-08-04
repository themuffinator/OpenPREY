# Prey map load conDump audit

- Generated: 2026-08-02T12:35:27+01:00
- Started: 2026-08-02T12:16:21+01:00
- Map manifest: `.vscode/prey-maps.json`
- Renderer API: `gl`
- Retail asset root: `C:\Program Files (x86)\R.G. Mechanics\Prey`
- openPREY install root: `.install`
- Raw audit output: `.tmp/map-load-condumps/20260802-121621`
- Per-map timeout: 120s
- Post-load settle time before conDump: 2500ms

## Summary

- Maps attempted: 37
- Maps with conDumps: 37
- Map-command loads that completed: 37
- Positive target-map completion markers: 37
- Process exits with conDumps and verified map loads: 37
- Detected map-load failures: 0
- Missing positive target-map completion markers: 0
- Timeouts: 0
- Non-zero process exits: 0
- Unknown process exit statuses: 0
- Unique issue signatures: 33
- Actionable issue signatures: 0
- Known notice signatures: 33

## Issue families

| Family | Severity | Unique issues | Maps | Occurrences | Examples |
| --- | --- | ---: | --- | ---: | --- |
| Uppercase retail asset path diagnostic | notice | 33 | game/biolabsa, game/deathwalk3, game/dmroadhouse, game/dmsphere, game/feedingtowera, game/feedingtowerc, game/feedingtowerd, game/girlfriendx, … (+8) | 1078 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/barrel`; `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/neon_sign`; `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/extinguisher` |

## Unique issues

| ID | Severity | Maps | Occurrences | Representative issue |
| --- | --- | --- | ---: | --- |
| U001 | notice | game/feedingtowera, game/feedingtowerc, game/feedingtowerd, game/roadhouse, game/spherebrain | 72 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/neon_sign` |
| U002 | notice | game/girlfriendx, game/harvestera, game/spindlea | 51 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/Spindle` |
| U003 | notice | game/feedingtowerc, game/feedingtowerd | 34 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/Portalframe` |
| U004 | notice | game/roadhouse, game/spherebrain | 34 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/barrel` |
| U005 | notice | game/roadhouse, game/spherebrain | 22 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/beertap` |
| U006 | notice | game/roadhouse, game/spherebrain | 22 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/beertap_handle` |
| U007 | notice | game/roadhouse, game/spherebrain | 22 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/beertap_handle3` |
| U008 | notice | game/salvage, game/salvageboss | 153 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SalvageBoss` |
| U009 | notice | game/feedingtowerc | 51 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/Alien_Roadhouse` |
| U010 | notice | game/deathwalk3 | 34 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/deathwalk/ASH` |
| U011 | notice | game/dmsphere | 55 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/DM` |
| U012 | notice | game/keeperfortress | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/Keeper_couch` |
| U013 | notice | game/keeperfortress | 51 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/Portal_generator` |
| U014 | notice | game/feedingtowerc | 34 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/barstool` |
| U015 | notice | game/feedingtowerc | 12 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/beercase` |
| U016 | notice | game/feedingtowerc | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/boxblue` |
| U017 | notice | game/feedingtowerd | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/boxred` |
| U018 | notice | game/roadhouse | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/extinguisher` |
| U019 | notice | game/feedingtowerd | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/fan` |
| U020 | notice | game/feedingtowera | 11 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/truck` |
| U021 | notice | game/dmroadhouse | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/RoadHouse/wastecan` |
| U022 | notice | game/biolabsa | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SpiritBridges/biolabs` |
| U023 | notice | game/feedingtowerc | 34 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SpiritBridges/FeedingTowerC` |
| U024 | notice | game/feedingtowerd | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SpiritBridges/FeedingTowerD` |
| U025 | notice | game/girlfriendx | 51 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SpiritBridges/girlfriendx` |
| U026 | notice | game/keeperfortress | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SpiritBridges/KeeperFortress` |
| U027 | notice | game/salvage | 34 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SpiritBridges/Salvage` |
| U028 | notice | game/salvageboss | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SpiritBridges/salvageboss` |
| U029 | notice | game/salvageboss | 34 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SpiritBridges/Schoolbus` |
| U030 | notice | game/spindlea | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SpiritBridges/SpindleA` |
| U031 | notice | game/spindleb | 51 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/SpiritBridges/spindleb` |
| U032 | notice | game/spindlea | 17 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/mapobjects/Tubes` |
| U033 | notice | game/harvestera | 12 | `WARNING: Non-portable: path contains uppercase characters: <game-root>/models/monsters/Hunter/gibs` |

## Per-map results

| Map | Kind | Status | Time | Issues | conDump | Log |
| --- | --- | --- | ---: | --- | --- | --- |
| game/roadhouse | sp | ok | 30.8s | U004, U005, U006, U007, U018, U001 | `.tmp/map-load-condumps/20260802-121621/savepaths/01_game_roadhouse/basepr/logs/condumps/01_game_roadhouse.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/01_game_roadhouse/basepr/logs/01_game_roadhouse.log` |
| game/feedingtowera | sp | ok | 41.0s | U001, U020 | `.tmp/map-load-condumps/20260802-121621/savepaths/02_game_feedingtowera/basepr/logs/condumps/02_game_feedingtowera.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/02_game_feedingtowera/basepr/logs/02_game_feedingtowera.log` |
| game/feedingtowerb | sp | ok | 43.6s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/03_game_feedingtowerb/basepr/logs/condumps/03_game_feedingtowerb.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/03_game_feedingtowerb/basepr/logs/03_game_feedingtowerb.log` |
| game/lotaa | sp | ok | 24.1s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/04_game_lotaa/basepr/logs/condumps/04_game_lotaa.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/04_game_lotaa/basepr/logs/04_game_lotaa.log` |
| game/feedingtowerc | sp | ok | 44.1s | U009, U003, U014, U015, U016, U001, U023 | `.tmp/map-load-condumps/20260802-121621/savepaths/05_game_feedingtowerc/basepr/logs/condumps/05_game_feedingtowerc.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/05_game_feedingtowerc/basepr/logs/05_game_feedingtowerc.log` |
| game/feedingtowerd | sp | ok | 42.5s | U003, U017, U019, U001, U024 | `.tmp/map-load-condumps/20260802-121621/savepaths/06_game_feedingtowerd/basepr/logs/condumps/06_game_feedingtowerd.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/06_game_feedingtowerd/basepr/logs/06_game_feedingtowerd.log` |
| game/salvage | sp | ok | 34.6s | U008, U027 | `.tmp/map-load-condumps/20260802-121621/savepaths/07_game_salvage/basepr/logs/condumps/07_game_salvage.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/07_game_salvage/basepr/logs/07_game_salvage.log` |
| game/salvageboss | sp | ok | 35.2s | U008, U029, U028 | `.tmp/map-load-condumps/20260802-121621/savepaths/08_game_salvageboss/basepr/logs/condumps/08_game_salvageboss.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/08_game_salvageboss/basepr/logs/08_game_salvageboss.log` |
| game/lotab | sp | ok | 20.9s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/09_game_lotab/basepr/logs/condumps/09_game_lotab.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/09_game_lotab/basepr/logs/09_game_lotab.log` |
| game/shuttlea | sp | ok | 38.1s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/10_game_shuttlea/basepr/logs/condumps/10_game_shuttlea.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/10_game_shuttlea/basepr/logs/10_game_shuttlea.log` |
| game/shuttleb | sp | ok | 40.5s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/11_game_shuttleb/basepr/logs/condumps/11_game_shuttleb.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/11_game_shuttleb/basepr/logs/11_game_shuttleb.log` |
| game/biolabsa | sp | ok | 39.8s | U022 | `.tmp/map-load-condumps/20260802-121621/savepaths/12_game_biolabsa/basepr/logs/condumps/12_game_biolabsa.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/12_game_biolabsa/basepr/logs/12_game_biolabsa.log` |
| game/biolabsb | sp | ok | 34.3s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/13_game_biolabsb/basepr/logs/condumps/13_game_biolabsb.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/13_game_biolabsb/basepr/logs/13_game_biolabsb.log` |
| game/superportal | sp | ok | 32.8s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/14_game_superportal/basepr/logs/condumps/14_game_superportal.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/14_game_superportal/basepr/logs/14_game_superportal.log` |
| game/harvestera | sp | ok | 32.9s | U002, U033 | `.tmp/map-load-condumps/20260802-121621/savepaths/15_game_harvestera/basepr/logs/condumps/15_game_harvestera.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/15_game_harvestera/basepr/logs/15_game_harvestera.log` |
| game/harvesterb | sp | ok | 36.2s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/16_game_harvesterb/basepr/logs/condumps/16_game_harvesterb.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/16_game_harvesterb/basepr/logs/16_game_harvesterb.log` |
| game/spindlea | sp | ok | 40.3s | U002, U030, U032 | `.tmp/map-load-condumps/20260802-121621/savepaths/17_game_spindlea/basepr/logs/condumps/17_game_spindlea.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/17_game_spindlea/basepr/logs/17_game_spindlea.log` |
| game/spindleb | sp | ok | 46.2s | U031 | `.tmp/map-load-condumps/20260802-121621/savepaths/18_game_spindleb/basepr/logs/condumps/18_game_spindleb.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/18_game_spindleb/basepr/logs/18_game_spindleb.log` |
| game/girlfriendx | sp | ok | 46.1s | U002, U025 | `.tmp/map-load-condumps/20260802-121621/savepaths/19_game_girlfriendx/basepr/logs/condumps/19_game_girlfriendx.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/19_game_girlfriendx/basepr/logs/19_game_girlfriendx.log` |
| game/lotad | sp | ok | 39.0s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/20_game_lotad/basepr/logs/condumps/20_game_lotad.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/20_game_lotad/basepr/logs/20_game_lotad.log` |
| game/keeperfortress | sp | ok | 45.5s | U012, U013, U026 | `.tmp/map-load-condumps/20260802-121621/savepaths/21_game_keeperfortress/basepr/logs/condumps/21_game_keeperfortress.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/21_game_keeperfortress/basepr/logs/21_game_keeperfortress.log` |
| game/spherebrain | sp | ok | 32.5s | U004, U005, U006, U007, U001 | `.tmp/map-load-condumps/20260802-121621/savepaths/22_game_spherebrain/basepr/logs/condumps/22_game_spherebrain.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/22_game_spherebrain/basepr/logs/22_game_spherebrain.log` |
| game/deathwalk1 | sp | ok | 18.7s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/23_game_deathwalk1/basepr/logs/condumps/23_game_deathwalk1.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/23_game_deathwalk1/basepr/logs/23_game_deathwalk1.log` |
| game/deathwalk2 | sp | ok | 19.3s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/24_game_deathwalk2/basepr/logs/condumps/24_game_deathwalk2.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/24_game_deathwalk2/basepr/logs/24_game_deathwalk2.log` |
| game/deathwalk3 | sp | ok | 19.8s | U010 | `.tmp/map-load-condumps/20260802-121621/savepaths/25_game_deathwalk3/basepr/logs/condumps/25_game_deathwalk3.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/25_game_deathwalk3/basepr/logs/25_game_deathwalk3.log` |
| game/dmescher | mp | ok | 21.4s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/26_game_dmescher/basepr/logs/condumps/26_game_dmescher.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/26_game_dmescher/basepr/logs/26_game_dmescher.log` |
| game/dmescher2 | mp | ok | 21.4s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/27_game_dmescher2/basepr/logs/condumps/27_game_dmescher2.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/27_game_dmescher2/basepr/logs/27_game_dmescher2.log` |
| game/dmgravitylab_6 | mp | ok | 22.2s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/28_game_dmgravitylab_6/basepr/logs/condumps/28_game_dmgravitylab_6.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/28_game_dmgravitylab_6/basepr/logs/28_game_dmgravitylab_6.log` |
| game/dmplanes_4 | mp | ok | 19.8s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/29_game_dmplanes_4/basepr/logs/condumps/29_game_dmplanes_4.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/29_game_dmplanes_4/basepr/logs/29_game_dmplanes_4.log` |
| game/dmroadhouse | mp | ok | 21.8s | U021 | `.tmp/map-load-condumps/20260802-121621/savepaths/30_game_dmroadhouse/basepr/logs/condumps/30_game_dmroadhouse.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/30_game_dmroadhouse/basepr/logs/30_game_dmroadhouse.log` |
| game/dmsalvagewalk | mp | ok | 22.5s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/31_game_dmsalvagewalk/basepr/logs/condumps/31_game_dmsalvagewalk.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/31_game_dmsalvagewalk/basepr/logs/31_game_dmsalvagewalk.log` |
| game/dmshuttle1 | mp | ok | 21.0s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/32_game_dmshuttle1/basepr/logs/condumps/32_game_dmshuttle1.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/32_game_dmshuttle1/basepr/logs/32_game_dmshuttle1.log` |
| game/dmshuttle2 | mp | ok | 20.8s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/33_game_dmshuttle2/basepr/logs/condumps/33_game_dmshuttle2.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/33_game_dmshuttle2/basepr/logs/33_game_dmshuttle2.log` |
| game/dmsphere | mp | ok | 22.6s | U011 | `.tmp/map-load-condumps/20260802-121621/savepaths/34_game_dmsphere/basepr/logs/condumps/34_game_dmsphere.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/34_game_dmsphere/basepr/logs/34_game_dmsphere.log` |
| game/dmtopillogical_4 | mp | ok | 20.3s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/35_game_dmtopillogical_4/basepr/logs/condumps/35_game_dmtopillogical_4.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/35_game_dmtopillogical_4/basepr/logs/35_game_dmtopillogical_4.log` |
| game/dmtunnelrat_8 | mp | ok | 20.2s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/36_game_dmtunnelrat_8/basepr/logs/condumps/36_game_dmtunnelrat_8.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/36_game_dmtunnelrat_8/basepr/logs/36_game_dmtunnelrat_8.log` |
| game/dmwallwalk2 | mp | ok | 22.0s |  | `.tmp/map-load-condumps/20260802-121621/savepaths/37_game_dmwallwalk2/basepr/logs/condumps/37_game_dmwallwalk2.txt` | `.tmp/map-load-condumps/20260802-121621/savepaths/37_game_dmwallwalk2/basepr/logs/37_game_dmwallwalk2.log` |

## Reproduction

From the repository root:

```powershell
py -3 tools\validation\prey_map_load_condump_audit.py --renderer-api gl --basepath "C:\Program Files (x86)\R.G. Mechanics\Prey"
```

Every launch is forced windowed with `+set r_fullscreen 0` and uses the engine `conDump` command.
