# VictronVE.Bus-ESPHome
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

## Verzeichnisstruktur

```
vebus-ess/
├── vebus-ess.yaml
├── components/
│   └── vebus/
│       ├── __init__.py
│       ├── vebus.h
│       └── vebus.cpp
```

## vebus-ess.yaml

```yaml
substitutions:
  name: vebus-ess
  friendly: "Victron VE.Bus ESS"

esphome:
  name: ${name}
  friendly_name: ${friendly}

esp32:
  board: esp32dev
  framework:
    type: arduino

external_components:
  - source:
      type: local
      path: components

logger:
  level: INFO

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: "${name} Fallback"

uart:
  - id: ve_uart
    rx_pin: GPIO16
    tx_pin: GPIO17
    baud_rate: 256000
    rx_buffer_size: 256

vebus:
  id: vebus_hub
  uart_id: ve_uart
  de_pin: GPIO4
  shelly_ip: "192.168.178.47"   # Shelly 3EM, leer lassen wenn extern gesetzt
  battery_capacity: 305         # Ah
  loop_interval: 5000           # ms, Kontrollintervall
```

## Entitäten

Die folgenden Blöcke gehören in dieselbe YAML-Datei, unter die vebus-Sektion:

```yaml
sensor:
  - platform: template
    name: "Batteriespannung"
    unit_of_measurement: "V"
    device_class: voltage
    state_class: measurement
    accuracy_decimals: 2
    update_interval: 5s
    lambda: return id(vebus_hub).get_bat_voltage();

  - platform: template
    name: "Geschätzte Spannung"
    unit_of_measurement: "V"
    device_class: voltage
    state_class: measurement
    accuracy_decimals: 2
    update_interval: 5s
    lambda: return id(vebus_hub).get_est_voltage();

  - platform: template
    name: "AC Leistung"
    unit_of_measurement: "W"
    device_class: power
    state_class: measurement
    accuracy_decimals: 0
    update_interval: 5s
    lambda: return id(vebus_hub).get_ac_power();

  - platform: template
    name: "DC Strom"
    unit_of_measurement: "A"
    device_class: current
    state_class: measurement
    accuracy_decimals: 1
    update_interval: 5s
    lambda: return id(vebus_hub).get_dc_current();

  - platform: template
    name: "SoC"
    unit_of_measurement: "%"
    device_class: battery
    state_class: measurement
    accuracy_decimals: 1
    update_interval: 5s
    lambda: return id(vebus_hub).get_soc_percent();

  - platform: template
    name: "Netzleistung (Shelly)"
    unit_of_measurement: "W"
    device_class: power
    state_class: measurement
    accuracy_decimals: 0
    update_interval: 5s
    lambda: return id(vebus_hub).get_meter_power();

  - platform: template
    name: "ESS Leistung"
    unit_of_measurement: "W"
    device_class: power
    state_class: measurement
    accuracy_decimals: 0
    update_interval: 5s
    lambda: return id(vebus_hub).get_ess_power();

binary_sensor:
  - platform: template
    name: "VE.Bus Sync"
    lambda: return id(vebus_hub).get_sync();

  - platform: template
    name: "Batterie niedrig"
    lambda: return id(vebus_hub).get_bat_low();

  - platform: template
    name: "Batterie voll"
    lambda: return id(vebus_hub).get_bat_high();

switch:
  # Ein/Ausschalten des Wechselrichters (Wakeup/Sleep über VE.Bus)
  - platform: template
    name: "Wechselrichter"
    id: sw_inverter
    lambda: return id(vebus_hub).get_inverter_on();
    turn_on_action:
      - lambda: id(vebus_hub).set_inverter_on(true);
    turn_off_action:
      - lambda: id(vebus_hub).set_inverter_on(false);

  - platform: template
    name: "Auto Modus (Net-Zero)"
    id: sw_auto
    lambda: return id(vebus_hub).get_auto_mode();
    turn_on_action:
      - lambda: id(vebus_hub).set_auto_mode(true);
    turn_off_action:
      - lambda: id(vebus_hub).set_auto_mode(false);

  - platform: template
    name: "Nur Laden"
    id: sw_chgonly
    lambda: return id(vebus_hub).get_chg_only();
    turn_on_action:
      - lambda: id(vebus_hub).set_chg_only(true);
    turn_off_action:
      - lambda: id(vebus_hub).set_chg_only(false);

  - platform: template
    name: "Auto Aufwachen"
    id: sw_autowakeup
    lambda: return id(vebus_hub).get_auto_wakeup();
    turn_on_action:
      - lambda: id(vebus_hub).set_auto_wakeup(true);
    turn_off_action:
      - lambda: id(vebus_hub).set_auto_wakeup(false);

number:
  # Manuelle Leistungsvorgabe (deaktiviert Auto-Modus)
  - platform: template
    name: "ESS Leistungsvorgabe"
    id: num_ess
    min_value: -1875
    max_value: 1875
    step: 25
    initial_value: 0
    optimistic: true
    mode: box
    unit_of_measurement: "W"
    on_value:
      then:
        - lambda: id(vebus_hub).set_ess_power((int) x);

button:
  - platform: template
    name: "SoC zurücksetzen (50%)"
    on_press:
      - lambda: id(vebus_hub).reset_soc(50.0f);
```

