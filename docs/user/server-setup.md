# Server Setup Guide

This guide covers a simple way to host an openPREY dedicated server.

## What You Need

- A working openPREY install
- Access to the original Prey (2006) retail assets
- The `openPREY-ded_<arch>` executable from your openPREY release or local build
- A 64-bit host that matches the package architecture

## Dedicated Server Requirements

The dedicated server does not need a GPU or an OpenGL-capable desktop session, but it still needs the same retail `base/` assets and matching openPREY game modules as the client.

For light testing, plan on a modern 64-bit host with at least 2 GB RAM available to the operating system and server, plus the same package and Prey (2006) asset storage described in the [Getting Started system requirements](getting-started.md#system-requirements). For public servers, prefer 4 GB+ RAM, a stable wired connection, and enough upload bandwidth for the player count you advertise.

## Quick Start

1. Make sure openPREY can see your Prey (2006) assets.
2. Launch the dedicated server executable.
3. Set a server name, map, and game type.
4. Start the server with `spawnServer`.

Example startup flow:

```text
openPREY-ded_x64 +set si_name "My openPREY Server" +set si_map game/dmescher +set si_gameType deathmatch +spawnServer
```

## Common Server Variables

| Variable | What it controls |
|---|---|
| `si_name` | Server name shown to players |
| `si_map` | Starting map |
| `si_gameType` | Multiplayer game type |
| `si_fragLimit` | Frag limit |
| `si_timeLimit` | Time limit |
| `si_warmup` | Whether warmup is used |
| `g_mapCycle` | Map cycle script |

Default multiplayer values are seeded from `content/basepr/pak0/default.cfg`.

## Useful Console Commands

| Command | What it does |
|---|---|
| `spawnServer` | Starts the server |
| `disconnect` | Shuts the server down |
| `serverMapRestart` | Restarts the current map |
| `serverNextMap` | Advances to the next map |
| `kick` | Kicks a client by slot number |
| `gameKick` | Kicks a client by player name |

> [!NOTE]
> The inherited bot and repeater systems are intentionally default-off while their
> extended OpenQ4 game-API calls are adapted to Prey's unified v7 game API.

## Multiplayer Tuning

If you want to tune prediction or lag compensation behavior, see [Multiplayer Networking](multiplayer-networking.md).

## Notes

- openPREY uses its own engine and game modules.
- openPREY is not a drop-in runtime for the original proprietary Prey (2006) DLL mods.
- For advanced configuration, file layout, and path behavior, see [TECHNICAL.md](../../TECHNICAL.md).
