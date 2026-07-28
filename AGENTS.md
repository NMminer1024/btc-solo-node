# Project Memory

- Primary board: `Rockbase`
- Primary defconfig: `k230_rockbase_defconfig`
- Web root for the Rockbase image: `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web`
- Debug workflow rule: for every feature or page change, first hot-deploy the changed files to the live board with `scp`/`ssh` and validate online behavior there.
- Solidification rule: only after the live hotfix passes, write the same change back into the image source in this repo and then refresh the build output/image.
- Do not reverse this order unless live hot-deploy debugging is impossible for that specific change.
- Business-code change tracking rule: during hot debugging, keep track of every changed business file so it can be solidified into the image source later without guessing.
- "Bake into image" rule: when the user asks to "打进镜像", only solidify the already validated hot-debug changes into the image source. Do not compile or export a new image unless the user explicitly asks to compile/build.
