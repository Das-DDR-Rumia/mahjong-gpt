#include "mahjong_viz.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MV_RGB(r, g, b) RGB((r), (g), (b))

typedef struct DrawContext {
    HDC dc;
    HFONT font_small;
    HFONT font_body;
    HFONT font_heading;
    HFONT font_metric;
    HFONT font_mono;
} DrawContext;

struct MahjongVizWindow {
    MahjongVizFrame frame;
    int width;
    int height;
    HWND handle;
};

typedef struct MahjongVizWindow WindowState;

static void set_error(char *error, size_t capacity, const char *message) {
    if (error != NULL && capacity > 0) {
        snprintf(error, capacity, "%s", message);
    }
}

static int valid_tile(uint8_t tile) {
    return tile < 34;
}

void mahjong_viz_frame_init(MahjongVizFrame *frame) {
    if (frame != NULL) {
        memset(frame, 0, sizeof(*frame));
        frame->active_seat = -1;
        frame->selected_action = -1;
    }
}

int mahjong_viz_validate_frame(
    const MahjongVizFrame *frame,
    char *error,
    size_t error_capacity
) {
    int seat;
    int index;
    int meld;

    if (frame == NULL) {
        set_error(error, error_capacity, "frame is null");
        return 0;
    }
    if (frame->active_seat < -1 || frame->active_seat >= MV_SEAT_COUNT) {
        set_error(error, error_capacity, "active seat is outside 0..3");
        return 0;
    }
    if (frame->dora_count > MV_MAX_DORA) {
        set_error(error, error_capacity, "too many dora indicators");
        return 0;
    }
    for (index = 0; index < frame->dora_count; ++index) {
        if (!valid_tile(frame->dora_indicators[index])) {
            set_error(error, error_capacity, "invalid dora tile");
            return 0;
        }
    }
    if (frame->reward_event_count > MV_MAX_REWARD_EVENTS) {
        set_error(error, error_capacity, "too many reward events");
        return 0;
    }
    for (seat = 0; seat < MV_SEAT_COUNT; ++seat) {
        const MahjongVizSeat *view = &frame->seats[seat];
        if (view->river_count > MV_MAX_RIVER_TILES) {
            set_error(error, error_capacity, "too many river tiles");
            return 0;
        }
        if (view->meld_count > MV_MAX_MELDS) {
            set_error(error, error_capacity, "too many melds");
            return 0;
        }
        for (index = 0; index < view->river_count; ++index) {
            if (!valid_tile(view->river[index])) {
                set_error(error, error_capacity, "invalid river tile");
                return 0;
            }
        }
        for (meld = 0; meld < view->meld_count; ++meld) {
            const MahjongVizMeld *group = &view->melds[meld];
            if (group->tile_count < 3 || group->tile_count > MV_MAX_MELD_TILES) {
                set_error(error, error_capacity, "meld must contain 3 or 4 tiles");
                return 0;
            }
            for (index = 0; index < group->tile_count; ++index) {
                if (!valid_tile(group->tiles[index])) {
                    set_error(error, error_capacity, "invalid meld tile");
                    return 0;
                }
            }
        }
    }
    set_error(error, error_capacity, "");
    return 1;
}

static HFONT make_font(int pixels, int weight, const wchar_t *face) {
    return CreateFontW(
        -pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face
    );
}

static void context_init(DrawContext *ctx, HDC dc) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->dc = dc;
    ctx->font_small = make_font(14, FW_MEDIUM, L"Segoe UI");
    ctx->font_body = make_font(17, FW_MEDIUM, L"Segoe UI");
    ctx->font_heading = make_font(23, FW_SEMIBOLD, L"Segoe UI");
    ctx->font_metric = make_font(31, FW_BOLD, L"Segoe UI");
    ctx->font_mono = make_font(14, FW_NORMAL, L"Consolas");
    SetBkMode(dc, TRANSPARENT);
}

static void context_destroy(DrawContext *ctx) {
    DeleteObject(ctx->font_small);
    DeleteObject(ctx->font_body);
    DeleteObject(ctx->font_heading);
    DeleteObject(ctx->font_metric);
    DeleteObject(ctx->font_mono);
}