## components/vebus/__init__.py

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart

vebus_ns = cg.esphome_ns.namespace("vebus")
VebusHub = vebus_ns.class_("VebusHub", cg.Component)

CONF_DE_PIN = "de_pin"
CONF_SHELLY_IP = "shelly_ip"
CONF_BATTERY_CAPACITY = "battery_capacity"
CONF_LOOP_INTERVAL = "loop_interval"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VebusHub),
        cv.Required("uart_id"): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_DE_PIN): pins.internal_gpio_output_pin_schema,
        cv.Optional(CONF_SHELLY_IP, default=""): cv.string,
        cv.Optional(CONF_BATTERY_CAPACITY, default=305.0): cv.float_,
        cv.Optional(CONF_LOOP_INTERVAL, default=5000): cv.uint32_t,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    paren = await cg.get_variable(config["uart_id"])
    cg.add(var.set_uart_parent(paren))
    if CONF_DE_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_DE_PIN])
        cg.add(var.set_de_pin(pin))
    cg.add(var.set_shelly_ip(config[CONF_SHELLY_IP]))
    cg.add(var.set_battery_capacity(config[CONF_BATTERY_CAPACITY]))
    cg.add(var.set_loop_interval(config[CONF_LOOP_INTERVAL]))
```

## components/vebus/vebus.h

```cpp
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart_device.h"

namespace esphome {
namespace vebus {

// Regelparameter (wie im Original-Projekt)
static const int I_MAX_POWER = 1875;   // max Inverterleistung
static const int C_MAX_POWER = -1875;  // max Ladeleistung (negativ)
static const int INV_HI = 150, INV_LO = 100;    // Inverter-Hysterese
static const int CHG_HI = -10, CHG_LO = -50;    // Lade-Hysterese
static const int TARGET_HI = 30, TARGET_LO = -30;
static const float ABS_LO_BAT = 48.0f;   // Not-Aus Invertieren
static const float ABS_HI_BAT = 55.0f;   // Not-Aus Laden
static const float HI_SOC = 90.0f, LO_SOC = 20.0f;
static const uint32_t TX_DELAY = 8;      // ms nach Sync bis TX

class VebusHub : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Konfiguration
  void set_de_pin(GPIOPin *pin) { this->de_pin_ = pin; }
  void set_shelly_ip(const std::string &ip) { this->shelly_ip_ = ip; }
  void set_battery_capacity(float ah) { this->battery_capacity_ = ah; }
  void set_loop_interval(uint32_t ms) { this->loop_interval_ = ms; }

  // Zustand
  bool get_sync() const { return !this->no_sync_; }
  bool get_inverter_on() const { return !this->sleeping_; }
  bool get_auto_mode() const { return this->auto_mode_; }
  bool get_chg_only() const { return this->chg_only_; }
  bool get_auto_wakeup() const { return this->auto_wakeup_; }
  float get_bat_voltage() const { return this->bat_voltage_; }
  float get_est_voltage() const { return this->est_volt_; }
  float get_ac_power() const { return this->ac_power_; }
  float get_dc_current() const { return this->dc_current_; }
  float get_soc_percent() const { return 100.0f * this->soc_ / this->battery_capacity_; }
  int get_ess_power() const { return this->ess_power_; }
  int get_meter_power() const { return this->meter_power_; }
  bool get_bat_low() const { return this->bat_low_; }
  bool get_bat_high() const { return this->bat_high_; }

