# Victron Multiplus 2 (VE.Bus) als ESPHome-Device

Portierung des Arduino-Projekts [PepeTheFroggie/Victron-VE.Bus---esp32](https://github.com/PepeTheFroggie/Victron-VE.Bus---esp32) zu ESPHome. Funktionsumfang:

- VE.Bus-Kommunikation über RS485 (256000 Baud, Half-Duplex, DE-Pin GPIO4)
- Sensoren: Batteriespannung, AC-Leistung, DC-Strom, geschätzte Spannung, SoC (Coulomb-Counting), Netzleistung (Shelly), ESS-Leistung
- Schalter "Wechselrichter" zum Ein- und Ausschalten über die originalen Wakeup- und Sleep-Frames (`98F7 FE .. 3F07/3F04 ..`)
- Auto-Modus (Net-Zero mit Hysterese) und manueller Modus mit Leistungsvorgabe (-1875 bis 1875 W)
- Weitere Schalter: Nur Laden und Auto-Aufwachen

## Voraussetzungen

Wie im Original-Projekt:

- Der Multiplus 2 muss einmalig per MK2-Interface in den ESS-Modus versetzt werden (Anleitung: pv-baxi/esp32ess).
- Der RS485-Wandler muss modifiziert werden (Widerstände entfernen, siehe Schaltplan im Original-Repo).
- Der ESP32 darf nie gleichzeitig per USB und am VE.Bus angeschlossen sein: ESP32 allein flashen, USB trennen, erst danach an den VE.Bus anschließen.
- Keine Gewährleistung, Einsatz auf eigene Gefahr.

## Anschlussplan MAX485


| MAX485-Pin | Funktion                      | ESP32 / Sonstiges                     |
| ---------- | ----------------------------- | ------------------------------------- |
| VCC        | Versorgung                    | 5V (Buck-Converter), bei MAX3485: 3V3 |
| GND        | Masse                         | GND (gemeinsam mit ESP32)             |
| DI         | Treiber-Eingang (Senden)      | GPIO17 (TX)                           |
| RO         | Empfänger-Ausgang (Empfangen) | GPIO16 (RX)                           |
| DE         | Driver Enable                 | GPIO4                                 |
| RE         | Receiver Enable               | GPIO4 (mit DE brücken)                |
| A          | RS485 Data A                  | VE.Bus-Datenleitung A                 |
| B          | RS485 Data B                  | VE.Bus-Datenleitung B                 |


Hinweise zum Anschluss:

- Die Richtungsumschaltung übernimmt die Software über GPIO4: HIGH beim Senden, LOW beim Empfangen.
- Laut Original-Projekt müssen die Widerstände auf dem RS485-Wandler entfernt werden (Terminierung und Bias, siehe Anleitung von pv-baxi/esp32ess).
- Der Standard-MAX485 läuft mit 5V. Der ESP32 ist nicht 5V-tolerant, daher benötigt RO einen Spannungsteiler (z. B. 1kΩ/2kΩ) oder besser einen Pegelwandler. Einfachste Lösung: MAX3485 verwenden, der direkt mit 3V3 arbeitet und ohne Zusatzbeschaltung auskommt.
- Die Zuordnung der A- und B-Leitung zum VE.Bus-Port des Multiplus ergibt sich aus dem Schaltplan des Original-Repos.

## Verzeichnisstruktur

```
esphome/
├── vebus-ess.yaml
├── components/
│   └── vebus/
│       ├── __init__.py
│       ├── vebus.h
│       └── vebus.cpp
```



## Hinweise

- Wechselrichter-Schalter: EIN sendet das Wakeup-Frame (0x3F07), AUS das Sleep-Frame (0x3F04), identisch zu den `/cm?cmnd=Power on|off`-Endpunkten der Original-Firmware. Der Multiplus meldet den Zustand nicht zurück, er wird intern mitgeführt.
- Alle Entitäten erscheinen automatisch in Home Assistant über die ESPHome-API.
- Statt der internen Shelly-Abfrage kann die Netzleistung auch extern gesetzt werden, z. B. per `api:`-Action oder aus einer Home-Assistant-Automation über `set_meter_power(...)`.
- Die Regelparameter (Hysterese, SoC-Grenzen, maximale Leistung) stehen als Konstanten in `vebus.h` und lassen sich dort anpassen.
- Vor der ersten Inbetriebnahme anzupassen: WLAN-Zugangsdaten, Shelly-IP und Batteriekapazität.
- Kompatibel mit Arduino- und ESP-IDF-Framework. Der Shelly-Abruf nutzt rohe BSD-Sockets (lwip) statt HTTPClient und blockiert die Hauptschleife für maximal ca. 2 Sekunden pro Abfrage.