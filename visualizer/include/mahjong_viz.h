#ifndef MAHJONG_VIZ_H
#define MAHJONG_VIZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MV_SEAT_COUNT 4
#define MV_MAX_RIVER_TILES 24
#define MV_MAX_HAND_TILES 14
#define MV_MAX_MELDS 4
#define MV_MAX_MELD_TILES 4
#define MV_MAX_DORA 5
#define MV_MAX_REWARD_EVENTS 8
#define MV_TEXT_CAPACITY 2048

#define MV_ACTION_CHI_UP 34
#define MV_ACTION_CHI_MID 35
#define MV_ACTION_CHI_DOWN 36
#define MV_ACTION_PON 37
#define MV_ACTION_KAN_OPEN 38
#define MV_ACTION_KAN_ADD 39
#define MV_ACTION_KAN_CLOSED 40
#define MV_ACTION_RIICHI 42
#define MV_ACTION_RON 43
#define MV_ACTION_TSUMO 44
#define MV_ACTION_PASS 45

typedef enum MahjongVizCommand {
    MV_COMMAND_NONE = 0,
    MV_COMMAND_TOGGLE_RUN,
    MV_COMMAND_STEP,
    MV_COMMAND_RESET,
    MV_COMMAND_ROUND_DOWN,
    MV_COMMAND_ROUND_UP,
    MV_COMMAND_AVERAGE_REWARD_DOWN,
    MV_COMMAND_AVERAGE_REWARD_UP,
    MV_COMMAND_SEAT_PREVIOUS,
    MV_COMMAND_SEAT_NEXT,
    MV_COMMAND_ACTION_PREVIOUS,
    MV_COMMAND_ACTION_NEXT,
    MV_COMMAND_CONFIDENCE_DOWN,
    MV_COMMAND_CONFIDENCE_UP,
    MV_COMMAND_DORA_REMOVE,
    MV_COMMAND_DORA_ADD,
    MV_COMMAND_DORA_CYCLE,
    MV_COMMAND_RIICHI_TOGGLE,
    MV_COMMAND_RIVER_REMOVE,
    MV_COMMAND_RIVER_ADD,
    MV_COMMAND_RIVER_CYCLE,
    MV_COMMAND_MELD_REMOVE,
    MV_COMMAND_MELD_ADD,
    MV_COMMAND_MELD_CYCLE,
    MV_COMMAND_REWARD_PREVIOUS,
    MV_COMMAND_REWARD_NEXT,
    MV_COMMAND_REWARD_DELTA_DOWN,
    MV_COMMAND_REWARD_DELTA_UP,
    MV_COMMAND_TOGGLE_ADVANCED,
    MV_COMMAND_SPEED_SLOWER,
    MV_COMMAND_SPEED_FASTER,
    MV_COMMAND_EXPORT_BMP
} MahjongVizCommand;

typedef struct MahjongVizMeld {
    uint8_t tiles[MV_MAX_MELD_TILES];
    uint8_t tile_count;
    uint8_t is_open;
} MahjongVizMeld;

typedef struct MahjongVizSeat {
    uint8_t hand[MV_MAX_HAND_TILES];
    uint8_t hand_count;
    uint8_t river[MV_MAX_RIVER_TILES];
    uint8_t river_count;
    MahjongVizMeld melds[MV_MAX_MELDS];
    uint8_t meld_count;
    int is_riichi;
    int riichi_discard_index;
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
    int training_running;
    int show_advanced_controls;
    uint32_t step_interval_ms;
    int selected_reward_event;
    int last_discard_seat;
    int last_discard_tile;
    int bot_seat;
    int pending_discard_seat;
    uint8_t simulation_wall[136];
    uint16_t simulation_wall_position;
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

void mahjong_viz_tile_label(
    uint8_t tile,
    char *label,
    size_t capacity
);

int mahjong_viz_apply_command(
    MahjongVizFrame *frame,
    MahjongVizCommand command
);

int mahjong_viz_apply_model_action(
    MahjongVizFrame *frame,
    int seat,
    int action,
    int tile_hint
);

int mahjong_viz_make_bot_observation(
    const MahjongVizFrame *frame,
    int bot_seat,
    MahjongVizFrame *observation
);

int mahjong_viz_shuffle_table(
    MahjongVizFrame *frame,
    uint32_t seed
);

const char *mahjong_viz_command_name(MahjongVizCommand command);

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

int mahjong_viz_window_get_frame(
    const MahjongVizWindow *window,
    MahjongVizFrame *frame
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
