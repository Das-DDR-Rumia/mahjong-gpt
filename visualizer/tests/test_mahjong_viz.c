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
    static const uint8_t hands[4][14] = {
        {4, 17, 22, 23, 0, 1, 2, 9, 10, 11, 27, 28, 29, 30},
        {4, 4, 12, 17, 17, 17, 3, 5, 6, 13, 14, 15, 31, 32},
        {8, 31, 31, 31, 31, 1, 2, 3, 10, 11, 18, 19, 20, 33},
        {21, 0, 8, 9, 17, 18, 26, 27, 28, 29, 30, 31, 32, 33},
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
        memcpy(frame->seats[seat].hand, hands[seat], sizeof(hands[seat]));
        frame->seats[seat].hand_count = 14;
        memcpy(frame->seats[seat].river, rivers[seat], sizeof(rivers[seat]));
        frame->seats[seat].river_count = 12;
    }
    frame->seats[1].is_riichi = 1;
    frame->seats[1].riichi_discard_index = 5;
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
    frame->selected_reward_event = 3;
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

static int physical_tile_counts_are_valid(const MahjongVizFrame *frame) {
    int counts[34] = {0};
    int seat;
    int index;
    int meld;
    for (seat = 0; seat < MV_SEAT_COUNT; ++seat) {
        const MahjongVizSeat *view = &frame->seats[seat];
        for (index = 0; index < view->hand_count; ++index) counts[view->hand[index]] += 1;
        for (index = 0; index < view->river_count; ++index) counts[view->river[index]] += 1;
        for (meld = 0; meld < view->meld_count; ++meld) {
            int tile;
            for (tile = 0; tile < view->melds[meld].tile_count; ++tile) {
                counts[view->melds[meld].tiles[tile]] += 1;
            }
        }
    }
    for (index = 0; index < frame->dora_count; ++index) {
        counts[frame->dora_indicators[index]] += 1;
    }
    for (index = frame->simulation_wall_position; index < 136; ++index) {
        counts[frame->simulation_wall[index]] += 1;
    }
    for (index = 0; index < 34; ++index) {
        if (counts[index] != 4) return 0;
    }
    return 1;
}

