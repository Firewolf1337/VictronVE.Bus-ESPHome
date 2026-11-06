#include "vebus.h"
#include "esphome/core/log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <algorithm>

namespace esphome
{
    namespace vebus
    {

        static const char *TAG = "vebus";

        // IP-String (a.b.c.d) in numerische Form wandeln, host byte order
        static uint32_t parse_ip(const std::string &s)
        {
            uint32_t ip = 0;
            int part = 0;
            uint32_t v = 0;
            for (char c : s)
            {
                if (c >= '0' && c <= '9')
                {
                    v = v * 10 + (c - '0');
                }
                else if (c == '.')
                {
                    ip |= v << (8 * part++);
                    v = 0;
                }
                else
                {
                    break;
                }
            }
            ip |= v << (8 * part);
            return ip;
        }

        void VebusHub::setup()
        {
            if (this->de_pin_ != nullptr)
            {
                this->de_pin_->setup();
                this->de_pin_->digital_write(false);
            }
            this->soc_ = this->battery_capacity_ * 0.5f;
        }

        void VebusHub::dump_config()
        {
            ESP_LOGCONFIG(TAG, "VE.Bus Hub:");
            ESP_LOGCONFIG(TAG, "  Shelly IP: %s", this->shelly_ip_.c_str());
            ESP_LOGCONFIG(TAG, "  Battery: %.0f Ah", this->battery_capacity_);
        }

        // ---------- Frame-Aufbau (Portierung aus vebusstuff.ino) ----------

        int VebusHub::build_ess_cmd(uint8_t *out, int16_t power, uint8_t frame_nr)
        {
            int j = 0;
            out[j++] = 0x98; // MK3 interface to Multiplus
            out[j++] = 0xf7;
            out[j++] = 0xfe; // data frame
            out[j++] = frame_nr;
            out[j++] = 0x00; // eigene ID
            out[j++] = 0xe6;
            out[j++] = 0x37; // CommandWriteViaID
            out[j++] = 0x02; // RAMvar, kein EEPROM
            out[j++] = 0x83; // ESS-Leistung (Assistant RAMvar)
            out[j++] = (uint8_t)(power & 0xFF);
            out[j++] = (uint8_t)(power >> 8);
            return j;
        }

        int VebusHub::build_read_cmd(uint8_t *out, uint8_t frame_nr)
        {
            int j = 0;
            out[j++] = 0x98;
            out[j++] = 0xf7;
            out[j++] = 0xfe;
            out[j++] = frame_nr;
            out[j++] = 0x00;
            out[j++] = 0xe6;
            out[j++] = 0x30; // read RAMvar
            out[j++] = 0x04; // 4 = Bat volt
            out[j++] = 0x0E; // 14 = AC Power
            return j;
        }

        int VebusHub::build_onoff_cmd(uint8_t *out, uint8_t frame_nr, bool on)
        {
            // 98F7 FE 60 3F07 0000005DFF wakeup / 98F7 FE 6F 3F04 00000051FF sleep
            int j = 0;
            out[j++] = 0x98;
            out[j++] = 0xf7;
            out[j++] = 0xfe;
            out[j++] = frame_nr;
            out[j++] = 0x3f;             // cmd
            out[j++] = on ? 0x07 : 0x04; // wakeup / sleep
            out[j++] = 0x00;
            out[j++] = 0x00;
            out[j++] = 0x00;
            return j;
        }

        int VebusHub::stuff_fa(uint8_t *out, const uint8_t *in, int len)
        {
            int j = 0;
            for (int i = 0; i < 4; i++)
                out[j++] = in[i];
            for (int i = 4; i < len; i++)
            {
                uint8_t c = in[i];
                if (c >= 0xfa)
                {
                    out[j++] = 0xfa;
                    out[j++] = 0x70 | (c & 0x0f);
                }
                else
                {
                    out[j++] = c;
                }
            }
            return j;
        }

        int VebusHub::destuff_fa(uint8_t *out, const uint8_t *in, int len)
        {
            int j = 0;
            for (int i = 0; i < 4; i++)
                out[j++] = in[i];
            for (int i = 4; i < len; i++)
            {
                uint8_t c = in[i];
                if (c == 0xfa)
                {
                    c = in[++i];
                    if (c == 0xff)
                    { // Checksum-Ende bleibt wie es ist
                        out[j++] = 0xfa;
                        out[j++] = c;
                    }
                    else
                    {
                        out[j++] = c + 0x80;
                    }
                }
                else
                {
                    out[j++] = c;
                }
            }
            return j;
        }

        bool VebusHub::verify_checksum(const uint8_t *buf, int len)
        {
            uint8_t cs = 0;
            for (int i = 2; i < len; i++)
                cs += buf[i];
            return cs == 0;
        }

        int VebusHub::append_checksum(uint8_t *buf, int len)
        {
            uint8_t cs = 1;
            for (int i = 2; i < len; i++)
                cs -= buf[i];
            int j = len;
            if (cs >= 0xfb)
            {
                buf[j++] = 0xfa;
                buf[j++] = cs - 0xfa;
            }
            else
            {
                buf[j++] = cs;
            }
            buf[j++] = 0xff; // End Of Frame
            return j;
        }