static void fill_rect_color(HDC dc, int x, int y, int w, int h, COLORREF color) {
    RECT rect = {x, y, x + w, y + h};
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

static void round_panel(
    HDC dc, int x, int y, int w, int h, int radius,
    COLORREF fill, COLORREF border
) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, x, y, x + w, y + h, radius, radius);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void draw_text_box(
    DrawContext *ctx,
    const char *text,
    int x, int y, int w, int h,
    HFONT font,
    COLORREF color,
    UINT format
) {
    RECT rect = {x, y, x + w, y + h};
    HGDIOBJ old_font = SelectObject(ctx->dc, font);
    SetTextColor(ctx->dc, color);
    DrawTextA(ctx->dc, text != NULL ? text : "", -1, &rect, format);
    SelectObject(ctx->dc, old_font);
}

static void tile_label(uint8_t tile, char *label, size_t capacity) {
    static const char *honors[7] = {"E", "S", "W", "N", "Wh", "G", "R"};
    if (tile < 9) {
        snprintf(label, capacity, "%dm", tile + 1);
    } else if (tile < 18) {
        snprintf(label, capacity, "%dp", tile - 8);
    } else if (tile < 27) {
        snprintf(label, capacity, "%ds", tile - 17);
    } else {
        snprintf(label, capacity, "%s", honors[tile - 27]);
    }
}

static COLORREF tile_ink(uint8_t tile) {
    if (tile < 9) return MV_RGB(190, 48, 73);
    if (tile < 18) return MV_RGB(46, 94, 170);
    if (tile < 27) return MV_RGB(31, 128, 89);
    if (tile == 33) return MV_RGB(205, 45, 66);
    if (tile == 32) return MV_RGB(24, 138, 87);
    return MV_RGB(44, 50, 62);
}

static void draw_tile(DrawContext *ctx, int x, int y, int w, int h, uint8_t tile) {
    char label[8];
    tile_label(tile, label, sizeof(label));
    round_panel(ctx->dc, x, y, w, h, 6, MV_RGB(250, 248, 241), MV_RGB(199, 204, 211));
    fill_rect_color(ctx->dc, x + 3, y + h - 5, w - 6, 2, MV_RGB(208, 211, 219));
    draw_text_box(
        ctx, label, x, y + (h - 18) / 2, w, 20, ctx->font_small,
        tile_ink(tile), DT_CENTER | DT_SINGLELINE | DT_VCENTER
    );
}

static void draw_river(
    DrawContext *ctx,
    const MahjongVizSeat *seat,
    int seat_index,
    int x,
    int y
) {
    int index;
    char label[40];
    snprintf(label, sizeof(label), "P%d RIVER%s", seat_index,
             seat->is_riichi ? "  /  RIICHI" : "");
    draw_text_box(ctx, label, x, y, 250, 22, ctx->font_small,
                  seat->is_riichi ? MV_RGB(238, 96, 121) : MV_RGB(191, 205, 213),
                  DT_LEFT | DT_SINGLELINE);
    for (index = 0; index < seat->river_count; ++index) {
        int col = index % 6;
        int row = index / 6;
        draw_tile(ctx, x + col * 29, y + 24 + row * 38, 25, 34, seat->river[index]);
    }
}

static void draw_melds(
    DrawContext *ctx,
    const MahjongVizSeat *seat,
    int x,
    int y
) {
    int meld;
    int cursor = x;
    draw_text_box(ctx, "OPEN MELDS", x, y, 180, 20, ctx->font_small,
                  MV_RGB(161, 174, 185), DT_LEFT | DT_SINGLELINE);
    for (meld = 0; meld < seat->meld_count; ++meld) {
        int tile;
        for (tile = 0; tile < seat->melds[meld].tile_count; ++tile) {
            draw_tile(ctx, cursor, y + 21, 22, 30, seat->melds[meld].tiles[tile]);
            cursor += 24;
        }
        cursor += 8;
    }
    if (seat->meld_count == 0) {
        draw_text_box(ctx, "--", x, y + 23, 80, 25, ctx->font_body,
                      MV_RGB(101, 119, 128), DT_LEFT | DT_SINGLELINE);
    }
}

