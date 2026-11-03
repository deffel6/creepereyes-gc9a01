# CreeperEyes für runde GC9A01-Displays (ESP32-C3 und ESP32)

Port von [tooluser/CreeperEyes](https://github.com/tooluser/CreeperEyes) (Uncanny Eyes) auf
**runde GC9A01-Displays** (240×240), mit LovyanGFX. Es gibt zwei Varianten:

| Board | Installieren im Browser (Chrome oder Edge am Computer) | Sketch |
|---|---|---|
| ESP32-C3 Super Mini, 2 Displays | https://deffel6.github.io/creepereyes-gc9a01 | `src/creepereyes_c3/` |
| Klassischer ESP32, 1 oder 2 Displays | https://deffel6.github.io/creepereyes-gc9a01/esp32/ | `src/creepereyes_esp32_gc9a01/` |

Für eckige 128×128-OLEDs (SSD1351) gibt es den [anderen Installer](https://github.com/deffel6/creepereyes-webinstaller);
auf einem GC9A01 zeigt der nur Flackern.

## Verdrahtung ESP32-C3 Super Mini

| GC9A01 | Linkes Auge | Rechtes Auge |
|---|---|---|
| VCC / GND | 3V3 / GND | 3V3 / GND |
| SCL | GPIO 6 | GPIO 6 |
| SDA | GPIO 7 | GPIO 7 |
| DC | GPIO 2 | GPIO 2 |
| RST | GPIO 1 | GPIO 1 |
| BLK (falls vorhanden) | GPIO 3 | GPIO 3 |
| CS | GPIO 10 | GPIO 4 |

## Verdrahtung klassischer ESP32

Dieselben Pins wie beim SSD1351-Installer, nur ein anderes Display.

| GC9A01 | Linkes Auge | Rechtes Auge |
|---|---|---|
| VCC / GND | 3V3 / GND | 3V3 / GND |
| SCL | GPIO 18 | GPIO 18 |
| SDA | GPIO 23 | GPIO 23 |
| DC | GPIO 16 | GPIO 16 |
| RES | GPIO 5 | GPIO 5 |
| BLK (falls vorhanden) | 3V3 | 3V3 |
| CS | GPIO 17 | GPIO 4 |

Mit nur einem Display genügt das linke (CS an GPIO 17); dafür die Variante „1 Display“ installieren.

## Selbst bauen

ESP32-C3: Sketch in `src/creepereyes_c3/`, Board „ESP32C3 Dev Module“, Bibliothek LovyanGFX.

Klassischer ESP32: Sketch in `src/creepereyes_esp32_gc9a01/`, Board „ESP32 Dev Module“, Bibliothek LovyanGFX.
Für ein einzelnes Display das Build-Flag `-DNUM_EYES=1` setzen. Der SPI-Takt steht in `SPI_MHZ` (27 MHz;
mit kurzen Kabeln kann 40 MHz klappen).

Grundlage: Adafruit „Uncanny Eyes“ von Phil Burgess (MIT-Lizenz), Anpassung von Laurent Moll, CreeperEyes von tooluser.
