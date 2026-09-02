#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <XSpaceBioV10.h>
#include <XSControl.h>

// Minimal plotting example. It keeps the original project's useful idea while
// fixing the out-of-bounds third strip and the unbounded display coordinates.
constexpr uint8_t TFT_CS = 17;
constexpr uint8_t TFT_RST = 21;
constexpr uint8_t TFT_DC = 22;
constexpr uint16_t TFT_WIDTH = 320;
constexpr uint16_t TFT_HEIGHT = 240;
constexpr uint16_t STRIP_HEIGHT = TFT_HEIGHT / 3;
constexpr uint16_t FILTER_INTERVAL_MS = 1;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XSpaceBioV10Board Board;
XSFilter filterLeadI;
XSFilter filterLeadII;
XSFilter filterLeadIII;

struct SampleFrame {
  float leadI;
  float leadII;
  float leadIII;
  uint32_t sequence;
};

volatile SampleFrame latest{0.0f, 0.0f, 0.0f, 0};
portMUX_TYPE sampleMux = portMUX_INITIALIZER_UNLOCKED;

uint32_t lastSequence = 0;
uint16_t xPosition = 0;
int previousY1 = 40;
int previousY2 = 120;
int previousY3 = 200;
bool hasPrevious = false;

int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

int signalToY(float signal, int stripTop, int baseline) {
  const long scaled = constrain(lroundf(signal * 15000.0f), -11000L, 2000L);
  const int offset = static_cast<int>(map(scaled, -11000L, 2000L, 28L, -28L));
  return clampInt(baseline + offset, stripTop + 16, stripTop + STRIP_HEIGHT - 2);
}

void drawFrame() {
  tft.fillScreen(ILI9341_BLACK);
  tft.drawFastHLine(0, STRIP_HEIGHT, TFT_WIDTH, ILI9341_DARKGREY);
  tft.drawFastHLine(0, STRIP_HEIGHT * 2, TFT_WIDTH, ILI9341_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_RED);
  tft.setCursor(4, 3);
  tft.print("Lead I");
  tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(4, STRIP_HEIGHT + 3);
  tft.print("Lead II");
  tft.setTextColor(ILI9341_BLUE);
  tft.setCursor(4, STRIP_HEIGHT * 2 + 3);
  tft.print("Lead III = II - I");
}

void FilterTask(void *parameter) {
  (void)parameter;
  for (;;) {
    const float leadI = static_cast<float>(filterLeadI.SecondOrderLPF(
        Board.AD8232_GetVoltage(AD8232_XS1), 40, 0.001));
    const float leadII = static_cast<float>(filterLeadII.SecondOrderLPF(
        Board.AD8232_GetVoltage(AD8232_XS2), 40, 0.001));
    const float leadIII = static_cast<float>(filterLeadIII.SecondOrderLPF(
        leadII - leadI, 40, 0.001));

    portENTER_CRITICAL(&sampleMux);
    latest.leadI = leadI;
    latest.leadII = leadII;
    latest.leadIII = leadIII;
    ++latest.sequence;
    portEXIT_CRITICAL(&sampleMux);

    vTaskDelay(pdMS_TO_TICKS(FILTER_INTERVAL_MS));
  }
}

bool readLatest(SampleFrame &frame) {
  portENTER_CRITICAL(&sampleMux);
  frame.leadI = latest.leadI;
  frame.leadII = latest.leadII;
  frame.leadIII = latest.leadIII;
  frame.sequence = latest.sequence;
  portEXIT_CRITICAL(&sampleMux);
  return frame.sequence != 0;
}

void drawSample(const SampleFrame &frame) {
  if (xPosition == 0) {
    drawFrame();
    hasPrevious = false;
  } else {
    tft.drawFastVLine(xPosition, 16, STRIP_HEIGHT - 18, ILI9341_BLACK);
    tft.drawFastVLine(xPosition, STRIP_HEIGHT + 16, STRIP_HEIGHT - 18, ILI9341_BLACK);
    tft.drawFastVLine(xPosition, STRIP_HEIGHT * 2 + 16, STRIP_HEIGHT - 18, ILI9341_BLACK);
  }

  const int y1 = signalToY(frame.leadI, 0, 40);
  const int y2 = signalToY(frame.leadII, STRIP_HEIGHT, 120);
  const int y3 = signalToY(frame.leadIII, STRIP_HEIGHT * 2, 200);
  if (hasPrevious) {
    tft.drawLine(xPosition - 1, previousY1, xPosition, y1, ILI9341_RED);
    tft.drawLine(xPosition - 1, previousY2, xPosition, y2, ILI9341_GREEN);
    tft.drawLine(xPosition - 1, previousY3, xPosition, y3, ILI9341_BLUE);
  }
  previousY1 = y1;
  previousY2 = y2;
  previousY3 = y3;
  hasPrevious = true;
  if (++xPosition >= TFT_WIDTH) xPosition = 0;
}

void setup() {
  Serial.begin(115200);
  tft.begin();
  tft.setRotation(1);
  Board.init();
  Board.AD8232_Wake(AD8232_XS1);
  Board.AD8232_Wake(AD8232_XS2);
  xTaskCreate(FilterTask, "FilterTask", 3072, nullptr, 1, nullptr);
  drawFrame();
}

void loop() {
  SampleFrame frame{};
  if (readLatest(frame) && frame.sequence != lastSequence) {
    lastSequence = frame.sequence;
    drawSample(frame);
  }
  delay(8);  // display refresh is independent from the 1 ms acquisition task
}

