#include "app.h"
#include "lvgl.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define DIGIT_COUNT 9
#define SEG_COUNT 7

/* Main HH:MM:SS digits are large; milliseconds are smaller and grey. */
#define MAIN_DIGIT_COUNT 6
#define MAIN_DIGIT_W 72
#define MAIN_DIGIT_H 112
#define MAIN_SEG_T 12
#define MS_DIGIT_W 34
#define MS_DIGIT_H 54
#define MS_SEG_T 7

static lv_obj_t *time_digits[DIGIT_COUNT][SEG_COUNT];
static bool digit_is_ms[DIGIT_COUNT];
static lv_obj_t *rate_label;
static lv_obj_t *status_label;
static lv_obj_t *gesture_layer;

static bool running = false;
static double elapsed_us = 0.0;
static int64_t start_timestamp_us = 0;
static int rate_index = 3;   // +x1 at startup

/*
 * Speed order, exactly as requested:
 *
 * +x3 -> +x2 -> +x1.5 -> +x1 -> -x1 -> -x1.5 -> -x2 -> -x3
 *
 * Swipe DOWN = move forward through this list.
 * Swipe UP   = move backward through this list.
 */
static const double rates[] = {
     3.0,  2.0,  1.5,  1.0,
    -1.0, -1.5, -2.0, -3.0
};

static const char *rate_names[] = {
    "X 3.0", "X 2.0", "X 1.5", "X 1.0",
    "X -1.0", "X -1.5", "X -2.0", "X -3.0"
};

#define RATE_COUNT 8

/*
 * Scale indicator color gradient.
 *
 * Anchor points:
 *   -3.0  red
 *   -2.0  orange
 *   -1.5  orange/yellow
 *   -1.0  yellow
 *   +1.0  green
 *   +1.5  light blue/green
 *   +2.0  light blue
 *   +3.0  blue
 *
 * The color is interpolated between the anchors, so the indicator
 * transitions smoothly rather than jumping between only eight colors.
 */
typedef struct {
    float rate;
    uint32_t color;
} rate_color_stop_t;

static const rate_color_stop_t rate_color_stops[] = {
    {-3.0f, 0xFF0000},
    {-2.0f, 0xFF8000},
    {-1.5f, 0xFFBF00},
    {-1.0f, 0xFFFF00},
    { 1.0f, 0x00FF00},
    { 1.5f, 0x00FFB0},
    { 2.0f, 0x00BFFF},
    { 3.0f, 0x0080FF}
};

static lv_color_t rate_indicator_color(double rate)
{
    const int count = sizeof(rate_color_stops) / sizeof(rate_color_stops[0]);

    if (rate <= rate_color_stops[0].rate)
        return lv_color_hex(rate_color_stops[0].color);

    if (rate >= rate_color_stops[count - 1].rate)
        return lv_color_hex(rate_color_stops[count - 1].color);

    for (int i = 0; i < count - 1; ++i) {
        const rate_color_stop_t &a = rate_color_stops[i];
        const rate_color_stop_t &b = rate_color_stops[i + 1];

        if (rate >= a.rate && rate <= b.rate) {
            const float t = (float)((rate - a.rate) / (b.rate - a.rate));

            const uint8_t ar = (a.color >> 16) & 0xFF;
            const uint8_t ag = (a.color >> 8) & 0xFF;
            const uint8_t ab = a.color & 0xFF;

            const uint8_t br = (b.color >> 16) & 0xFF;
            const uint8_t bg = (b.color >> 8) & 0xFF;
            const uint8_t bb = b.color & 0xFF;

            const uint8_t r = (uint8_t)(ar + (br - ar) * t + 0.5f);
            const uint8_t g = (uint8_t)(ag + (bg - ag) * t + 0.5f);
            const uint8_t bl = (uint8_t)(ab + (bb - ab) * t + 0.5f);

            return lv_color_make(r, g, bl);
        }
    }

    return lv_color_hex(0xFFFFFF);
}

static lv_point_t press_point;
static uint32_t press_tick = 0;
static bool long_reset_done = false;

/* Seven-segment geometry. */
#define DIGIT_W 50
#define DIGIT_H 78
#define SEG_T   9
#define DIGIT_GAP 5
#define COLON_W 18
#define DOT_W 18

static double get_elapsed_us()
{
    if (!running)
        return elapsed_us;

    int64_t now = esp_timer_get_time();
    return elapsed_us +
           (double)(now - start_timestamp_us) * rates[rate_index];
}

