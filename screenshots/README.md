# Screenshots

None yet. Per `AGENTS.md`'s Definition of Done, hardware verification comes
before screenshot capture, and this app hasn't been exercised on hardware
yet.

When captured, they'll follow bleepsentry's method: read the 128x64
page-packed framebuffer over the RPC screen stream
(`flipperzero_protobuf`, `rpc_gui_snapshot_screen`), render at 4x with the
catalog's exact two-colour pair (black `(0,0,0)` ink on Flipper orange
`(254,138,44)` paper), driving each state with RPC input events.
