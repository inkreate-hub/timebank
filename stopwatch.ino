#include "user_config.h"
#include "lvgl_port.h"
#include "esp_err.h"
#include "i2c_bsp.h"
#include "src/lcd_bl_bsp/lcd_bl_pwm_bsp.h"
#include "app.h"
#include "driver/gpio.h"

// Waveshare ESP32-S3-Touch-LCD-3.49 PWR button
#define PWR_BUTTON_GPIO GPIO_NUM_16
#define PWR_LONG_PRESS_MS 1500

static bool pwrPressed = false;
static unsigned long pwrPressedAt = 0;

static void shutdownFromPowerButton()
{
  Serial.println("PWR long press: saving stopwatch state and shutting down");
  app_prepare_for_shutdown();
  delay(100);
  power_expander_set_hold(false);
  delay(500);
}

static void handlePowerButton()
{
  bool pressed = (gpio_get_level(PWR_BUTTON_GPIO) == 0);
  unsigned long now = millis();

  if (pressed && !pwrPressed) {
    pwrPressed = true;
    pwrPressedAt = now;
  }

  if (!pressed && pwrPressed) {
    pwrPressed = false;
  }

  if (pressed && pwrPressed && (now - pwrPressedAt >= PWR_LONG_PRESS_MS)) {
    shutdownFromPowerButton();
    pwrPressed = false;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("ESP32-S3 Touch LCD 3.49 - STOPWATCH");

  i2c_master_Init();

  // Take over the battery power latch as early as possible.
  // EXIO6 of the onboard TCA9554 is SYS_EN on this Waveshare board.
  uint8_t powerRet = power_expander_set_hold(true);
  if (powerRet == ESP_OK) {
    Serial.println("Battery power hold enabled (TCA9554 EXIO6)");
  } else {
    Serial.printf("WARNING: battery power hold init failed: %d\n", powerRet);
  }

  gpio_config_t pwr_cfg = {};
  pwr_cfg.pin_bit_mask = (1ULL << PWR_BUTTON_GPIO);
  pwr_cfg.mode = GPIO_MODE_INPUT;
  pwr_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  pwr_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  pwr_cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&pwr_cfg);

  lvgl_port_init();
  lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

  Serial.println("Display, touch and LVGL initialized");
  Serial.println("Stopwatch ready");
}

void loop()
{
  handlePowerButton();
  delay(10);
}
