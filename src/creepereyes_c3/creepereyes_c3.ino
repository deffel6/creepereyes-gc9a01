//--------------------------------------------------------------------------
// CreeperEyes – Port fuer ESP32-C3 Super Mini + 2x GC9A01 (240x240 rund)
//
// Basiert auf:
// CreeperEyes by tooluser – https://github.com/tooluser/CreeperEyes
// 2018 Modified by Laurent Moll for Uncanny Eyes costume
// https://www.hackster.io/projects/376a13/
// Uncanny eyes – Written by Phil Burgess / Paint Your Dragon for
// Adafruit Industries. MIT license.
//
// Aenderungen gegenueber CreeperEyes:
//  - Displays: 2x GC9A01 240x240 ueber LovyanGFX statt 2x SSD1351 128x128
//  - die 128x128-Augendaten werden vergroessert (ZOOM_VIEW), Ausgabe in
//    zwei Streifen per DMA
//  - ESP32-C3 statt klassischem ESP32, CS von Hand geschaltet
//  - wie bei CreeperEyes: beide Augen schauen gleich, die Lider des
//    linken Auges sind gespiegelt; jedes Auge blinzelt fuer sich
//
// Verdrahtung (an BEIDEN Displays parallel):
//   VCC -> 3V3   GND -> GND   SCL -> GPIO 6   SDA -> GPIO 7
//   DC  -> GPIO 2             RST -> GPIO 1   BLK -> GPIO 3 (falls vorhanden)
// Getrennt:
//   CS Display 1 (links)  -> GPIO 10
//   CS Display 2 (rechts) -> GPIO 4
//
// Board in der Arduino IDE: "ESP32C3 Dev Module"
// Bibliothek: LovyanGFX
//--------------------------------------------------------------------------

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// AUGE WAEHLEN ------------------------------------------------------------
// 0 = defaultEye.h (menschliches, haselnussbraunes Auge)
// 1 = newtEye.h    (Molch-Auge)
#define EYE_TYP 0

// #define SYMMETRICAL_EYELID  // einkommentieren fuer symmetrische Lider

#if EYE_TYP == 1
  #include "newtEye.h"
#else
  #include "defaultEye.h"
#endif

// DISPLAY HARDWARE CONFIG -------------------------------------------------
#define PIN_SCLK  6
#define PIN_MOSI  7
#define PIN_DC    2
#define PIN_RST   1
#define PIN_BL    3   // Display ohne BLK-Pin: egal, dann leuchtet es immer
#define PIN_CS1  10   // Display 1 (linkes Auge)
#define PIN_CS2   4   // Display 2 (rechtes Auge)
#define NUM_EYES  2

#define HELLIGKEIT 255   // normale Helligkeit 0..255
#define FLACKERN   0     // 1 = Hintergrundbeleuchtung flackert ab und zu

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
public:
  LGFX() {
    { auto c = _bus.config(); c.spi_host = SPI2_HOST; c.spi_mode = 0;
      c.freq_write = 40000000; c.freq_read = 16000000;
      c.spi_3wire = true; c.use_lock = true; c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = PIN_SCLK; c.pin_mosi = PIN_MOSI; c.pin_miso = -1; c.pin_dc = PIN_DC;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config(); c.pin_cs = -1; c.pin_rst = PIN_RST; c.pin_busy = -1;
      c.panel_width = 240; c.panel_height = 240; c.readable = false;
      c.invert = true; c.rgb_order = false;
      _panel.config(c); }
    { auto c = _light.config(); c.pin_bl = PIN_BL; c.invert = false;
      c.freq = 44100; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    setPanel(&_panel);
  }
};

LGFX tft;

// INPUT CONFIG ------------------------------------------------------------

#define TRACKING          // If enabled, eyelid tracks pupil
#define AUTOBLINK         // If enabled, eyes blink autonomously
#ifndef IRIS_MIN          // newtEye.h bringt eigene Werte mit
#define IRIS_MIN      150 // (WAS: 120)
#endif
#ifndef IRIS_MAX
#define IRIS_MAX      400 // (WAS: 720)
#endif

