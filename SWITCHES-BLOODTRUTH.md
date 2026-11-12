# Blood & Truth VR: switches

Set these in PowerShell before starting the launcher, in a new window each time (a window keeps what was set
in it earlier):

```powershell
cd <repo>\bloodtruth-launcher
$env:SHADPS4_BT_NO_GPU_CULLING = '0'
$env:SHADPS4_WAVE64_HOIST      = '2'
$env:SHADPS4_VR_FOV            = '112'
$env:SHADPS4_VR_SHARPEN        = '0'
.\launch-bloodtruth.ps1 -GamePath '<game>\uroot\eboot.bin' -NoGui -EyeUpscale 1.5 -Resolution High
```

That is the recommended setup today. Everything below is optional.

## Launcher parameters

| Parameter | Values | What it does |
|---|---|---|
| `-GamePath` | path to `eboot.bin` | Blood & Truth CUSA11108 1.00 (the launcher checks the hash) |
| `-NoGui` | switch | start without the settings window |
| `-Resolution` | `Original` (default), `High`, `Ultra8x` | the game's render size per eye: game-controlled, 1.5x each axis (1440x1632), 2.83x each axis |
| `-EyeUpscale` | `Auto`, `1`, `1.5`, `2` | FSR 1 enlargement of each finished eye picture before the headset (no new detail, sharper than the compositor's stretch) |
| `-MirrorFps` | `30` (default), `60`, `0` | desktop mirror window rate (`0` = uncapped) |

## Culling and wave64 (the "objects vanish" fixes)

The game culls its geometry on the GPU with compute shaders written for the PS4's 64-wide waves. NVIDIA runs 32
wide; these switches control how the emulator makes those shaders behave as on a PS4.

| Switch | Default | What it does |
|---|---|---|
| `SHADPS4_BT_NO_GPU_CULLING` | `1` (game's GPU culling off) | `0` lets the game cull on the GPU: far fewer draws, much higher fps. Needs a wave64 fix below or objects vanish |
| `SHADPS4_WAVE64_HOIST` | `0` | `1` fixes the culling votes; `2` also fixes whole-wave min/max (recommended). `0` = upstream behaviour |
| `SHADPS4_WAVE64_PREDICATE` | `0` | `1` = alternative fix ported from PS4Recomp. Works but slow (~7 fps) and flickers: not recommended |
| `SHADPS4_WAVE64_HOIST_ONLY` / `SHADPS4_WAVE64_HOIST2_ONLY` / `SHADPS4_WAVE64_PREDICATE_ONLY` | unset | comma-separated shader hashes: apply that fix only to these shaders (for bisecting) |
| `SHADPS4_WAVE64_FORCE_NONUNIFORM`, `SHADPS4_WAVE64_HALF_READLANE`, `SHADPS4_WWM_ENTER` | `0` | diagnostics from the culling hunt; leave off |

## Performance

| Switch | Default | What it does |
|---|---|---|
| `SHADPS4_ASYNC_PIPELINES` | on | new graphics pipelines (shaders) compile on worker threads instead of freezing the game. `0` = old behaviour |
| `SHADPS4_ASYNC_PIPELINE_SKIP` | on | while a pipeline compiles, skip that object's draw if its render target is drawn every frame (one-off, full-screen, depth-only and indirect draws always wait). `0` = never skip, only compile in parallel |
| `SHADPS4_ASYNC_PIPELINE_THREADS` | cores / 4, 1-4 | number of compile threads (1-8) |
| `SHADPS4_DMA_SYNC_SKIP` | `1` | skip re-checking GPU-visible memory per draw when nothing changed. `0` = check every draw (slow) |
| `SHADPS4_DMA_SYNC_STATS` | `0` | `1` logs how often that check ran |
| `SHADPS4_TITLE_RES_LOCK` | `1` (launcher) | keeps the game's adaptive resolution at the top by capping its GPU frame-time stamps; `_MS` (default 2) and `_STRIDE` (launcher 16) tune it |
| `SHADPS4_HMD_END_EVENT_DELAY_US` | `2000` (launcher) | delay between the headset's reprojection start/end events; fixes the game counting every second refresh (18 -> 31+ fps) |
| `SHADPS4_SUBMIT_ORDER`, `SHADPS4_ASC_RELEASE_FINISH`, `SHADPS4_DISPATCH_BARRIER` | `0` | diagnostics from the culling hunt (ordering and barrier experiments); leave off |

The shader disk cache is on (`pipeline_cache_enabled` in `user/config.json`). The launcher empties
`user/cache` when `shadps4.exe` or any `SHADPS4_WAVE64*` / `SHADPS4_WWM*` setting changes, because the cache
does not know about them; the first run after that compiles again.

## Picture

| Switch | Default | What it does |
|---|---|---|
| `SHADPS4_BT_VR_SCALE` | from `-Resolution` | the game's resolution ceiling (1.0-3.0); the launcher sets it |
| `SHADPS4_VR_UPSCALE` | from `-EyeUpscale` | FSR 1 eye enlargement factor |
| `SHADPS4_VR_SHARPEN` | `0.3` (launcher) | FSR sharpening strength; `0` = none |
| `SHADPS4_VR_FOV` | `100` (launcher) | % of the headset's field of view the game draws; `112` fills a Quest 2 better |
| `SHADPS4_VR_FOV_OF` | `headset` | what the FOV percentage refers to |

## Audio

| Switch | Default | What it does |
|---|---|---|
| `SHADPS4_VIRTUAL_SURROUND` | on with a headset | renders the game's 7.1 mix for headphones (virtual speakers, limiter). `0` = plain fold to stereo |
| `SHADPS4_AUDIO3D_TRACE` | `0` | `1` logs where the loudest 3D sound objects are (needs `Lib.Audio3d:Info` in the log filter) |

3D sound objects (voices, effects) are placed around the head with a head model (`audio3d.cpp`,
`audio3d_spatializer.cpp`); the speaker-array functions the game's Wwise engine uses are implemented.

## VR runtime (from evertec's build, unchanged)

`SHADPS4_VR`, `SHADPS4_OPENXR` (launcher sets both), `SHADPS4_XR_*` (controllers, pacing, test input),
`SHADPS4_VR_*` (window, refresh, desktop view, turning), `SHADPS4_MOVE_SWAP_HANDS`,
`SHADPS4_BLOODTRUTH_DMEM_ALIAS` (launcher sets `1`). See `launch-bloodtruth.ps1` and `src/core/vr/`.

## Contributors

- **eyork** (github.com/eyork): GPU culling / wave64 fixes, GPU command-thread speedups, background pipeline
  compilation, 3D audio, VR resolution and pacing work, this document. Written with AI assistance (Claude Code).
- Built on **evertec82**'s Blood & Truth PC VR prototype (github.com/evertec82/BloodTruth) and shadPS4.
- Ideas and code adapted from: the AstroQuest shadPS4 fork (Audio3d object spatializer), the Wipeport PSVR
  port (background pipeline compilation), PS4Recomp (wave64 predication pass).

## Known problems

- Some objects can still blink or go missing in places (doors, the photos on the interrogation whiteboard)
  with GPU culling on. `SHADPS4_BT_NO_GPU_CULLING=1` shows everything at a much lower fps.
- The first time an area is visited, new shaders still cause short hitches (shorter with async pipelines);
  later visits use the disk cache.
