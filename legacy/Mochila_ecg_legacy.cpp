#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <SD.h>
#include <XSpaceBioV10.h>
#include <XSControl.h>

// Definiciones para la pantalla TFT y la SD
#define TFT_CS     17
#define TFT_RST    21
#define TFT_DC     22
#define SD_CS      16
#define SD_MOSI    23
#define SD_MISO    19
#define SD_SCK     18

// Definiciones para los pines de botones y EKG
#define BTN_UP     0
#define BTN_DOWN   2
#define BTN_SELECT 32
#define VBAT_LVL   36

// Variables globales
bool isPaused = false;

// Inicializa la pantalla
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
File root;
File dataFile;
// Variables de estado
int fileIndex = 0;
int totalFiles = 0;
int displayOffset = 0; // Offset para el scroll de archivos
const int filesPerPage = 8; // Número de archivos que pueden ser mostrados en una página
String fileNames[500]; // Suponemos un máximo de 50 archivos en la SD
// Instancia de la placa XSpaceBioV10 para interactuar con la placa
XSpaceBioV10Board Board;
XSFilter Filter1;
XSFilter Filter2;
XSFilter Filter3;
XSFilter Filter4;
XSFilter Filter5;
XSFilter Filter6;
XSFilter Filter7;

// Variables del menú
int currentMenu = 0;
const int menuItems = 3;
String menuOptions[menuItems] = {"Nueva medicion", "Antiguas mediciones", "Creditos"};

// Variables de debouncing
unsigned long lastDebounceTimeUp = 0;
unsigned long lastDebounceTimeDown = 0;
unsigned long lastDebounceTimeSelect = 0;
unsigned long lastDebounceTimeLongPress = 0;
unsigned long debounceDelay = 200; // 200 ms de delay para el debouncing
bool longPressActive = false;

// Variables de la opción de guardar
int saveOption = 0; // 0 = "Sí", 1 = "No"

// Estado del menú: 0 = menú principal, 1 = medición EKG, 2 = confirmación de guardado, 3 = mediciones antiguas
int menuState = 0;

// Array para almacenar las lecturas del EKG
const int maxReadings = 1500; // Ajusta según sea necesario
double ekgReadings1[maxReadings], ekgReadings2[maxReadings], ekgReadings3[maxReadings];
int readingIndex = 0;

// Variables globales para almacenar los valores ECG
double raw_ecg = 0;
double filtered_ecg = 0;
double raw_ecg_2 = 0;
double filtered_ecg_2 = 0;
double ecg_2_minus_ecg1 = 0;
double filtered_ecg_3 = 0;
double filtered_ecg_1_2 = 0;
double filtered_ecg_2_2 = 0;
double filtered_ecg_1_3 = 0;
double filtered_ecg_2_3 = 0;

// Variables para el ploteo
const int graphWidth = 320;
const int graphHeight = 240;
const int screenWidth = 320;
const int screenHeight = 240;
int xPos = 0;
int prevXPos = 0;

// Posiciones Y anteriores para cada derivación
int prevYPos1 = screenHeight / 6;
int prevYPos2 = screenHeight / 2;
int prevYPos3 = (5 * screenHeight) / 6;

///Variables para BPM
const float UpperThreshold =2.45; //2.60 
const float LowerThreshold = 2.60; //2.40
float BPM = 0.0;
bool IgnoreReading = false;
int pulseCount = 0;
unsigned long pulseTimes[4] = {0, 0, 0, 0};
unsigned long PulseInterval = 0;
unsigned long lastBpmUpdateTime = 0;

// Variables para almacenar BPM y calcular el promedio
const int bpmBufferSize = 7;  // Ajustar a 7 para promediar los últimos 7 valores
float bpmBuffer[bpmBufferSize] = {0};
int bpmBufferIndex = 0;

