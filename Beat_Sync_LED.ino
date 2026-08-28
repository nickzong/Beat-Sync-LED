// Ctrl + F "TUNE ME" to find values that may need adjustment based on hardware
#include <arduinoFFT.h>
#include <FastLED.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

struct OnsetBand;
struct ContinuousBand;
void updateOnsetBand(OnsetBand &b, const char* label);
void updateContinuousBand(ContinuousBand &b, const char* label);

// LED config
#define LED_DATA_PIN 5
#define NUM_LEDS 300
CRGB leds[NUM_LEDS];
const char* bandNames[5] = {"Kick", "Snare", "Cymbal", "Vocal", "Instr"};

// one base color, other four selected around that value
const int8_t hueOffsets[5] = {-20, -10, 0, 10, 20};
uint8_t currentBaseHue = 160; // default value, override with color knob

// OLED config
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Mode switch: beat sync & ambient
#define MODE_SWITCH_PIN 26
bool ambientMode = false; =
const uint8_t AMBIENT_BRIGHTNESS = 180; // TUNE ME

// Knob config
#define BAND_SELECT_PIN 32
#define START_KNOB_PIN 33
#define END_KNOB_PIN 35
#define COLOR_KNOB_PIN 36

// Mic/FFT config
#define MIC_PIN 34
#define SAMPLES 512
#define SAMPLING_FREQUENCY 20000
const float HZ_PER_BIN = (float)SAMPLING_FREQUENCY / SAMPLES;

ArduinoFFT<double> FFT = ArduinoFFT<double>();
double vReal[SAMPLES];
double vImag[SAMPLES];
unsigned int samplingPeriodUs;

// Frequency bands
const int kickStart = 1,   kickEnd = 4;
const int snareStart = 4,  snareEnd = 13;
const int cymbalStart = 102, cymbalEnd = 230;
const int vocalStart = 13,  vocalEnd = 64;
const int instrStart = 2,   instrEnd = 12;

int bandStartBin[5] = {kickStart, snareStart, cymbalStart, vocalStart, instrStart};
int bandEndBin[5]   = {kickEnd,   snareEnd,   cymbalEnd,   vocalEnd,   instrEnd};
const int MIN_USABLE_BIN = 1;
const int MAX_USABLE_BIN = 255;
const int MIN_BAND_WIDTH = 2;

const bool DEBUG_PRINT_LEVELS = false;

const float ledGamma = 2.2;

uint8_t applyGamma(uint8_t linearBrightness) {
  return (uint8_t)(pow(linearBrightness / 255.0, ledGamma) * 255.0 + 0.5);
}

uint8_t bandBrightness[5] = {0, 0, 0, 0, 0};

// =========================================================
// Knob reading + live band tuning
// =========================================================
float bandSelectSmooth = 0, startKnobSmooth = 0, endKnobSmooth = 0;
const float knobAlpha = 0.2;
const float OFF_ZONE_THRESHOLD = 200; // TUNE ME

int currentSelectedBand = -1;  // -1 = off, 0-4 = editing that band
int lastSelectedBand = -1;

float startKnobRef = 1, endKnobRef = 1;
int startEntryBin = 0, endEntryBin = 0;

const float MIN_REF_READING = 50;
const float MAX_RATIO = 4.0;

void readKnobsAndRetune() {
  int rawBandSelect = analogRead(BAND_SELECT_PIN);
  bandSelectSmooth = knobAlpha * rawBandSelect + (1 - knobAlpha) * bandSelectSmooth;
  startKnobSmooth   = knobAlpha * analogRead(START_KNOB_PIN)  + (1 - knobAlpha) * startKnobSmooth;
  endKnobSmooth     = knobAlpha * analogRead(END_KNOB_PIN)    + (1 - knobAlpha) * endKnobSmooth;

  if (DEBUG_PRINT_LEVELS) {
    Serial.print("bandSelect raw="); Serial.print(rawBandSelect);
    Serial.print(" smooth="); Serial.println(bandSelectSmooth);
  }

  if (bandSelectSmooth < OFF_ZONE_THRESHOLD) { // off mode
    currentSelectedBand = -1;
    lastSelectedBand = -1;  
    return;
  }

  // map remaining range excluding off mode into 5 even zones
  float usableRange = 4095.0 - OFF_ZONE_THRESHOLD;
  int zone = (int)((bandSelectSmooth - OFF_ZONE_THRESHOLD) / usableRange * 5);
  currentSelectedBand = constrain(zone, 0, 4);

  // captures current output voltage of potentiometer serving as voltage divider as start position 
  // & changes start/end value based on relative change in output voltage
  if (currentSelectedBand != lastSelectedBand) {
    startKnobRef = max(startKnobSmooth, MIN_REF_READING);
    endKnobRef   = max(endKnobSmooth, MIN_REF_READING);
    startEntryBin = bandStartBin[currentSelectedBand];
    endEntryBin   = bandEndBin[currentSelectedBand];
    lastSelectedBand = currentSelectedBand;
  } else { 
    float startRatio = constrain(startKnobSmooth / startKnobRef, 1.0f / MAX_RATIO, MAX_RATIO);
    float endRatio   = constrain(endKnobSmooth / endKnobRef,     1.0f / MAX_RATIO, MAX_RATIO);

    int newStart = constrain((int)(startEntryBin * startRatio), MIN_USABLE_BIN, MAX_USABLE_BIN);
    int newEnd   = constrain((int)(endEntryBin * endRatio),     MIN_USABLE_BIN, MAX_USABLE_BIN);

    if (newEnd <= newStart + MIN_BAND_WIDTH) newEnd = newStart + MIN_BAND_WIDTH;
    newEnd = min(newEnd, MAX_USABLE_BIN);

    bandStartBin[currentSelectedBand] = newStart;
    bandEndBin[currentSelectedBand] = newEnd;
  }
}

