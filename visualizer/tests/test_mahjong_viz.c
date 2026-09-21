#include "mahjong_viz.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int passed = 0;
static int failed = 0;
static char report[MV_TEXT_CAPACITY];

static void record(int condition, const char *name) {
    char line[96];
    snprintf(line, sizeof(line), "%s  %s\n", condition ? "PASS" : "FAIL", name);
    strncat(report, line, sizeof(report) - strlen(report) - 1);
    if (condition) {
        passed += 1;
    } else {
        failed += 1;
    }
}

static void fill_demo_frame(MahjongVizFrame *frame) {
    static const uint8_t rivers[4][12] = {
        {0, 8, 12, 17, 27, 4, 21, 31, 2, 25, 10, 6},
        {18, 19, 24, 29, 30, 9, 16, 20, 3, 11, 26, 32},
        {5, 14, 23, 28, 7, 15, 22, 33, 1, 13, 19, 27},
        {6, 15, 24, 30, 8, 17, 26, 31, 0, 12, 21, 29},
    };
    int seat;

    mahjong_viz_frame_init(frame);
    frame->training_round = 1280;
    frame->average_reward = 42.75;
    frame->active_seat = 0;
    frame->selected_action = 6;
    frame->action_confidence = 0.684;
    snprintf(frame->last_action, sizeof(frame->last_action), "P0 discard 7m");

    for (seat = 0; seat < MV_SEAT_COUNT; ++seat) {
        memcpy(frame->seats[seat].river, rivers[seat], sizeof(rivers[seat]));
        frame->seats[seat].river_count = 12;
    }
    frame->seats[1].is_riichi = 1;
    frame->seats[2].meld_count = 1;
    frame->seats[2].melds[0].tile_count = 3;
    frame->seats[2].melds[0].is_open = 1;
    frame->seats[2].melds[0].tiles[0] = 31;
    frame->seats[2].melds[0].tiles[1] = 31;
    frame->seats[2].melds[0].tiles[2] = 31;
    frame->seats[3].meld_count = 1;
    frame->seats[3].melds[0].tile_count = 3;
    frame->seats[3].melds[0].is_open = 1;
    frame->seats[3].melds[0].tiles[0] = 9;
    frame->seats[3].melds[0].tiles[1] = 10;
    frame->seats[3].melds[0].tiles[2] = 11;

    frame->dora_count = 2;
    frame->dora_indicators[0] = 3;
    frame->dora_indicators[1] = 29;

    frame->reward_event_count = 4;
    frame->reward_events[0] = (MahjongVizRewardEvent){0, 6, 4.0, "ukeire +4"};
    frame->reward_events[1] = (MahjongVizRewardEvent){2, 37, 5.0, "yakuhai confirmed"};
    frame->reward_events[2] = (MahjongVizRewardEvent){1, 12, -3.6, "ukeire -3"};
    frame->reward_events[3] = (MahjongVizRewardEvent){0, 42, 10.0, "riichi"};
}

static uint32_t read_u32_le(const unsigned char *bytes) {
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
}

int main(void) {
    MahjongVizFrame frame;
    MahjongVizFrame invalid;
    char error[256] = {0};
    char screenshot_path[512];
    unsigned char header[54] = {0};
    FILE *file;
    long file_size = 0;

    fill_demo_frame(&frame);
    record(mahjong_viz_validate_frame(&frame, error, sizeof(error)) == 1,
           "valid frame");

    invalid = frame;
    invalid.seats[0].river[0] = 34;
    record(mahjong_viz_validate_frame(&invalid, error, sizeof(error)) == 0,
           "reject invalid tile");

    invalid = frame;
    invalid.dora_count = MV_MAX_DORA + 1;
    record(mahjong_viz_validate_frame(&invalid, error, sizeof(error)) == 0,
           "reject dora overflow");

    snprintf(
        frame.test_output,
        sizeof(frame.test_output),
        "Visualizer self-test\n%sRESULT  %d passed / %d failed",
        report,
        passed,
        failed
    );
    snprintf(screenshot_path, sizeof(screenshot_path), "%s/mahjong_viz_demo.bmp",
             MV_ARTIFACT_DIR);

    record(mahjong_viz_render_bmp(
               screenshot_path, &frame, 1280, 760, error, sizeof(error)) == 1,
           "render BMP");

    file = fopen(screenshot_path, "rb");
    record(file != NULL, "open BMP");
    if (file != NULL) {
        fread(header, 1, sizeof(header), file);
        fseek(file, 0, SEEK_END);
        file_size = ftell(file);
        fclose(file);
    }
    record(header[0] == 'B' && header[1] == 'M', "BMP signature");
    record(read_u32_le(header + 18) == 1280 && read_u32_le(header + 22) == 760,
           "BMP dimensions");
    record(file_size > 100000, "pixel payload");

    snprintf(
        frame.test_output,
        sizeof(frame.test_output),
        "C library test suite\n%sRESULT  %d passed / %d failed",
        report,
        passed,
        failed
    );
    mahjong_viz_render_bmp(
        screenshot_path, &frame, 1280, 760, error, sizeof(error)
    );

    printf("%s", report);
    printf("RESULT  %d passed / %d failed\n", passed, failed);
    printf("ARTIFACT  %s\n", screenshot_path);
    return failed == 0 ? 0 : 1;
}
