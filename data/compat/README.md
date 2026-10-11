# Compatibility data

One TOML file per game; `scripts/compat.py render` turns them into
`docs/COMPAT.md`, and `scripts/compat.py check` fails when that page is stale
or an entry is malformed.

**Rule: every report is something a person looked at on the card's monitor.**
A readback (`rdnuc grab`) or a frame counter is evidence for the notes, never
the rating. A game nobody has watched is `unrated`.

## Tiers

| tier | meaning |
|---|---|
| `perfect` | Looks and plays right, no known problem |
| `playable` | Plays start to finish; a minor glitch or a needed setting |
| `glitches` | Runs, but wrong pictures or crashes that get in the way |
| `menu` | Reaches the menu or the first screen, not the game |
| `broken` | Does not start, or draws nothing |
| `unrated` | Seeded from elsewhere; needs someone to look |

## A game file

```toml
name = "Doom 3 (demo)"          # required
publisher = "id Software / Aspyr (Mac)"
year = 2004                     # required
engine = "id Tech 4"
api = ["OpenGL 1.x", "ARB programs"]   # what it asks of the driver

[[report]]                      # one per test; the newest date is the one shown
date = "2026-10-06"             # required
tier = "playable"               # required, see above
version = "Doom 3 Demo.app"     # the game's own version or patch level
driver = "bundle f4d22db0"      # our bundle hash or commit; "installer 2026-10-09"
gpu = "HD 7570 (Turks PRO)"
machine = "PowerMac11,2"
resolution = "1920x1080"
mode = "fullscreen"             # or "windowed"
fps = 48
baseline_fps = 26               # the GeForce 6600 LE (or other card) on the same test
settings = "ultra, 4x MSAA"
repro = "how someone else gets the same number"
switches = ["RDN_VAR=1"]        # what the game needs from us to run well
issues = ["docs/JOURNAL.md 2026-10-06"]   # where the known problem is written up
notes = "free text"
reporter = "name or handle"
```

The file name is the slug: lower case, digits and dashes.

## Reporting a game

Open an issue with the "Compatibility report" form. The maintainer turns an
accepted report into a `[[report]]` entry and closes the issue with the
commit. A pull request that adds the entry yourself is just as good: run
`scripts/compat.py render` and include `docs/COMPAT.md`.
