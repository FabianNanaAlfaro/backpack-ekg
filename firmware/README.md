# Firmware

The sketches target an ESP32-WROOM-32 plus the Xspace Bio v1.0 board, two AD8232 channels, an ILI9341 TFT and an SD card.

## Recommended sketch

Open [`backpack_ekg/backpack_ekg.ino`](backpack_ekg/backpack_ekg.ino). It contains:

- non-blocking menu, acquisition and save states;
- corrected three-strip TFT plotting;
- acquisition-side timestamping;
- two-threshold BPM estimation;
- optional throttled SD logging;
- safe handling of an empty SD card and a pending recording.

## Minimal visualization sketch

[`three_lead_plot/three_lead_plot.ino`](three_lead_plot/three_lead_plot.ino) is the smaller plotting example recovered from the old repository. It is useful when validating the two AD8232 channels, derived Lead III and TFT coordinates before enabling menus and storage.

## Board-specific dependency

`XSpaceBioV10.h` and `XSControl.h` are not redistributed. Install the XSpace Bio v1.0 library supplied by the board environment, then install the Adafruit GFX and ILI9341 libraries through the Arduino Library Manager.

