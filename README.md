# RedCat Spookkasteel, Nintendo DS version

A separate DS version of RedCat Spookkasteel, built from the data of the main port in `../RCSPOOK_NEW`.
That folder is only read; nothing here changes it. The ROM is `redcat_ds.nds`. Version 0.1.0; see `CHANGELOG.md`.

## Controls

| Button | Action |
| --- | --- |
| D-pad up/down | Walk forward/back |
| D-pad left/right | Turn |
| L / R | Step sideways |
| A | Jump |
| B | Shoot, once a fairy has given RedCat that skill. Holding it locks onto the marked target (enemy, shootable button or crate) and turns RedCat to it. With the charge skill: hold and release |
| X / Y | Look up/down |
| Touch screen | Drag to turn |
| Start | Skip the running cutscene, otherwise pause (Select in the pause screen returns to the menu) |

## Build

The SDK is BlocksDS from the Wonderful toolchain, installed in `/opt/wonderful` in the Debian container.

```sh
. ./env.sh
python3 tools/convert_level.py     # levels, actors, scripts: writes nitrofiles/
python3 tools/convert_assets.py    # sound and the bottom screen picture
make BENCH=0                       # BENCH=1: FPS tour of all levels; BENCH=2: script stress test
```

After changing the `BENCH` value, run `touch source/*.c`: make does not see the flag change.

Two more builds are test tools:

- `BENCH=3` steps through the camera poses in `test/poses.h` with the A key (`test/pose-test.sh`), for side-by-side views
  with the main port. `tools/make_poses.py` writes that table from the poses the main port's scene script saved.
- `BENCH=4` starts a level directly: X skips the cutscene and Y jumps to a named object. Set it with
  `SCENE='-DSCENE_OBJECT=\"csmc01_tr\" -DSCENE_FACING=9 -DSCENE_LEVEL=0 -DSCENE_ABOVE=6'`.

## Measured in melonDS 1.1

`make BENCH=1` tours all five levels past their enemies, with RedCat shooting, scripts and enemies active.
FPS is counted in emulated VBlanks, so a slow host does not change it. Frames after the first second of a level;
polygons are the hardware's own count of what it rendered:

| Level | FPS | Late frames | Most polygons rendered |
| --- | ---: | ---: | ---: |
| Het Bos | 30.0 | 1 | 1041 |
| Het Kasteel | 30.0 | 0 | 2012 |
| Het Kerkhof | 29.9 | 1 | 1296 |
| De Grotten | 30.0 | 1 | 1670 |
| De Toren | 30.0 | 0 | 1129 |

The late frames coincide with the tour skipping a cutscene it walks into (up to 934 ms in one frame), which completes
the scene's timeline at once. Pressing Start in a cutscene costs the same single frame.

`make BENCH=2` fires every trigger, door and button of each level: no crashes and no unknown script commands, with
2.0 to 3.5 MB of the 4 MB main memory in use.

**The tables above predate the latest features**, which were compiled but not run. **Not tested:** a real DS or DS Lite, and a full playthrough of any level. Melon's timing is close to hardware, not identical.
Scenes were checked in melonDS against the main port for the openings of all levels and the first fairy encounter.


## How it reaches 30 FPS

- **Display lists:** world geometry is compiled into packed GX display lists per grid cell, sent by DMA.
- **Visibility:** the original BSP PVS selects cells; frustum and draw distance cull the rest. Fog hides the limit.
- **Polygon budget:** back faces and polygons off screen do not use the hardware's 2048, so up to 6000 polygons are sent
  per frame, nearest first. What is left is actually rendered: 2010 at most in the tour above.
- **Lighting:** the world's lightmaps are baked into vertex colors; dynamic lights are left out.
- **Actors:** at most 190 polygons, with a second, lighter model beyond 320 units, and at most 25 bones in the hardware
  matrix stack, so no CPU skinning. RedCat keeps all 556 polygons, 28 bones and a 256x256, 256-colour texture. The nearest
  objects in view get 700 polygons on top of him.
- **Animation:** RedCat 10 frames per second, everything else 6, interpolated.
- **Textures:** at most 64x64 with 16 colors, sprites 32x32 with 8 colors and alpha.

## Taken from the main port

- **Level scripts:** the original compiled scripts run on a small VM (`source/script.c`), with real-time cutscenes,
  their script cameras and dialogue with subtitles and voices. Start skips a scene by completing its timeline in order.
- **Fleuriefee:** flight, orbiting stars, sparks and the appear, idle and leave sounds follow `NativeFairyEffect`, with
  fewer sprites.
- **Model sizes:** each actor's `Scale`, rotation order, bind pose and turning, and hiding with the brush it stands on.
- **Moving brushes:** platforms carry RedCat and push him aside; a blocked mover waits. MoveTo and SetTo on controllers.
- **Rules:** exact door, button and trigger contact, wind, ooze and death liquids, landing damage, respawn with five
  seconds of protection, shooting gated by skill, shots on brushes, items resting on what is below them, surface decals.
- **Camera:** the original's 54 degrees and its first camera preset; cinema bars in cutscenes.
- **Targeting:** nearest enemy or crate in front within 480 units, shootable buttons after them at nine times that, refreshed
  twice a second, with the rotating marker, the lock while firing and the target's name and health on the bottom screen.
- **Shooting:** the shot leaves at 46% of the shoot motion and the next can start at 80%, with the three shot types' own
  speeds, damage, sounds and impacts per difficulty.
- **Sound:** the main port's choices: door cues only on real door panels (DoorType, with a script's own cue replacing it),
  button types, lock and error cues, footsteps, landing, hurt strengths, per-item pickups, secret found, enemy idle, alert,
  attack, hurt and death sounds, boss voices, positioned one-shots and the two nearest ambience loops.
- **Enemies:** sight with range, view cone, memory and the 500 ms test; stats per difficulty; patrols over the level's
  waypoints; hanging spiders, skeleton piles and stone knights and gargoyles that wake and return; zombies rising from
  graves; touch pursuit with once-a-second contact damage; salvos and retreat distance; hit reactions; projectile types
  with gravity and homing. Dungeon Max, Jester Max and Grizella run their own state machines, including teleports.
- **Effects:** torch flames and smoke, coronas with a sight check, beams including their damage and events, save point
  beacons, teleporter sparks, flash bursts, explosions with blast damage that chains through props, enemy smoke, score
  cards, pickup sparks, shot impacts, RedCat's ground shadow.
- **Game flow:** difficulty choice, level overview with the 15-point bonus, retry screen, combat and boss music, secrets,
  the hedge maze's reset after dying, respawn clip and full player animation set including strafing.
- **Saving:** at every save point to `redcat_ds.sav` on the card's FAT system, plus the five best scores, with "Verder
  spelen" in the menu. It needs a flash card, or melonDS with DLDI turned on; without one the game runs without saving.

## What differs from the PC game

- No intro movies, level-select locks, help pages, cheats, on-screen touch controls or settings menu.
- Enemies walk in straight lines and along waypoint links, without the main port's full route search.
- Boss machinery that is separate art (Dungeon Max's turret motions) and knights' flying armour parts are left out.
- No dynamic lights, so gargoyles and skeletons look greyer near torches. Sprites are not animated frame by frame.
- Level sounds are cut to two seconds, music to sixty, both at a lower sample rate, to fit the 4 MB of memory.
- The follow camera has no line-of-sight search; RedCat is not drawn when a wall puts the view inside him.
- Rotating brush models collide with an axis-aligned player box in model space.