  // Befehle
  void set_inverter_on(bool on) { this->pending_on_ = true; this->pending_on_state_ = on; }
  void set_auto_mode(bool a) { this->auto_mode_ = a; this->ess_dirty_ = true; }
  void set_chg_only(bool c) { this->chg_only_ = c; this->ess_dirty_ = true; }
  void set_auto_wakeup(bool a) { this->auto_wakeup_ = a; }
  void set_ess_power(int w) { this->auto_mode_ = false; this->ess_power_ = w; this->ess_dirty_ = true; }
  void set_meter_power(int w) { this->meter_power_ = w; }  // externe Quelle möglich
  void reset_soc(float percent) { this->soc_ = this->battery_capacity_ * percent / 100.0f; }

 protected:
  // Frame-Aufbau (Identisch zum Original-Protokoll)
  int build_ess_cmd(uint8_t *out, int16_t power, uint8_t frame_nr);
  int build_read_cmd(uint8_t *out, uint8_t frame_nr);
  int build_onoff_cmd(uint8_t *out, uint8_t frame_nr, bool on);
  int stuff_fa(uint8_t *out, const uint8_t *in, int len);
  int destuff_fa(uint8_t *out, const uint8_t *in, int len);
  bool verify_checksum(const uint8_t *buf, int len);
  int append_checksum(uint8_t *buf, int len);
  void send_frame(const uint8_t *buf, int len);
  void send_ess();
  void send_read();
  void send_onoff(bool on);
  void decode_frame(const uint8_t *frame, int len);
  void run_control();
  bool fetch_shelly();

  // Konfiguration
  GPIOPin *de_pin_{nullptr};
  std::string shelly_ip_{};
  float battery_capacity_{305.0f};
  uint32_t loop_interval_{5000};

  // Empfang
  uint8_t fr_[128];
  uint8_t fr2_[128];
  uint8_t frp_{0};

  // Zustand Multiplus
  float bat_voltage_{0}, est_volt_{0};
  float ac_power_{0}, dc_current_{0}, temp_{0};
  bool got_mp2_data_{false};
  uint8_t frame_nr_{0};
  bool sync_rxed_{false}, no_sync_{true};
  uint32_t sync_time_{0}, last_frame_{0};

  // Regelung
  bool auto_mode_{true}, chg_only_{false};
  bool pending_on_{false}, pending_on_state_{false};
  bool sleeping_{false}, auto_wakeup_{true};
  bool ess_dirty_{true};
  bool bat_low_{false}, bat_high_{false};
  bool do_inv_{false}, do_chg_{false};
  int ess_power_{0}, req_power_{0};
  int meter_power_{0}, old_meter_power_{0};
  float filt_power_{0};
  float soc_;
  uint32_t ctrl_due_{0}, read_due_{0};
  uint32_t shelly_fail_{0};
};

}  // namespace vebus
}  // namespace esphome
```

## components/vebus/vebus.cpp

```cpp
#include "vebus.h"
#include "esphome/core/log.h"
#include <HTTPClient.h>
#include <algorithm>

