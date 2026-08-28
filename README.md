# Beat-Sync-LED

A custom ESP32-based PCB that listens to music through a microphone and drives a 300-LED WS2812B strip in sync with the beat — five-band FFT analysis, physical knobs for live tuning, and an OLED status display.

![Beat-Sync-LED PCB (unpopulated)](pcb-bare.png)

*Unpopulated board — front and back.*

## How it works

The ESP32 samples the mic input (512 samples at 20 kHz) and runs an FFT each loop, splitting the spectrum into 5 bands: **Kick, Snare, Cymbal, Vocal, Instrumental**.

- **Kick / Snare / Cymbal** are treated as percussive: the code watches for a spike in spectral flux (a sudden jump in energy relative to a running average) and flashes that band's LEDs to full brightness on the hit, then lets it decay — with a refractory period so one hit doesn't retrigger itself.
- **Vocal / Instrumental** are treated as continuous: their brightness just tracks a smoothed energy level above a threshold.
- Each band drives every 5th LED down the strip (`i % 5`), colored as an offset from a single base hue, so the strip reads as one blended palette rather than 5 unrelated colors.

**Modes**, selected by a physical switch (`MODE_SWITCH_PIN`, GPIO26):
- **Beat Sync** — the FFT-reactive behavior above.
- **Ambient** — strip fills a static color from the color knob; audio input is ignored.

## Controls

| Control | Pin | Function |
|---|---|---|
| Mode switch | GPIO26 | Beat Sync vs. Ambient |
| Band Select knob | GPIO32 | Choose which of the 5 bands to retune (5 zones + an "off" zone at the low end) |
| Start knob | GPIO33 | While a band is selected, scales that band's *low* frequency edge |
| End knob | GPIO35 | While a band is selected, scales that band's *high* frequency edge |
| Color knob | GPIO36 | Base hue — active in both modes; the other 4 bands' colors are fixed offsets from it |

Start/End knobs work relative to whatever position they were in when you selected the band (the code snapshots that reading as a reference and scales the frequency range up/down from there), not as an absolute frequency dial — so re-selecting a band resets the reference point.

## Hardware

| | |
|---|---|
| **MCU** | ESP32 dev module (30-pin DevKitC/WROOM-style, socketed via headers) |
| **Audio in** | Mic amp module (AR/OUT/GAIN/VCC/GND pinout, e.g. MAX9814-style) → GPIO34 (ADC) |
| **LED strip** | WS2812B, 300 LEDs, data on GPIO5 → level-shifter IC + series resistor → 3-pin output header (GND/Din/+5V) |
| **Display** | SSD1306 128x64 OLED over I2C (default ESP32 pins: SDA GPIO21 / SCL GPIO22), address `0x3C` |
| **Power** | 2x USB-C input — one for MCU/logic, one dedicated to the LED strip (kept separate so LED current doesn't share the MCU's rail) |
| **Board** | 2-layer, 100 x 75 mm, 1.6 mm FR4 |

FastLED is capped at 5 V / 2200 mA (`setMaxPowerInVoltsAndMilliamps`) to stay under a USB-C port's ~3 A/15 W rating with headroom for the ESP32, mic, and OLED sharing the rail — worth knowing if you swap in a different strip length or a beefier power source.

## Repo contents

<!-- TODO: adjust to match your actual folder names -->

| Path | Contents |
|---|---|
| `/pcb` | KiCad-exported Gerbers + drill files, as submitted to JLCPCB |
| `/firmware` | `Beat_Sync_LED.ino` |
| `pcb-bare.png` | Photo of the unpopulated board |

## Getting the PCB made

1. Go to [JLCPCB](https://jlcpcb.com) (or your preferred fab) and upload the Gerber/drill files from `/pcb`.
2. Board is 2-layer, 1.6 mm, no specific surface finish called out in the job file — JLCPCB's HASL default works fine unless you specified otherwise.
3. Populate: ESP32 module, mic amp module, level-shifter IC, 5x potentiometer, SSD1306 OLED, connectors.
   <!-- TODO: link/attach a BOM if you have one -->

## Flashing the firmware

1. Install the Arduino IDE with ESP32 board support.
2. Install libraries: `arduinoFFT`, `FastLED`, `Adafruit_GFX`, `Adafruit_SSD1306` (`Wire` ships with the core).
3. Open `Beat_Sync_LED.ino`.
4. Search for `TUNE ME` — a handful of constants (onset sensitivity, refractory time, ambient brightness, knob deadzone, etc.) are meant to be adjusted for your specific mic/strip/environment.
5. Select your ESP32 board variant and flash.

> **Heads up:** as uploaded, line 31 (`bool ambientMode = false; =`) has a stray `=` that won't compile — looks like a leftover from an edit. Worth fixing before anyone tries to build this from the README.

## License

<!-- TODO: pick one, e.g. MIT — see LICENSE -->
