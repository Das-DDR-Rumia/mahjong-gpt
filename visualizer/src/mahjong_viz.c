#include "mahjong_viz.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

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

typedef struct ControlButton {
    RECT bounds;
    MahjongVizCommand command;
    const char *label;
} ControlButton;

struct MahjongVizWindow {
    MahjongVizFrame frame;
    MahjongVizFrame initial_frame;
    int width;
    int height;
    HWND handle;
    DWORD last_step_tick;
    HDC back_dc;
    HBITMAP back_bitmap;
    HGDIOBJ back_old_bitmap;
    int back_width;
    int back_height;
};

typedef struct MahjongVizWindow WindowState;

void mahjong_viz_tile_label(uint8_t tile, char *label, size_t capacity);

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
        frame->selected_reward_event = -1;
        frame->last_discard_seat = -1;
        frame->last_discard_tile = -1;
        frame->bot_seat = 0;
        frame->step_interval_ms = 100;
        frame->pending_discard_seat = -1;
        frame->simulation_wall_position = 136;
        {
            int seat;
            for (seat = 0; seat < MV_SEAT_COUNT; ++seat) {
                frame->seats[seat].riichi_discard_index = -1;
            }
        }
    }
}

static int remove_hand_tile(MahjongVizSeat *seat, int tile) {
    int index;
    for (index = 0; index < seat->hand_count; ++index) {
        if (seat->hand[index] == tile) {
            memmove(&seat->hand[index], &seat->hand[index + 1],
                    (size_t)(seat->hand_count - index - 1));
            seat->hand_count -= 1;
            return 1;
        }
    }
    return 0;
}

static int count_hand_tile(const MahjongVizSeat *seat, int tile) {
    int index;
    int count = 0;
    for (index = 0; index < seat->hand_count; ++index) {
        if (seat->hand[index] == tile) count += 1;
    }
    return count;
}

int mahjong_viz_make_bot_observation(
    const MahjongVizFrame *frame,
    int bot_seat,
    MahjongVizFrame *observation
) {
    int seat;
    if (frame == NULL || observation == NULL
        || bot_seat < 0 || bot_seat >= MV_SEAT_COUNT
        || !mahjong_viz_validate_frame(frame, NULL, 0)) {
        return 0;
    }
    *observation = *frame;
    observation->bot_seat = bot_seat;
    memset(observation->simulation_wall, 0, sizeof(observation->simulation_wall));
    observation->simulation_wall_position = 136;
    for (seat = 0; seat < MV_SEAT_COUNT; ++seat) {
        if (seat != bot_seat) {
            memset(observation->seats[seat].hand, 0,
                   sizeof(observation->seats[seat].hand));
            observation->seats[seat].hand_count = 0;
        }
    }
    return 1;
}

