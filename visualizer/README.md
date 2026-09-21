# Mahjong-GPT C Training Visualizer

A small Windows-native C library for rendering Mahjong-GPT training frames.
It uses Win32/GDI only and has no third-party runtime dependencies.

## Displayed data

- four player rivers and open melds;
- dora indicators and riichi markers;
- active seat, selected action and model confidence;
- training round, average reward and recent per-step reward changes;
- an embedded test/output panel.

Tile values use the project's `tile34` convention: `0..8` man, `9..17` pin,
`18..26` sou and `27..33` honors.

## Library interface

Include `include/mahjong_viz.h`, initialize one `MahjongVizFrame`, then call:

```c
mahjong_viz_validate_frame(&frame, error, sizeof(error));
mahjong_viz_render_bmp("preview.bmp", &frame, 1280, 760, error, sizeof(error));
mahjong_viz_show_window("Mahjong-GPT Training", &frame, 1280, 760);
```

For a live training loop, use the non-blocking window interface:

```c
MahjongVizWindow *window = mahjong_viz_window_open(
    "Mahjong-GPT Training", &frame, 1280, 760
);

while (mahjong_viz_window_pump(window)) {
    /* Update frame from the latest environment and policy output. */
    mahjong_viz_window_update(window, &frame);
}

mahjong_viz_window_close(window);
```

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