// Eye blinks are a tiny 3-state machine.
#define NOBLINK 0     // Not currently engaged in a blink
#define ENBLINK 1     // Eyelid is currently closing
#define DEBLINK 2     // Eyelid is currently opening
typedef struct {
  uint8_t  state;     // NOBLINK/ENBLINK/DEBLINK
  uint32_t duration;  // Duration of blink state (micros)
  uint32_t startTime; // Time (micros) of last state change
} eyeBlink;

eyeBlink blink[NUM_EYES];

const uint8_t csPin[2] = {PIN_CS1, PIN_CS2};
void selectEye(int e) {   // e = -1: keins, 99: beide
  for (int i = 0; i < 2; i++)
    digitalWrite(csPin[i], (e == 99 || e == i) ? LOW : HIGH);
}

// Ausgabe: Ausschnitt der 128x128-Augendaten, auf 240x240 vergroessert.
// ZOOM_VIEW = wie viele Datenpixel auf die Displaybreite kommen:
//   120 = ganzes Auge (2x), 96 = 2,5x, 80 = 3x (Lidwinkel werden abgeschnitten)
#define ZOOM_VIEW 108
#define VIEW_X0    ((SCREEN_WIDTH - ZOOM_VIEW) / 2)  // linker Rand des Ausschnitts
#define VIEW_Y0    (62 - ZOOM_VIEW / 2)               // Augenoeffnung liegt um y=62
#define STRIP_H    120                                // Displayzeilen pro Streifen
uint16_t* stripBuf[2];                                // je 240 x 120 Displaypixel
uint8_t   lutX[240], lutY[240];                       // Displaypixel -> Datenpixel

// INITIALIZATION -- runs once at startup ----------------------------------

void setup(void) {
  Serial.begin(115200);
  randomSeed(esp_random());
  delay(1500);
  Serial.printf("\n[START] CreeperEyes C3, freier Speicher %u\n", (unsigned)ESP.getFreeHeap());

  pinMode(PIN_CS1, OUTPUT);
  pinMode(PIN_CS2, OUTPUT);
  selectEye(99);                // beide Displays gleichzeitig initialisieren
  tft.init();
  tft.setBrightness(HELLIGKEIT);
  tft.fillScreen(TFT_BLACK);
  selectEye(-1);

  for (int i = 0; i < 2; i++) {
    stripBuf[i] = (uint16_t*)heap_caps_malloc(240 * STRIP_H * 2, MALLOC_CAP_DMA);
    if (!stripBuf[i]) {
      selectEye(99);
      tft.setTextColor(TFT_RED);
      tft.drawString("kein RAM", 90, 110);
      while (true) delay(1000);
    }
  }
  for (int i = 0; i < 240; i++) {
    lutX[i] = VIEW_X0 + i * ZOOM_VIEW / 240;
    int y = VIEW_Y0 + i * ZOOM_VIEW / 240;
    lutY[i] = y < 0 ? 0 : (y > SCREEN_HEIGHT - 1 ? SCREEN_HEIGHT - 1 : y);
  }
}

// EYE-RENDERING FUNCTION --------------------------------------------------

static inline uint16_t swap16(uint16_t v) { return (v >> 8) | (v << 8); }