        void VebusHub::send_frame(const uint8_t *buf, int len)
        {
            if (this->de_pin_ != nullptr)
                this->de_pin_->digital_write(true);
            this->write_array(buf, len);
            this->flush(); // auf TX-Ende warten
            if (this->de_pin_ != nullptr)
                this->de_pin_->digital_write(false);
        }

        void VebusHub::send_ess()
        {
            uint8_t t1[32], t2[32];
            int len = this->build_ess_cmd(t1, (int16_t)this->ess_power_, (this->frame_nr_ + 1) & 0x7f);
            len = this->stuff_fa(t2, t1, len);
            len = this->append_checksum(t2, len);
            this->send_frame(t2, len);
            this->ess_dirty_ = false;
        }

        void VebusHub::send_read()
        {
            uint8_t t1[32], t2[32];
            int len = this->build_read_cmd(t1, (this->frame_nr_ + 1) & 0x7f);
            len = this->stuff_fa(t2, t1, len);
            len = this->append_checksum(t2, len);
            this->send_frame(t2, len);
        }

        void VebusHub::send_onoff(bool on)
        {
            uint8_t t1[32], t2[32];
            int len = this->build_onoff_cmd(t1, (this->frame_nr_ + 1) & 0x7f, on);
            len = this->stuff_fa(t2, t1, len);
            len = this->append_checksum(t2, len);
            this->send_frame(t2, len);
        }

        // ---------- Dekodierung (Portierung aus decodeVEbusFrame) ----------

        void VebusHub::decode_frame(const uint8_t *frame, int len)
        {
            switch (frame[4])
            {
            case 0x80: // Condition of Charger/Inverter (Temp + Current)
                if ((frame[5] == 0x80) && ((frame[6] & 0xfe) == 0x12) && (frame[8] == 0x80) &&
                    ((frame[11] & 0x10) == 0x10))
                {
                    int16_t t = 256 * frame[10] + frame[9];
                    this->dc_current_ = 0.1f * t;
                    if ((frame[11] & 0xf0) == 0x30)
                        this->temp_ = 0.1f * frame[15];
                }
                break;
            case 0x41: // Multiplus mode / master LED
                if ((len == 19) && (frame[5] == 0x10))
                {
                    // LED-Register, Status, Stromlimit-Einstellungen (siehe Original)
                }
                break;
            case 0x00: // Ack / Antwort auf eigene Befehle
                if (frame[5] == 0xe6)
                {
                    if (frame[6] == 0x87)
                    {
                        // acked
                    }
                    else if (frame[6] == 0x85)
                    {
                        this->got_mp2_data_ = true;
                        int16_t v = 256 * frame[8] + frame[7];
                        this->bat_voltage_ = 0.01f * v;
                        this->ac_power_ = (int16_t)(256 * frame[10] + frame[9]);
                    }
                }
                break;
            default:
                break;
            }
        }

        // ---------- Hauptschleife ----------

        void VebusHub::loop()
        {
            uint32_t now = millis();

            // UART empfangen
            while (this->available())
            {
                uint8_t c = (uint8_t)this->read();
                if (this->frp_ < sizeof(this->fr_))
                    this->fr_[this->frp_++] = c;
                else
                    this->frp_ = 0;
                if (c == 0x55 && this->frp_ == 5)
                    this->sync_time_ = now;
                if (c == 0xff)
                {
                    if ((this->fr_[2] == 0xfd) && (this->fr_[4] == 0x55))
                    {
                        // Sync-Frame
                        this->frame_nr_ = this->fr_[3];
                        this->sync_rxed_ = true;
                        this->no_sync_ = false;
                        this->last_frame_ = now;
                    }
                    else if ((this->fr_[0] == 0x83) && (this->fr_[1] == 0x83) && (this->fr_[2] == 0xfe))
                    {
                        if (this->verify_checksum(this->fr_, this->frp_))
                        {
                            int fl = this->destuff_fa(this->fr2_, this->fr_, this->frp_);
                            this->decode_frame(this->fr2_, fl);
                        }
                        this->last_frame_ = now;
                    }
                    this->frp_ = 0;
                }
                else
                {
                    this->sync_rxed_ = false;
                }
            }

            if (this->last_frame_ != 0 && (now - this->last_frame_ > 1000))
                this->no_sync_ = true;

            // TX-Zeitschlitz nach Sync
            if (this->sync_rxed_ && (now - this->sync_time_ >= TX_DELAY))
            {
                this->sync_rxed_ = false;
                if (this->pending_on_)
                {
                    this->send_onoff(this->pending_on_state_);
                    this->sleeping_ = !this->pending_on_state_;
                    this->pending_on_ = false;
                }
                else if (this->ess_dirty_)
                {
                    this->send_ess();
                }
                else if (now >= this->read_due_)
                {
                    this->send_read();
                    this->read_due_ = now + this->loop_interval_;
                }
            }

            // Regelung
            if (now >= this->ctrl_due_)
            {
                this->ctrl_due_ = now + (this->sleeping_ ? this->loop_interval_ * 4 : this->loop_interval_);
                this->run_control();
            }
        }

