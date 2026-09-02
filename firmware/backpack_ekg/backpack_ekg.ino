#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SD.h>
#include <SPI.h>
#include <XSpaceBioV10.h>
#include <XSControl.h>

#include "BpmEstimator.h"

// Hardware contract inherited from the public Backpack EKG prototype.
constexpr uint8_t TFT_CS = 17;
constexpr uint8_t TFT_RST = 21;
constexpr uint8_t TFT_DC = 22;
constexpr uint8_t SD_CS = 16;
constexpr uint8_t SD_MOSI = 23;
constexpr uint8_t SD_MISO = 19;
constexpr uint8_t SD_SCK = 18;
constexpr uint8_t BTN_UP = 0;
constexpr uint8_t BTN_DOWN = 2;
constexpr uint8_t BTN_SELECT = 32;

constexpr uint16_t TFT_WIDTH = 320;
constexpr uint16_t TFT_HEIGHT = 240;
constexpr uint16_t STRIP_HEIGHT = TFT_HEIGHT / 3;
constexpr uint16_t DISPLAY_INTERVAL_MS = 16;  // approximately 60 FPS
constexpr uint16_t LOG_INTERVAL_MS = 8;       // approximately 120-125 Hz
constexpr uint16_t FILTER_INTERVAL_MS = 1;    // nominal acquisition update
constexpr float BPM_UPPER_THRESHOLD = 2.18f;
constexpr float BPM_LOWER_THRESHOLD = 2.14f;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XSpaceBioV10Board Board;
XSFilter filterLeadI;
XSFilter filterLeadII;
XSFilter filterLeadIII;

struct SampleFrame {
  float leadI;
  float leadII;
  float leadIII;
  uint32_t timestampMs;
  uint32_t sequence;
  float bpm;
  bool bpmReady;
};

// The acquisition task is the only writer. The critical section makes the
// multi-field snapshot consistent when the UI task reads it.
volatile float latestLeadI = 0.0f;
volatile float latestLeadII = 0.0f;
volatile float latestLeadIII = 0.0f;
volatile uint32_t latestTimestampMs = 0;
volatile uint32_t latestSequence = 0;
volatile float latestBpm = 0.0f;
volatile bool latestBpmReady = false;
volatile bool bpmEnabled = false;
volatile bool bpmResetRequested = false;
portMUX_TYPE sampleMux = portMUX_INITIALIZER_UNLOCKED;

enum class ScreenState {
  MENU,
  MEASUREMENT,
  SAVE_PROMPT,
  FILE_LIST,
  FILE_VIEW,
  CREDITS
};

ScreenState screenState = ScreenState::MENU;
const char *const menuItems[] = {
    "Nueva medicion",
    "Mediciones anteriores",
    "Creditos"};
constexpr uint8_t MENU_ITEM_COUNT = sizeof(menuItems) / sizeof(menuItems[0]);
uint8_t menuIndex = 0;
uint8_t saveIndex = 0;  // 0 = save, 1 = discard

struct ButtonState {
  uint8_t pin;
  bool wasDown;
  uint32_t lastEventMs;
};

ButtonState buttonUp{BTN_UP, false, 0};
ButtonState buttonDown{BTN_DOWN, false, 0};
ButtonState buttonSelect{BTN_SELECT, false, 0};
constexpr uint16_t BUTTON_DEBOUNCE_MS = 120;

bool sdDetected = false;
bool recordingToSd = false;
File activeFile;
uint32_t lastLoggedMs = 0;
uint16_t writesSinceFlush = 0;

constexpr uint8_t MAX_FILES = 50;
String fileNames[MAX_FILES];
uint8_t totalFiles = 0;
uint8_t fileIndex = 0;  // totalFiles is the "return" row
uint8_t fileDisplayOffset = 0;
constexpr uint8_t FILE_ROWS = 9;

uint32_t lastFrameSequence = 0;
uint32_t lastDisplayMs = 0;
uint16_t xPosition = 0;
int previousY1 = 40;
int previousY2 = 120;
int previousY3 = 200;
bool hasPreviousPoint = false;

void drawMenu();
void drawPlotFrame();
void drawSavePrompt();
void drawFileList();
void drawCredits();
void selectMenuItem();
void startMeasurement();
void finishMeasurement();
void finalizeRecording();
void serviceMeasurement();
void handleButtons();
void loadFileNames();
void plotSelectedFile();

