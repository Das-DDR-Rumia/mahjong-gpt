#ifndef MAHJONG_VIZ_H
#define MAHJONG_VIZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MV_SEAT_COUNT 4
#define MV_MAX_RIVER_TILES 24
#define MV_MAX_MELDS 4
#define MV_MAX_MELD_TILES 4
#define MV_MAX_DORA 5
#define MV_MAX_REWARD_EVENTS 8
#define MV_TEXT_CAPACITY 512

typedef struct MahjongVizMeld {
    uint8_t tiles[MV_MAX_MELD_TILES];
    uint8_t tile_count;
    uint8_t is_open;
} MahjongVizMeld;

typedef struct MahjongVizSeat {
    uint8_t river[MV_MAX_RIVER_TILES];
    uint8_t river_count;
    MahjongVizMeld melds[MV_MAX_MELDS];
    uint8_t meld_count;
    int is_riichi;
} MahjongVizSeat;

typedef struct MahjongVizRewardEvent {
    int seat;
    int action;
    double delta;
    char label[40];
} MahjongVizRewardEvent;

typedef struct MahjongVizFrame {
    uint64_t training_round;
    double average_reward;
    int active_seat;
    int selected_action;
    double action_confidence;
    char last_action[64];

    MahjongVizSeat seats[MV_SEAT_COUNT];
    uint8_t dora_indicators[MV_MAX_DORA];
    uint8_t dora_count;

    MahjongVizRewardEvent reward_events[MV_MAX_REWARD_EVENTS];
    uint8_t reward_event_count;
    char test_output[MV_TEXT_CAPACITY];
} MahjongVizFrame;

typedef struct MahjongVizWindow MahjongVizWindow;

void mahjong_viz_frame_init(MahjongVizFrame *frame);

int mahjong_viz_validate_frame(
    const MahjongVizFrame *frame,
    char *error,
    size_t error_capacity
);

int mahjong_viz_render_bmp(
    const char *path,
    const MahjongVizFrame *frame,
    int width,
    int height,
    char *error,
    size_t error_capacity
);

MahjongVizWindow *mahjong_viz_window_open(
    const char *title,
    const MahjongVizFrame *frame,
    int width,
    int height
);

int mahjong_viz_window_update(
    MahjongVizWindow *window,
    const MahjongVizFrame *frame
);

int mahjong_viz_window_pump(MahjongVizWindow *window);

void mahjong_viz_window_close(MahjongVizWindow *window);

int mahjong_viz_show_window(
    const char *title,
    const MahjongVizFrame *frame,
    int width,
    int height
);

#ifdef __cplusplus
}
#endif

#endif