// Prototipos de funciones
void drawMenu();
void selectMenuOption();
void startEKGMeasurement();
void endEKGMeasurement();
void displaySaveOption();
void handleSaveOption();
void displayCredits();
void displayPreviousMeasurements();
void plotEKGValue(int value1, int value2, int value3);
void drawGraphAxes();
void enterFileName();
float calculateAverageBPM();
void loadFileNames();
void displayMenu();
void plotSelectedFile();
bool debounce(int pin, unsigned long &lastDebounceTime);
void handleMainButtonPresses();
void handlePreviousMeasurementsButtonPresses();
void handleLongPress();
void resetToMainMenu();

bool menuActive = false;
int menuSelection = 0; // 0: Restart, 1: Exit
bool sdDetected = false;

void FilterTask(void *pv) {
  while (1) {
    // Leer el voltaje crudo del ECG del primer AD8232
    raw_ecg = Board.AD8232_GetVoltage(AD8232_XS1);
    // Aplicar filtro de paso bajo de segundo orden
    filtered_ecg = Filter6.SecondOrderLPF(raw_ecg, 40, 0.001);

    // Leer el voltaje crudo del ECG del segundo AD8232
    raw_ecg_2 = Board.AD8232_GetVoltage(AD8232_XS2);
    // Aplicar filtro de paso bajo de segundo orden
    filtered_ecg_2 = Filter7.SecondOrderLPF(raw_ecg_2, 40, 0.001);

    // Calcular la tercera derivación como la diferencia entre las dos primeras
    ecg_2_minus_ecg1 = filtered_ecg_2 - filtered_ecg;
    filtered_ecg_3 = Filter3.SecondOrderLPF(ecg_2_minus_ecg1, 40, 0.001);

    // Esperar 1 milisegundo antes de la siguiente iteración
    vTaskDelay(1);
  }
  // Nunca se llega aquí
  vTaskDelete(NULL);
}

void setup() {
  // Inicializar comunicación serial
  Serial.begin(115200);
  // Configura los pines de los botones como entradas con resistencias pull-up internas
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  
  // Inicializa la pantalla
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);
  
  // Inicializa la tarjeta SD
  sdDetected = SD.begin(SD_CS);
  
  // Inicializa la placa XSpace Bio v1.0
  Board.init();
  // Activa el sensor AD8232 en la ranura XS1 para empezar a monitorear
  Board.AD8232_Wake(AD8232_XS1);
  // Activa el sensor AD8232 en la ranura XS2 para empezar a monitorear
  Board.AD8232_Wake(AD8232_XS2);

  // Crea la tarea de filtrado con un tamaño de pila de 3000 bytes
  xTaskCreate(FilterTask, "FilterTask", 3000, NULL, 1, NULL);

  // Carga los nombres de los archivos
  loadFileNames();

  // Dibuja el menú inicial
  drawMenu();
}

void loop() {
  if (menuState == 3) {
    handlePreviousMeasurementsButtonPresses();
  } else {
    handleMainButtonPresses();
  }
}

void drawMenu() {
  tft.fillScreen(ILI9341_BLACK);
  
  // Título
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10); // Posición del título
  tft.print("Proyecto Instrumentacion");
  
  // Opciones del menú
  tft.setTextSize(2);
  int initialY = 60; // Posición y inicial para las opciones, ajustada para dejar un espacio
  int ySpacing = 30; // Espaciado entre las opciones
  for (int i = 0; i < menuItems; i++) {
    if (i == currentMenu) {
      tft.setTextColor(ILI9341_YELLOW);
    } else {
      tft.setTextColor(ILI9341_WHITE);
    }
    tft.setCursor(10, initialY + i * ySpacing);
    tft.print(menuOptions[i]);
  }
}

void selectMenuOption() {
  switch (currentMenu) {
    case 0:
      startEKGMeasurement();
      break;
    case 1:
      menuState = 3;
      displayPreviousMeasurements();
      break;
    case 2:
      displayCredits();
      break;
  }
}

