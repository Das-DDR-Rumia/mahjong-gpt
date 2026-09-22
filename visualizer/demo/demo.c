#include "mahjong_viz.h"

#include <stdio.h>
#include <string.h>

static void fill_demo(MahjongVizFrame *frame) {
    static const uint8_t rivers[4][12] = {
        {0, 8, 12, 17, 27, 4, 21, 31, 2, 25, 10, 6},
        {18, 19, 24, 29, 30, 9, 16, 20, 3, 11, 26, 32},
        {5, 14, 23, 28, 7, 15, 22, 33, 1, 13, 19, 27},
        {6, 15, 24, 30, 8, 17, 26, 31, 0, 12, 21, 29},
    };
    int seat;

    mahjong_viz_frame_init(frame);
    mahjong_viz_shuffle_table(frame, 0);
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
    frame->seats[1].riichi_discard_index = 5;
    frame->seats[2].meld_count = 1;
    frame->seats[2].melds[0] = (MahjongVizMeld){{31, 31, 31, 0}, 3, 1};
    frame->seats[3].meld_count = 1;
    frame->seats[3].melds[0] = (MahjongVizMeld){{9, 10, 11, 0}, 3, 1};
    frame->dora_count = 2;
    frame->dora_indicators[0] = 3;
    frame->dora_indicators[1] = 29;
    frame->reward_event_count = 4;
    frame->selected_reward_event = 3;
    frame->reward_events[0] = (MahjongVizRewardEvent){0, 6, 4.0, "ukeire +4"};
    frame->reward_events[1] = (MahjongVizRewardEvent){2, 37, 5.0, "yakuhai confirmed"};
    frame->reward_events[2] = (MahjongVizRewardEvent){1, 12, -3.6, "ukeire -3"};
    frame->reward_events[3] = (MahjongVizRewardEvent){0, 42, 10.0, "riichi"};
    snprintf(frame->test_output, sizeof(frame->test_output),
             "Visualizer ready\nRun ctest for verified results.");
}

int main(void) {
    MahjongVizFrame frame;
    char error[256] = {0};

    fill_demo(&frame);
    if (!mahjong_viz_render_bmp(
            "mahjong_viz_demo.bmp", &frame, 1440, 900, error, sizeof(error))) {
        fprintf(stderr, "render failed: %s\n", error);
        return 1;
    }
    return mahjong_viz_show_window("Mahjong-GPT Training", &frame, 1440, 900) ? 0 : 1;
}
