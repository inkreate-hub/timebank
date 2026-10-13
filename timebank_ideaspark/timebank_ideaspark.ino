/*
 * Timebank prototype 2
 * ideaspark ESP32 dev board, 1.14" 135x240 ST7789 LCD, CH340, LiPo.
 *
 * A stopwatch whose time can run faster, slower or backwards:
 *   X 3.0, X 2.0, X 1.5, X 1.0, X -1.0, X -1.5, X -2.0, X -3.0
 *
 * The board has one user button (BOOT, GPIO0):
 *   click         pause / resume
 *   double click  step the speed up on the current side
 *                 (1.0 -> 1.5 -> 2.0 -> 3.0, or -1.0 -> ... -> -3.0)
 *   hold ~1 s     flip direction (jump to X 1.0 or X -1.0)
 *   hold 5 s      reset to 00:00:00.000 and X 1.0
 *
 * Libraries: Adafruit GFX, Adafruit ST7735 and ST7789, Adafruit BusIO.
 * Board: "ESP32 Dev Module".
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Preferences.h>
#include "esp_timer.h"

/* ---- Board pins (ideaspark ESP32 1.14" LCD). Check against your board's silkscreen. ---- */
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
#define TFT_BL   32
#define BUTTON_PIN 0

#define SCREEN_W 240
#define SCREEN_H 135

#define CLICK_GAP_MS     350
#define FLIP_HOLD_MS     800
#define RESET_HOLD_MS   5000
#define FRAME_MS          33
#define SAVE_PERIOD_MS 10000

// Hardware SPI: the LCD sits on the ESP32's VSPI pins.
Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(SCREEN_W, SCREEN_H);
Preferences prefs;

/* ---- Timer state ---- */

static const double rates[] = {3.0, 2.0, 1.5, 1.0, -1.0, -1.5, -2.0, -3.0};
static const char *rate_names[] = {
    "X 3.0", "X 2.0", "X 1.5", "X 1.0",
    "X -1.0", "X -1.5", "X -2.0", "X -3.0"};
#define RATE_COUNT 8
#define RATE_PLUS_ONE 3
#define RATE_MINUS_ONE 4

static bool running = false;
static double elapsed_us = 0.0;
static int64_t start_us = 0;
static int rate_index = RATE_PLUS_ONE;

static double get_elapsed_us()
{
    if (!running)
        return elapsed_us;
    return elapsed_us + (double)(esp_timer_get_time() - start_us) * rates[rate_index];
}

/* Bank the time run so far at the old rate before changing anything. */
static void rebank()
{
    if (running) {
        elapsed_us = get_elapsed_us();
        start_us = esp_timer_get_time();
    }
}

static void save_state()
{
    prefs.putDouble("elapsed_us", get_elapsed_us());
    prefs.putInt("rate_index", rate_index);
}

static void load_state()
{
    elapsed_us = prefs.getDouble("elapsed_us", 0.0);
    rate_index = prefs.getInt("rate_index", RATE_PLUS_ONE);
    if (rate_index < 0 || rate_index >= RATE_COUNT)
        rate_index = RATE_PLUS_ONE;
    // Time cannot run while the board is off, so always come back paused.
    running = false;
}

static void toggle_running()
{
    if (running) {
        elapsed_us = get_elapsed_us();
        running = false;
    } else {
        start_us = esp_timer_get_time();
        running = true;
    }
    save_state();
}

static void step_speed()
{
    rebank();
    switch (rate_index) {
        case 3: rate_index = 2; break;  // 1.0 -> 1.5
        case 2: rate_index = 1; break;  // 1.5 -> 2.0
        case 1: rate_index = 0; break;  // 2.0 -> 3.0
        case 4: rate_index = 5; break;  // -1.0 -> -1.5
        case 5: rate_index = 6; break;  // -1.5 -> -2.0
        case 6: rate_index = 7; break;  // -2.0 -> -3.0
        default: break;                 // already at +/-3.0
    }
    save_state();
}

static void flip_direction()
{
    rebank();
    rate_index = rates[rate_index] > 0.0 ? RATE_MINUS_ONE : RATE_PLUS_ONE;
    save_state();
}

static void reset_all()
{
    running = false;
    elapsed_us = 0.0;
    start_us = 0;
    rate_index = RATE_PLUS_ONE;
    save_state();
}

/* ---- Colours ---- */

static uint16_t rgb565(uint32_t rgb)
{
    return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}