void startEKGMeasurement() {
  tft.fillScreen(ILI9341_BLACK);
  drawGraphAxes(); // Dibuja los ejes de la gráfica
  menuState = 1; // Cambia el estado del menú

  // Realiza las lecturas del EKG y dibuja la gráfica
  readingIndex = 0; // Reinicia el índice de lectura

  while (readingIndex < maxReadings) {
    // Borrar la columna actual para sobreescribir la señal
    tft.drawFastVLine(xPos, 0, screenHeight, ILI9341_BLACK);
    // Redibujar ejes y leyendas
    tft.drawLine(xPos, screenHeight / 3, xPos + 1, screenHeight / 3, ILI9341_WHITE); // Eje X para la primera derivación
    tft.drawLine(xPos, 2*(screenHeight)/3, xPos + 1, 2*(screenHeight)/3, ILI9341_WHITE); // Eje X para la segunda derivación
    tft.drawLine(xPos, screenHeight, xPos + 1, screenHeight, ILI9341_WHITE); // Eje X para la tercera derivación
    // Mapeo de los valores filtrados de las derivaciones en la pantalla TFT
    int yPos1 = map(filtered_ecg * 15000, -11000, 2000, (screenHeight / 3) + 10, (screenHeight / 3) - 10);  // Derivación 1
    int yPos2 = map(filtered_ecg_2 * 15000, -11000, 2000, ((3*screenHeight) / 4) + 10, ((3*screenHeight) / 4) - 10);  // Derivación 2
    int yPos3 = map(filtered_ecg_3 * 15000, -11000, 2000, (4 * screenHeight / 5) + 10, (4 * screenHeight / 5) - 10);  // Derivación 3

    // Dibujar líneas desde las posiciones anteriores a las nuevas posiciones
    tft.drawLine(prevXPos, prevYPos1, xPos, yPos1, ILI9341_RED); // Derivación 1
    tft.drawLine(prevXPos, prevYPos2, xPos, yPos2, ILI9341_GREEN); // Derivación 2
    tft.drawLine(prevXPos, prevYPos3, xPos, yPos3, ILI9341_BLUE); // Derivación 3

    // Actualizar las posiciones anteriores
    prevXPos = xPos;
    prevYPos1 = yPos1;
    prevYPos2 = yPos2;
    prevYPos3 = yPos3;

    // Mover la posición x
    xPos++;
    ekgReadings1[readingIndex] = filtered_ecg;
    ekgReadings2[readingIndex] = filtered_ecg_2;
    ekgReadings3[readingIndex] = filtered_ecg_3;
    readingIndex++;
    if (xPos >= screenWidth) {
      xPos = 0;
      prevXPos = 0;
      drawGraphAxes();
    }

    // Detección de picos y cálculo de BPM
    if(filtered_ecg >= UpperThreshold && !IgnoreReading) {
      for (int i = 0; i < 3; i++) {
        pulseTimes[i] = pulseTimes[i + 1];
      }
      pulseTimes[3] = millis();
      pulseCount++;
      IgnoreReading = true;
    }

    if(filtered_ecg < LowerThreshold) {
      IgnoreReading = false;
    }

    if (pulseCount >= 4 && millis() > 2000) { 
      PulseInterval = pulseTimes[2] - pulseTimes[0];
      float tempBPM = (3.0 / (PulseInterval / 1000.0)) * 60.0;
      if (tempBPM >= 29 && tempBPM <= 330) {
        BPM = tempBPM;
        bpmBuffer[bpmBufferIndex] = BPM;
        bpmBufferIndex = (bpmBufferIndex + 1) % bpmBufferSize;
      } else {
        // Ajustar la matriz si el valor de BPM no es correcto
        for (int i = 0; i < 3; i++) {
          pulseTimes[i] = pulseTimes[i + 1];
        }
        pulseCount = 3;
      }
    }

    // Calcular el BPM promedio
    float avgBPM = calculateAverageBPM();

    // Muestra el BPM en la pantalla si ha sido actualizado
    tft.fillRect(10, 10, 120, 20, ILI9341_BLACK); // Limpiar área donde se mostrará el BPM
    tft.setTextColor(ILI9341_GREEN);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("BPM: ");
    tft.print(avgBPM,0); // Mostrar BPM promedio sin decimales

    // Mostrar el BPM en la esquina superior derecha
    tft.setTextSize(1);
    tft.setCursor(0, screenHeight / 3 - 10);
    tft.print("Derivacion 1");
    tft.setCursor(0, (2*screenHeight) / 3 - 10);
    tft.print("Derivacion 2");
    tft.setCursor(0, (4 * screenHeight) / 4 - 10);
    tft.print("Derivacion 3");

    // Imprimir valores en el monitor serial para depuración
    Serial.print(filtered_ecg, 6);
    Serial.print(" ");
    Serial.print(filtered_ecg_2, 6);
    Serial.print(" ");
    Serial.println(filtered_ecg_3, 6);
    delay(5); // Ajusta según sea necesario
  }

  // Después de la medición, solicita si se desea guardar
  menuState = 2;
  displaySaveOption();
}

