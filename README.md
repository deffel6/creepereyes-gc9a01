# CreeperEyes für runde GC9A01-Displays (ESP32-C3)

Port von [tooluser/CreeperEyes](https://github.com/tooluser/CreeperEyes) (Uncanny Eyes) auf einen
**ESP32-C3 Super Mini** mit **zwei runden GC9A01-Displays** (240×240), mit LovyanGFX.

**Installieren im Browser:** https://deffel6.github.io/creepereyes-gc9a01 (Chrome oder Edge am Computer)

## Verdrahtung

| GC9A01 | Linkes Auge | Rechtes Auge |
|---|---|---|
| VCC / GND | 3V3 / GND | 3V3 / GND |
| SCL | GPIO 6 | GPIO 6 |
| SDA | GPIO 7 | GPIO 7 |
| DC | GPIO 2 | GPIO 2 |
| RST | GPIO 1 | GPIO 1 |
| BLK (falls vorhanden) | GPIO 3 | GPIO 3 |
| CS | GPIO 10 | GPIO 4 |

## Selbst bauen

Sketch in `src/creepereyes_c3/`, Board „ESP32C3 Dev Module“, Bibliothek LovyanGFX.

Grundlage: Adafruit „Uncanny Eyes“ von Phil Burgess (MIT-Lizenz), Anpassung von Laurent Moll, CreeperEyes von tooluser.