/* Same gradient as prototype 1: red/yellow backwards, green/blue forwards. */
static const struct { float rate; uint32_t color; } rate_stops[] = {
    {-3.0f, 0xFF0000}, {-2.0f, 0xFF8000}, {-1.5f, 0xFFBF00}, {-1.0f, 0xFFFF00},
    { 1.0f, 0x00FF00}, { 1.5f, 0x00FFB0}, { 2.0f, 0x00BFFF}, { 3.0f, 0x0080FF}};

static uint16_t rate_color(double rate)
{
    const int count = sizeof(rate_stops) / sizeof(rate_stops[0]);
    if (rate <= rate_stops[0].rate)
        return rgb565(rate_stops[0].color);
    for (int i = 0; i < count - 1; ++i) {
        if (rate >= rate_stops[i].rate && rate <= rate_stops[i + 1].rate) {
            float t = (float)((rate - rate_stops[i].rate) /
                              (rate_stops[i + 1].rate - rate_stops[i].rate));
            uint32_t a = rate_stops[i].color, b = rate_stops[i + 1].color;
            uint32_t out = 0;
            for (int shift = 16; shift >= 0; shift -= 8) {
                float ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
                out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << shift;
            }
            return rgb565(out);
        }
    }
    return rgb565(rate_stops[count - 1].color);
}

#define COLOR_WHITE  0xFFFF
#define COLOR_RED    rgb565(0xFF0000)
#define COLOR_GREY   rgb565(0x9A9A9A)
#define COLOR_DIMRED rgb565(0xB06060)
#define COLOR_DARK   rgb565(0x202020)

/* ---- Seven-segment drawing ----
 *
 *       0
 *     -----
 *   1 |     | 2
 *     |- 3 -|
 *   4 |     | 5
 *     -----
 *       6
 */
static const uint8_t segment_bits[10] = {
    0b1110111, 0b0100100, 0b1011101, 0b1101101, 0b0101110,
    0b1101011, 0b1111011, 0b0100101, 0b1111111, 0b1101111};

static void draw_digit(int x, int y, int w, int h, int t, int value, uint16_t color)
{
    const uint8_t bits = segment_bits[value];
    const int half = h / 2;
    const int r = t / 2;
    const int vh = half - t - 1;  // vertical segment height

    if (bits & (1 << 0)) canvas.fillRoundRect(x + t, y, w - 2 * t, t, r, color);
    if (bits & (1 << 1)) canvas.fillRoundRect(x, y + t, t, vh, r, color);
    if (bits & (1 << 2)) canvas.fillRoundRect(x + w - t, y + t, t, vh, r, color);
    if (bits & (1 << 3)) canvas.fillRoundRect(x + t, y + (h - t) / 2, w - 2 * t, t, r, color);
    if (bits & (1 << 4)) canvas.fillRoundRect(x, y + half + 1, t, vh, r, color);
    if (bits & (1 << 5)) canvas.fillRoundRect(x + w - t, y + half + 1, t, vh, r, color);
    if (bits & (1 << 6)) canvas.fillRoundRect(x + t, y + h - t, w - 2 * t, t, r, color);
}

/* Main HH:MM:SS digits. */
#define MAIN_W 26
#define MAIN_H 54
#define MAIN_T 6
#define MAIN_GAP 4
#define COLON_W 16
#define MAIN_X 24
#define MAIN_Y 30

/* Smaller grey .mmm under the seconds. */
#define MS_W 14
#define MS_H 24
#define MS_T 3
#define MS_GAP 4
#define MS_Y 94

static void draw_time(double time_us)
{
    const bool negative = time_us < 0.0;
    if (negative)
        time_us = -time_us;

    int64_t total_ms = (int64_t)(time_us / 1000.0 + 0.5);
    int ms = total_ms % 1000;
    int64_t total_s = total_ms / 1000;
    int s = total_s % 60;
    int m = (total_s / 60) % 60;
    int h = (int)((total_s / 3600) % 100);

    const uint16_t main_color = negative ? COLOR_RED : COLOR_WHITE;
    const uint16_t ms_color = negative ? COLOR_DIMRED : COLOR_GREY;

    if (negative)
        canvas.fillRoundRect(4, MAIN_Y + (MAIN_H - MAIN_T) / 2, 12, MAIN_T, MAIN_T / 2, main_color);

    const int groups[3] = {h, m, s};
    int x = MAIN_X;
    for (int g = 0; g < 3; ++g) {
        draw_digit(x, MAIN_Y, MAIN_W, MAIN_H, MAIN_T, groups[g] / 10, main_color);
        x += MAIN_W + MAIN_GAP;
        draw_digit(x, MAIN_Y, MAIN_W, MAIN_H, MAIN_T, groups[g] % 10, main_color);
        x += MAIN_W;
        if (g < 2) {
            const int cx = x + COLON_W / 2;
            canvas.fillCircle(cx, MAIN_Y + MAIN_H / 3, 3, main_color);
            canvas.fillCircle(cx, MAIN_Y + 2 * MAIN_H / 3, 3, main_color);
            x += COLON_W;
        }
    }

    // Right-align the milliseconds with the seconds.
    const int main_right = x;
    int ms_x = main_right - (3 * MS_W + 2 * MS_GAP);
    canvas.fillCircle(ms_x - 6, MS_Y + MS_H - 2, 2, ms_color);
    const int ms_digits[3] = {ms / 100, (ms / 10) % 10, ms % 10};
    for (int d = 0; d < 3; ++d) {
        draw_digit(ms_x, MS_Y, MS_W, MS_H, MS_T, ms_digits[d], ms_color);
        ms_x += MS_W + MS_GAP;
    }
}