void drawEye( // Renders one eye. Inputs must be pre-clipped & valid.
  uint8_t  e,       // Eye array index; 0 or 1 for left/right
  uint32_t iScale,  // Scale factor for iris
  uint8_t  scleraX0,// First pixel X offset into sclera image
  uint8_t  scleraY0,// First pixel Y offset into sclera image
  uint8_t  uT,      // Upper eyelid threshold value
  uint8_t  lT) {    // Lower eyelid threshold value

  uint16_t line[SCREEN_WIDTH];   // eine Zeile Datenpixel (bereits byte-getauscht)
  selectEye(e);
  tft.startWrite();
  for (int s = 0; s < 240 / STRIP_H; s++) {
    uint16_t* buf = stripBuf[s & 1];
    int lastY = -1;
    for (int dy = 0; dy < STRIP_H; dy++) {
      uint16_t* out = buf + dy * 240;
      int screenY = lutY[s * STRIP_H + dy];
      if (screenY == lastY) {          // gleiche Datenzeile -> nur kopieren
        memcpy(out, out - 240, 240 * 2);
        continue;
      }
      lastY = screenY;
      int scleraY = scleraY0 + screenY;
      int irisY   = scleraY - (SCLERA_HEIGHT - IRIS_HEIGHT) / 2;
      for (int screenX = lutX[0]; screenX <= lutX[239]; screenX++) {
        int scleraX = scleraX0 + screenX;
        int irisX   = scleraX - (SCLERA_WIDTH - IRIS_WIDTH) / 2;
        // wie CreeperEyes: Lider des linken Auges gespiegelt
        int effectiveX = e == 1 ? screenX : SCREEN_WIDTH - 1 - screenX;
        uint16_t p;
        if ((lower[screenY][effectiveX] <= lT) ||
            (upper[screenY][effectiveX] <= uT)) {             // Covered by eyelid
          p = 0;
        } else if ((irisY < 0) || (irisY >= IRIS_HEIGHT) ||
                   (irisX < 0) || (irisX >= IRIS_WIDTH)) {    // In sclera
          p = sclera[scleraY][scleraX];
        } else {                                              // Maybe iris...
          p = polar[irisY][irisX];                            // Polar angle/dist
          uint32_t d = (iScale * (p & 0x7F)) / 128;           // Distance (Y)
          if (d < IRIS_MAP_HEIGHT) {                          // Within iris area
            uint16_t a = (IRIS_MAP_WIDTH * (p >> 7)) / 512;   // Angle (X)
            p = iris[d][a];                                   // Pixel = iris
          } else {                                            // Not in iris
            p = sclera[scleraY][scleraX];                     // Pixel = sclera
          }
        }
        line[screenX] = swap16(p);     // Display erwartet High-Byte zuerst
      }
      for (int dx = 0; dx < 240; dx++) out[dx] = line[lutX[dx]];
    }
    // Streifen per DMA senden; waehrenddessen wird der naechste berechnet
    tft.pushImageDMA(0, s * STRIP_H, 240, STRIP_H, (lgfx::swap565_t*)buf);
  }
  tft.waitDMA();
  tft.endWrite();
  selectEye(-1);
}

// EYE ANIMATION -----------------------------------------------------------

const uint8_t ease[] = { // Ease in/out curve for eye movements 3*t^2-2*t^3
    0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  2,  2,  2,  3,   // T
    3,  3,  4,  4,  4,  5,  5,  6,  6,  7,  7,  8,  9,  9, 10, 10,   // h
   11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 22, 23,   // x
   24, 25, 26, 27, 27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 38, 39,   // 2
   40, 41, 42, 44, 45, 46, 47, 48, 50, 51, 52, 53, 54, 56, 57, 58,   // A
   60, 61, 62, 63, 65, 66, 67, 69, 70, 72, 73, 74, 76, 77, 78, 80,   // l
   81, 83, 84, 85, 87, 88, 90, 91, 93, 94, 96, 97, 98,100,101,103,   // e
  104,106,107,109,110,112,113,115,116,118,119,121,122,124,125,127,   // c
  128,130,131,133,134,136,137,139,140,142,143,145,146,148,149,151,   // J
  152,154,155,157,158,159,161,162,164,165,167,168,170,171,172,174,   // a
  175,177,178,179,181,182,183,185,186,188,189,190,192,193,194,195,   // c
  197,198,199,201,202,203,204,205,207,208,209,210,211,213,214,215,   // o
  216,217,218,219,220,221,222,224,225,226,227,228,228,229,230,231,   // b
  232,233,234,235,236,237,237,238,239,240,240,241,242,243,243,244,   // s
  245,245,246,246,247,248,248,249,249,250,250,251,251,251,252,252,   // o
  252,253,253,253,254,254,254,254,254,255,255,255,255,255,255,255 }; // n

#ifdef AUTOBLINK
uint32_t timeOfLastBlink = 0L, timeToNextBlink = 0L;
#endif

#if FLACKERN
uint32_t flickerEnd = 0, nextFlicker = 6000;
void flicker() {
  uint32_t now = millis();
  if (now >= nextFlicker && flickerEnd == 0) flickerEnd = now + random(150, 900);
  if (flickerEnd) {
    if (now >= flickerEnd) {
      flickerEnd = 0;
      nextFlicker = now + random(3000, 15000);
      tft.setBrightness(HELLIGKEIT);
    } else {
      tft.setBrightness(random(100) < 35 ? random(0, 40) : random(120, HELLIGKEIT + 1));
    }
  }
}
#endif