static void draw_rewards(DrawContext *ctx, const MahjongVizFrame *frame, int x, int y) {
    int index;
    for (index = 0; index < frame->reward_event_count; ++index) {
        const MahjongVizRewardEvent *event = &frame->reward_events[index];
        char left[96];
        char value[32];
        COLORREF color = event->delta >= 0.0
            ? MV_RGB(75, 210, 142)
            : MV_RGB(247, 104, 125);
        snprintf(left, sizeof(left), "P%d  %s", event->seat, event->label);
        snprintf(value, sizeof(value), "%+.1f", event->delta);
        draw_text_box(ctx, left, x, y + index * 31, 230, 24, ctx->font_small,
                      MV_RGB(207, 216, 225), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        draw_text_box(ctx, value, x + 225, y + index * 31, 65, 24, ctx->font_body,
                      color, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
}

static void draw_frame(HDC dc, int width, int height, const MahjongVizFrame *frame) {
    DrawContext ctx;
    char metric[96];
    int sidebar_x = width - 360;
    int index;

    context_init(&ctx, dc);
    fill_rect_color(dc, 0, 0, width, height, MV_RGB(13, 18, 28));

    draw_text_box(&ctx, "MAHJONG-GPT", 30, 20, 300, 38, ctx.font_heading,
                  MV_RGB(237, 240, 246), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    draw_text_box(&ctx, "training table / model observation", 222, 26, 420, 28,
                  ctx.font_small, MV_RGB(135, 148, 165), DT_LEFT | DT_SINGLELINE);

    round_panel(dc, 24, 67, sidebar_x - 42, height - 90, 18,
                MV_RGB(20, 82, 72), MV_RGB(51, 118, 103));
    round_panel(dc, 42, 84, 190, 82, 12,
                MV_RGB(19, 31, 42), MV_RGB(65, 91, 106));
    draw_text_box(&ctx, "DORA INDICATORS", 57, 95, 160, 20, ctx.font_small,
                  MV_RGB(164, 178, 190), DT_LEFT | DT_SINGLELINE);
    for (index = 0; index < frame->dora_count; ++index) {
        draw_tile(&ctx, 57 + index * 31, 119, 27, 37, frame->dora_indicators[index]);
    }

    draw_river(&ctx, &frame->seats[2], 2, 342, 93);
    draw_river(&ctx, &frame->seats[3], 3, 72, 277);
    draw_river(&ctx, &frame->seats[1], 1, 645, 277);
    draw_river(&ctx, &frame->seats[0], 0, 342, 493);

    draw_melds(&ctx, &frame->seats[2], 342, 216);
    draw_melds(&ctx, &frame->seats[3], 72, 416);
    draw_melds(&ctx, &frame->seats[1], 645, 416);
    draw_melds(&ctx, &frame->seats[0], 342, 636);

    round_panel(dc, 356, 303, 220, 145, 18,
                MV_RGB(14, 50, 49), MV_RGB(102, 180, 161));
    snprintf(metric, sizeof(metric), "ACTIVE PLAYER  P%d", frame->active_seat);
    draw_text_box(&ctx, metric, 371, 319, 190, 24, ctx.font_small,
                  MV_RGB(142, 224, 203), DT_CENTER | DT_SINGLELINE);
    draw_text_box(&ctx, frame->last_action, 371, 350, 190, 32, ctx.font_body,
                  MV_RGB(247, 247, 242), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    snprintf(metric, sizeof(metric), "action %d   confidence %.1f%%",
             frame->selected_action, frame->action_confidence * 100.0);
    draw_text_box(&ctx, metric, 371, 391, 190, 25, ctx.font_small,
                  MV_RGB(171, 184, 195), DT_CENTER | DT_SINGLELINE);

    fill_rect_color(dc, sidebar_x, 0, width - sidebar_x, height, MV_RGB(18, 24, 36));
    draw_text_box(&ctx, "TRAINING", sidebar_x + 24, 24, 300, 32, ctx.font_heading,
                  MV_RGB(235, 238, 245), DT_LEFT | DT_SINGLELINE);

    round_panel(dc, sidebar_x + 20, 70, 150, 104, 13,
                MV_RGB(28, 35, 51), MV_RGB(55, 65, 85));
    draw_text_box(&ctx, "ROUND", sidebar_x + 34, 84, 120, 20, ctx.font_small,
                  MV_RGB(145, 158, 176), DT_LEFT | DT_SINGLELINE);
    snprintf(metric, sizeof(metric), "%llu", (unsigned long long)frame->training_round);
    draw_text_box(&ctx, metric, sidebar_x + 34, 110, 120, 48, ctx.font_metric,
                  MV_RGB(222, 210, 255), DT_LEFT | DT_SINGLELINE);

    round_panel(dc, sidebar_x + 185, 70, 155, 104, 13,
                MV_RGB(28, 35, 51), MV_RGB(55, 65, 85));
    draw_text_box(&ctx, "AVG REWARD", sidebar_x + 199, 84, 130, 20, ctx.font_small,
                  MV_RGB(145, 158, 176), DT_LEFT | DT_SINGLELINE);
    snprintf(metric, sizeof(metric), "%+.2f", frame->average_reward);
    draw_text_box(&ctx, metric, sidebar_x + 199, 110, 125, 48, ctx.font_metric,
                  frame->average_reward >= 0.0 ? MV_RGB(83, 214, 149) : MV_RGB(247, 104, 125),
                  DT_LEFT | DT_SINGLELINE);

    round_panel(dc, sidebar_x + 20, 190, 320, 254, 13,
                MV_RGB(24, 31, 46), MV_RGB(53, 63, 82));
    draw_text_box(&ctx, "STEP REWARDS", sidebar_x + 36, 206, 280, 24, ctx.font_body,
                  MV_RGB(230, 233, 240), DT_LEFT | DT_SINGLELINE);
    draw_rewards(&ctx, frame, sidebar_x + 36, 240);

    round_panel(dc, sidebar_x + 20, 460, 320, height - 482, 13,
                MV_RGB(9, 14, 22), MV_RGB(53, 63, 82));
    draw_text_box(&ctx, "TEST OUTPUT", sidebar_x + 36, 476, 280, 23, ctx.font_body,
                  MV_RGB(221, 225, 234), DT_LEFT | DT_SINGLELINE);
    draw_text_box(&ctx, frame->test_output, sidebar_x + 36, 508, 285, height - 540,
                  ctx.font_mono, MV_RGB(151, 224, 182),
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    context_destroy(&ctx);
}

int mahjong_viz_render_bmp(
    const char *path,
    const MahjongVizFrame *frame,
    int width,
    int height,
    char *error,
    size_t error_capacity
) {
    BITMAPINFO bitmap_info;
    BITMAPINFOHEADER output_header;
    BITMAPFILEHEADER file_header;
    HDC memory_dc;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    void *pixels = NULL;
    FILE *file;
    DWORD pixel_bytes;
    int row;

    if (!mahjong_viz_validate_frame(frame, error, error_capacity)) {
        return 0;
    }
    if (path == NULL || width < 960 || height < 640) {
        set_error(error, error_capacity, "path is null or render size is below 960x640");
        return 0;
    }

    memset(&bitmap_info, 0, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    memory_dc = CreateCompatibleDC(NULL);
    bitmap = CreateDIBSection(
        memory_dc, &bitmap_info, DIB_RGB_COLORS, &pixels, NULL, 0
    );
    if (memory_dc == NULL || bitmap == NULL || pixels == NULL) {
        if (bitmap != NULL) DeleteObject(bitmap);
        if (memory_dc != NULL) DeleteDC(memory_dc);
        set_error(error, error_capacity, "unable to create offscreen surface");
        return 0;
    }

    old_bitmap = SelectObject(memory_dc, bitmap);
    draw_frame(memory_dc, width, height, frame);
    GdiFlush();

    pixel_bytes = (DWORD)(width * height * 4);
    memset(&file_header, 0, sizeof(file_header));
    file_header.bfType = 0x4D42;
    file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = file_header.bfOffBits + pixel_bytes;
    output_header = bitmap_info.bmiHeader;
    output_header.biHeight = height;

    file = fopen(path, "wb");
    if (file == NULL) {
        SelectObject(memory_dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(memory_dc);
        set_error(error, error_capacity, "unable to open BMP output path");
        return 0;
    }
    fwrite(&file_header, sizeof(file_header), 1, file);
    fwrite(&output_header, sizeof(output_header), 1, file);
    for (row = height - 1; row >= 0; --row) {
        const unsigned char *scanline =
            (const unsigned char *)pixels + (size_t)row * (size_t)width * 4u;
        fwrite(scanline, (size_t)width * 4u, 1, file);
    }
    fclose(file);

    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    set_error(error, error_capacity, "");
    return 1;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    WindowState *state = (WindowState *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    if (message == WM_PAINT && state != NULL) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        draw_frame(dc, state->width, state->height, &state->frame);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_DESTROY) {
        if (state != NULL) state->handle = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static int register_window_class(HINSTANCE instance) {
    static const wchar_t *class_name = L"MahjongGPTVisualizerWindow";
    WNDCLASSW window_class;
    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.lpszClassName = class_name;
    return RegisterClassW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

MahjongVizWindow *mahjong_viz_window_open(
    const char *title,
    const MahjongVizFrame *frame,
    int width,
    int height
) {
    static const wchar_t *class_name = L"MahjongGPTVisualizerWindow";
    HINSTANCE instance = GetModuleHandleW(NULL);
    MahjongVizWindow *state;
    wchar_t wide_title[128];

    if (!mahjong_viz_validate_frame(frame, NULL, 0) || width < 960 || height < 640) {
        return NULL;
    }
    if (!register_window_class(instance)) return NULL;

    state = (MahjongVizWindow *)calloc(1, sizeof(*state));
    if (state == NULL) return NULL;
    state->frame = *frame;
    state->width = width;
    state->height = height;
    MultiByteToWideChar(CP_UTF8, 0, title != NULL ? title : "Mahjong-GPT", -1,
                        wide_title, (int)(sizeof(wide_title) / sizeof(wide_title[0])));
    state->handle = CreateWindowExW(
        0, class_name, wide_title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width + 16, height + 39,
        NULL, NULL, instance, state
    );
    if (state->handle == NULL) {
        free(state);
        return NULL;
    }
    ShowWindow(state->handle, SW_SHOW);
    UpdateWindow(state->handle);
    return state;
}

int mahjong_viz_window_update(
    MahjongVizWindow *window,
    const MahjongVizFrame *frame
) {
    if (window == NULL || window->handle == NULL) return 0;
    if (!mahjong_viz_validate_frame(frame, NULL, 0)) return 0;
    window->frame = *frame;
    InvalidateRect(window->handle, NULL, FALSE);
    return 1;
}

int mahjong_viz_window_pump(MahjongVizWindow *window) {
    MSG message;
    if (window == NULL || window->handle == NULL) return 0;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return 0;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return window->handle != NULL;
}

void mahjong_viz_window_close(MahjongVizWindow *window) {
    if (window == NULL) return;
    if (window->handle != NULL) DestroyWindow(window->handle);
    free(window);
}

int mahjong_viz_show_window(
    const char *title,
    const MahjongVizFrame *frame,
    int width,
    int height
) {
    static const wchar_t *class_name = L"MahjongGPTVisualizerWindow";
    WindowState state;
    wchar_t wide_title[128];
    HWND window;
    MSG message;
    HINSTANCE instance = GetModuleHandleW(NULL);

    if (!mahjong_viz_validate_frame(frame, NULL, 0) || width < 960 || height < 640) {
        return 0;
    }
    if (!register_window_class(instance)) return 0;

    memset(&state, 0, sizeof(state));
    state.frame = *frame;
    state.width = width;
    state.height = height;
    MultiByteToWideChar(CP_UTF8, 0, title != NULL ? title : "Mahjong-GPT", -1,
                        wide_title, (int)(sizeof(wide_title) / sizeof(wide_title[0])));

    window = CreateWindowExW(
        0, class_name, wide_title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width + 16, height + 39,
        NULL, NULL, instance, &state
    );
    if (window == NULL) return 0;
    state.handle = window;
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 1;
}