static uint32_t shuffle_random(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

int mahjong_viz_shuffle_table(MahjongVizFrame *frame, uint32_t seed) {
    uint8_t wall[136];
    uint32_t state = seed;
    uint32_t used_seed;
    LARGE_INTEGER counter;
    int index;
    int seat;
    int cursor = 0;
    if (frame == NULL) return 0;
    if (state == 0) {
        if (BCryptGenRandom(NULL, (PUCHAR)&state, sizeof(state),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            QueryPerformanceCounter(&counter);
            state = (uint32_t)counter.LowPart ^ (uint32_t)counter.HighPart
                ^ (uint32_t)GetTickCount();
        }
        if (state == 0) state = 0x6D2B79F5u;
    }
    used_seed = state;
    for (index = 0; index < 136; ++index) wall[index] = (uint8_t)(index / 4);
    for (index = 135; index > 0; --index) {
        int other = (int)(shuffle_random(&state) % (uint32_t)(index + 1));
        uint8_t tile = wall[index];
        wall[index] = wall[other];
        wall[other] = tile;
    }
    for (seat = 0; seat < MV_SEAT_COUNT; ++seat) {
        MahjongVizSeat *view = &frame->seats[seat];
        memset(view, 0, sizeof(*view));
        view->riichi_discard_index = -1;
    }
    for (index = 0; index < 13; ++index) {
        for (seat = 0; seat < MV_SEAT_COUNT; ++seat) {
            MahjongVizSeat *view = &frame->seats[seat];
            view->hand[view->hand_count++] = wall[cursor++];
        }
    }
    frame->seats[0].hand[frame->seats[0].hand_count++] = wall[cursor++];
    frame->dora_count = 1;
    frame->dora_indicators[0] = wall[cursor++];
    memcpy(frame->simulation_wall, wall, sizeof(wall));
    frame->simulation_wall_position = (uint16_t)cursor;
    frame->active_seat = 0;
    frame->selected_action = -1;
    frame->action_confidence = 0.0;
    frame->last_discard_seat = -1;
    frame->last_discard_tile = -1;
    frame->pending_discard_seat = 0;
    snprintf(frame->last_action, sizeof(frame->last_action), "New shuffled hand");
    snprintf(frame->test_output, sizeof(frame->test_output),
             "New table shuffled.\nSeed: %u", used_seed);
    return mahjong_viz_validate_frame(frame, NULL, 0);
}

int mahjong_viz_apply_model_action(
    MahjongVizFrame *frame,
    int seat,
    int action,
    int tile_hint
) {
    MahjongVizSeat *actor;
    MahjongVizMeld *meld;
    int claimed_tile;
    int index;
    char label[8];

    if (frame == NULL || seat < 0 || seat >= MV_SEAT_COUNT
        || action < 0 || action > MV_ACTION_PASS
        || tile_hint < -1 || tile_hint >= 34) {
        return 0;
    }
    actor = &frame->seats[seat];
    frame->active_seat = seat;
    frame->selected_action = action;

    if (action <= 33) {
        if (actor->river_count >= MV_MAX_RIVER_TILES) return 0;
        if (!remove_hand_tile(actor, action)) return 0;
        if (actor->is_riichi && actor->riichi_discard_index < 0) {
            actor->riichi_discard_index = actor->river_count;
        }
        actor->river[actor->river_count++] = (uint8_t)action;
        frame->last_discard_seat = seat;
        frame->last_discard_tile = action;
        mahjong_viz_tile_label((uint8_t)action, label, sizeof(label));
        snprintf(frame->last_action, sizeof(frame->last_action),
                 "P%d discard %s", seat, label);
        return mahjong_viz_validate_frame(frame, NULL, 0);
    }

    if (action == MV_ACTION_RIICHI) {
        actor->is_riichi = 1;
        actor->riichi_discard_index = -1;
        snprintf(frame->last_action, sizeof(frame->last_action),
                 "P%d declares RIICHI", seat);
        return mahjong_viz_validate_frame(frame, NULL, 0);
    }

    if (action >= MV_ACTION_CHI_UP && action <= MV_ACTION_KAN_OPEN) {
        MahjongVizSeat *discarder;
        int base;
        if (frame->last_discard_seat < 0 || frame->last_discard_seat == seat
            || frame->last_discard_tile < 0 || actor->meld_count >= MV_MAX_MELDS) {
            return 0;
        }
        discarder = &frame->seats[frame->last_discard_seat];
        claimed_tile = frame->last_discard_tile;
        if (discarder->river_count == 0
            || discarder->river[discarder->river_count - 1] != claimed_tile) {
            return 0;
        }
        if (action >= MV_ACTION_CHI_UP && action <= MV_ACTION_CHI_DOWN) {
            if (claimed_tile >= 27) return 0;
            base = claimed_tile;
            if (action == MV_ACTION_CHI_MID) base -= 1;
            if (action == MV_ACTION_CHI_DOWN) base -= 2;
            if (base < 0 || base / 9 != claimed_tile / 9 || base % 9 > 6) return 0;
            for (index = 0; index < 3; ++index) {
                int needed = base + index;
                if (needed != claimed_tile && count_hand_tile(actor, needed) < 1) {
                    return 0;
                }
            }
        } else if (count_hand_tile(actor, claimed_tile)
                   < (action == MV_ACTION_PON ? 2 : 3)) {
            return 0;
        }
        meld = &actor->melds[actor->meld_count];
        memset(meld, 0, sizeof(*meld));
        meld->is_open = 1;
        if (action >= MV_ACTION_CHI_UP && action <= MV_ACTION_CHI_DOWN) {
            meld->tile_count = 3;
            meld->tiles[0] = (uint8_t)base;
            meld->tiles[1] = (uint8_t)(base + 1);
            meld->tiles[2] = (uint8_t)(base + 2);
            for (index = 0; index < 3; ++index) {
                if (meld->tiles[index] != claimed_tile) {
                    remove_hand_tile(actor, meld->tiles[index]);
                }
            }
            snprintf(frame->last_action, sizeof(frame->last_action), "P%d calls CHI", seat);
        } else {
            meld->tile_count = action == MV_ACTION_PON ? 3 : 4;
            for (index = 0; index < meld->tile_count; ++index) {
                meld->tiles[index] = (uint8_t)claimed_tile;
            }
            for (index = 0; index < meld->tile_count - 1; ++index) {
                remove_hand_tile(actor, claimed_tile);
            }
            snprintf(frame->last_action, sizeof(frame->last_action),
                     "P%d calls %s", seat,
                     action == MV_ACTION_PON ? "PON" : "KAN");
        }
        discarder->river_count -= 1;
        if (discarder->riichi_discard_index >= discarder->river_count) {
            discarder->riichi_discard_index = -1;
        }
        actor->meld_count += 1;
        frame->last_discard_seat = -1;
        frame->last_discard_tile = -1;
        return mahjong_viz_validate_frame(frame, NULL, 0);
    }

    if (action == MV_ACTION_KAN_ADD) {
        if (tile_hint < 0 || count_hand_tile(actor, tile_hint) < 1) return 0;
        for (index = 0; index < actor->meld_count; ++index) {
            meld = &actor->melds[index];
            if (meld->tile_count == 3 && meld->tiles[0] == tile_hint
                && meld->tiles[1] == tile_hint && meld->tiles[2] == tile_hint) {
                meld->tiles[3] = (uint8_t)tile_hint;
                meld->tile_count = 4;
                remove_hand_tile(actor, tile_hint);
                snprintf(frame->last_action, sizeof(frame->last_action),
                         "P%d calls added KAN", seat);
                return mahjong_viz_validate_frame(frame, NULL, 0);
            }
        }
        return 0;
    }

    if (action == MV_ACTION_KAN_CLOSED) {
        if (tile_hint < 0 || actor->meld_count >= MV_MAX_MELDS
            || count_hand_tile(actor, tile_hint) < 4) return 0;
        meld = &actor->melds[actor->meld_count++];
        memset(meld, 0, sizeof(*meld));
        meld->tile_count = 4;
        for (index = 0; index < 4; ++index) {
            meld->tiles[index] = (uint8_t)tile_hint;
            remove_hand_tile(actor, tile_hint);
        }
        snprintf(frame->last_action, sizeof(frame->last_action),
                 "P%d calls closed KAN", seat);
        return mahjong_viz_validate_frame(frame, NULL, 0);
    }

    snprintf(frame->last_action, sizeof(frame->last_action), "P%d %s", seat,
             action == MV_ACTION_RON ? "RON"
             : action == MV_ACTION_TSUMO ? "TSUMO" : "PASS");
    return mahjong_viz_validate_frame(frame, NULL, 0);
}

static double clamp_double(double value, double minimum, double maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static void append_reward_event(MahjongVizFrame *frame, double delta, const char *label) {
    MahjongVizRewardEvent *event;
    if (frame->reward_event_count == MV_MAX_REWARD_EVENTS) {
        memmove(&frame->reward_events[0], &frame->reward_events[1],
                sizeof(frame->reward_events[0]) * (MV_MAX_REWARD_EVENTS - 1));
        frame->reward_event_count -= 1;
    }
    event = &frame->reward_events[frame->reward_event_count++];
    memset(event, 0, sizeof(*event));
    event->seat = frame->active_seat < 0 ? 0 : frame->active_seat;
    event->action = frame->selected_action;
    event->delta = delta;
    snprintf(event->label, sizeof(event->label), "%s", label);
    frame->selected_reward_event = frame->reward_event_count - 1;
}

static int find_chi_action(const MahjongVizSeat *seat, int tile) {
    int action;
    if (tile < 0 || tile >= 27) return -1;
    for (action = MV_ACTION_CHI_UP; action <= MV_ACTION_CHI_DOWN; ++action) {
        int base = tile;
        int index;
        int valid = 1;
        if (action == MV_ACTION_CHI_MID) base -= 1;
        if (action == MV_ACTION_CHI_DOWN) base -= 2;
        if (base < 0 || base / 9 != tile / 9 || base % 9 > 6) continue;
        for (index = 0; index < 3; ++index) {
            int needed = base + index;
            if (needed != tile && count_hand_tile(seat, needed) < 1) {
                valid = 0;
                break;
            }
        }
        if (valid) return action;
    }
    return -1;
}

static int find_legal_claim(
    const MahjongVizFrame *frame,
    int *claimant,
    int *action
) {
    int distance;
    int discarder = frame->last_discard_seat;
    int tile = frame->last_discard_tile;
    if (discarder < 0 || tile < 0) return 0;
    for (distance = 1; distance < MV_SEAT_COUNT; ++distance) {
        int seat = (discarder + distance) % MV_SEAT_COUNT;
        const MahjongVizSeat *view = &frame->seats[seat];
        if (!view->is_riichi && view->meld_count < MV_MAX_MELDS
            && count_hand_tile(view, tile) >= 3) {
            *claimant = seat;
            *action = MV_ACTION_KAN_OPEN;
            return 1;
        }
    }
    for (distance = 1; distance < MV_SEAT_COUNT; ++distance) {
        int seat = (discarder + distance) % MV_SEAT_COUNT;
        const MahjongVizSeat *view = &frame->seats[seat];
        if (!view->is_riichi && view->meld_count < MV_MAX_MELDS
            && count_hand_tile(view, tile) >= 2) {
            *claimant = seat;
            *action = MV_ACTION_PON;
            return 1;
        }
    }
    {
        int seat = (discarder + 1) % MV_SEAT_COUNT;
        const MahjongVizSeat *view = &frame->seats[seat];
        int chi = find_chi_action(view, tile);
        if (!view->is_riichi && view->meld_count < MV_MAX_MELDS && chi >= 0) {
            *claimant = seat;
            *action = chi;
            return 1;
        }
    }
    return 0;
}

static int draw_simulation_tile(MahjongVizFrame *frame, int seat) {
    MahjongVizSeat *view = &frame->seats[seat];
    if (view->hand_count >= MV_MAX_HAND_TILES
        || frame->simulation_wall_position >= 136) {
        return 0;
    }
    view->hand[view->hand_count++] =
        frame->simulation_wall[frame->simulation_wall_position++];
    return 1;
}

static int first_quad_tile(const MahjongVizSeat *seat) {
    int tile;
    for (tile = 0; tile < 34; ++tile) {
        if (count_hand_tile(seat, tile) >= 4) return tile;
    }
    return -1;
}

static int first_added_kan_tile(const MahjongVizSeat *seat) {
    int meld;
    for (meld = 0; meld < seat->meld_count; ++meld) {
        const MahjongVizMeld *group = &seat->melds[meld];
        if (group->tile_count == 3
            && group->tiles[0] == group->tiles[1]
            && group->tiles[1] == group->tiles[2]
            && count_hand_tile(seat, group->tiles[0]) >= 1) {
            return group->tiles[0];
        }
    }
    return -1;
}

static void apply_training_step(MahjongVizFrame *frame) {
    uint64_t previous_round = frame->training_round;
    double delta;
    int actor = -1;
    int action = -1;
    int tile_hint = -1;
    int applied = 0;

    frame->training_round += 1;
    if (frame->pending_discard_seat >= 0) {
        actor = frame->pending_discard_seat;
        frame->pending_discard_seat = -1;
    } else if (frame->last_discard_seat >= 0) {
        int discarder = frame->last_discard_seat;
        if (find_legal_claim(frame, &actor, &action)) {
            applied = mahjong_viz_apply_model_action(frame, actor, action, -1);
            if (applied) frame->pending_discard_seat = actor;
        } else {
            frame->last_discard_seat = -1;
            frame->last_discard_tile = -1;
            actor = (discarder + 1) % MV_SEAT_COUNT;
        }
    } else {
        actor = frame->active_seat < 0 ? 0 : (frame->active_seat + 1) % MV_SEAT_COUNT;
    }

    if (!applied) {
        MahjongVizSeat *view;
        int concealed_base;
        if (actor < 0) actor = 0;
        view = &frame->seats[actor];
        concealed_base = 13 - 3 * view->meld_count;
        if (view->hand_count <= concealed_base) {
            if (!draw_simulation_tile(frame, actor)) {
                mahjong_viz_shuffle_table(frame, 0);
                actor = 0;
                frame->pending_discard_seat = -1;
                view = &frame->seats[actor];
            }
        }

        tile_hint = first_added_kan_tile(view);
        if (tile_hint >= 0 && (frame->training_round % 3u) == 0) {
            action = MV_ACTION_KAN_ADD;
            applied = mahjong_viz_apply_model_action(frame, actor, action, tile_hint);
            if (applied) frame->pending_discard_seat = actor;
        }
        if (!applied) {
            tile_hint = first_quad_tile(view);
            if (tile_hint >= 0 && view->meld_count < MV_MAX_MELDS) {
                action = MV_ACTION_KAN_CLOSED;
                applied = mahjong_viz_apply_model_action(frame, actor, action, tile_hint);
                if (applied) frame->pending_discard_seat = actor;
            }
        }
        if (!applied && view->hand_count > 0) {
            int pick = (int)(frame->training_round % view->hand_count);
            action = view->hand[pick];
            applied = mahjong_viz_apply_model_action(frame, actor, action, -1);
        }
    }

    if (!applied) {
        mahjong_viz_shuffle_table(frame, 0);
        snprintf(frame->test_output, sizeof(frame->test_output),
                 "Started a new hand after the wall or legal actions were exhausted.");
        return;
    }
    frame->action_confidence += 0.037;
    if (frame->action_confidence > 0.98) frame->action_confidence = 0.52;
    delta = ((int)(frame->training_round % 9u) - 4) * 0.8;
    frame->average_reward = previous_round == 0
        ? delta
        : (frame->average_reward * (double)previous_round + delta)
          / (double)frame->training_round;
    append_reward_event(frame, delta, "interactive step");
    snprintf(frame->test_output, sizeof(frame->test_output),
             "Interactive training step completed.\nRound %llu / reward %+.1f",
             (unsigned long long)frame->training_round, delta);
}

const char *mahjong_viz_command_name(MahjongVizCommand command) {
    static const char *names[] = {
        "none", "toggle run", "step", "reset", "round down", "round up",
        "average reward down", "average reward up", "previous seat", "next seat",
        "previous action", "next action", "confidence down", "confidence up",
        "remove dora", "add dora", "cycle dora", "toggle riichi",
        "remove river tile", "add river tile", "cycle river tile",
        "remove meld", "add meld", "cycle meld tile",
        "previous reward", "next reward", "reward delta down", "reward delta up",
        "toggle advanced controls",
        "slower", "faster",
        "export BMP"
    };
    if (command < MV_COMMAND_NONE || command > MV_COMMAND_EXPORT_BMP) return "unknown";
    return names[(int)command];
}

int mahjong_viz_apply_command(MahjongVizFrame *frame, MahjongVizCommand command) {
    MahjongVizSeat *seat;
    MahjongVizRewardEvent *reward;
    if (frame == NULL || command <= MV_COMMAND_NONE || command > MV_COMMAND_EXPORT_BMP) {
        return 0;
    }
    seat = frame->active_seat >= 0 && frame->active_seat < MV_SEAT_COUNT
        ? &frame->seats[frame->active_seat] : &frame->seats[0];
    switch (command) {
        case MV_COMMAND_TOGGLE_RUN:
            frame->training_running = !frame->training_running;
            break;
        case MV_COMMAND_STEP:
            apply_training_step(frame);
            break;
        case MV_COMMAND_ROUND_DOWN:
            if (frame->training_round > 0) frame->training_round -= 1;
            break;
        case MV_COMMAND_ROUND_UP:
            frame->training_round += 1;
            break;
        case MV_COMMAND_AVERAGE_REWARD_DOWN:
            frame->average_reward -= 1.0;
            break;
        case MV_COMMAND_AVERAGE_REWARD_UP:
            frame->average_reward += 1.0;
            break;
        case MV_COMMAND_SEAT_PREVIOUS:
            frame->active_seat = frame->active_seat < 0 ? 0
                : (frame->active_seat + MV_SEAT_COUNT - 1) % MV_SEAT_COUNT;
            break;
        case MV_COMMAND_SEAT_NEXT:
            frame->active_seat = frame->active_seat < 0 ? 0
                : (frame->active_seat + 1) % MV_SEAT_COUNT;
            break;
        case MV_COMMAND_ACTION_PREVIOUS:
            frame->selected_action = frame->selected_action <= 0 ? 45 : frame->selected_action - 1;
            break;
        case MV_COMMAND_ACTION_NEXT:
            frame->selected_action = frame->selected_action < 0 ? 0 : (frame->selected_action + 1) % 46;
            break;
        case MV_COMMAND_CONFIDENCE_DOWN:
            frame->action_confidence = clamp_double(frame->action_confidence - 0.05, 0.0, 1.0);
            break;
        case MV_COMMAND_CONFIDENCE_UP:
            frame->action_confidence = clamp_double(frame->action_confidence + 0.05, 0.0, 1.0);
            break;
        case MV_COMMAND_DORA_REMOVE:
            if (frame->dora_count > 0) frame->dora_count -= 1;
            break;
        case MV_COMMAND_DORA_ADD:
            if (frame->dora_count < MV_MAX_DORA) frame->dora_indicators[frame->dora_count++] = 0;
            break;
        case MV_COMMAND_DORA_CYCLE:
            if (frame->dora_count > 0) {
                int index = frame->dora_count - 1;
                frame->dora_indicators[index] = (uint8_t)((frame->dora_indicators[index] + 1) % 34);
            }
            break;
        case MV_COMMAND_RIICHI_TOGGLE:
            seat->is_riichi = !seat->is_riichi;
            if (!seat->is_riichi) seat->riichi_discard_index = -1;
            break;
        case MV_COMMAND_RIVER_REMOVE:
            if (seat->river_count > 0) {
                seat->river_count -= 1;
                if (seat->riichi_discard_index >= seat->river_count) {
                    seat->riichi_discard_index = -1;
                }
            }
            break;
        case MV_COMMAND_RIVER_ADD:
            if (seat->river_count < MV_MAX_RIVER_TILES) {
                if (seat->is_riichi && seat->riichi_discard_index < 0) {
                    seat->riichi_discard_index = seat->river_count;
                }
                seat->river[seat->river_count++] = 0;
            }
            break;
        case MV_COMMAND_RIVER_CYCLE:
            if (seat->river_count > 0) {
                int index = seat->river_count - 1;
                seat->river[index] = (uint8_t)((seat->river[index] + 1) % 34);
            }
            break;
        case MV_COMMAND_MELD_REMOVE:
            if (seat->meld_count > 0) seat->meld_count -= 1;
            break;
        case MV_COMMAND_MELD_ADD: {
            if (seat->meld_count < MV_MAX_MELDS) {
                MahjongVizMeld *meld = &seat->melds[seat->meld_count++];
                memset(meld, 0, sizeof(*meld));
                meld->tile_count = 3;
                meld->is_open = 1;
            }
            break;
        }
        case MV_COMMAND_MELD_CYCLE: {
            if (seat->meld_count > 0) {
                MahjongVizMeld *meld = &seat->melds[seat->meld_count - 1];
                int index = meld->tile_count - 1;
                meld->tiles[index] = (uint8_t)((meld->tiles[index] + 1) % 34);
            }
            break;
        }
        case MV_COMMAND_REWARD_PREVIOUS:
            if (frame->reward_event_count > 0) {
                frame->selected_reward_event = frame->selected_reward_event <= 0
                    ? frame->reward_event_count - 1 : frame->selected_reward_event - 1;
            }
            break;
        case MV_COMMAND_REWARD_NEXT:
            if (frame->reward_event_count > 0) {
                frame->selected_reward_event = (frame->selected_reward_event + 1)
                    % frame->reward_event_count;
            }
            break;
        case MV_COMMAND_REWARD_DELTA_DOWN:
        case MV_COMMAND_REWARD_DELTA_UP:
            if (frame->selected_reward_event >= 0
                && frame->selected_reward_event < frame->reward_event_count) {
                reward = &frame->reward_events[frame->selected_reward_event];
                reward->delta += command == MV_COMMAND_REWARD_DELTA_UP ? 0.5 : -0.5;
            }
            break;
        case MV_COMMAND_TOGGLE_ADVANCED:
            frame->show_advanced_controls = !frame->show_advanced_controls;
            break;
        case MV_COMMAND_SPEED_SLOWER:
            if (frame->step_interval_ms <= 50) frame->step_interval_ms = 100;
            else if (frame->step_interval_ms <= 100) frame->step_interval_ms = 250;
            else if (frame->step_interval_ms <= 250) frame->step_interval_ms = 500;
            else frame->step_interval_ms = 1000;
            break;
        case MV_COMMAND_SPEED_FASTER:
            if (frame->step_interval_ms >= 1000) frame->step_interval_ms = 500;
            else if (frame->step_interval_ms >= 500) frame->step_interval_ms = 250;
            else if (frame->step_interval_ms >= 250) frame->step_interval_ms = 100;
            else frame->step_interval_ms = 50;
            break;
        case MV_COMMAND_RESET:
        case MV_COMMAND_EXPORT_BMP:
            return 1;
        default:
            return 0;
    }
    return mahjong_viz_validate_frame(frame, NULL, 0);
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
    if (frame->bot_seat < 0 || frame->bot_seat >= MV_SEAT_COUNT) {
        set_error(error, error_capacity, "bot seat is outside 0..3");
        return 0;
    }
    if (frame->pending_discard_seat < -1
        || frame->pending_discard_seat >= MV_SEAT_COUNT) {
        set_error(error, error_capacity, "pending discard seat is outside 0..3");
        return 0;
    }
    if (frame->simulation_wall_position > 136) {
        set_error(error, error_capacity, "simulation wall position is invalid");
        return 0;
    }
    for (index = frame->simulation_wall_position; index < 136; ++index) {
        if (!valid_tile(frame->simulation_wall[index])) {
            set_error(error, error_capacity, "invalid tile in simulation wall");
            return 0;
        }
    }
    if (frame->training_running != 0 && frame->training_running != 1) {
        set_error(error, error_capacity, "training running flag must be 0 or 1");
        return 0;
    }
    if (frame->show_advanced_controls != 0 && frame->show_advanced_controls != 1) {
        set_error(error, error_capacity, "advanced controls flag must be 0 or 1");
        return 0;
    }
    if (frame->step_interval_ms < 50 || frame->step_interval_ms > 1000) {
        set_error(error, error_capacity, "step interval must be between 50 and 1000 ms");
        return 0;
    }
    if (frame->selected_reward_event < -1
        || frame->selected_reward_event >= frame->reward_event_count) {
        set_error(error, error_capacity, "selected reward event is outside the list");
        return 0;
    }
    if ((frame->last_discard_seat == -1) != (frame->last_discard_tile == -1)
        || frame->last_discard_seat < -1 || frame->last_discard_seat >= MV_SEAT_COUNT
        || frame->last_discard_tile < -1 || frame->last_discard_tile >= 34) {
        set_error(error, error_capacity, "last discard reference is invalid");
        return 0;
    }
    for (seat = 0; seat < MV_SEAT_COUNT; ++seat) {
        const MahjongVizSeat *view = &frame->seats[seat];
        if (view->hand_count > MV_MAX_HAND_TILES) {
            set_error(error, error_capacity, "too many hand tiles");
            return 0;
        }
        if (view->river_count > MV_MAX_RIVER_TILES) {
            set_error(error, error_capacity, "too many river tiles");
            return 0;
        }
        if (view->meld_count > MV_MAX_MELDS) {
            set_error(error, error_capacity, "too many melds");
            return 0;
        }
        if (view->riichi_discard_index < -1
            || view->riichi_discard_index >= view->river_count) {
            set_error(error, error_capacity, "riichi discard index is outside the river");
            return 0;
        }
        for (index = 0; index < view->hand_count; ++index) {
            if (!valid_tile(view->hand[index])) {
                set_error(error, error_capacity, "invalid hand tile");
                return 0;
            }
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

static void draw_output_tail(
    DrawContext *ctx,
    const char *text,
    int x,
    int y,
    int width,
    int height
) {
    const char *start = text != NULL ? text : "";
    const char *cursor = start + strlen(start);
    int lines = 0;
    while (cursor > start) {
        cursor -= 1;
        if (*cursor == '\n' && ++lines == 8) {
            start = cursor + 1;
            break;
        }
    }
    draw_text_box(ctx, start, x, y, width, height, ctx->font_mono,
                  MV_RGB(151, 224, 182),
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

void mahjong_viz_tile_label(uint8_t tile, char *label, size_t capacity) {
    if (tile < 9) {
        snprintf(label, capacity, "%dm", tile + 1);
    } else if (tile < 18) {
        snprintf(label, capacity, "%dp", tile - 8);
    } else if (tile < 27) {
        snprintf(label, capacity, "%ds", tile - 17);
    } else {
        snprintf(label, capacity, "%dz", tile - 26);
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
    mahjong_viz_tile_label(tile, label, sizeof(label));
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
    if (seat->is_riichi) {
        round_panel(ctx->dc, x + 174, y + 5, 66, 10, 5,
                    MV_RGB(245, 240, 222), MV_RGB(215, 220, 220));
        fill_rect_color(ctx->dc, x + 199, y + 7, 16, 6, MV_RGB(221, 52, 72));
    }
    for (index = 0; index < seat->river_count; ++index) {
        int col = index % 6;
        int row = index / 6;
        int row_start = row * 6;
        int extra = seat->riichi_discard_index >= row_start
            && seat->riichi_discard_index < index ? 10 : 0;
        if (index == seat->riichi_discard_index) {
            draw_tile(ctx, x + col * 29, y + 29 + row * 38, 34, 25, seat->river[index]);
        } else {
            draw_tile(ctx, x + col * 29 + extra, y + 24 + row * 38,
                      25, 34, seat->river[index]);
        }
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
    int row;
    int start = frame->reward_event_count > 3 ? frame->reward_event_count - 3 : 0;
    if (frame->selected_reward_event >= 0 && frame->reward_event_count > 3) {
        start = frame->selected_reward_event - 1;
        if (start < 0) start = 0;
        if (start > frame->reward_event_count - 3) start = frame->reward_event_count - 3;
    }
    for (index = start; index < frame->reward_event_count && index < start + 3; ++index) {
        const MahjongVizRewardEvent *event = &frame->reward_events[index];
        char left[96];
        char value[32];
        COLORREF color = event->delta >= 0.0
            ? MV_RGB(75, 210, 142)
            : MV_RGB(247, 104, 125);
        snprintf(left, sizeof(left), "%sP%d  %s",
                 index == frame->selected_reward_event ? "> " : "  ",
                 event->seat, event->label);
        snprintf(value, sizeof(value), "%+.1f", event->delta);
        row = index - start;
        draw_text_box(ctx, left, x, y + row * 25, 270, 22, ctx->font_small,
                      MV_RGB(207, 216, 225), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        draw_text_box(ctx, value, x + 275, y + row * 25, 65, 22, ctx->font_body,
                      color, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
}

static void draw_hand(
    DrawContext *ctx,
    const MahjongVizSeat *seat,
    int seat_index,
    int bot_seat,
    int x,
    int y
) {
    int index;
    char label[64];
    int bot_visible = seat_index == bot_seat;
    snprintf(label, sizeof(label), "P%d HAND  /  %s", seat_index,
             bot_visible ? "BOT" : "USER VIEW");
    draw_text_box(ctx, label, x, y, 308, 18, ctx->font_small,
                  bot_visible ? MV_RGB(142, 224, 203) : MV_RGB(240, 184, 102),
                  DT_LEFT | DT_SINGLELINE);
    for (index = 0; index < seat->hand_count; ++index) {
        draw_tile(ctx, x + index * 22, y + 20, 20, 28, seat->hand[index]);
    }
    if (seat->hand_count == 0) {
        draw_text_box(ctx, "HIDDEN", x, y + 20, 100, 28, ctx->font_small,
                      MV_RGB(104, 118, 132), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

static int add_button(
    ControlButton *buttons,
    int count,
    int x,
    int y,
    int width,
    int height,
    MahjongVizCommand command,
    const char *label
) {
    buttons[count].bounds = (RECT){x, y, x + width, y + height};
    buttons[count].command = command;
    buttons[count].label = label;
    return count + 1;
}

static int build_buttons(
    int width,
    const MahjongVizFrame *frame,
    ControlButton *buttons
) {
    int x = width - 420;
    int count = 0;
    count = add_button(buttons, count, x + 318, 25, 36, 30,
                       MV_COMMAND_SPEED_SLOWER, "-");
    count = add_button(buttons, count, x + 364, 25, 36, 30,
                       MV_COMMAND_SPEED_FASTER, "+");
    count = add_button(buttons, count, x + 20, 193, 116, 40,
                       MV_COMMAND_TOGGLE_RUN, frame->training_running ? "PAUSE" : "RUN");
    count = add_button(buttons, count, x + 146, 193, 116, 40,
                       MV_COMMAND_STEP, "STEP");
    count = add_button(buttons, count, x + 272, 193, 128, 40,
                       MV_COMMAND_RESET, "RESET");
    count = add_button(buttons, count, x + 20, 242, 380, 30,
                       MV_COMMAND_TOGGLE_ADVANCED,
                       frame->show_advanced_controls ? "HIDE ADVANCED" : "ADVANCED CONTROLS");

    if (!frame->show_advanced_controls) return count;

    count = add_button(buttons, count, x + 184, 271, 56, 28, MV_COMMAND_ROUND_DOWN, "-");
    count = add_button(buttons, count, x + 248, 271, 56, 28, MV_COMMAND_ROUND_UP, "+");
    count = add_button(buttons, count, x + 184, 307, 56, 28, MV_COMMAND_AVERAGE_REWARD_DOWN, "-");
    count = add_button(buttons, count, x + 248, 307, 56, 28, MV_COMMAND_AVERAGE_REWARD_UP, "+");
    count = add_button(buttons, count, x + 184, 343, 56, 28, MV_COMMAND_SEAT_PREVIOUS, "<");
    count = add_button(buttons, count, x + 248, 343, 56, 28, MV_COMMAND_SEAT_NEXT, ">");
    count = add_button(buttons, count, x + 184, 379, 56, 28, MV_COMMAND_ACTION_PREVIOUS, "<");
    count = add_button(buttons, count, x + 248, 379, 56, 28, MV_COMMAND_ACTION_NEXT, ">");
    count = add_button(buttons, count, x + 184, 415, 56, 28, MV_COMMAND_CONFIDENCE_DOWN, "-");
    count = add_button(buttons, count, x + 248, 415, 56, 28, MV_COMMAND_CONFIDENCE_UP, "+");
    count = add_button(buttons, count, x + 184, 451, 48, 28, MV_COMMAND_DORA_REMOVE, "-");
    count = add_button(buttons, count, x + 240, 451, 48, 28, MV_COMMAND_DORA_ADD, "+");
    count = add_button(buttons, count, x + 296, 451, 86, 28, MV_COMMAND_DORA_CYCLE, "CYCLE");
    count = add_button(buttons, count, x + 184, 487, 120, 28, MV_COMMAND_RIICHI_TOGGLE, "TOGGLE");
    count = add_button(buttons, count, x + 184, 523, 48, 28, MV_COMMAND_RIVER_REMOVE, "-");
    count = add_button(buttons, count, x + 240, 523, 48, 28, MV_COMMAND_RIVER_ADD, "+");
    count = add_button(buttons, count, x + 296, 523, 86, 28, MV_COMMAND_RIVER_CYCLE, "CYCLE");
    count = add_button(buttons, count, x + 184, 559, 48, 28, MV_COMMAND_MELD_REMOVE, "-");
    count = add_button(buttons, count, x + 240, 559, 48, 28, MV_COMMAND_MELD_ADD, "+");
    count = add_button(buttons, count, x + 296, 559, 86, 28, MV_COMMAND_MELD_CYCLE, "CYCLE");
    count = add_button(buttons, count, x + 184, 595, 42, 28, MV_COMMAND_REWARD_PREVIOUS, "<");
    count = add_button(buttons, count, x + 232, 595, 42, 28, MV_COMMAND_REWARD_NEXT, ">");
    count = add_button(buttons, count, x + 280, 595, 48, 28, MV_COMMAND_REWARD_DELTA_DOWN, "-0.5");
    count = add_button(buttons, count, x + 334, 595, 48, 28, MV_COMMAND_REWARD_DELTA_UP, "+0.5");
    count = add_button(buttons, count, x + 184, 631, 198, 30, MV_COMMAND_EXPORT_BMP, "EXPORT BMP");
    return count;
}

static void draw_button(
    DrawContext *ctx,
    const ControlButton *button,
    int emphasized
) {
    int width = button->bounds.right - button->bounds.left;
    int height = button->bounds.bottom - button->bounds.top;
    round_panel(ctx->dc, button->bounds.left, button->bounds.top, width, height, 7,
                emphasized ? MV_RGB(118, 88, 196) : MV_RGB(42, 50, 70),
                emphasized ? MV_RGB(179, 154, 244) : MV_RGB(76, 88, 116));
    draw_text_box(ctx, button->label, button->bounds.left, button->bounds.top,
                  width, height, ctx->font_small, MV_RGB(239, 239, 247),
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

static void draw_controls(DrawContext *ctx, const MahjongVizFrame *frame, int width) {
    static const char *labels[] = {
        "Round", "Avg reward", "Active seat", "Action", "Confidence",
        "Dora last", "Riichi / active", "River last / active", "Meld last / active",
        "Reward event", "Snapshot"
    };
    ControlButton buttons[32];
    int count = build_buttons(width, frame, buttons);
    int sidebar_x = width - 420;
    int index;

    if (frame->show_advanced_controls) {
        round_panel(ctx->dc, sidebar_x + 20, 252, 380, 425, 13,
                    MV_RGB(24, 31, 46), MV_RGB(53, 63, 82));
        for (index = 0; index < 11; ++index) {
            draw_text_box(ctx, labels[index], sidebar_x + 36, 274 + index * 36,
                          138, 26, ctx->font_small, MV_RGB(164, 177, 195),
                          DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
    }
    for (index = 0; index < count; ++index) {
        int emphasized = buttons[index].command == MV_COMMAND_TOGGLE_RUN
            && frame->training_running;
        draw_button(ctx, &buttons[index], emphasized);
    }
}

static void draw_frame(HDC dc, int width, int height, const MahjongVizFrame *frame) {
    DrawContext ctx;
    char metric[96];
    int sidebar_x = width - 420;
    int index;

    context_init(&ctx, dc);
    fill_rect_color(dc, 0, 0, width, height, MV_RGB(13, 18, 28));

    draw_text_box(&ctx, "MAHJONG-GPT", 30, 20, 300, 38, ctx.font_heading,
                  MV_RGB(237, 240, 246), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    draw_text_box(&ctx, "P0 bot view  /  opponents visible to user", 222, 26, 420, 28,
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

    draw_hand(&ctx, &frame->seats[2], 2, frame->bot_seat, 320, 273);
    draw_hand(&ctx, &frame->seats[3], 3, frame->bot_seat, 28, 470);
    draw_hand(&ctx, &frame->seats[1], 1, frame->bot_seat, 645, 470);
    draw_hand(&ctx, &frame->seats[0], 0, frame->bot_seat, 320, 690);

    round_panel(dc, 356, 326, 220, 145, 18,
                MV_RGB(14, 50, 49), MV_RGB(102, 180, 161));
    snprintf(metric, sizeof(metric), "ACTIVE PLAYER  P%d", frame->active_seat);
    draw_text_box(&ctx, metric, 371, 342, 190, 24, ctx.font_small,
                  MV_RGB(142, 224, 203), DT_CENTER | DT_SINGLELINE);
    draw_text_box(&ctx, frame->last_action, 371, 373, 190, 32, ctx.font_body,
                  MV_RGB(247, 247, 242), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    snprintf(metric, sizeof(metric), "action %d   confidence %.1f%%",
             frame->selected_action, frame->action_confidence * 100.0);
    draw_text_box(&ctx, metric, 371, 414, 190, 25, ctx.font_small,
                  MV_RGB(171, 184, 195), DT_CENTER | DT_SINGLELINE);

    fill_rect_color(dc, sidebar_x, 0, width - sidebar_x, height, MV_RGB(18, 24, 36));
    snprintf(metric, sizeof(metric), "TRAINING  /  %u SPS",
             frame->step_interval_ms == 0 ? 0 : 1000u / frame->step_interval_ms);
    draw_text_box(&ctx, metric, sidebar_x + 24, 24, 286, 32, ctx.font_heading,
                  MV_RGB(235, 238, 245), DT_LEFT | DT_SINGLELINE);

    round_panel(dc, sidebar_x + 20, 70, 180, 104, 13,
                MV_RGB(28, 35, 51), MV_RGB(55, 65, 85));
    draw_text_box(&ctx, "ROUND", sidebar_x + 34, 84, 120, 20, ctx.font_small,
                  MV_RGB(145, 158, 176), DT_LEFT | DT_SINGLELINE);
    snprintf(metric, sizeof(metric), "%llu", (unsigned long long)frame->training_round);
    draw_text_box(&ctx, metric, sidebar_x + 34, 110, 120, 48, ctx.font_metric,
                  MV_RGB(222, 210, 255), DT_LEFT | DT_SINGLELINE);

    round_panel(dc, sidebar_x + 210, 70, 190, 104, 13,
                MV_RGB(28, 35, 51), MV_RGB(55, 65, 85));
    draw_text_box(&ctx, "AVG REWARD", sidebar_x + 224, 84, 160, 20, ctx.font_small,
                  MV_RGB(145, 158, 176), DT_LEFT | DT_SINGLELINE);
    snprintf(metric, sizeof(metric), "%+.2f", frame->average_reward);
    draw_text_box(&ctx, metric, sidebar_x + 224, 110, 160, 48, ctx.font_metric,
                  frame->average_reward >= 0.0 ? MV_RGB(83, 214, 149) : MV_RGB(247, 104, 125),
                  DT_LEFT | DT_SINGLELINE);

    draw_controls(&ctx, frame, width);

    {
        int rewards_y = frame->show_advanced_controls ? 684 : 292;
        int output_y = frame->show_advanced_controls ? 790 : 458;
        round_panel(dc, sidebar_x + 20, rewards_y, 380,
                    frame->show_advanced_controls ? 104 : 146, 13,
                MV_RGB(24, 31, 46), MV_RGB(53, 63, 82));
        draw_text_box(&ctx, "STEP REWARDS", sidebar_x + 36, rewards_y + 6,
                      340, 24, ctx.font_body,
                  MV_RGB(230, 233, 240), DT_LEFT | DT_SINGLELINE);
        draw_rewards(&ctx, frame, sidebar_x + 36, rewards_y + 29);

        round_panel(dc, sidebar_x + 20, output_y, 380, height - output_y - 20, 13,
                MV_RGB(9, 14, 22), MV_RGB(53, 63, 82));
        draw_text_box(&ctx, "OUTPUT", sidebar_x + 36, output_y + 8,
                      340, 23, ctx.font_body,
                  MV_RGB(221, 225, 234), DT_LEFT | DT_SINGLELINE);
        draw_output_tail(&ctx, frame->test_output, sidebar_x + 36, output_y + 34,
                         344, height - output_y - 54);
    }

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
    if (path == NULL || width < 1280 || height < 860) {
        set_error(error, error_capacity, "path is null or render size is below 1280x860");
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

static void run_window_command(WindowState *state, MahjongVizCommand command) {
    char error[128] = {0};
    if (state == NULL) return;
    if (command == MV_COMMAND_RESET) {
        LARGE_INTEGER counter;
        state->frame = state->initial_frame;
        QueryPerformanceCounter(&counter);
        mahjong_viz_shuffle_table(
            &state->frame,
            (uint32_t)counter.LowPart ^ (uint32_t)counter.HighPart
            ^ (uint32_t)GetTickCount()
        );
    } else if (command == MV_COMMAND_EXPORT_BMP) {
        if (mahjong_viz_render_bmp("mahjong_viz_interactive.bmp", &state->frame,
                                   state->width, state->height, error, sizeof(error))) {
            snprintf(state->frame.test_output, sizeof(state->frame.test_output),
                     "Snapshot saved:\nmahjong_viz_interactive.bmp");
        } else {
            snprintf(state->frame.test_output, sizeof(state->frame.test_output),
                     "Snapshot failed:\n%s", error);
        }
    } else if (mahjong_viz_apply_command(&state->frame, command)) {
        if (command == MV_COMMAND_TOGGLE_RUN) {
            state->last_step_tick = GetTickCount();
        }
        if (command != MV_COMMAND_STEP) {
            snprintf(state->frame.test_output, sizeof(state->frame.test_output),
                     "Control applied: %s", mahjong_viz_command_name(command));
        }
    }
    InvalidateRect(state->handle, NULL, FALSE);
}

static MahjongVizCommand hit_test_command(WindowState *state, int x, int y) {
    ControlButton buttons[32];
    POINT point = {x, y};
    int index;
    int count = build_buttons(state->width, &state->frame, buttons);
    for (index = 0; index < count; ++index) {
        if (PtInRect(&buttons[index].bounds, point)) return buttons[index].command;
    }
    return MV_COMMAND_NONE;
}

static void destroy_back_buffer(WindowState *state) {
    if (state == NULL) return;
    if (state->back_dc != NULL && state->back_old_bitmap != NULL) {
        SelectObject(state->back_dc, state->back_old_bitmap);
    }
    if (state->back_bitmap != NULL) DeleteObject(state->back_bitmap);
    if (state->back_dc != NULL) DeleteDC(state->back_dc);
    state->back_dc = NULL;
    state->back_bitmap = NULL;
    state->back_old_bitmap = NULL;
    state->back_width = 0;
    state->back_height = 0;
}

static int ensure_back_buffer(WindowState *state, HDC target, int width, int height) {
    if (state->back_dc != NULL
        && state->back_width == width && state->back_height == height) {
        return 1;
    }
    destroy_back_buffer(state);
    state->back_dc = CreateCompatibleDC(target);
    state->back_bitmap = CreateCompatibleBitmap(target, width, height);
    if (state->back_dc == NULL || state->back_bitmap == NULL) {
        destroy_back_buffer(state);
        return 0;
    }
    state->back_old_bitmap = SelectObject(state->back_dc, state->back_bitmap);
    state->back_width = width;
    state->back_height = height;
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
        RECT client;
        HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &client);
        if (ensure_back_buffer(state, dc, client.right, client.bottom)) {
            draw_frame(state->back_dc, client.right, client.bottom, &state->frame);
            BitBlt(dc, 0, 0, client.right, client.bottom,
                   state->back_dc, 0, 0, SRCCOPY);
        } else {
            draw_frame(dc, client.right, client.bottom, &state->frame);
        }
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_LBUTTONDOWN && state != NULL) {
        MahjongVizCommand command = hit_test_command(
            state, (short)LOWORD(lparam), (short)HIWORD(lparam)
        );
        if (command != MV_COMMAND_NONE) run_window_command(state, command);
        return 0;
    }
    if (message == WM_KEYDOWN && state != NULL) {
        MahjongVizCommand command = MV_COMMAND_NONE;
        if (wparam == VK_SPACE) command = MV_COMMAND_TOGGLE_RUN;
        else if (wparam == VK_RETURN) command = MV_COMMAND_STEP;
        else if (wparam == VK_HOME) command = MV_COMMAND_RESET;
        else if (wparam == VK_LEFT) command = MV_COMMAND_ACTION_PREVIOUS;
        else if (wparam == VK_RIGHT) command = MV_COMMAND_ACTION_NEXT;
        else if (wparam == VK_UP) command = MV_COMMAND_CONFIDENCE_UP;
        else if (wparam == VK_DOWN) command = MV_COMMAND_CONFIDENCE_DOWN;
        else if (wparam == VK_OEM_4) command = MV_COMMAND_SPEED_SLOWER;
        else if (wparam == VK_OEM_6) command = MV_COMMAND_SPEED_FASTER;
        if (command != MV_COMMAND_NONE) run_window_command(state, command);
        return 0;
    }
    if (message == WM_TIMER && state != NULL && state->frame.training_running) {
        DWORD now = GetTickCount();
        DWORD interval = state->frame.step_interval_ms;
        int catch_up_steps = 0;
        while (now - state->last_step_tick >= interval && catch_up_steps < 8) {
            state->last_step_tick += interval;
            run_window_command(state, MV_COMMAND_STEP);
            catch_up_steps += 1;
        }
        if (catch_up_steps == 8 && now - state->last_step_tick >= interval) {
            state->last_step_tick = now;
        }
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        MINMAXINFO *limits = (MINMAXINFO *)lparam;
        limits->ptMinTrackSize.x = 1296;
        limits->ptMinTrackSize.y = 899;
        return 0;
    }
    if (message == WM_SIZE && state != NULL && wparam != SIZE_MINIMIZED) {
        state->width = LOWORD(lparam);
        state->height = HIWORD(lparam);
        InvalidateRect(window, NULL, FALSE);
        return 0;
    }
    if (message == WM_DESTROY) {
        KillTimer(window, 1);
        if (state != NULL) {
            destroy_back_buffer(state);
            state->handle = NULL;
        }
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

    if (!mahjong_viz_validate_frame(frame, NULL, 0) || width < 1280 || height < 860) {
        return NULL;
    }
    if (!register_window_class(instance)) return NULL;

    state = (MahjongVizWindow *)calloc(1, sizeof(*state));
    if (state == NULL) return NULL;
    state->frame = *frame;
    state->initial_frame = *frame;
    state->width = width;
    state->height = height;
    state->last_step_tick = GetTickCount();
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
    SetTimer(state->handle, 1, 16, NULL);
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

int mahjong_viz_window_get_frame(
    const MahjongVizWindow *window,
    MahjongVizFrame *frame
) {
    if (window == NULL || window->handle == NULL || frame == NULL) return 0;
    *frame = window->frame;
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

    if (!mahjong_viz_validate_frame(frame, NULL, 0) || width < 1280 || height < 860) {
        return 0;
    }
    if (!register_window_class(instance)) return 0;

    memset(&state, 0, sizeof(state));
    state.frame = *frame;
    state.initial_frame = *frame;
    state.width = width;
    state.height = height;
    state.last_step_tick = GetTickCount();
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
    SetTimer(window, 1, 16, NULL);
    UpdateWindow(window);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 1;
}
