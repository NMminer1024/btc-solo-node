# Project Memory

- Primary board: `Rockbase`
- Primary defconfig: `k230_rockbase_defconfig`
- Web root for the Rockbase image: `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web`
- Debug workflow rule: for every feature or page change, first hot-deploy the changed files to the live board with `scp`/`ssh` and validate online behavior there.
- Solidification rule: only after the live hotfix passes, write the same change back into the image source in this repo and then refresh the build output/image.
- Do not reverse this order unless live hot-deploy debugging is impossible for that specific change.