void frame( // Process motion for a single frame
  uint16_t        iScale) {     // Iris scale (0-1023) passed in
  static uint8_t  eyeIndex = 0; // eye[] array counter
  int16_t         eyeX, eyeY;
  uint32_t        t = micros(); // Time at start of function

  if(++eyeIndex >= NUM_EYES) eyeIndex = 0; // Cycle through eyes, 1 per call

#if FLACKERN
  flicker();
#endif

  // Autonomous X/Y eye motion
  // Periodically initiates motion to a new random point, random speed,
  // holds there for random period until next motion.

  static bool     eyeInMotion      = false;
  static int16_t  eyeOldX=512, eyeOldY=512, eyeNewX=512, eyeNewY=512;
  static uint32_t eyeMoveStartTime = 0L;
  static int32_t  eyeMoveDuration  = 0L;

  int32_t dt = t - eyeMoveStartTime;      // uS elapsed since last eye event

  if(eyeInMotion) {                       // Currently moving?
    if(dt >= eyeMoveDuration) {           // Time up?  Destination reached.
      eyeInMotion      = false;           // Stop moving
      eyeMoveDuration  = random(3000000); // 0-3 sec stop
      eyeMoveStartTime = t;               // Save initial time of stop
      eyeX = eyeOldX = eyeNewX;           // Save position
      eyeY = eyeOldY = eyeNewY;
    } else { // Move time's not yet fully elapsed -- interpolate position
      int16_t e = ease[255 * dt / eyeMoveDuration] + 1;   // Ease curve
      eyeX = eyeOldX + (((eyeNewX - eyeOldX) * e) / 256); // Interp X
      eyeY = eyeOldY + (((eyeNewY - eyeOldY) * e) / 256); // and Y
    }
  } else {                                // Eye stopped
    eyeX = eyeOldX;
    eyeY = eyeOldY;
    if(dt > eyeMoveDuration) {            // Time up?  Begin new move.
      int16_t  dx, dy;
      uint32_t d;
      do {                                // Pick new dest in circle
        eyeNewX = random(1024);
        eyeNewY = random(1024);
        dx      = (eyeNewX * 2) - 1023;
        dy      = (eyeNewY * 2) - 1023;
      } while((d = (dx * dx + dy * dy)) > (1023 * 1023)); // Keep trying
      eyeMoveDuration  = random(72000, 144000); // ~1/14 - ~1/7 sec
      eyeMoveStartTime = t;               // Save initial time of move
      eyeInMotion      = true;            // Start move on next frame
    }
  }

  // Blinking
#ifdef AUTOBLINK
  // Similar to the autonomous eye movement above -- blink start times
  // and durations are random (within ranges).
  if((t - timeOfLastBlink) >= timeToNextBlink) { // Start new blink?
    timeOfLastBlink = t;
    uint32_t blinkDuration = random(36000, 72000); // ~1/28 - ~1/14 sec
    for(uint8_t e=0; e<NUM_EYES; e++) {
      if(blink[e].state == NOBLINK) {
        blink[e].state     = ENBLINK;
        blink[e].startTime = t;
        blink[e].duration  = blinkDuration;
      }
    }
    timeToNextBlink = blinkDuration * 3 + random(4000000);
  }
#endif

  if(blink[eyeIndex].state) { // Eye currently blinking?
    // Check if current blink state time has elapsed
    if((t - blink[eyeIndex].startTime) >= blink[eyeIndex].duration) {
      if(++blink[eyeIndex].state > DEBLINK) {  // Deblinking finished?
        blink[eyeIndex].state = NOBLINK;       // No longer blinking
      } else {                       // Advancing from ENBLINK to DEBLINK mode
        blink[eyeIndex].duration *= 2;         // DEBLINK is 1/2 ENBLINK speed
        blink[eyeIndex].startTime = t;
      }
    }
  }

  // Process motion, blinking and iris scale into renderable values

  // Iris scaling: remap from 0-1023 input to iris map height pixel units
  iScale = ((IRIS_MAP_HEIGHT + 1) * 1024) /
           (1024 - (iScale * (IRIS_MAP_HEIGHT - 1) / IRIS_MAP_HEIGHT));

  // Scale eye X/Y positions (0-1023) to pixel units used by drawEye()
  eyeX = map(eyeX, 0, 1023, 0, SCLERA_WIDTH  - 128);
  eyeY = map(eyeY, 0, 1023, 0, SCLERA_HEIGHT - 128);

  // Horizontal position is offset so that eyes are very slightly crossed
  // to appear fixated (converged) at a conversational distance.
  eyeX += 4;
  if(eyeX > (SCLERA_WIDTH - 128)) eyeX = (SCLERA_WIDTH - 128);

  // Eyelids are rendered using a brightness threshold image.  This same
  // map can be used to make the upper eyelid track the pupil.
  static uint8_t uThreshold = 128;
  uint8_t        lThreshold, n;
#ifdef TRACKING
  int16_t sampleX = SCLERA_WIDTH  / 2 - (eyeX / 2), // Reduce X influence
          sampleY = SCLERA_HEIGHT / 2 - (eyeY + IRIS_HEIGHT / 4);
  // Eyelid is slightly asymmetrical, so two readings are taken, averaged
  if(sampleY < 0) n = 0;
  else            n = (upper[sampleY][sampleX] +
                       upper[sampleY][SCREEN_WIDTH - 1 - sampleX]) / 2;
  uThreshold = (uThreshold * 3 + n) / 4; // Filter/soften motion
  // Lower eyelid is pulled upward by tension from the upper lid.
  lThreshold = 254 - uThreshold;
#else // No tracking -- eyelids full open unless blink modifies them
  uThreshold = lThreshold = 0;
#endif

  // The upper/lower thresholds are then scaled relative to the current
  // blink position so that blinks work together with pupil tracking.
  if(blink[eyeIndex].state) { // Eye currently blinking?
    uint32_t s = (t - blink[eyeIndex].startTime);
    if(s >= blink[eyeIndex].duration) s = 255;   // At or past blink end
    else s = 255 * s / blink[eyeIndex].duration; // Mid-blink
    s          = (blink[eyeIndex].state == DEBLINK) ? 1 + s : 256 - s;
    n          = (uThreshold * s + 254 * (257 - s)) / 256;
    lThreshold = (lThreshold * s + 254 * (257 - s)) / 256;
  } else {
    n          = uThreshold;
  }

  // Pass all the derived values to the eye-rendering function:
  drawEye(eyeIndex, iScale, eyeX, eyeY, n, lThreshold);
}