void drawGraphAxes() {
  // Dibujar ejes en la pantalla
  tft.drawLine(0, screenHeight / 3, screenWidth, screenHeight / 3, ILI9341_WHITE); // Eje X para la primera derivación
  tft.drawLine(0, 2*(screenHeight)/3, screenWidth, 2*(screenHeight)/3, ILI9341_WHITE); // Eje X para la segunda derivación
  tft.drawLine(0, screenHeight, screenWidth, screenHeight, ILI9341_WHITE); // Eje X para la tercera derivación
  // Dibujar leyendas
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(0, screenHeight / 3 - 10);
  tft.print("Derivacion 1");
  tft.setCursor(0, (2*screenHeight) / 3 - 10);
  tft.print("Derivacion 2");
  tft.setCursor(0, (4 * screenHeight) / 4 - 10);
  tft.print("Derivacion 3");
}

void endEKGMeasurement() {
  menuState = 0; // Vuelve al menú principal
  resetToMainMenu();
}

void displaySaveOption() {
  tft.fillScreen(ILI9341_BLACK);
  
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10); // Posición del mensaje de guardado
  tft.print("Guardar medicion?");
  
  String options[2] = {"Si", "No"};
  
  int initialY = 60; // Posición y inicial para las opciones, ajustada para dejar un espacio
  int ySpacing = 30; // Espaciado entre las opciones
  for (int i = 0; i < 2; i++) {
    if (i == saveOption) {
      tft.setTextColor(ILI9341_YELLOW);
    } else {
      tft.setTextColor(ILI9341_WHITE);
    }
    tft.setCursor(10, initialY + i * ySpacing);
    tft.print(options[i]);
  }
}

void handleSaveOption() {
  if (saveOption == 0) {
    enterFileName();
  } else {
    endEKGMeasurement();
  }
}

void enterFileName() {
  tft.fillScreen(ILI9341_BLACK);
  
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10); // Posición del mensaje
  tft.print("Guardando...");

  // Genera un nombre de archivo único basado en el tiempo
  String fileName = "/ECG_" + String(millis()) + ".txt";
  
  File dataFile = SD.open(fileName.c_str(), FILE_WRITE);
  if (dataFile) {
    for (int i = 0; i < readingIndex; i++) {
      dataFile.print(ekgReadings1[i]);
      dataFile.print(",");
      dataFile.print(ekgReadings2[i]);
      dataFile.print(",");
      dataFile.println(ekgReadings3[i]);
    }
    dataFile.close();
    tft.setCursor(10, 50);
    tft.print("Guardado en ");
    tft.print(fileName);
  } else {
    tft.setCursor(10, 50);
    tft.print("Error guardando datos");
  }
  delay(2000); // Espera para mostrar el mensaje
  endEKGMeasurement();
}