// =========================================================
// Color knob - picks a base hue, other 4 band colors derived from it
// =========================================================
float colorKnobSmooth = 0;

// TUNE ME with actual min/max
const int COLOR_KNOB_MIN = 0;
const int COLOR_KNOB_MAX = 4095;

void readColorKnob() {
  colorKnobSmooth = knobAlpha * analogRead(COLOR_KNOB_PIN) + (1 - knobAlpha) * colorKnobSmooth;

  float clamped = constrain(colorKnobSmooth, COLOR_KNOB_MIN, COLOR_KNOB_MAX);
  currentBaseHue = (uint8_t)((clamped - COLOR_KNOB_MIN) / (float)(COLOR_KNOB_MAX - COLOR_KNOB_MIN) * 255);

  if (DEBUG_PRINT_LEVELS) {
    Serial.print("colorKnob smooth="); Serial.print(colorKnobSmooth);
    Serial.print(" hue="); Serial.println(currentBaseHue);
  }
}

// =========================================================
// Percussion (onset/flash) bands 
// =========================================================
struct OnsetBand {
  int startBin, endBin;
  int outIndex;
  float prevMag = 0;
  float fluxAvg = 0;
  float envelope = 0;
  unsigned long lastTrigger = 0;
  float onsetMultiplier;
  float fluxAvgAlpha;
  float decayRate;
  unsigned long refractoryMs;
  float minMagnitude;
};

// TUNE ME PERCUSSION
OnsetBand kick   = {kickStart,   kickEnd,   0, 0,0,0,0, /*onsetMult*/2.2, /*fluxAlpha*/0.05, /*decay*/0.85, /*refractory*/90, /*minMag*/30000};
OnsetBand snare  = {snareStart,  snareEnd,  1, 0,0,0,0, /*onsetMult*/2.0, /*fluxAlpha*/0.05, /*decay*/0.80, /*refractory*/90, /*minMag*/100000};
OnsetBand cymbal = {cymbalStart, cymbalEnd, 2, 0,0,0,0, /*onsetMult*/2.8, /*fluxAlpha*/0.10, /*decay*/0.75, /*refractory*/90, /*minMag*/80000};

void updateOnsetBand(OnsetBand &b, const char* label) {
  float mag = 0;
  for (int i = b.startBin; i < b.endBin; i++) mag += vReal[i];

  float flux = mag - b.prevMag;
  if (flux < 0) flux = 0;
  b.prevMag = mag;

  unsigned long now = millis();
  bool loudEnough = mag > b.minMagnitude;
  bool spikedEnough = flux > b.fluxAvg * b.onsetMultiplier;
  bool offCooldown = (now - b.lastTrigger) > b.refractoryMs;

  if (loudEnough && spikedEnough && offCooldown) {
    b.envelope = 255;
    b.lastTrigger = now;
  } else {
    b.envelope *= b.decayRate;
  }

  b.fluxAvg = b.fluxAvgAlpha * flux + (1 - b.fluxAvgAlpha) * b.fluxAvg;
  bandBrightness[b.outIndex] = applyGamma(constrain((int)b.envelope, 0, 255));

  if (DEBUG_PRINT_LEVELS) {
    Serial.print(label);
    Serial.print(" mag="); Serial.print(mag);
    Serial.print(" flux="); Serial.println(flux);
  }
}

// =========================================================
// Vocal / instrument (continuous) bands 
// =========================================================
struct ContinuousBand {
  int startBin, endBin;
  int outIndex;
  float alpha;
  float scale;
  float threshold;
  float smooth = 0;
};

ContinuousBand vocalBand = {vocalStart, vocalEnd, 3, /*alpha*/0.30, /*scale*/0.003, /*threshold*/80000};
ContinuousBand instrBand = {instrStart, instrEnd, 4, /*alpha*/0.25, /*scale*/0.003, /*threshold*/40000};

