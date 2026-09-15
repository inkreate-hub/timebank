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