int main(void) {
    MahjongVizFrame frame;
    MahjongVizFrame invalid;
    MahjongVizFrame interactive;
    MahjongVizFrame action_frame;
    MahjongVizFrame bot_observation;
    MahjongVizFrame shuffled_a;
    MahjongVizFrame shuffled_b;
    MahjongVizFrame shuffled_c;
    char error[256] = {0};
    char tile_name[8] = {0};
    char screenshot_path[512];
    unsigned char header[54] = {0};
    FILE *file;
    long file_size = 0;
    int expected_seat;
    int previous_river_count;
    int previous_meld_count;
    int simulation_ok = 1;
    int saw_legal_call = 0;
    int action_history[22];
    int sequence_repeated = 1;
    int step;

    fill_demo_frame(&frame);
    mahjong_viz_tile_label(27, tile_name, sizeof(tile_name));
    record(strcmp(tile_name, "1z") == 0, "east uses 1z label");
    mahjong_viz_tile_label(33, tile_name, sizeof(tile_name));
    record(strcmp(tile_name, "7z") == 0, "red dragon uses 7z label");
    mahjong_viz_frame_init(&shuffled_a);
    mahjong_viz_frame_init(&shuffled_b);
    mahjong_viz_frame_init(&shuffled_c);
    record(mahjong_viz_shuffle_table(&shuffled_a, 12345u) == 1
           && mahjong_viz_shuffle_table(&shuffled_b, 12345u) == 1
           && memcmp(shuffled_a.seats, shuffled_b.seats,
                     sizeof(shuffled_a.seats)) == 0,
           "shuffle is reproducible with a fixed seed");
    record(mahjong_viz_shuffle_table(&shuffled_c, 54321u) == 1
           && memcmp(shuffled_a.seats, shuffled_c.seats,
                     sizeof(shuffled_a.seats)) != 0
           && shuffled_c.seats[0].hand_count == 14
           && shuffled_c.seats[1].hand_count == 13,
           "new seed produces a different valid deal");
    mahjong_viz_frame_init(&shuffled_a);
    mahjong_viz_frame_init(&shuffled_b);
    mahjong_viz_shuffle_table(&shuffled_a, 0);
    mahjong_viz_shuffle_table(&shuffled_b, 0);
    record(memcmp(shuffled_a.simulation_wall, shuffled_b.simulation_wall,
                  sizeof(shuffled_a.simulation_wall)) != 0,
           "automatic seeds produce different wall orders");

    mahjong_viz_frame_init(&shuffled_c);
    mahjong_viz_shuffle_table(&shuffled_c, 24680u);
    for (step = 0; step < 120; ++step) {
        if (!mahjong_viz_apply_command(&shuffled_c, MV_COMMAND_STEP)
            || !physical_tile_counts_are_valid(&shuffled_c)) {
            simulation_ok = 0;
            break;
        }
        if (step < 22) action_history[step] = shuffled_c.selected_action;
        if (shuffled_c.selected_action >= MV_ACTION_CHI_UP
            && shuffled_c.selected_action <= MV_ACTION_KAN_CLOSED) {
            saw_legal_call = 1;
        }
    }
    if (simulation_ok) {
        for (step = 0; step < 11; ++step) {
            if (action_history[step] != action_history[step + 11]) {
                sequence_repeated = 0;
                break;
            }
        }
    }
    record(simulation_ok, "long simulation preserves four physical copies per tile");
    record(saw_legal_call, "simulation produces a hand-backed legal call");
    record(!sequence_repeated, "simulation does not repeat the old fixed action script");
    record(mahjong_viz_validate_frame(&frame, error, sizeof(error)) == 1,
           "valid frame");
    record(mahjong_viz_make_bot_observation(&frame, 0, &bot_observation) == 1
           && bot_observation.seats[0].hand_count == frame.seats[0].hand_count
           && bot_observation.seats[1].hand_count == 0
           && bot_observation.seats[2].hand_count == 0
           && bot_observation.seats[3].hand_count == 0
           && frame.seats[1].hand_count == 14,
           "bot observation hides opponent hands");

    invalid = frame;
    invalid.seats[0].river[0] = 34;
    record(mahjong_viz_validate_frame(&invalid, error, sizeof(error)) == 0,
           "reject invalid tile");

    invalid = frame;
    invalid.dora_count = MV_MAX_DORA + 1;
    record(mahjong_viz_validate_frame(&invalid, error, sizeof(error)) == 0,
           "reject dora overflow");

    interactive = frame;
    record(interactive.show_advanced_controls == 0,
           "advanced controls are collapsed by default");
    record(mahjong_viz_apply_command(
               &interactive, MV_COMMAND_TOGGLE_ADVANCED) == 1
           && interactive.show_advanced_controls == 1,
           "advanced controls can be expanded");
    record(mahjong_viz_apply_command(
               &interactive, MV_COMMAND_TOGGLE_ADVANCED) == 1
           && interactive.show_advanced_controls == 0,
           "advanced controls can be collapsed");
    record(interactive.step_interval_ms <= 100,
           "default inference interval is at most 100 ms");
    record(mahjong_viz_apply_command(&interactive, MV_COMMAND_SPEED_FASTER) == 1
           && interactive.step_interval_ms < 100,
           "increase inference speed from the interface");
    record(mahjong_viz_apply_command(&interactive, MV_COMMAND_SPEED_SLOWER) == 1
           && interactive.step_interval_ms == 100,
           "decrease inference speed from the interface");
    record(mahjong_viz_apply_command(&interactive, MV_COMMAND_TOGGLE_RUN) == 1
           && interactive.training_running == 1,
           "toggle training run state");
    record(mahjong_viz_apply_command(&interactive, MV_COMMAND_STEP) == 1
           && interactive.training_round == frame.training_round + 1,
           "single training step");
    expected_seat = (interactive.active_seat + 1) % MV_SEAT_COUNT;
    record(mahjong_viz_apply_command(&interactive, MV_COMMAND_SEAT_NEXT) == 1
           && interactive.active_seat == expected_seat,
           "select active seat");
    record(mahjong_viz_apply_command(&interactive, MV_COMMAND_DORA_ADD) == 1
           && interactive.dora_count == 3,
           "add dora indicator");
    previous_river_count = interactive.seats[expected_seat].river_count;
    record(mahjong_viz_apply_command(&interactive, MV_COMMAND_RIVER_ADD) == 1
           && interactive.seats[expected_seat].river_count == previous_river_count + 1,
           "edit active river");
    previous_meld_count = interactive.seats[expected_seat].meld_count;
    record(mahjong_viz_apply_command(&interactive, MV_COMMAND_MELD_ADD) == 1
           && interactive.seats[expected_seat].meld_count == previous_meld_count + 1,
           "edit active melds");
    record(mahjong_viz_apply_command(&interactive, MV_COMMAND_REWARD_DELTA_UP) == 1,
           "adjust selected reward");

    mahjong_viz_frame_init(&action_frame);
    action_frame.seats[0].hand[0] = 4;
    action_frame.seats[0].hand_count = 1;
    action_frame.seats[1].hand[0] = 4;
    action_frame.seats[1].hand[1] = 4;
    action_frame.seats[1].hand_count = 2;
    action_frame.seats[2].hand[0] = 8;
    action_frame.seats[2].hand_count = 1;
    record(mahjong_viz_apply_model_action(&action_frame, 0, 4, -1) == 1
           && action_frame.seats[0].river_count == 1
           && action_frame.seats[0].river[0] == 4,
           "discard action updates river");
    record(action_frame.seats[0].hand_count == 0,
           "discard removes tile from visible hand");
    record(mahjong_viz_apply_model_action(&action_frame, 1, MV_ACTION_PON, -1) == 1
           && action_frame.seats[0].river_count == 0
           && action_frame.seats[1].meld_count == 1
           && action_frame.seats[1].melds[0].tile_count == 3,
           "pon removes claimed discard and adds meld");
    record(mahjong_viz_apply_model_action(&action_frame, 2, MV_ACTION_RIICHI, -1) == 1
           && action_frame.seats[2].is_riichi == 1,
           "riichi action sets visible state");
    record(mahjong_viz_apply_model_action(&action_frame, 2, 8, -1) == 1
           && action_frame.seats[2].riichi_discard_index == 0,
           "riichi discard is marked for rotation");

    mahjong_viz_frame_init(&action_frame);
    action_frame.seats[0].hand[0] = 3;
    action_frame.seats[0].hand_count = 1;
    action_frame.seats[1].hand[0] = 4;
    action_frame.seats[1].hand[1] = 5;
    action_frame.seats[1].hand_count = 2;
    record(mahjong_viz_apply_model_action(&action_frame, 0, 3, -1) == 1
           && mahjong_viz_apply_model_action(
               &action_frame, 1, MV_ACTION_CHI_UP, -1) == 1
           && action_frame.seats[0].river_count == 0
           && action_frame.seats[1].melds[0].tiles[0] == 3
           && action_frame.seats[1].melds[0].tiles[2] == 5,
           "chi removes discard and records sequence");

    mahjong_viz_frame_init(&action_frame);
    action_frame.seats[0].hand[0] = 31;
    action_frame.seats[0].hand_count = 1;
    action_frame.seats[1].hand[0] = 31;
    action_frame.seats[1].hand[1] = 31;
    action_frame.seats[1].hand[2] = 31;
    action_frame.seats[1].hand_count = 3;
    record(mahjong_viz_apply_model_action(&action_frame, 0, 31, -1) == 1
           && mahjong_viz_apply_model_action(
               &action_frame, 1, MV_ACTION_KAN_OPEN, -1) == 1
           && action_frame.seats[0].river_count == 0
           && action_frame.seats[1].melds[0].tile_count == 4,
           "open kan removes discard and records quad");

    mahjong_viz_frame_init(&action_frame);
    action_frame.seats[0].hand[0] = 4;
    action_frame.seats[0].hand_count = 1;
    action_frame.seats[1].hand[0] = 7;
    action_frame.seats[1].hand_count = 1;
    mahjong_viz_apply_model_action(&action_frame, 0, 4, -1);
    record(mahjong_viz_apply_model_action(
               &action_frame, 1, MV_ACTION_PON, -1) == 0
           && action_frame.seats[0].river_count == 1
           && action_frame.seats[1].meld_count == 0,
           "reject pon without two matching hand tiles");

    mahjong_viz_frame_init(&action_frame);
    action_frame.seats[0].hand[0] = 3;
    action_frame.seats[0].hand_count = 1;
    action_frame.seats[1].hand[0] = 4;
    action_frame.seats[1].hand_count = 1;
    mahjong_viz_apply_model_action(&action_frame, 0, 3, -1);
    record(mahjong_viz_apply_model_action(
               &action_frame, 1, MV_ACTION_CHI_UP, -1) == 0
           && action_frame.seats[0].river_count == 1
           && action_frame.seats[1].meld_count == 0,
           "reject chi without both required hand tiles");

    mahjong_viz_frame_init(&action_frame);
    action_frame.seats[0].hand[0] = 31;
    action_frame.seats[0].hand_count = 1;
    action_frame.seats[1].hand[0] = 31;
    action_frame.seats[1].hand_count = 1;
    mahjong_viz_apply_model_action(&action_frame, 0, 31, -1);
    record(mahjong_viz_apply_model_action(
               &action_frame, 1, MV_ACTION_KAN_OPEN, -1) == 0
           && action_frame.seats[0].river_count == 1
           && action_frame.seats[1].meld_count == 0,
           "reject open kan without three matching hand tiles");

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
               screenshot_path, &frame, 1440, 900, error, sizeof(error)) == 1,
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
    record(read_u32_le(header + 18) == 1440 && read_u32_le(header + 22) == 900,
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
        screenshot_path, &frame, 1440, 900, error, sizeof(error)
    );

    printf("%s", report);
    printf("RESULT  %d passed / %d failed\n", passed, failed);
    printf("ARTIFACT  %s\n", screenshot_path);
    return failed == 0 ? 0 : 1;
}