static void format_time(double time_us, char *out, size_t len)
{
    bool negative = time_us < 0.0;
    if (negative)
        time_us = -time_us;

    int64_t total_ms = (int64_t)(time_us / 1000.0 + 0.5);
    int64_t ms = total_ms % 1000;
    int64_t total_s = total_ms / 1000;
    int64_t s = total_s % 60;
    int64_t total_m = total_s / 60;
    int64_t m = total_m % 60;
    int64_t h = total_m / 60;

    snprintf(out, len, "%s%02lld:%02lld:%02lld.%03lld",
             negative ? "-" : "",
             (long long)h,
             (long long)m,
             (long long)s,
             (long long)ms);
}

/*
 * Segment positions:
 *
 *       0
 *     -----
 *   1 |     | 2
 *     |- 3 -|
 *   4 |     | 5
 *     -----
 *       6
 */
static const bool segment_map[10][SEG_COUNT] = {
    {true,  true,  true,  false, true,  true,  true }, // 0
    {false, false, true, false, false, true,  false}, // 1
    {true,  false, true,  true,  true,  false, true }, // 2
    {true,  false, true,  true,  false, true,  true }, // 3
    {false, true,  true,  true,  false, true,  false}, // 4
    {true,  true,  false, true,  false, true,  true }, // 5
    {true,  true,  false, true,  true,  true,  true }, // 6
    {true,  false, true, false, false, true,  false}, // 7
    {true,  true,  true, true,  true,  true,  true }, // 8
    {true,  true,  true, true, false, true,  true }  // 9
};

static int current_digit_w = MAIN_DIGIT_W;
static int current_digit_h = MAIN_DIGIT_H;
static int current_seg_t = MAIN_SEG_T;
static bool current_rotated_ms = false;

static void set_segment_geometry(lv_obj_t *seg, int segment)
{
    const int digit_w = current_digit_w;
    const int digit_h = current_digit_h;
    const int seg_t = current_seg_t;
    int x = 0;
    int y = 0;
    int w = seg_t;
    int h = seg_t;

    switch (segment) {
        case 0:
            x = seg_t;
            y = 0;
            w = digit_w - 2 * seg_t;
            h = seg_t;
            break;
        case 1:
            x = 0;
            y = seg_t;
            w = seg_t;
            h = digit_h / 2 - seg_t - 2;
            break;
        case 2:
            x = digit_w - seg_t;
            y = seg_t;
            w = seg_t;
            h = digit_h / 2 - seg_t - 2;
            break;
        case 3:
            x = seg_t;
            y = (digit_h - seg_t) / 2;
            w = digit_w - 2 * seg_t;
            h = seg_t;
            break;
        case 4:
            x = 0;
            y = digit_h / 2 + 2;
            w = seg_t;
            h = digit_h / 2 - seg_t - 2;
            break;
        case 5:
            x = digit_w - seg_t;
            y = digit_h / 2 + 2;
            w = seg_t;
            h = digit_h / 2 - seg_t - 2;
            break;
        case 6:
            x = seg_t;
            y = digit_h - seg_t;
            w = digit_w - 2 * seg_t;
            h = seg_t;
            break;
    }

    if (current_rotated_ms) {
        /* Rotate the complete seven-segment digit 90 degrees counter-clockwise. */
        const int original_w = digit_w;
        const int original_x = x;
        const int original_y = y;
        const int original_seg_w = w;
        const int original_seg_h = h;
        x = original_y;
        y = original_w - original_x - original_seg_w;
        w = original_seg_h;
        h = original_seg_w;
    }

    lv_obj_set_size(seg, w, h);
    lv_obj_set_pos(seg, x, y);
    lv_obj_set_style_radius(seg, seg_t / 2, 0);
}