// AUTONOMOUS IRIS SCALING (if no photocell or dial) -----------------------
// Fractal behavior to simulate both the major reaction of the eye plus
// the continuous smaller adjustments that occur.

uint16_t oldIris = (IRIS_MIN + IRIS_MAX) / 2, newIris;

void split( // Subdivides motion path into two sub-paths w/randomization
  int16_t  startValue, // Iris scale value (IRIS_MIN to IRIS_MAX) at start
  int16_t  endValue,   // Iris scale value at end
  uint32_t startTime,  // micros() at start
  int32_t  duration,   // Start-to-end time, in microseconds
  int16_t  range) {    // Allowable scale value variance when subdividing

  if(range >= 8) {     // Limit subdvision count, because recursion
    range    /= 2;     // Split range & time in half for subdivision,
    duration /= 2;     // then pick random center point within range:
    int16_t  midValue = (startValue + endValue - range) / 2 + random(range);
    uint32_t midTime  = startTime + duration;
    split(startValue, midValue, startTime, duration, range); // First half
    split(midValue  , endValue, midTime  , duration, range); // Second half
  } else {             // No more subdivisons, do iris motion...
    int32_t dt;        // Time (micros) since start of motion
    int16_t v;         // Interim value
    while((dt = (micros() - startTime)) < duration) {
      v = startValue + (((endValue - startValue) * dt) / duration);
      if(v < IRIS_MIN)      v = IRIS_MIN; // Clip just in case
      else if(v > IRIS_MAX) v = IRIS_MAX;
      frame(v);        // Draw frame w/interim iris scale value
    }
  }
}

// MAIN LOOP -- runs continuously after setup() ----------------------------

void loop() {
  Serial.printf("[STATUS] laeuft, freier Speicher %u\n", (unsigned)ESP.getFreeHeap());
  newIris = random(IRIS_MIN, IRIS_MAX);
  split(oldIris, newIris, micros(), 10000000L, IRIS_MAX - IRIS_MIN);
  oldIris = newIris;
}