void displayCredits() {
  tft.fillScreen(ILI9341_BLACK); // Limpia la pantalla para la nueva opción seleccionada
  
  // Imprime los títulos en amarillo
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(10, 10);
  tft.println("Integrantes del grupo:");
  
  // Imprime los nombres en blanco
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 30);
  tft.println("Andrea Razuri");
  tft.setCursor(10, 50);
  tft.println("Nadira Oviedo");
  tft.setCursor(10, 70);
  tft.println("Alvaro Cigaran");
  tft.setCursor(10, 90);
  tft.println("Bruno Tello");
  tft.setCursor(10, 110);
  tft.println("Fabian Nana");
  tft.setCursor(10, 130);
  tft.println("Adrian Gutierrez (Inversor)");

  // Imprime los títulos en amarillo
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(10, 150);
  tft.println("Profesores:");
  
  // Imprime los nombres en blanco
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 170);
  tft.println("Yesenia Cieza, Mag.");
  tft.setCursor(10, 190);
  tft.println("Domingo Flores, Mag.");
  tft.setCursor(10, 210);
  tft.println("Julissa Venancio, Ing.");

  delay(7000); // Muestra los créditos por 7 segundos
  drawMenu();
}

void displayPreviousMeasurements() {
  tft.fillScreen(ILI9341_BLACK);
  
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Mediciones Antiguas");

  tft.setTextSize(1);
  int initialY = 60; // Posición y inicial para las opciones, ajustada para dejar un espacio
  int ySpacing = 20; // Espaciado entre las opciones

  // Mostrar los archivos con el offset aplicado
  for (int i = displayOffset; i < min(displayOffset + filesPerPage, totalFiles); i++) {
    if (i == fileIndex) {
      tft.setTextColor(ILI9341_YELLOW);
    } else {
      tft.setTextColor(ILI9341_WHITE);
    }
    tft.setCursor(10, initialY + (i - displayOffset) * ySpacing);
    tft.print(fileNames[i]);
  }

  // Opción de volver al menú principal
  if (fileIndex == totalFiles) {
    tft.setTextColor(ILI9341_YELLOW);
  } else {
    tft.setTextColor(ILI9341_WHITE);
  }
  tft.setCursor(10, initialY + (totalFiles - displayOffset) * ySpacing);
  tft.print("Volver al menu principal");

  // Dibujar barra de scroll
  int barHeight = screenHeight - 80;
  int scrollBarHeight = barHeight * filesPerPage / (totalFiles + 1); // +1 para la opción de volver
  int scrollBarPosition = barHeight * displayOffset / (totalFiles + 1);
  tft.fillRect(screenWidth - 10, 60 + scrollBarPosition, 5, scrollBarHeight, ILI9341_WHITE);
}

float calculateAverageBPM() {
  float sum = 0.0;
  int count = 0;
  for (int i = 0; i < bpmBufferSize; i++) {
    if (bpmBuffer[i] > 0) {
      sum += bpmBuffer[i];
      count++;
    }
  }
  return (count > 0) ? sum / count : 0.0;
}

void loadFileNames() {
  root = SD.open("/");
  totalFiles = 0;

  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      // No hay más archivos
      break;
    }
    if (!entry.isDirectory()) {
      String fileName = entry.name();
      Serial.print("Archivo encontrado: ");
      Serial.println(fileName);  // Mostrar nombres de archivos en el monitor serie
      if (fileName.endsWith(".txt") && totalFiles < 50) {
        fileNames[totalFiles++] = fileName;
      }
    }
    entry.close();
  }
  root.close();
}