static lv_obj_t *create_digit(lv_obj_t *parent, int x, int y, bool is_ms)
{
    current_digit_w = is_ms ? MS_DIGIT_W : MAIN_DIGIT_W;
    current_digit_h = is_ms ? MS_DIGIT_H : MAIN_DIGIT_H;
    current_seg_t = is_ms ? MS_SEG_T : MAIN_SEG_T;
    current_rotated_ms = is_ms;
    lv_obj_t *digit = lv_obj_create(parent);
    /* Rotated milliseconds use the transposed bounding box. */
    lv_obj_set_size(digit,
                    is_ms ? current_digit_h : current_digit_w,
                    is_ms ? current_digit_w : current_digit_h);
    lv_obj_set_pos(digit, x, y);
    lv_obj_set_style_bg_opa(digit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(digit, 0, 0);
    lv_obj_set_style_pad_all(digit, 0, 0);
    lv_obj_clear_flag(digit, LV_OBJ_FLAG_CLICKABLE);

    for (int s = 0; s < SEG_COUNT; ++s) {
        time_digits[0][0] = time_digits[0][0]; // keep compiler happy on old LVGL builds

        lv_obj_t *seg = lv_obj_create(digit);
        lv_obj_set_style_bg_color(seg, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_pad_all(seg, 0, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        set_segment_geometry(seg, s);
    }

    return digit;
}

static void create_time_display(lv_obj_t *screen)
{
    /* Large HH:MM:SS fills the display; .mmm sits smaller at the lower right. */
    const int main_y = 34;
    const int main_gap = 7;
    const int colon_w = 20;
    const int main_width = 6 * MAIN_DIGIT_W + 4 * main_gap + 2 * colon_w;
    /* Shift the main time slightly left to make room for the ms column. */
    int x = (640 - main_width) / 2 - 30;
    int slot = 0;

    for (int group = 0; group < 3; ++group) {
        for (int d = 0; d < 2; ++d) {
            lv_obj_t *digit = create_digit(screen, x, main_y, false);
            for (int seg = 0; seg < SEG_COUNT; ++seg)
                time_digits[slot][seg] = lv_obj_get_child(digit, seg);
            digit_is_ms[slot] = false;
            slot++;
            x += MAIN_DIGIT_W + main_gap;
        }
        if (group < 2) {
            lv_obj_t *dot1 = lv_obj_create(screen);
            lv_obj_set_size(dot1, 11, 11);
            lv_obj_set_pos(dot1, x + 2, main_y + 31);
            lv_obj_set_style_bg_color(dot1, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_width(dot1, 0, 0);
            lv_obj_set_style_radius(dot1, 6, 0);
            lv_obj_t *dot2 = lv_obj_create(screen);
            lv_obj_set_size(dot2, 11, 11);
            lv_obj_set_pos(dot2, x + 2, main_y + 75);
            lv_obj_set_style_bg_color(dot2, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_width(dot2, 0, 0);
            lv_obj_set_style_radius(dot2, 6, 0);
            x += colon_w;
        }
    }

    /*
     * Milliseconds: three small digits, each rotated 90 degrees
     * counter-clockwise and stacked vertically. Together the column has
     * approximately the same height as one large seconds digit.
     */
    const int ms_gap = 5;
    const int ms_total_h = 3 * MS_DIGIT_W + 2 * ms_gap;
    const int ms_y = main_y + (MAIN_DIGIT_H - ms_total_h) / 2;
    const int ms_x = x + 8;

    lv_obj_t *decimal = lv_obj_create(screen);
    lv_obj_set_size(decimal, 8, 8);
    lv_obj_set_pos(decimal, ms_x - 13, main_y + MAIN_DIGIT_H - 10);
    lv_obj_set_style_bg_color(decimal, lv_color_hex(0x9A9A9A), 0);
    lv_obj_set_style_bg_opa(decimal, LV_OPA_50, 0);
    lv_obj_set_style_border_width(decimal, 0, 0);
    lv_obj_set_style_radius(decimal, 4, 0);

    for (int d = 0; d < 3; ++d) {
        lv_obj_t *digit = create_digit(screen, ms_x,
                                       ms_y + d * (MS_DIGIT_W + ms_gap), true);
        for (int seg = 0; seg < SEG_COUNT; ++seg)
            time_digits[slot][seg] = lv_obj_get_child(digit, seg);
        digit_is_ms[slot] = true;
        slot++;
    }
    current_rotated_ms = false;
}

static void update_time_display(double time_us)
{
    char buf[40];
    format_time(time_us, buf, sizeof(buf));
    const lv_color_t main_color = (time_us < 0.0) ? lv_color_hex(0xFF0000) : lv_color_hex(0xFFFFFF);
    const lv_color_t ms_color = (time_us < 0.0) ? lv_color_hex(0xB06060) : lv_color_hex(0x9A9A9A);
    int slot = 0;
    for (int i = 0; buf[i] != '\0' && slot < DIGIT_COUNT; ++i) {
        char c = buf[i];
        if (c >= '0' && c <= '9') {
            int digit = c - '0';
            lv_color_t color = digit_is_ms[slot] ? ms_color : main_color;
            lv_opa_t opa = digit_is_ms[slot] ? LV_OPA_50 : LV_OPA_COVER;
            for (int seg = 0; seg < SEG_COUNT; ++seg) {
                lv_obj_set_style_bg_color(time_digits[slot][seg], color, 0);
                lv_obj_set_style_bg_opa(time_digits[slot][seg], segment_map[digit][seg] ? opa : LV_OPA_0, 0);
            }
            slot++;
        }
    }
}

static void refresh_ui()
{
    update_time_display(get_elapsed_us());

    lv_label_set_text(rate_label, rate_names[rate_index]);
    lv_obj_set_style_text_color(
        rate_label, rate_indicator_color(rates[rate_index]), 0);

    if (running)
        lv_label_set_text(status_label, "RUNNING");
    else
        lv_label_set_text(status_label, "PAUSED");
}

static void ui_timer_callback(lv_timer_t *)
{
    refresh_ui();
}

static void toggle_running()
{
    if (running) {
        elapsed_us = get_elapsed_us();
        running = false;
    } else {
        start_timestamp_us = esp_timer_get_time();
        running = true;
    }

    refresh_ui();
}

static void reset_all()
{
    running = false;
    elapsed_us = 0.0;
    start_timestamp_us = 0;

    /* Reset to normal forward speed. */
    rate_index = 3;

    refresh_ui();
}

static void change_rate(int direction)
{
    if (running) {
        elapsed_us = get_elapsed_us();
        start_timestamp_us = esp_timer_get_time();
    }

    rate_index += direction;

    if (rate_index < 0)
        rate_index = 0;
    if (rate_index >= RATE_COUNT)
        rate_index = RATE_COUNT - 1;

    refresh_ui();
}

static void gesture_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev)
            return;

        lv_indev_get_point(indev, &press_point);
        press_tick = lv_tick_get();
        long_reset_done = false;
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (!long_reset_done && lv_tick_elaps(press_tick) >= 5000) {
            reset_all();
            long_reset_done = true;
        }
        return;
    }

    if (code != LV_EVENT_RELEASED || long_reset_done)
        return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev)
        return;

    lv_point_t release_point;
    lv_indev_get_point(indev, &release_point);

    int dx = release_point.x - press_point.x;
    int dy = release_point.y - press_point.y;

    if (LV_ABS(dy) > 40 && LV_ABS(dy) > LV_ABS(dx)) {
        /*
         * Requested direction:
         * DOWN = +x3 -> +x2 -> +x1.5 -> +x1 -> -x1 -> ...
         * UP   = reverse.
         */
        if (dy > 0)
            change_rate(+1);
        else
            change_rate(-1);
        return;
    }

    if (LV_ABS(dx) > 40)
        return;

    /* Short tap = pause/resume. */
    toggle_running();
}

void app_ui_init(void)
{
    lv_obj_t *screen = lv_screen_active();

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    create_time_display(screen);

    rate_label = lv_label_create(screen);
    lv_label_set_text(rate_label, "X 1.0");
    lv_obj_set_style_text_color(rate_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(rate_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(rate_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(rate_label, 620);
    lv_obj_align(rate_label, LV_ALIGN_TOP_MID, 0, 8);

    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "PAUSED");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(status_label, 172);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    gesture_layer = lv_obj_create(screen);
    lv_obj_set_size(gesture_layer, 640, 172);
    lv_obj_set_pos(gesture_layer, 0, 0);
    lv_obj_set_style_bg_opa(gesture_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gesture_layer, 0, 0);
    lv_obj_set_style_pad_all(gesture_layer, 0, 0);
    lv_obj_clear_flag(gesture_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gesture_layer, LV_OBJ_FLAG_CLICKABLE);

    /*
     * Keep the gesture layer in the background so it does not hide the
     * digital display.
     */
    lv_obj_move_background(gesture_layer);

    lv_obj_add_event_cb(
        gesture_layer, gesture_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(
        gesture_layer, gesture_event, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(
        gesture_layer, gesture_event, LV_EVENT_RELEASED, NULL);

    lv_timer_create(ui_timer_callback, 20, NULL);
    refresh_ui();
}
