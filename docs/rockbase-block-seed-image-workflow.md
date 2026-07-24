# Rockbase Block Seed Image Workflow

## 1. Purpose

This document defines the Rockbase image-build workflow for bundling an external pruned Bitcoin data seed into the final `sysimage-sdcard.img`.

The seed data stays outside this git repo and is injected only during image build.

## 2. Default Seed Path

Rockbase `post-build.sh` now checks this path by default:

- `/mnt/d/code/pony/IoT/k230/img/seed/current/root-block`

You can override it for one build with either environment variable:

- `BLOCK_SEED_DIR=/your/path`
- `ROCKBASE_BLOCK_SEED_DIR=/your/path`

`BLOCK_SEED_DIR` takes precedence.

## 3. Expected Seed Layout

The external seed directory must contain at least:

```text
/mnt/d/code/pony/IoT/k230/img/seed/current/root-block/
  blocks/
  chainstate/
```

Optional files and directories may also be present, for example:

```text
  indexes/
  peers.dat
  banlist.json
  fee_estimates.dat
```

Runtime garbage should not be kept in the seed. The build step will delete these if found after copy:

- `.lock`
- `.cookie`
- `bitcoind.pid`
- `LOCK`

## 4. Daily Update Workflow

1. Stop `bitcoind` cleanly on the source device.
2. Remove the source SD card and mount it on the host.
3. Copy `/root/block` from the source card into a fresh directory under:
   - `/mnt/d/code/pony/IoT/k230/img/seed/releases/YYYYMMDD-HHMMSS/root-block`
4. After the copy completes, update:
   - `/mnt/d/code/pony/IoT/k230/img/seed/current/root-block`

Recommended rule:

- Do not overwrite `current/root-block` in place while a build may be reading it.
- Prepare a full new release directory first, then replace `current/root-block` atomically.

## 5. Build Behavior

During Rockbase image build, `post-build.sh` does the following:

1. Checks whether the external seed path exists.
2. Skips injection if the directory is missing or empty.
3. Fails the build if the directory is non-empty but lacks `blocks/` or `chainstate/`.
4. Copies the seed into:
   - `output/k230_rockbase_defconfig/target/root/block`
5. Removes runtime lock and pid files.
6. Lets Buildroot pack that target root into `rootfs.ext4`.
7. Lets `post-image.sh` assemble the final:
   - `sysimage-sdcard.img`

## 6. Outputs

Intermediate output after seed injection:

- `output/k230_rockbase_defconfig/target/root/block`

Packed root filesystem:

- `output/k230_rockbase_defconfig/images/rootfs.ext4`

Final flash image:

- `output/k230_rockbase_defconfig/images/sysimage-sdcard.img`

Exported image:

- `/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-YYYYMMDD-HHMMSS/sysimage-sdcard.img`

## 7. Capacity Warning

The current Rockbase rootfs size in defconfig is still the normal small-image setting.

That is fine while the external seed directory is empty and this framework is only being staged.

Before enabling a real pruned seed build, you must increase `BR2_TARGET_ROOTFS_EXT2_SIZE` enough to hold:

- the full `/root/block` seed
- the root filesystem itself
- extra safety margin

For a `~17G` seed, start from `24G` rather than sizing too tightly.