        bool VebusHub::fetch_shelly()
        {
            if (this->shelly_ip_.empty())
                return false;

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(80);
            addr.sin_addr.s_addr = htonl(parse_ip(this->shelly_ip_));

            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0)
                return false;
            fcntl(fd, F_SETFL, O_NONBLOCK);

            // Verbindung (nichtblockierend mit Timeout)
            int r = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
            if (r != 0 && errno != EINPROGRESS)
            {
                close(fd);
                this->shelly_fail_++;
                return false;
            }
            uint32_t start = millis();
            bool connected = (r == 0);
            while (!connected && millis() - start < 1000)
            {
                fd_set wset;
                FD_ZERO(&wset);
                FD_SET(fd, &wset);
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 100000;
                if (select(fd + 1, nullptr, &wset, nullptr, &tv) > 0)
                {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                    connected = (err == 0);
                    break;
                }
            }
            if (!connected)
            {
                close(fd);
                this->shelly_fail_++;
                return false;
            }

            // HTTP-GET senden
            std::string req = "GET /status HTTP/1.0\r\nHost: " + this->shelly_ip_ + "\r\n\r\n";
            const char *p = req.c_str();
            size_t left = req.size();
            while (left > 0)
            {
                int n = send(fd, p, left, 0);
                if (n <= 0)
                    break;
                p += n;
                left -= n;
            }

            // Antwort lesen
            std::string body;
            char buf[512];
            start = millis();
            while (millis() - start < 1000)
            {
                fd_set rset;
                FD_ZERO(&rset);
                FD_SET(fd, &rset);
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 100000;
                int s = select(fd + 1, &rset, nullptr, nullptr, &tv);
                if (s == 0)
                    continue;
                int n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0)
                    break;
                body.append(buf, n);
            }
            close(fd);

            size_t idx = body.find("\"total_power\":");
            if (idx == std::string::npos)
            {
                this->shelly_fail_++;
                return false;
            }
            this->meter_power_ = atoi(body.c_str() + idx + 14);
            return true;
        }

        void VebusHub::run_control()
        {
            uint32_t now = millis();

            // Shelly abfragen (falls konfiguriert)
            if (!this->shelly_ip_.empty())
                this->fetch_shelly();

            // SoC Coulomb-Counting (immer)
            float hours = (float)this->loop_interval_ / 3600000.0f;
            this->soc_ = std::min(std::max(this->soc_ + this->dc_current_ * hours, 0.0f), this->battery_capacity_);

            if (this->sleeping_)
            {
                // Auto-Wakeup bei starkem Netzbezug
                if (this->auto_wakeup_)
                {
                    if ((this->old_meter_power_ < -50) && (this->meter_power_ < -50))
                    {
                        this->chg_only_ = true;
                        this->auto_mode_ = true;
                        this->pending_on_ = true;
                        this->pending_on_state_ = true;
                    }
                    this->old_meter_power_ = this->meter_power_;
                }
                return;
            }

            this->est_volt_ = this->bat_voltage_ - this->dc_current_ * this->battery_internal_resistance_;

            if (!this->auto_mode_)
            {
                this->ess_dirty_ = true; // manuellen Wert senden
                return;
            }

            // Net-Zero (Portierung aus esp32ess.ino)
            float expected;
            if (this->got_mp2_data_)
            {
                this->got_mp2_data_ = false;
                expected = -this->ac_power_;
                if (this->est_volt_ < ABS_LO_BAT)
                    this->bat_low_ = true;
                if (this->est_volt_ > ABS_HI_BAT)
                    this->bat_high_ = true;
                if (this->est_volt_ < 49.5f)
                    this->bat_low_ = false; // Entwarnung (Erweiterung ggü. Original)
                if (this->est_volt_ > 54.0f)
                    this->bat_high_ = false;
                if (this->est_volt_ > 53.8f && this->soc_ < 0.9f * this->battery_capacity_)
                    this->soc_ = 0.9f * this->battery_capacity_;
                if (this->est_volt_ < 48.0f && this->soc_ > 0.1f * this->battery_capacity_)
                    this->soc_ = 0.1f * this->battery_capacity_;
            }
            else
            {
                expected = 0.9f * this->req_power_;
            }

            int total = this->meter_power_ + (int)expected;

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

            if ((this->meter_power_ < -50 && expected > 50) || (this->meter_power_ > 50 && expected < -50))
            {
                this->filt_power_ = 0.9f * this->req_power_; // schnelle Anpassung
            }
            else
            {
                this->filt_power_ = 0.5f * (this->filt_power_ + (float)this->req_power_);
            }
            this->req_power_ = (int)this->filt_power_;

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

    } // namespace vebus
} // namespace esphome