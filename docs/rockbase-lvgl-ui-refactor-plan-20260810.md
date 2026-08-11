# Rockbase LVGL UI Refactor Plan - 2026-08-10

## Background

The Rockbase LCD UI became visibly less smooth after real monitoring data was connected. The current implementation keeps LVGL `tileview`, page snapshot caching, software rotation, DRM buffer presentation, touch handling, and status parsing in one `main.cpp` loop. Two small hotfix attempts reduced unnecessary label updates and deferred status refreshes while touching, but the user still observes jitter inside page objects during horizontal swipes.

The remaining issue is architectural: during interaction, the display path can mix pre-rendered page cache composition with LVGL live object rendering. That makes text/cards appear unstable even when the page itself does not jump.

## Product Requirement

- Four horizontal tiles/pages.
- Each tile has a full-screen background image.
- Each tile overlays a small number of monitoring values.
- Data refresh frequency is page/field dependent:
  - Clock can refresh every 1 second.
  - Bitcoin/system/network values can refresh every 2-5 seconds by default.
  - High-frequency values should be opt-in and isolated to the active page.
- Horizontal swipe must feel smooth and stable.
- During a swipe, page contents should not jitter, relayout, or visually update mid-transition.

## Design Principles

- Swipe rendering and data rendering must be separate paths.
- Touch/swipe should compose already-rendered page buffers only.
- LVGL may be used to lay out and render a page, but it should not drive page movement.
- File IO and JSON parsing must not run in the display critical path.
- Data updates during swipe should be queued and applied after the swipe settles.
- Page buffers should be kept in the physical display orientation where possible, avoiding per-frame full-screen rotation.

## Target Architecture

### Data Model

Introduce a `UiModel` snapshot generated from `/tmp/status-api.json` and local time. The data refresh path owns file IO, JSON parsing, formatting, and per-page dirty decisions. The display loop consumes only an already-parsed model.

### Page Renderers

Each page has a renderer that owns its LVGL objects and a cached full-page buffer:

- `BitcoinPage`
- `StratumPage`
- `SystemPage`
- `NetworkPage`

Renderers update LVGL labels only when relevant model values change. They rebuild their page cache only when their page is dirty and the pager is idle.

### Pager Compositor

Replace LVGL `tileview` movement with a custom pager:

- Touch input calculates `visual_scroll_x`.
- During touch/settle animation, compositor copies from adjacent page buffers into the DRM buffer.
- No LVGL object flush is allowed to present during swipe.
- After release, a short deterministic settle animation snaps to the selected page.

### Display Backend

Display should present physical-orientation buffers directly. If DRM hardware rotation cannot be used reliably, page cache generation should pre-rotate into the physical `480x800` layout so swipe composition can use row/column copies without invoking LVGL full-screen rotation every frame.

## Migration Plan

### Phase 1 - Model And Refresh Isolation

- Introduce `UiModel`, `PageDirtyMask`, and refresh scheduling helpers.
- Keep current visual layout.
- Keep current page cache mechanism.
- Ensure status file parsing and per-page dirty decisions are isolated from widget update code.
- Hot deploy to `192.168.124.28`.

### Phase 2 - Disable LVGL Tile Movement

- Stop using LVGL `tileview` as the swipe compositor.
- Keep LVGL pages only as offscreen/static page render sources.
- Handle swipe state with custom touch tracking.
- Compose cached physical page buffers during touch.
- Hot deploy and verify object jitter is gone.

### Phase 3 - Page Renderer Split

- Split the monolithic `main.cpp` page construction/update sections into page-level modules.
- Each page exposes:
  - `build()`
  - `apply_model()`
  - `render_cache()`
  - `cache_dirty()`
- Preserve existing visual output.

### Phase 4 - Physical Buffer Rendering

- Store page caches in physical display orientation.
- Remove per-frame logical landscape to physical portrait rotation from the swipe path.
- Keep LVGL rotation only for occasional offscreen cache generation if necessary.

### Phase 5 - Cleanup And Metrics

- Remove transitional screenshot/blend code that is no longer needed.
- Keep only low-rate FPS/latency logs for diagnosis.
- Add clear logs for:
  - model refresh duration
  - page cache rebuild duration
  - swipe frame present interval

## Acceptance Criteria

- Continuous left/right swipes on `124.28` do not show text/card jitter.
- Data values do not update visually mid-swipe.
- Normal idle updates still refresh clock and monitoring values.
- `rockbase-lcdui` CPU remains acceptable while bitcoind is syncing.
- The same source can be built into both 1G and 2G images.

## Current Notes

- The previous micro-optimization changed label updates to skip unchanged text and deferred status refreshes during interaction.
- That did not solve the user-visible object jitter, so future work should avoid further patching inside the old mixed rendering path.
- Phase 1 has been implemented and hot-deployed to `192.168.124.28`: status parsing now builds a `UiModel`, page updates consume that model, and page dirty marking is page-scoped.
- Phase 2 initial implementation has been hot-deployed to `192.168.124.28`: LVGL no longer receives pressed/drag state for tile movement; swiping is handled by custom page-cache composition.
- Phase 2 follow-up added custom release snap animation, also hot-deployed to `192.168.124.28`: release interpolates the cached pager frame to the target page, then syncs the hidden LVGL tileview without animation.
- Legacy tileview interaction glue has started to be removed: old `run_lv_for()` tileview invalidation/snap handling and dead helper functions (`sync_label`, `bitcoin_phase_label`, `set_label_fmt`) are gone.
