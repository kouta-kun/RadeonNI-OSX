Patches that measure or try something and are not part of the build (mesa/patches/ is).
Apply by hand to the patched Mesa tree (after everything in mesa/patches/) to repeat the experiment.

- 0024-experiment-glthread-upload-stats.patch: RDN_UPLOAD_STATS=1 writes /tmp/rdn-upload.txt: what glthread copies for
  client arrays and how much of it is byte-identical to the last copy from the same address (The Sims 2: 88 %).
- 0027-experiment-glthread-upload-cache.patch (needs 0024): RDN_UPLOAD_CACHE=full|sample keeps copied data in chunks and
  reuses it for an unchanged copy from the same address. No gain: see docs/agents/accel-state.md.
