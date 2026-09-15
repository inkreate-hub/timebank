# ESP32-S3 Touch LCD 3.49 - Digital Gesture Stopwatch

## Gestures

### Swipe DOWN
Moves forward through the speed list:

`+x3 → +x2 → +x1.5 → +x1 → -x1 → -x1.5 → -x2 → -x3`

### Swipe UP
Moves backward through that list.

### Tap
Pause / resume.

### Hold for 5 seconds
Reset the timer and return to `+x1`.

## Speed meanings

- `+x3` = 3× normal speed
- `+x2` = 2× normal speed
- `+x1.5` = 1.5× normal speed
- `+x1` = normal forward speed
- `-x1` = backwards at normal speed
- `-x1.5` = backwards at 1.5× speed
- `-x2` = backwards at 2× speed
- `-x3` = backwards at 3× speed

## Display

The timer is now rendered as a **large seven-segment digital display** rather than normal text. It occupies most of the 640×172 landscape screen.

The rest of the UI remains black and white with the small speed/status indicator underneath.