void plotSelectedFile() {
  tft.fillScreen(ILI9341_BLACK);
  drawGraphAxes(); // Dibujar ejes y leyendas
  String filePath = "/" + fileNames[fileIndex];
  dataFile = SD.open(filePath.c_str());
  if (dataFile) {
    Serial.print("Abriendo archivo: ");
    Serial.println(filePath);  // Mostrar en el monitor serie
    int x = 0;
    float prevXPos = 0, prevYPos1 = 0, prevYPos2 = 0, prevYPos3 = 0;
    bool firstDataPoint = true;

    while (dataFile.available()) {
      String dataLine = dataFile.readStringUntil('\n');
      Serial.println(dataLine);  // Mostrar cada línea en el monitor serie
      int commaIndex1 = dataLine.indexOf(',');
      int commaIndex2 = dataLine.lastIndexOf(',');
      if (commaIndex1 > 0 && commaIndex2 > commaIndex1) {
        float xData = dataLine.substring(0, commaIndex1).toFloat();
        float yData = dataLine.substring(commaIndex1 + 1, commaIndex2).toFloat();
        float zData = dataLine.substring(commaIndex2 + 1).toFloat();

        // Mapeo de los valores filtrados de las derivaciones en la pantalla TFT
        int screenHeight = tft.height();
        int yPos1 = map(xData * 15000, -11000, 2000, (screenHeight / 3) + 10, (screenHeight / 3) - 10);  // Derivación 1
        int yPos2 = map(yData * 15000, -11000, 2000, ((3 * screenHeight) / 4) + 10, ((3 * screenHeight) / 4) - 10);  // Derivación 2
        int yPos3 = map(zData * 15000, -11000, 2000, (4 * screenHeight / 5) + 10, (4 * screenHeight / 5) - 10);  // Derivación 3

        // Dibujar líneas desde las posiciones anteriores a las nuevas posiciones
        if (!firstDataPoint) {
          tft.drawLine(prevXPos, prevYPos1, x, yPos1, ILI9341_RED); // Derivación 1
          tft.drawLine(prevXPos, prevYPos2, x, yPos2, ILI9341_GREEN); // Derivación 2
          tft.drawLine(prevXPos, prevYPos3, x, yPos3, ILI9341_BLUE); // Derivación 3
        } else {
          firstDataPoint = false;
        }

        // Actualizar las posiciones anteriores
        prevXPos = x;
        prevYPos1 = yPos1;
        prevYPos2 = yPos2;
        prevYPos3 = yPos3;

        // Mover la posición x
        x++;
        if (x >= tft.width()) {
          break;
        }
      }
    }
    dataFile.close();
  } else {
    tft.println("Error abriendo el archivo");
    Serial.print("Error abriendo el archivo: ");
    Serial.println(filePath);  // Mostrar el nombre del archivo que falla
  }
}

// Función para manejar el debouncing de botones
bool debounce(int pin, unsigned long &lastDebounceTime) {
  bool buttonState = digitalRead(pin);
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (buttonState == LOW) {
      lastDebounceTime = millis();
      while (digitalRead(pin) == LOW); // Esperar a que se suelte el botón
      return true;
    }
  }
  return false;
}

// Función para manejar las pulsaciones de botones en el menú principal
void handleMainButtonPresses() {
  unsigned long currentTime = millis();
  // Detecta la pulsación de los botones con debouncing
  if (digitalRead(BTN_UP) == LOW && (currentTime - lastDebounceTimeUp) > debounceDelay) {
    if (menuState == 0) {
      currentMenu = (currentMenu - 1 + menuItems) % menuItems;
      drawMenu();
    } else if (menuState == 2) {
      saveOption = (saveOption - 1 + 2) % 2;
      displaySaveOption();
    }
    lastDebounceTimeUp = currentTime; // Actualiza el tiempo del último cambio
  }
  if (digitalRead(BTN_DOWN) == LOW && (currentTime - lastDebounceTimeDown) > debounceDelay) {
    if (menuState == 0) {
      currentMenu = (currentMenu + 1) % menuItems;
      drawMenu();
    } else if (menuState == 2) {
      saveOption = (saveOption + 1) % 2;
      displaySaveOption();
    }
    lastDebounceTimeDown = currentTime; // Actualiza el tiempo del último cambio
  }
  if (digitalRead(BTN_SELECT) == LOW && (currentTime - lastDebounceTimeSelect) > debounceDelay) {
    if (menuState == 0) {
      selectMenuOption();
    } else if (menuState == 2) {
      handleSaveOption();
    }
    lastDebounceTimeSelect = currentTime; // Actualiza el tiempo del último cambio
  }
}

