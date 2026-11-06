#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"

namespace esphome
{
    namespace vebus
    {

        // Regelparameter (wie im Original-Projekt)
        static const int I_MAX_POWER = 1875;         // max Inverterleistung
        static const int C_MAX_POWER = -1875;        // max Ladeleistung (negativ)
        static const int INV_HI = 150, INV_LO = 100; // Inverter-Hysterese
        static const int CHG_HI = -10, CHG_LO = -50; // Lade-Hysterese
        static const int TARGET_HI = 30, TARGET_LO = -30;
        static const float ABS_LO_BAT = 48.0f; // Not-Aus Invertieren
        static const float ABS_HI_BAT = 55.0f; // Not-Aus Laden
        static const float HI_SOC = 90.0f, LO_SOC = 20.0f;
        static const uint32_t TX_DELAY = 8; // ms nach Sync bis TX

        class VebusHub : public Component, public uart::UARTDevice
        {
        public:
            void setup() override;
            void loop() override;
            void dump_config() override;

            // Konfiguration
            void set_de_pin(GPIOPin *pin) { this->de_pin_ = pin; }
            void set_shelly_ip(const std::string &ip) { this->shelly_ip_ = ip; }
            void set_battery_capacity(float ah) { this->battery_capacity_ = ah; }
            void set_loop_interval(uint32_t ms) { this->loop_interval_ = ms; }
            void set_battery_internal_resistance(float ohm) { this->battery_internal_resistance_ = ohm; }

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
            float get_temp() const { return this->temp_; }
            float get_soc_percent() const { return 100.0f * this->soc_ / this->battery_capacity_; }
            int get_ess_power() const { return this->ess_power_; }
            int get_meter_power() const { return this->meter_power_; }
            bool get_bat_low() const { return this->bat_low_; }
            bool get_bat_high() const { return this->bat_high_; }

            // Befehle
            void set_inverter_on(bool on)
            {
                this->pending_on_ = true;
                this->pending_on_state_ = on;
            }
            void set_auto_mode(bool a)
            {
                this->auto_mode_ = a;
                this->ess_dirty_ = true;
            }
            void set_chg_only(bool c)
            {
                this->chg_only_ = c;
                this->ess_dirty_ = true;
            }
            void set_auto_wakeup(bool a) { this->auto_wakeup_ = a; }
            void set_ess_power(int w)
            {
                this->auto_mode_ = false;
                this->ess_power_ = w;
                this->ess_dirty_ = true;
            }
            void set_meter_power(int w) { this->meter_power_ = w; } // externe Quelle möglich
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
            float battery_internal_resistance_{0.017f};

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

    } // namespace vebus
} // namespace esphome