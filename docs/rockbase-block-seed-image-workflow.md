# Rockbase Block Seed Image Workflow

## 1. Purpose

This document defines the Rockbase image-build workflow for bundling an external pruned Bitcoin data seed into the final `sysimage-sdcard.img`.

The seed data is stored in a git-ignored repo-local directory and is injected only during image build.

## 2. Default Seed Path

Rockbase `post-build.sh` now checks this path by default:

- `seed/root-block`

You can override it for one build with either environment variable:

- `BLOCK_SEED_DIR=/your/path`
- `ROCKBASE_BLOCK_SEED_DIR=/your/path`

`BLOCK_SEED_DIR` takes precedence.

## 3. Expected Seed Layout

The external seed directory must contain at least:

```text
seed/root-block/
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
3. Copy the source Bitcoin data directly into:
   - `seed/root-block`

Recommended rule:

- Do not copy directly into `seed/root-block` by hand while a build may be reading it.
- Use `tools/copy-rockbase-seed-from-sd.sh`; it copies into a temporary directory, validates LevelDB manifest files, then replaces `seed/root-block`.
- The default source path is the RDK X5 layout: `/mnt/sdcard/home/sunrise/Desktop/bitcoind/data`.
- If the source card is a K230 layout, pass the source path explicitly: `tools/copy-rockbase-seed-from-sd.sh /mnt/sdcard/root/block`.

Helper script:

- `tools/init-rockbase-seed.sh`
- `tools/copy-rockbase-seed-from-sd.sh`
- `tools/attach-sd-to-wsl.ps1`
- `tools/mount-sdcard-in-wsl.sh`
- `tools/umount-sdcard-in-wsl.sh`
- `tools/detach-sd-from-wsl.ps1`

This creates the git-ignored seed directory:

- `seed/root-block`

## 5. Attach the Source SD Card to WSL On Demand

The SD card is not bridged into WSL automatically just because it is inserted into Windows.

Recommended flow:

1. Insert the USB card reader into Windows.
2. In Windows PowerShell, run:
   - `powershell -ExecutionPolicy Bypass -File .\tools\attach-sd-to-wsl.ps1`
3. In WSL, mount the ext4 partition read-only:
   - `sudo tools/mount-sdcard-in-wsl.sh`
4. After the copy finishes, unmount in WSL:
   - `sudo tools/umount-sdcard-in-wsl.sh`
5. Then detach in Windows PowerShell:
   - `powershell -ExecutionPolicy Bypass -File .\tools\detach-sd-from-wsl.ps1`

Notes:

- `attach-sd-to-wsl.ps1` relaunches itself with Administrator privileges when needed.
- If multiple USB storage devices are connected, pass the explicit bus id:
  - `powershell -ExecutionPolicy Bypass -File .\tools\attach-sd-to-wsl.ps1 -BusId 6-2`
- `mount-sdcard-in-wsl.sh` mounts the detected ext4 partition with `ro` by default for seed-copy safety.

## 6. Build Behavior

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

## 7. Outputs

Intermediate output after seed injection:

- `output/k230_rockbase_defconfig/target/root/block`

Packed root filesystem:

- `output/k230_rockbase_defconfig/images/rootfs.ext4`

Final flash image:

- `output/k230_rockbase_defconfig/images/sysimage-sdcard.img`

Exported image:

- `/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-YYYYMMDD-HHMMSS/sysimage-sdcard.img`

## 8. Capacity Warning

The current Rockbase rootfs size in defconfig is still the normal small-image setting.

That is fine while the external seed directory is empty and this framework is only being staged.

Before enabling a real pruned seed build, you must increase `BR2_TARGET_ROOTFS_EXT2_SIZE` enough to hold:

- the full `/root/block` seed
- the root filesystem itself
- extra safety margin

For a `~17G` seed, start from `24G` rather than sizing too tightly.
