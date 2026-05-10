#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <furi_hal.h>

#define TAG "FlipperFapper"

// Speed levels: delay in ms between UP/DOWN
#define SPEED_LEVELS      5
#define SPEED_MIN_MS      150
#define SPEED_MAX_MS      1200

static const uint32_t speed_delays[SPEED_LEVELS] = {1200, 800, 500, 300, 150};
static const char* speed_labels[SPEED_LEVELS] = {"SLOW", "EASY", "MED", "FAST", "MAX"};

typedef struct {
    bool        running;
    bool        direction_up;   // true = UP, false = DOWN
    bool        vibration;
    int         speed_level;    // 0..SPEED_LEVELS-1
    uint32_t    last_tick;
    FuriMutex*  mutex;
} FapperState;

// ── Notification sequences ──────────────────────────────────────────────────

static const NotificationSequence seq_vibe_short = {
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    NULL,
};

// ── Draw callback ────────────────────────────────────────────────────────────

static void fapper_draw(Canvas* canvas, void* ctx) {
    FapperState* state = ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    // Title
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, "FLIPPER FAPPER");

    // Divider
    canvas_draw_line(canvas, 0, 15, 128, 15);

    // Big direction text
    canvas_set_font(canvas, FontBigNumbers);
    const char* dir = state->direction_up ? "  UP  " : " DOWN ";
    canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignTop, dir);

    // Arrow indicator
    if(state->direction_up) {
        // Up arrow
        canvas_draw_triangle(canvas, 64, 20, 12, 8, CanvasDirectionBottomToTop);
    } else {
        // Down arrow
        canvas_draw_triangle(canvas, 64, 52, 12, 8, CanvasDirectionTopToBottom);
    }

    // Bottom bar: speed + vibe
    canvas_draw_line(canvas, 0, 54, 128, 54);
    canvas_set_font(canvas, FontSecondary);

    // Speed
    char spd_buf[24];
    snprintf(spd_buf, sizeof(spd_buf), "< SPD:%s >", speed_labels[state->speed_level]);
    canvas_draw_str_aligned(canvas, 50, 56, AlignCenter, AlignTop, spd_buf);

    // Vibe toggle
    canvas_draw_str_aligned(
        canvas, 110, 56, AlignCenter, AlignTop, state->vibration ? "VIB:ON" : "VIB:--");

    furi_mutex_release(state->mutex);
}

// ── Input callback ───────────────────────────────────────────────────────────

static void fapper_input(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, FuriWaitForever);
}

// ── Entry point ──────────────────────────────────────────────────────────────

int32_t flipper_fapper_app(void* p) {
    UNUSED(p);

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    FapperState* state = malloc(sizeof(FapperState));
    state->running      = true;
    state->direction_up = true;
    state->vibration    = true;
    state->speed_level  = 2; // medium default
    state->last_tick    = furi_get_tick();
    state->mutex        = furi_mutex_alloc(FuriMutexTypeNormal);

    // GUI
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, fapper_draw, state);
    view_port_input_callback_set(vp, fapper_input, queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    NotificationApp* notif = furi_record_open(RECORD_NOTIFICATION);

    // ── Main loop ────────────────────────────────────────────────────────────
    InputEvent event;
    while(state->running) {
        // Poll input (non-blocking: 10 ms timeout so we can still tick)
        if(furi_message_queue_get(queue, &event, 10) == FuriStatusOk) {
            if(event.type == InputTypeShort || event.type == InputTypeRepeat) {
                furi_mutex_acquire(state->mutex, FuriWaitForever);
                switch(event.key) {
                case InputKeyBack:
                    state->running = false;
                    break;
                case InputKeyLeft:
                    if(state->speed_level > 0) state->speed_level--;
                    break;
                case InputKeyRight:
                    if(state->speed_level < SPEED_LEVELS - 1) state->speed_level++;
                    break;
                case InputKeyOk:
                    state->vibration = !state->vibration;
                    break;
                default:
                    break;
                }
                furi_mutex_release(state->mutex);
            }
        }

        // Tick: flip direction at current speed
        furi_mutex_acquire(state->mutex, FuriWaitForever);
        uint32_t delay = speed_delays[state->speed_level];
        uint32_t now   = furi_get_tick();
        bool do_flip   = (now - state->last_tick) >= furi_ms_to_ticks(delay);
        if(do_flip) {
            state->direction_up = !state->direction_up;
            state->last_tick    = now;
        }
        bool vibe = state->vibration && do_flip;
        furi_mutex_release(state->mutex);

        if(vibe) {
            notification_message(notif, &seq_vibe_short);
        }

        view_port_update(vp);
    }

    // ── Cleanup ──────────────────────────────────────────────────────────────
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(queue);
    furi_mutex_free(state->mutex);
    free(state);

    return 0;
}
