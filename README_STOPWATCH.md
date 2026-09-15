# Gesture Stopwatch

Controls:
- Tap: pause/resume
- Bottom status text shows RUNNING or PAUSED.
- Swipe up: larger timer
- Swipe down: smaller timer
- 5-second hold: complete reset

The timer uses `esp_timer_get_time()` for microsecond-resolution elapsed time.

- When the timer goes below zero, the complete time display turns red.

- The scale indicator is shown as `X 1.0`, `X 1.5`, `X -1.0`, etc.
- The scale indicator uses a smooth color gradient:
  - -3.0 red
  - -2.0 orange
  - -1.5 orange/yellow
  - -1.0 yellow
  - +1.0 green
  - +1.5 light blue/green
  - +2.0 light blue
  - +3.0 blue


## Side-tap time adjustment

The onboard QMI8658 accelerometer is used to detect a short side impact. A tap producing a positive X-axis acceleration adds 1 second; a tap producing negative X-axis acceleration subtracts 1 second. The threshold is 1.8 g and there is a 300 ms cooldown to prevent one impact from being counted multiple times. If the physical left/right direction is reversed on the device, change `IMU_TAP_POSITIVE_SIGN` in `app.cpp` from `+1` to `-1`.


### IMU side-tap scale behavior (v7)
- Positive tap from any negative scale switches to `X 1.0`.
- Negative tap from any positive scale switches to `X -1.0`.
- Positive taps then step `X 1.0 -> X 1.5 -> X 2.0 -> X 3.0`.
- Negative taps then step `X -1.0 -> X -1.5 -> X -2.0 -> X -3.0`.
