# Mahjong-GPT C Training Visualizer

A small Windows-native C library for rendering and interactively editing
Mahjong-GPT training frames.
It uses Win32/GDI only and has no third-party runtime dependencies.

## Displayed data

- four player rivers and open melds;
- all four hands in the user view, with only the configured bot seat marked as
  bot-visible;
- dora indicators and riichi markers;
- active seat, selected action and model confidence;
- training round, average reward and recent per-step reward changes;
- an embedded test/output panel.

## Interactive controls

The right-hand control panel supports the complete demo loop without a
terminal: run/pause, single-step and reset; round, average reward, active seat,
action and confidence adjustments; dora, riichi, river and meld editing;
reward-event selection and delta editing; and BMP snapshot export.

The default layout keeps only the transport, speed, reward summary and recent
output visible. Use `ADVANCED CONTROLS` to reveal the editing grid. Output is
limited to its most recent eight lines in the window while the full buffer
remains available to callers.

Keyboard shortcuts are also available: `Space` toggles run/pause, `Enter`
steps, `Home` resets, left/right changes the selected action, and up/down
changes model confidence. `[` and `]` decrease and increase run speed.

The default run interval is 100 ms (10 steps per second), with selectable
presets of 1, 2, 4, 10 and 20 steps per second. The current SPS is displayed
in the training header. A 16 ms UI timer and bounded catch-up loop prevent a
briefly busy window from permanently reducing the selected update rate.
Window painting uses a persistent off-screen bitmap and one final `BitBlt`, so
high-speed updates never expose partially drawn GDI frames or background erase
flashes.

Tile values use the project's `tile34` convention: `0..8` man, `9..17` pin,
`18..26` sou and `27..33` honors. Honors are displayed as `1z..7z` in the
order east, south, west, north, white, green and red.

## Library interface

Include `include/mahjong_viz.h`, initialize one `MahjongVizFrame`, then call:

```c
mahjong_viz_validate_frame(&frame, error, sizeof(error));
mahjong_viz_render_bmp("preview.bmp", &frame, 1440, 900, error, sizeof(error));
mahjong_viz_show_window("Mahjong-GPT Training", &frame, 1440, 900);
```

For a live training loop, use the non-blocking window interface:

```c
MahjongVizWindow *window = mahjong_viz_window_open(
    "Mahjong-GPT Training", &frame, 1440, 900
);

while (mahjong_viz_window_pump(window)) {
    /* Update frame from the latest environment and policy output. */
    mahjong_viz_window_update(window, &frame);
}

mahjong_viz_window_close(window);
```

The pure `mahjong_viz_apply_command` API applies the same operations without a
window, while `mahjong_viz_window_get_frame` reads edits back for a trainer or
Python binding.  A production trainer can continue to call
`mahjong_viz_window_update`; the standalone mutation behavior exists so the
demo remains fully usable on its own.

Model actions can be projected onto the table with:

```c
mahjong_viz_apply_model_action(&frame, seat, action, tile_hint);
```

Discards are appended to the acting seat's river. Chi, pon and open-kan calls
remove the claimed last discard and create the matching open meld. Riichi marks
the following discard for sideways rendering and shows a riichi stick. Added
and closed kan require `tile_hint` because action ids `39` and `40` do not encode
the affected tile; pass `-1` for actions where no hint is needed.

The renderer frame is the complete user/debug view. Before passing it to a
model, call `mahjong_viz_make_bot_observation`; it preserves the selected bot
seat's hand and clears all opponent hand arrays and counts. Do not feed the
full renderer frame directly into a policy.

`mahjong_viz_shuffle_table` builds a 136-tile wall, applies Fisher-Yates
shuffling, deals 14 tiles to seat 0 and 13 to the other seats, and selects a
new dora indicator. A non-zero seed reproduces a deal for tests; seed `0`
uses the Windows system random-number provider. Reset and automatic new-hand
transitions use fresh seeds.

The standalone run mode no longer uses a fixed action script. It keeps the
remaining wall, draws before turns, and derives chi, pon and kan choices from
the current concealed hands. Calls are rejected atomically when the required
two or three hand tiles are absent, leaving the river and melds unchanged.
The bot observation also clears the unrevealed simulation wall.

`MahjongVizFrame` is the seam between the Python trainer and the renderer.
Environment observations populate rivers, melds and dora indicators; policy
output populates `selected_action` and `action_confidence`; recorder values
populate the training counters and reward events.

## Build and test on Windows PowerShell

```powershell
cmake -S visualizer -B visualizer/build -G "Visual Studio 17 2022" -A x64
cmake --build visualizer/build --config Release
ctest --test-dir visualizer/build -C Release --output-on-failure
```

The DLL and import library are written to `visualizer/build/Release/`.
The test writes its verified preview to
`visualizer/artifacts/mahjong_viz_demo.bmp`.
