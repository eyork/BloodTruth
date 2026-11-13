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

The shader disk cache is on (`pipeline_cache_enabled` in `user/config.json`). It records every pipeline the
game has used, and at every boot all of them are built before the game starts, so a scene you have played
once does not compile again. The launcher empties `user/cache` only when a `SHADPS4_WAVE64*` /
`SHADPS4_WWM*` setting changes (the cache does not know about them) or when a build changes shader
translation (`$shaderCacheVersion` in the launcher). Pick one `SHADPS4_WAVE64_HOIST` value and keep it, or the
cache starts over.

### Sharing the shader cache

A playthrough's cache can be given to other players, so scenes they have never visited do not have to
compile (and lose parts while they do). The export is small: a 555 MB cache zips to about 19 MB.

```powershell
cd <repo>\bloodtruth-launcher
.\shader-cache.ps1 -Export bloodtruth-shaders.zip   # the player who played through
.\shader-cache.ps1 -Import bloodtruth-shaders.zip   # anyone else; prints the settings to start with
```

- Importing on top of a cache from the same GPU and settings adds the new pipelines to it. A cache from a
  different GPU or other settings is moved to `user/cache-backup-<date>`, not deleted.
- The cache holds shaders already translated for the GPU that made it (`profile.bin`). It helps players with a
  GPU that translates the same way, in practice the same vendor and family (for example two recent NVIDIA
  cards). On any other GPU the emulator notices at boot and starts a fresh cache of its own.
- Start the game with the same `SHADPS4_WAVE64*` settings the export was made with (the import prints them),
  or the launcher clears the cache.

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
| `SHADPS4_AUDIO3D_MIXDOWN` | `0` | `1` plays every 3D sound object in both ears at its own level, with no direction or head shadow. Use it if voices or effects are hard to hear |
| `SHADPS4_AUDIO3D_OBJECT_GAIN` | `1` | volume of the 3D sound objects (voices, effects) relative to the music/ambience bed, 0-8; e.g. `1.5` |
| `SHADPS4_AUDIO3D_HEAD_TRACKING` | `1` | `1` keeps 3D sounds in place in the world as you turn your head (the game places them around your body; the PS4 applied the head turn). `2` also turns the 5.1/7.1 bed with your head (radio and music then move too). `0` = sounds turn with your head |
| `SHADPS4_AUDIO3D_TRACE` | `0` | `1` logs where the loudest 3D sound objects are (needs `Lib.Audio3d:Info` in the log filter) |

3D sound objects (voices, effects) are placed around the head with a head model (`audio3d.cpp`,
`audio3d_spatializer.cpp`); the speaker-array functions the game's Wwise engine uses are implemented.
The game's 5.1/7.1 speaker bed is played in full: centre and LFE in both ears, surround channels placed to
the sides and behind (`SHADPS4_AUDIO3D_MIXDOWN=1` folds them flat instead).

### How the game's sound reaches you

- The game's Wwise engine sends two things through Audio3d: **3D objects** (voices, gunfire, effects, each with
  a position) and a **5.1/7.1 speaker bed** (music, ambience, and the centre channel where radio/comms dialogue
  usually sits).
- **Virtual Desktop and the Quest just carry stereo.** Neither places sounds around you; the emulator turns
  everything into a left-ear/right-ear signal with a head model, and that stereo is what streams to the headset.
  Don't enable Windows Sonic or Dolby Atmos for Headphones on top: that would process it twice.
- **Head tracking:** the game gives object positions relative to your body, not your head (on a PS4 the system
  applied the head's turn). Checked in the Meta XR Simulator: after a 100-degree head turn the game's positions
  did not change at all. The emulator now rotates every object by the headset's orientation, so a sound in front
  moves to your right ear when you look left. "Straight ahead" is where you faced when the game started; hold
  OPTIONS in the game to recenter.
- **Earlier bug (fixed):** only the bed's front left/right channels were played, so radio voices (centre) and
  sounds behind you (surrounds) were missing.

### If the sound is wrong

| Symptom | Try |
|---|---|
| Voices or effects too quiet next to music | `SHADPS4_AUDIO3D_OBJECT_GAIN=1.5` |
| Some sounds missing or muffled from one side | `SHADPS4_AUDIO3D_MIXDOWN=1` (no direction; if they are still missing, the game is not sending them) |
| Sounds move with your head | check `SHADPS4_AUDIO3D_HEAD_TRACKING` is not `0`; recenter with OPTIONS |
| Want the ambience to stay in the room too | `SHADPS4_AUDIO3D_HEAD_TRACKING=2` (radio and music will move as you turn) |
| Sound directions mirrored or rotated | report it with `SHADPS4_AUDIO3D_TRACE=1` and the log |

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