namespace esphome {
namespace vebus {

static const char *TAG = "vebus";

void VebusHub::setup() {
  if (this->de_pin_ != nullptr) {
    this->de_pin_->setup();
    this->de_pin_->digital_write(false);
  }
  this->soc_ = this->battery_capacity_ * 0.5f;
}

void VebusHub::dump_config() {
  ESP_LOGCONFIG(TAG, "VE.Bus Hub:");
  ESP_LOGCONFIG(TAG, "  Shelly IP: %s", this->shelly_ip_.c_str());
  ESP_LOGCONFIG(TAG, "  Battery: %.0f Ah", this->battery_capacity_);
}

// ---------- Frame-Aufbau (Portierung aus vebusstuff.ino) ----------

int VebusHub::build_ess_cmd(uint8_t *out, int16_t power, uint8_t frame_nr) {
  int j = 0;
  out[j++] = 0x98;              // MK3 interface to Multiplus
  out[j++] = 0xf7;
  out[j++] = 0xfe;              // data frame
  out[j++] = frame_nr;
  out[j++] = 0x00;              // eigene ID
  out[j++] = 0xe6;
  out[j++] = 0x37;              // CommandWriteViaID
  out[j++] = 0x02;              // RAMvar, kein EEPROM
  out[j++] = 0x83;              // ESS-Leistung (Assistant RAMvar)
  out[j++] = (uint8_t) (power & 0xFF);
  out[j++] = (uint8_t) (power >> 8);
  return j;
}

int VebusHub::build_read_cmd(uint8_t *out, uint8_t frame_nr) {
  int j = 0;
  out[j++] = 0x98;
  out[j++] = 0xf7;
  out[j++] = 0xfe;
  out[j++] = frame_nr;
  out[j++] = 0x00;
  out[j++] = 0xe6;
  out[j++] = 0x30;              // read RAMvar
  out[j++] = 0x04;              // 4 = Bat volt
  out[j++] = 0x0E;              // 14 = AC Power
  return j;
}

int VebusHub::build_onoff_cmd(uint8_t *out, uint8_t frame_nr, bool on) {
  // 98F7 FE 60 3F07 0000005DFF wakeup / 98F7 FE 6F 3F04 00000051FF sleep
  int j = 0;
  out[j++] = 0x98;
  out[j++] = 0xf7;
  out[j++] = 0xfe;
  out[j++] = frame_nr;
  out[j++] = 0x3f;              // cmd
  out[j++] = on ? 0x07 : 0x04;  // wakeup / sleep
  out[j++] = 0x00;
  out[j++] = 0x00;
  out[j++] = 0x00;
  return j;
}

int VebusHub::stuff_fa(uint8_t *out, const uint8_t *in, int len) {
  int j = 0;
  for (int i = 0; i < 4; i++)
    out[j++] = in[i];
  for (int i = 4; i < len; i++) {
    uint8_t c = in[i];
    if (c >= 0xfa) {
      out[j++] = 0xfa;
      out[j++] = 0x70 | (c & 0x0f);
    } else {
      out[j++] = c;
    }
  }
  return j;
}

int VebusHub::destuff_fa(uint8_t *out, const uint8_t *in, int len) {
  int j = 0;
  for (int i = 0; i < 4; i++)
    out[j++] = in[i];
  for (int i = 4; i < len; i++) {
    uint8_t c = in[i];
    if (c == 0xfa) {
      c = in[++i];
      if (c == 0xff) {  // Checksum-Ende bleibt wie es ist
        out[j++] = 0xfa;
        out[j++] = c;
      } else {
        out[j++] = c + 0x80;
      }
    } else {
      out[j++] = c;
    }
  }
  return j;
}

bool VebusHub::verify_checksum(const uint8_t *buf, int len) {
  uint8_t cs = 0;
  for (int i = 2; i < len; i++)
    cs += buf[i];
  return cs == 0;
}

int VebusHub::append_checksum(uint8_t *buf, int len) {
  uint8_t cs = 1;
  for (int i = 2; i < len; i++)
    cs -= buf[i];
  int j = len;
  if (cs >= 0xfb) {
    buf[j++] = 0xfa;
    buf[j++] = cs - 0xfa;
  } else {
    buf[j++] = cs;
  }
  buf[j++] = 0xff;  // End Of Frame
  return j;
}

void VebusHub::send_frame(const uint8_t *buf, int len) {
  if (this->de_pin_ != nullptr)
    this->de_pin_->digital_write(true);
  this->write_array(buf, len);
  this->flush();  // auf TX-Ende warten
  if (this->de_pin_ != nullptr)
    this->de_pin_->digital_write(false);
}

void VebusHub::send_ess() {
  uint8_t t1[32], t2[32];
  int len = this->build_ess_cmd(t1, (int16_t) this->ess_power_, (this->frame_nr_ + 1) & 0x7f);
  len = this->stuff_fa(t2, t1, len);
  len = this->append_checksum(t2, len);
  this->send_frame(t2, len);
  this->ess_dirty_ = false;
}

void VebusHub::send_read() {
  uint8_t t1[32], t2[32];
  int len = this->build_read_cmd(t1, (this->frame_nr_ + 1) & 0x7f);
  len = this->stuff_fa(t2, t1, len);
  len = this->append_checksum(t2, len);
  this->send_frame(t2, len);
}

void VebusHub::send_onoff(bool on) {
  uint8_t t1[32], t2[32];
  int len = this->build_onoff_cmd(t1, (this->frame_nr_ + 1) & 0x7f, on);
  len = this->stuff_fa(t2, t1, len);
  len = this->append_checksum(t2, len);
  this->send_frame(t2, len);
}

// ---------- Dekodierung (Portierung aus decodeVEbusFrame) ----------

void VebusHub::decode_frame(const uint8_t *frame, int len) {
  switch (frame[4]) {
    case 0x80:  // Condition of Charger/Inverter (Temp + Current)
      if ((frame[5] == 0x80) && ((frame[6] & 0xfe) == 0x12) && (frame[8] == 0x80) &&
          ((frame[11] & 0x10) == 0x10)) {
        int16_t t = 256 * frame[10] + frame[9];
        this->dc_current_ = 0.1f * t;
        if ((frame[11] & 0xf0) == 0x30)
          this->temp_ = 0.1f * frame[15];
      }
      break;
    case 0x41:  // Multiplus mode / master LED
      if ((len == 19) && (frame[5] == 0x10)) {
        // LED-Register, Status, Stromlimit-Einstellungen (siehe Original)
      }
      break;
    case 0x00:  // Ack / Antwort auf eigene Befehle
      if (frame[5] == 0xe6) {
        if (frame[6] == 0x87) {
          // acked
        } else if (frame[6] == 0x85) {
          this->got_mp2_data_ = true;
          int16_t v = 256 * frame[8] + frame[7];
          this->bat_voltage_ = 0.01f * v;
          this->ac_power_ = (int16_t) (256 * frame[10] + frame[9]);
        }
      }
      break;
    default:
      break;
  }
}

// ---------- Hauptschleife ----------

void VebusHub::loop() {
  uint32_t now = millis();

  // UART empfangen
  while (this->available()) {
    uint8_t c = (uint8_t) this->read();
    if (this->frp_ < sizeof(this->fr_))
      this->fr_[this->frp_++] = c;
    else
      this->frp_ = 0;
    if (c == 0x55 && this->frp_ == 5)
      this->sync_time_ = now;
    if (c == 0xff) {
      if ((this->fr_[2] == 0xfd) && (this->fr_[4] == 0x55)) {
        // Sync-Frame
        this->frame_nr_ = this->fr_[3];
        this->sync_rxed_ = true;
        this->no_sync_ = false;
        this->last_frame_ = now;
      } else if ((this->fr_[0] == 0x83) && (this->fr_[1] == 0x83) && (this->fr_[2] == 0xfe)) {
        if (this->verify_checksum(this->fr_, this->frp_)) {
          int fl = this->destuff_fa(this->fr2_, this->fr_, this->frp_);
          this->decode_frame(this->fr2_, fl);
        }
        this->last_frame_ = now;
      }
      this->frp_ = 0;
    } else {
      this->sync_rxed_ = false;
    }
  }

  if (this->last_frame_ != 0 && (now - this->last_frame_ > 1000))
    this->no_sync_ = true;

  // TX-Zeitschlitz nach Sync
  if (this->sync_rxed_ && (now - this->sync_time_ >= TX_DELAY)) {
    this->sync_rxed_ = false;
    if (this->pending_on_) {
      this->send_onoff(this->pending_on_state_);
      this->sleeping_ = !this->pending_on_state_;
      this->pending_on_ = false;
    } else if (this->ess_dirty_) {
      this->send_ess();
    } else if (now >= this->read_due_) {
      this->send_read();
      this->read_due_ = now + this->loop_interval_;
    }
  }

  // Regelung
  if (now >= this->ctrl_due_) {
    this->ctrl_due_ = now + (this->sleeping_ ? this->loop_interval_ * 4 : this->loop_interval_);
    this->run_control();
  }
}

bool VebusHub::fetch_shelly() {
  if (this->shelly_ip_.empty())
    return false;
  HTTPClient http;
  String url = "http://" + String(this->shelly_ip_.c_str()) + "/status";
  if (!http.begin(url))
    return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    this->shelly_fail_++;
    return false;
  }
  String body = http.getString();
  http.end();
  int idx = body.indexOf("\"total_power\":");
  if (idx < 0) {
    this->shelly_fail_++;
    return false;
  }
  this->meter_power_ = body.substring(idx + 14).toInt();
  return true;
}

void VebusHub::run_control() {
  uint32_t now = millis();

  // Shelly abfragen (falls konfiguriert)
  if (!this->shelly_ip_.empty())
    this->fetch_shelly();

  // SoC Coulomb-Counting (immer)
  float hours = (float) this->loop_interval_ / 3600000.0f;
  this->soc_ = std::min(std::max(this->soc_ + this->dc_current_ * hours, 0.0f), this->battery_capacity_);

  if (this->sleeping_) {
    // Auto-Wakeup bei starkem Netzbezug
    if (this->auto_wakeup_) {
      if ((this->old_meter_power_ < -50) && (this->meter_power_ < -50)) {
        this->chg_only_ = true;
        this->auto_mode_ = true;
        this->pending_on_ = true;
        this->pending_on_state_ = true;
      }
      this->old_meter_power_ = this->meter_power_;
    }
    return;
  }

  this->est_volt_ = this->bat_voltage_ - this->dc_current_ * 0.017f;

  if (!this->auto_mode_) {
    this->ess_dirty_ = true;  // manuellen Wert senden
    return;
  }

  // Net-Zero (Portierung aus esp32ess.ino)
  float expected;
  if (this->got_mp2_data_) {
    this->got_mp2_data_ = false;
    expected = -this->ac_power_;
    if (this->est_volt_ < ABS_LO_BAT)
      this->bat_low_ = true;
    if (this->est_volt_ > ABS_HI_BAT)
      this->bat_high_ = true;
    if (this->est_volt_ < 49.5f)
      this->bat_low_ = false;  // Entwarnung (Erweiterung ggü. Original)
    if (this->est_volt_ > 54.0f)
      this->bat_high_ = false;
    if (this->est_volt_ > 53.8f && this->soc_ < 0.9f * this->battery_capacity_)
      this->soc_ = 0.9f * this->battery_capacity_;
    if (this->est_volt_ < 48.0f && this->soc_ > 0.1f * this->battery_capacity_)
      this->soc_ = 0.1f * this->battery_capacity_;
  } else {
    expected = 0.9f * this->req_power_;
  }

  int total = this->meter_power_ + (int) expected;

  if (total > INV_HI)
    this->do_inv_ = true;
  else if (total < INV_LO)
    this->do_inv_ = false;
  if (total > CHG_HI)
    this->do_chg_ = false;
  else if (total < CHG_LO)
    this->do_chg_ = true;

  if (this->chg_only_)
    this->do_inv_ = false;
  if (this->bat_low_)
    this->do_inv_ = false;
  if (this->bat_high_)
    this->do_chg_ = false;

  float socp = this->get_soc_percent();
  if (socp > HI_SOC)
    this->do_chg_ = false;
  if (socp < LO_SOC)
    this->do_inv_ = false;

  this->req_power_ = (total > 0) ? (total - TARGET_HI) : (total - TARGET_LO);

  if ((this->meter_power_ < -50 && expected > 50) || (this->meter_power_ > 50 && expected < -50)) {
    this->filt_power_ = 0.9f * this->req_power_;  // schnelle Anpassung
  } else {
    this->filt_power_ = 0.5f * (this->filt_power_ + (float) this->req_power_);
  }
  this->req_power_ = (int) this->filt_power_;

  if (this->do_inv_)
    this->req_power_ = std::min(std::max(this->req_power_, 0), I_MAX_POWER);
  else if (this->do_chg_)
    this->req_power_ = std::min(std::max(this->req_power_, C_MAX_POWER), 0);
  else
    this->req_power_ = 0;

  this->ess_power_ = this->req_power_;
  this->ess_dirty_ = true;

  ESP_LOGD(TAG, "meter=%d expected=%.0f total=%d ess=%d soc=%.1f%%",
           this->meter_power_, expected, total, this->ess_power_, this->get_soc_percent());
}

}  // namespace vebus
}  // namespace esphome
```

## Hinweise

- Wechselrichter-Schalter: EIN sendet das Wakeup-Frame (0x3F07), AUS das Sleep-Frame (0x3F04), identisch zu den `/cm?cmnd=Power on|off`-Endpunkten der Original-Firmware. Der Multiplus meldet den Zustand nicht zurück, er wird intern mitgeführt.
- Alle Entitäten erscheinen automatisch in Home Assistant über die ESPHome-API.
- Statt der internen Shelly-Abfrage kann die Netzleistung auch extern gesetzt werden, z. B. per `api:`-Action oder aus einer Home-Assistant-Automation über `set_meter_power(...)`.
- Die Regelparameter (Hysterese, SoC-Grenzen, maximale Leistung) stehen als Konstanten in `vebus.h` und lassen sich dort anpassen.
- Vor der ersten Inbetriebnahme anzupassen: WLAN-Zugangsdaten, Shelly-IP und Batteriekapazität.