bool buttonPressed(ButtonState &button) {
  const bool isDown = digitalRead(button.pin) == LOW;
  const uint32_t now = millis();

  if (!isDown) {
    button.wasDown = false;
    return false;
  }

  if (!button.wasDown && (now - button.lastEventMs >= BUTTON_DEBOUNCE_MS)) {
    button.wasDown = true;
    button.lastEventMs = now;
    return true;
  }
  return false;
}

int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

long clampLong(long value, long low, long high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

int signalToY(float signal, int stripTop, int baseline) {
  // This preserves the legacy project's display gain while constraining the
  // output to the actual strip. Tune the input range for the board if needed.
  const long scaled = clampLong(lroundf(signal * 15000.0f), -11000L, 2000L);
  const int offset = static_cast<int>(map(scaled, -11000L, 2000L, 28L, -28L));
  return clampInt(baseline + offset, stripTop + 16, stripTop + STRIP_HEIGHT - 2);
}

void drawBpm(float bpm, bool ready) {
  tft.fillRect(215, 0, 105, 18, ILI9341_BLACK);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);
  tft.setCursor(215, 1);
  tft.print("BPM ");
  if (ready) {
    tft.print(static_cast<int>(lroundf(bpm)));
  } else {
    tft.print("--");
  }
}

void drawPlotFrame() {
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
  drawBpm(latestBpm, latestBpmReady);
}

void drawFrameSample(const SampleFrame &frame) {
  if (xPosition == 0) {
    drawPlotFrame();
    hasPreviousPoint = false;
  } else {
    tft.drawFastVLine(xPosition, 16, STRIP_HEIGHT - 18, ILI9341_BLACK);
    tft.drawFastVLine(xPosition, STRIP_HEIGHT + 16, STRIP_HEIGHT - 18, ILI9341_BLACK);
    tft.drawFastVLine(xPosition, STRIP_HEIGHT * 2 + 16, STRIP_HEIGHT - 18, ILI9341_BLACK);
  }

  const int y1 = signalToY(frame.leadI, 0, STRIP_HEIGHT / 2);
  const int y2 = signalToY(frame.leadII, STRIP_HEIGHT, STRIP_HEIGHT + STRIP_HEIGHT / 2);
  const int y3 = signalToY(frame.leadIII, STRIP_HEIGHT * 2, STRIP_HEIGHT * 2 + STRIP_HEIGHT / 2);

  if (hasPreviousPoint) {
    tft.drawLine(xPosition - 1, previousY1, xPosition, y1, ILI9341_RED);
    tft.drawLine(xPosition - 1, previousY2, xPosition, y2, ILI9341_GREEN);
    tft.drawLine(xPosition - 1, previousY3, xPosition, y3, ILI9341_BLUE);
  }

  previousY1 = y1;
  previousY2 = y2;
  previousY3 = y3;
  hasPreviousPoint = true;
  if (frame.bpmReady) {
    drawBpm(frame.bpm, true);
  }

  ++xPosition;
  if (xPosition >= TFT_WIDTH) {
    xPosition = 0;
  }
}

bool readLatestFrame(SampleFrame &frame) {
  portENTER_CRITICAL(&sampleMux);
  frame.leadI = latestLeadI;
  frame.leadII = latestLeadII;
  frame.leadIII = latestLeadIII;
  frame.timestampMs = latestTimestampMs;
  frame.sequence = latestSequence;
  frame.bpm = latestBpm;
  frame.bpmReady = latestBpmReady;
  portEXIT_CRITICAL(&sampleMux);
  return frame.sequence != 0;
}

