# Project Memory

- Primary board: `Rockbase`
- Primary defconfig: `k230_rockbase_defconfig`
- Web root for the Rockbase image: `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web`
- Debug workflow rule: for every feature or page change, first hot-deploy the changed files to the live board with `scp`/`ssh` and validate online behavior there.
- Solidification rule: only after the live hotfix passes, write the same change back into the image source in this repo and then refresh the build output/image.
- Do not reverse this order unless live hot-deploy debugging is impossible for that specific change.
- Business-code change tracking rule: during hot debugging, keep track of every changed business file so it can be solidified into the image source later without guessing.
- "Bake into image" rule: when the user asks to "打进镜像", only solidify the already validated hot-debug changes into the image source. Do not compile or export a new image unless the user explicitly asks to compile/build.
- Seed image rule: default to the non-seed image path for rebuilds and exports. Only build/export the seed-backed image when the user explicitly requests a seed image.
- WSL `/mnt/d` mount rule: sandboxed Codex commands may see `/mnt/d` as `ro` even when the real WSL mount is `rw`; before concluding Windows export is blocked by read-only mount, verify with an escalated/non-sandbox `findmnt` plus a real write test.
- Rockbase CMA test memory: 2G with CMA disabled caused `8189fs` WiFi Oops around `wiphy_register`; 4M CMA booted but WiFi registration failed; 64M CMA is the current working baseline for both 1G and 2G. If revisiting CMA minimization, test 32M next, then 16M if stable; use 128M only if 64M shows runtime DMA/WiFi instability.