void updateContinuousBand(ContinuousBand &b, const char* label) {
  float raw = 0;
  for (int i = b.startBin; i < b.endBin; i++) raw += vReal[i];
  b.smooth = b.alpha * raw + (1 - b.alpha) * b.smooth;

  uint8_t linearBrightness = 0;
  if (b.smooth > b.threshold) {
    linearBrightness = constrain((b.smooth - b.threshold) * b.scale, 0, 255);
  }
  bandBrightness[b.outIndex] = applyGamma(linearBrightness);

  if (DEBUG_PRINT_LEVELS) {
    Serial.print(label);
    Serial.print(" smooth="); Serial.print(b.smooth);
    Serial.print(" linear="); Serial.println(linearBrightness);
  }
}

void applyKnobRangesToBands() {
  kick.startBin = bandStartBin[0];   kick.endBin = bandEndBin[0];
  snare.startBin = bandStartBin[1];  snare.endBin = bandEndBin[1];
  cymbal.startBin = bandStartBin[2]; cymbal.endBin = bandEndBin[2];
  vocalBand.startBin = bandStartBin[3]; vocalBand.endBin = bandEndBin[3];
  instrBand.startBin = bandStartBin[4]; instrBand.endBin = bandEndBin[4];
}

// =========================================================
// Sampling / FFT
// =========================================================
void sampleAndFFT() {
  unsigned long microseconds;
  for (int i = 0; i < SAMPLES; i++) {
    microseconds = micros();
    vReal[i] = analogRead(MIC_PIN);
    vImag[i] = 0;
    while (micros() < (microseconds + samplingPeriodUs)) {}
  }

  double meanVal = 0;
  for (int i = 0; i < SAMPLES; i++) meanVal += vReal[i];
  meanVal /= SAMPLES;
  for (int i = 0; i < SAMPLES; i++) vReal[i] -= meanVal;

  FFT.windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(vReal, vImag, SAMPLES, FFT_FORWARD);
  FFT.complexToMagnitude(vReal, vImag, SAMPLES);
}

// =========================================================
// OLED display
// =========================================================
void updateDisplay() {
  static unsigned long lastDisplayUpdate = 0;
  const unsigned long DISPLAY_INTERVAL_MS = 150; // TUNE ME
  if (millis() - lastDisplayUpdate < DISPLAY_INTERVAL_MS) return;
  lastDisplayUpdate = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  if (ambientMode) {
    display.println("Mode: Ambient");
    display.print("Hue: ");
    display.println(currentBaseHue);
    display.display();
    return;
  }

  display.println("Mode: Beat sync");
  display.println(currentSelectedBand == -1 ? "Tuning: OFF" : "Tuning: ON");

  for (int i = 0; i < 5; i++) {
    int lowHz = (int)(bandStartBin[i] * HZ_PER_BIN);
    int highHz = (int)(bandEndBin[i] * HZ_PER_BIN);

    display.setCursor(0, 20 + i * 9);
    display.print(i == currentSelectedBand ? ">" : " ");
    display.print(bandNames[i]);
    display.print(": ");
    display.print(lowHz);
    display.print("-");
    display.print(highHz);
    display.print("Hz");
  }
  display.display();
}

// =========================================================
// Output
// =========================================================
void outputAmbient() {
  CRGB ambientColor = CHSV(currentBaseHue, 255, AMBIENT_BRIGHTNESS);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ambientColor;
  }
  FastLED.show();
}

void outputToStrip() {
  for (int i = 0; i < NUM_LEDS; i++) {
    int band = i % 5;
    leds[i] = CHSV(currentBaseHue + hueOffsets[band], 255, 255);
    leds[i].nscale8(bandBrightness[band]);
  }
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  samplingPeriodUs = round(1000000.0 / SAMPLING_FREQUENCY);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(BAND_SELECT_PIN, INPUT);
  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);

  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(150);
  // cap total draw below the port's ~3A/15W rating, leaving headroom for
  // the ESP32, mic, and OLED to share the same rail
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 2200);
  FastLED.clear();
  FastLED.show();

  Wire.begin();
  Wire.setTimeOut(50); // ms - prevents an I2C glitch from hanging the whole loop indefinitely
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed - check wiring/address");
  }
  display.clearDisplay();
  display.display();
}

void loop() {
  ambientMode = (digitalRead(MODE_SWITCH_PIN) == LOW);
  readColorKnob(); // color knob stays active in both modes

  if (ambientMode) {
    outputAmbient();
  } else {
    readKnobsAndRetune();
    applyKnobRangesToBands();

    sampleAndFFT();

    updateOnsetBand(kick, "kick");
    updateOnsetBand(snare, "snare");
    updateOnsetBand(cymbal, "cymbal");

    updateContinuousBand(vocalBand, "vocal");
    updateContinuousBand(instrBand, "instr");

    outputToStrip();
  }

  updateDisplay();
}