static void draw_centered_text(const char *text, int y, uint8_t size, uint16_t color)
{
    int16_t bx, by;
    uint16_t bw, bh;
    canvas.setTextSize(size);
    canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    canvas.setTextColor(color);
    canvas.setCursor((SCREEN_W - bw) / 2, y);
    canvas.print(text);
}

/* ---- Button ---- */

static bool btn_down = false;
static uint32_t btn_down_at = 0;
static bool btn_reset_fired = false;
static bool click_pending = false;
static uint32_t click_at = 0;

static void handle_button()
{
    const uint32_t now = millis();
    const bool pressed = digitalRead(BUTTON_PIN) == LOW;

    if (pressed && !btn_down) {
        btn_down = true;
        btn_down_at = now;
        btn_reset_fired = false;
    } else if (pressed && btn_down) {
        if (!btn_reset_fired && now - btn_down_at >= RESET_HOLD_MS) {
            reset_all();
            btn_reset_fired = true;
            click_pending = false;
        }
    } else if (!pressed && btn_down) {
        btn_down = false;
        const uint32_t held = now - btn_down_at;
        if (held < 30 || btn_reset_fired) {
            // Bounce, or the reset already happened while holding.
        } else if (held >= FLIP_HOLD_MS) {
            flip_direction();
            click_pending = false;
        } else if (click_pending && now - click_at <= CLICK_GAP_MS) {
            step_speed();
            click_pending = false;
        } else {
            click_pending = true;
            click_at = now;
        }
    }

    if (click_pending && !btn_down && now - click_at > CLICK_GAP_MS) {
        toggle_running();
        click_pending = false;
    }
}

/* Hint and progress bar while the button is held. */
static void draw_hold_feedback()
{
    if (!btn_down || btn_reset_fired)
        return;
    const uint32_t held = millis() - btn_down_at;
    if (held < FLIP_HOLD_MS)
        return;

    const char *hint = held < 2000 ? "RELEASE TO FLIP" : "KEEP HOLDING TO RESET";
    canvas.fillRect(0, 122, SCREEN_W, 13, 0x0000);
    draw_centered_text(hint, 124, 1, COLOR_WHITE);
    const int bar = (int)((uint64_t)SCREEN_W * held / RESET_HOLD_MS);
    canvas.fillRect(0, SCREEN_H - 2, bar, 2, COLOR_RED);
}

/* ---- Main ---- */

static void render()
{
    canvas.fillScreen(0x0000);

    draw_centered_text(rate_names[rate_index], 6, 2, rate_color(rates[rate_index]));
    draw_time(get_elapsed_us());

    canvas.setTextSize(1);
    canvas.setTextColor(running ? COLOR_WHITE : COLOR_GREY);
    canvas.setCursor(MAIN_X, MS_Y + 8);
    canvas.print(running ? "RUNNING" : "PAUSED");

    draw_hold_feedback();

    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_W, SCREEN_H);
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Timebank prototype 2 (ideaspark ESP32 1.14\")");

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.init(135, 240);
    tft.setSPISpeed(40000000);
    tft.setRotation(1);  // landscape, USB on the right
    tft.fillScreen(0x0000);

    prefs.begin("timebank", false);
    load_state();
}

void loop()
{
    static uint32_t last_frame = 0;
    static uint32_t last_save = 0;
    const uint32_t now = millis();

    handle_button();

    if (now - last_frame >= FRAME_MS) {
        last_frame = now;
        render();
    }

    if (running && now - last_save >= SAVE_PERIOD_MS) {
        last_save = now;
        save_state();
    }
}
