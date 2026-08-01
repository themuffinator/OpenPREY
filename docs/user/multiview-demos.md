# Multi-View Demos

Multi-view demo (MVD) recording and playback are intentionally unavailable in the
current openPREY compatibility baseline.

The inherited implementation depends on extended OpenQ4 game-API calls and a schema
that is not part of Prey's unified v7 game API. Those paths are default-off so they
cannot silently call incompatible game methods or write recordings that claim the
wrong schema.

Render demos and command demos remain separate engine facilities, but they also require
runtime validation against Prey assets before compatibility is claimed.

Developer tracking: [TODO-D9 in the rebase status ledger](../dev/prey-rebase/status-ledger.md#explicit-deferral-register).