void FilterTask(void *parameter) {
  (void)parameter;
  BpmEstimator estimator(BPM_UPPER_THRESHOLD, BPM_LOWER_THRESHOLD);

  for (;;) {
    const float rawLeadI = static_cast<float>(Board.AD8232_GetVoltage(AD8232_XS1));
    const float rawLeadII = static_cast<float>(Board.AD8232_GetVoltage(AD8232_XS2));
    const float filteredLeadI = static_cast<float>(filterLeadI.SecondOrderLPF(rawLeadI, 40, 0.001));
    const float filteredLeadII = static_cast<float>(filterLeadII.SecondOrderLPF(rawLeadII, 40, 0.001));
    const float derivedLeadIII = filteredLeadII - filteredLeadI;
    const float filteredLeadIII = static_cast<float>(filterLeadIII.SecondOrderLPF(derivedLeadIII, 40, 0.001));
    const uint32_t timestampMs = millis();

    if (bpmResetRequested) {
      estimator.reset();
      bpmResetRequested = false;
      portENTER_CRITICAL(&sampleMux);
      latestBpm = 0.0f;
      latestBpmReady = false;
      portEXIT_CRITICAL(&sampleMux);
    }

    const bool bpmUpdated = bpmEnabled && estimator.update(filteredLeadI, timestampMs);
    portENTER_CRITICAL(&sampleMux);
    latestLeadI = filteredLeadI;
    latestLeadII = filteredLeadII;
    latestLeadIII = filteredLeadIII;
    latestTimestampMs = timestampMs;
    ++latestSequence;
    if (bpmUpdated) {
      latestBpm = estimator.bpm();
      latestBpmReady = true;
    }
    portEXIT_CRITICAL(&sampleMux);

    vTaskDelay(pdMS_TO_TICKS(FILTER_INTERVAL_MS));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  Board.init();
  Board.AD8232_Wake(AD8232_XS1);
  Board.AD8232_Wake(AD8232_XS2);

  sdDetected = SD.begin(SD_CS);
  if (!sdDetected) {
    Serial.println("SD card not detected; measurement display remains available.");
  }
  loadFileNames();

  xTaskCreate(FilterTask, "FilterTask", 4096, nullptr, 1, nullptr);
  drawMenu();
}

void loop() {
  handleButtons();
  if (screenState == ScreenState::MEASUREMENT) {
    serviceMeasurement();
  }
  delay(1);
}

void drawMenu() {
  screenState = ScreenState::MENU;
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("BACKPACK EKG");
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setCursor(10, 34);
  tft.print("Standalone three-lead screening platform");

  for (uint8_t i = 0; i < MENU_ITEM_COUNT; ++i) {
    tft.setTextColor(i == menuIndex ? ILI9341_YELLOW : ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(14, 70 + i * 32);
    tft.print(i == menuIndex ? "> " : "  ");
    tft.print(menuItems[i]);
  }

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(10, 222);
  tft.print(sdDetected ? "SD ready" : "SD unavailable - display only");
}

void selectMenuItem() {
  if (menuIndex == 0) {
    startMeasurement();
  } else if (menuIndex == 1) {
    fileIndex = 0;
    fileDisplayOffset = 0;
    drawFileList();
  } else {
    drawCredits();
  }
}

void startMeasurement() {
  if (activeFile) {
    activeFile.close();
  }
  recordingToSd = false;

  if (sdDetected) {
    if (SD.exists("/PENDING.CSV")) {
      SD.remove("/PENDING.CSV");
    }
    activeFile = SD.open("/PENDING.CSV", FILE_WRITE);
    if (activeFile) {
      activeFile.println("t_ms,lead_i,lead_ii,lead_iii,bpm");
      recordingToSd = true;
    }
  }

  portENTER_CRITICAL(&sampleMux);
  latestBpm = 0.0f;
  latestBpmReady = false;
  portEXIT_CRITICAL(&sampleMux);
  bpmEnabled = false;
  bpmResetRequested = true;
  bpmEnabled = true;

  lastFrameSequence = 0;
  lastLoggedMs = 0;
  writesSinceFlush = 0;
  lastDisplayMs = millis();
  xPosition = 0;
  hasPreviousPoint = false;
  screenState = ScreenState::MEASUREMENT;
  drawPlotFrame();
}

void serviceMeasurement() {
  SampleFrame frame{};
  if (!readLatestFrame(frame) || frame.sequence == lastFrameSequence) {
    return;
  }
  lastFrameSequence = frame.sequence;

  if (recordingToSd && (frame.timestampMs - lastLoggedMs >= LOG_INTERVAL_MS)) {
    activeFile.print(frame.timestampMs);
    activeFile.print(',');
    activeFile.print(frame.leadI, 6);
    activeFile.print(',');
    activeFile.print(frame.leadII, 6);
    activeFile.print(',');
    activeFile.print(frame.leadIII, 6);
    activeFile.print(',');
    if (frame.bpmReady) {
      activeFile.println(frame.bpm, 3);
    } else {
      activeFile.println();
    }
    lastLoggedMs = frame.timestampMs;
    if (++writesSinceFlush >= 32) {
      activeFile.flush();
      writesSinceFlush = 0;
    }
  }

  const uint32_t now = millis();
  if (now - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    drawFrameSample(frame);
    lastDisplayMs = now;
  }
}

void finishMeasurement() {
  bpmEnabled = false;
  if (activeFile) {
    activeFile.flush();
    activeFile.close();
  }
  saveIndex = 0;
  drawSavePrompt();
}

void drawSavePrompt() {
  screenState = ScreenState::SAVE_PROMPT;
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 12);
  tft.print(recordingToSd ? "Guardar medicion?" : "SD no disponible");

  if (!recordingToSd) {
    tft.setTextSize(1);
    tft.setCursor(10, 52);
    tft.print("La grafica termino, pero no se creo un archivo.");
    tft.setCursor(10, 72);
    tft.print("Presiona SELECT para volver.");
    return;
  }

  const char *const options[] = {"Guardar en SD", "Descartar"};
  for (uint8_t i = 0; i < 2; ++i) {
    tft.setTextColor(i == saveIndex ? ILI9341_YELLOW : ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(14, 62 + i * 34);
    tft.print(i == saveIndex ? "> " : "  ");
    tft.print(options[i]);
  }
}

void finalizeRecording() {
  if (!recordingToSd) {
    drawMenu();
    return;
  }

  bool success = false;
  String finalPath;
  if (saveIndex == 0) {
    finalPath = String("/ECG_") + String(millis()) + ".CSV";
    success = SD.rename("/PENDING.CSV", finalPath.c_str());
  } else {
    success = SD.remove("/PENDING.CSV");
  }

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(success ? ILI9341_GREEN : ILI9341_RED);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.print(success ? "Operacion OK" : "Operacion fallo");
  tft.setTextSize(1);
  tft.setCursor(10, 55);
  if (saveIndex == 0 && success) {
    tft.print(finalPath);
  } else if (saveIndex == 1 && success) {
    tft.print("Medicion descartada");
  } else {
    tft.print("Revisa la tarjeta SD");
  }
  delay(800);
  recordingToSd = false;
  loadFileNames();
  drawMenu();
}

void loadFileNames() {
  totalFiles = 0;
  if (!sdDetected) {
    return;
  }

  File root = SD.open("/");
  if (!root) {
    return;
  }
  while (totalFiles < MAX_FILES) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    if (!entry.isDirectory()) {
      String name = entry.name();
      if (name.endsWith(".CSV") || name.endsWith(".csv")) {
        fileNames[totalFiles++] = name;
      }
    }
    entry.close();
  }
  root.close();
}

void drawFileList() {
  screenState = ScreenState::FILE_LIST;
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print("Mediciones SD");

  const uint8_t maxIndex = totalFiles;
  if (fileIndex > maxIndex) fileIndex = maxIndex;
  if (fileIndex < fileDisplayOffset) fileDisplayOffset = fileIndex;
  if (fileIndex >= fileDisplayOffset + FILE_ROWS) {
    fileDisplayOffset = fileIndex - FILE_ROWS + 1;
  }

  tft.setTextSize(1);
  for (uint8_t row = 0; row < FILE_ROWS; ++row) {
    const uint8_t index = fileDisplayOffset + row;
    if (index > totalFiles) break;
    const bool isReturn = index == totalFiles;
    tft.setTextColor(index == fileIndex ? ILI9341_YELLOW : ILI9341_WHITE);
    tft.setCursor(10, 34 + row * 18);
    tft.print(index == fileIndex ? "> " : "  ");
    if (isReturn) {
      tft.print("Volver al menu");
    } else {
      String shortName = fileNames[index];
      if (shortName.length() > 28) shortName = shortName.substring(0, 28);
      tft.print(shortName);
    }
  }
}

void plotSelectedFile() {
  if (fileIndex >= totalFiles || !sdDetected) {
    drawMenu();
    return;
  }

  String path = fileNames[fileIndex];
  if (!path.startsWith("/")) path = String("/") + path;
  File dataFile = SD.open(path.c_str());
  if (!dataFile) {
    drawFileList();
    return;
  }

  tft.fillScreen(ILI9341_BLACK);
  drawPlotFrame();
  int previousX = 0;
  int previousY1File = 40;
  int previousY2File = 120;
  int previousY3File = 200;
  bool firstPoint = true;
  int x = 0;

  while (dataFile.available() && x < TFT_WIDTH) {
    String line = dataFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("t_ms")) continue;
    const int comma1 = line.indexOf(',');
    const int comma2 = line.indexOf(',', comma1 + 1);
    const int comma3 = line.indexOf(',', comma2 + 1);
    if (comma1 < 1 || comma2 < 0 || comma3 < 0) continue;

    const float leadI = line.substring(comma1 + 1, comma2).toFloat();
    const float leadII = line.substring(comma2 + 1, comma3).toFloat();
    const int comma4 = line.indexOf(',', comma3 + 1);
    const float leadIII = line.substring(comma3 + 1, comma4 < 0 ? line.length() : comma4).toFloat();
    const int y1 = signalToY(leadI, 0, 40);
    const int y2 = signalToY(leadII, 80, 120);
    const int y3 = signalToY(leadIII, 160, 200);
    if (!firstPoint) {
      tft.drawLine(previousX, previousY1File, x, y1, ILI9341_RED);
      tft.drawLine(previousX, previousY2File, x, y2, ILI9341_GREEN);
      tft.drawLine(previousX, previousY3File, x, y3, ILI9341_BLUE);
    }
    previousX = x;
    previousY1File = y1;
    previousY2File = y2;
    previousY3File = y3;
    firstPoint = false;
    ++x;
  }
  dataFile.close();

  tft.fillRect(0, 226, TFT_WIDTH, 14, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(6, 230);
  tft.print("SELECT: volver a la lista");
  screenState = ScreenState::FILE_VIEW;
}

void drawCredits() {
  screenState = ScreenState::CREDITS;
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print("Creditos");
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  const char *const authors[] = {
      "Fabian A. Nana",
      "Andrea Razuri-Madrid",
      "Alvaro Cigaran",
      "Nadira Oviedo",
      "Bruno Tello",
      "Adrian Gutierrez",
      "Leslie Y. Cieza"};
  for (uint8_t i = 0; i < 7; ++i) {
    tft.setCursor(10, 34 + i * 16);
    tft.print(authors[i]);
  }
  tft.setTextColor(ILI9341_CYAN);
  tft.setCursor(10, 154);
  tft.print("XStudio Lab / XSpace Bio v1.0");
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(10, 184);
  tft.print("IEEE MeMeA 2026 | DOI 11537340");
  tft.setCursor(10, 220);
  tft.print("Pulsa un boton para volver");
}

void handleButtons() {
  const bool up = buttonPressed(buttonUp);
  const bool down = buttonPressed(buttonDown);
  const bool select = buttonPressed(buttonSelect);

  if (screenState == ScreenState::MENU) {
    if (up) {
      menuIndex = (menuIndex + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
      drawMenu();
    } else if (down) {
      menuIndex = (menuIndex + 1) % MENU_ITEM_COUNT;
      drawMenu();
    } else if (select) {
      selectMenuItem();
    }
    return;
  }

  if (screenState == ScreenState::MEASUREMENT) {
    if (select) finishMeasurement();
    return;
  }

  if (screenState == ScreenState::SAVE_PROMPT) {
    if (!recordingToSd) {
      if (select || up || down) drawMenu();
    } else if (up || down) {
      saveIndex = 1 - saveIndex;
      drawSavePrompt();
    } else if (select) {
      finalizeRecording();
    }
    return;
  }

  if (screenState == ScreenState::FILE_LIST) {
    if (up) {
      fileIndex = fileIndex == 0 ? totalFiles : fileIndex - 1;
      drawFileList();
    } else if (down) {
      fileIndex = fileIndex >= totalFiles ? 0 : fileIndex + 1;
      drawFileList();
    } else if (select) {
      if (fileIndex >= totalFiles) drawMenu();
      else plotSelectedFile();
    }
    return;
  }

  if (screenState == ScreenState::FILE_VIEW) {
    if (select || up || down) drawFileList();
    return;
  }

  if (screenState == ScreenState::CREDITS && (select || up || down)) {
    drawMenu();
  }
}