// Función para manejar las pulsaciones de botones en el menú de mediciones antiguas
void handlePreviousMeasurementsButtonPresses() {
  unsigned long currentTime = millis();
  
  if (debounce(BTN_UP, lastDebounceTimeUp)) {
    if (fileIndex > 0) {
      fileIndex--;
    } else {
      fileIndex = totalFiles; // Volver al final (opción de volver al menú principal)
    }
    if (fileIndex < displayOffset) {
      displayOffset = max(0, fileIndex - filesPerPage + 1);
    }
    displayPreviousMeasurements();
  }

  if (debounce(BTN_DOWN, lastDebounceTimeDown)) {
    if (fileIndex < totalFiles) {
      fileIndex++;
    } else {
      fileIndex = 0; // Volver al inicio
    }
    if (fileIndex >= displayOffset + filesPerPage) {
      displayOffset = min(fileIndex, totalFiles - filesPerPage);
    }
    displayPreviousMeasurements();
  }

  if (digitalRead(BTN_SELECT) == LOW) {
    if (!longPressActive) {
      longPressActive = true;
      lastDebounceTimeLongPress = currentTime;
    } else if (currentTime - lastDebounceTimeLongPress > 4000) {
      handleLongPress();
    }
  } else {
    if (longPressActive) {
      longPressActive = false;
      if (currentTime - lastDebounceTimeLongPress < 4000) {
        if (fileIndex == totalFiles) {
          // Opción de volver al menú principal
          menuState = 0;
          resetToMainMenu();
        } else {
          // Plotea el archivo seleccionado
          plotSelectedFile();
        }
      }
    }
    lastDebounceTimeSelect = currentTime; // Actualiza el tiempo del último cambio
  }
}

void handleLongPress() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Borrar archivo?");
  String options[2] = {"Si", "No"};
  
  int initialY = 60; // Posición y inicial para las opciones, ajustada para dejar un espacio
  int ySpacing = 30; // Espaciado entre las opciones
  int deleteOption = 0;
  
  for (int i = 0; i < 2; i++) {
    if (i == deleteOption) {
      tft.setTextColor(ILI9341_YELLOW);
    } else {
      tft.setTextColor(ILI9341_WHITE);
    }
    tft.setCursor(10, initialY + i * ySpacing);
    tft.print(options[i]);
  }
  
  while (true) {
    if (debounce(BTN_UP, lastDebounceTimeUp) || debounce(BTN_DOWN, lastDebounceTimeDown)) {
      deleteOption = (deleteOption == 0) ? 1 : 0;
      for (int i = 0; i < 2; i++) {
        if (i == deleteOption) {
          tft.setTextColor(ILI9341_YELLOW);
        } else {
          tft.setTextColor(ILI9341_WHITE);
        }
        tft.setCursor(10, initialY + i * ySpacing);
        tft.print(options[i]);
      }
    }
    if (debounce(BTN_SELECT, lastDebounceTimeSelect)) {
      if (deleteOption == 0) {
        String filePath = "/" + fileNames[fileIndex];
        SD.remove(filePath.c_str());
        loadFileNames();
        displayPreviousMeasurements();
      }
      break;
    }
  }
}

void resetToMainMenu() {
  fileIndex = 0;
  totalFiles = 0;
  displayOffset = 0;
  currentMenu = 0;
  saveOption = 0;
  menuState = 0;
  loadFileNames();
  drawMenu();
}
