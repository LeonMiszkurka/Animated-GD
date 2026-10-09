# Animated GD

A [Geode](https://geode-sdk.org) mod for Geometry Dash **2.2081** (Geode **5.3.0**) that adds animated portals, orbs and pads, plus gamemode transitions. See [about.md](about.md) for the feature list.

## Building on macOS

One-time setup:

```sh
xcode-select --install                     # Apple's compiler toolchain
brew install cmake ninja
brew install geode-sdk/geode/geode-cli     # Geode CLI
geode sdk install                          # sets GEODE_SDK (restart your shell afterwards)
geode sdk install-binaries
```

Then build and install into Steam GD:

```sh
./build.sh
```

Restart Geometry Dash and the mod shows up in the Geode mods list.

## Building with GitHub Actions

Push this folder to a GitHub repo. [.github/workflows/build.yml](.github/workflows/build.yml) builds macOS and Windows versions and uploads the `.geode` file as the **Build Output** artifact. Drop that file into `Geometry Dash.app/Contents/geode/mods`.

## How it works

- [src/main.cpp](src/main.cpp): hooks. `PlayLayer::setupHasCompleted` creates the effect node, `PlayLayer::resetLevel` clears it, and `EnhancedGameObject::activatedByPlayer` (with `PlayerObject::ringJump` and `GJBaseGameLayer::gravBumpPlayer` as backups) reports orb, pad and portal hits.
- [src/AnimFX.cpp](src/AnimFX.cpp): one node in the object layer that drives everything per frame:
  - idle overlays for on-screen portals, orbs and pads, created and destroyed as they scroll in and out of view
  - a small hand-rolled particle system for hit bursts
  - player-state polling (gamemode, gravity, size, dual) that triggers the transitions
- [src/Textures.cpp](src/Textures.cpp): glow, ring, dashed ring and spark textures generated in code, so the mod ships no image assets.

Effects are additive overlays and never change GD's collision data. The orb and pad bounce only changes the sprite's visual scale, through `CCSprite::setScaleX/Y`, and is restored afterwards.
