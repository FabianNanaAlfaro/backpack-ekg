# Full Backpack EKG sketch

1. Select an ESP32-WROOM-32 board in Arduino IDE.
2. Install `Adafruit GFX`, `Adafruit ILI9341` and the Xspace Bio v1.0 board library.
3. Keep `BpmEstimator.h` beside `backpack_ekg.ino`.
4. Confirm the GPIO contract in [`../../docs/hardware.md`](../../docs/hardware.md).
5. Flash the sketch with the TFT and SD card disconnected if your programming setup requires it, then reconnect the peripherals.

## Threshold tuning

`BPM_UPPER_THRESHOLD` and `BPM_LOWER_THRESHOLD` retain the functional values from the older project code. They are not universal ECG voltages. Tune them using a stable bench/simulator input and keep `upper > lower`.

## Recording format

When an SD card is present, measurement samples are written to `/PENDING.CSV` at approximately 120–125 Hz. The file is renamed to `/ECG_<timestamp>.CSV` only when the user selects “Guardar”. A discarded measurement removes the pending file from the device.

