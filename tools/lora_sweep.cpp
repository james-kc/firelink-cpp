// lora_sweep - bench transmitter for the RF-LORA-868-SO (SX1272) flight radio.
//
// Purpose (Milestone M2): the ground-station HAT decodes Firelink frames to
// a Python console, but the HAT's on-air LoRa parameters are proprietary and
// NOT readable, and the flight computer can no longer use the HAT to send.
// This tool sends real, CRC-valid Firelink frames from the SX1272 so a human
// can discover which (SF, BW, CR) tuple - if any - the HAT's air rate is
// actually set to. Each tuple is announced with a type-2 status frame whose
// text is decoded on the GS console next to the telemetry.
//
//     ./lora_sweep --freq 868125000 --sf 9 --bw 125 --cr 5 --sync 0x12 \
//                  --count 20 --interval-ms 300
//     ./lora_sweep --sweep --count 10
//
// Hardware: SX1272 on SPI0 with RST=25, RX_SWITCH=23, TX_SWITCH=24 (see
// include/telemetry/lora_sx1272.h). Never run without an antenna attached.
//
// Built by:  make sweep

#include "telemetry/lora_sx1272.h"
#include "telemetry/packet.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

void usage() {
    std::cerr
        << "lora_sweep - RF-LORA-868-SO (SX1272) bench transmitter for Firelink\n"
        << "\n"
        << "Fixed config (default)\n"
        << "  --freq <hz>      carrier, 860000000..1020000000 (def 868125000)\n"
        << "  --sf <n>         spreading factor 7..12 (def 9)\n"
        << "  --bw <khz>       bandwidth 125|250|500 (def 125)\n"
        << "  --cr <n>         coding rate 4/n, n = 5..8 (def 5)\n"
        << "  --sync <hex>     sync word, e.g. 0x12 (def 0x12)\n"
        << "  --preamble <n>   preamble symbols (def 8)\n"
        << "  --power <dbm>    TX power 2..20 (def 20)\n"
        << "  --rssi-append    append packet RSSI byte (start GS decoder with --rssi)\n"
        << "\n"
        << "Sweep - try every (SF, BW, CR) tuple in turn:\n"
        << "  --sweep\n"
        << "  --sweep-sf \"a b c\"   factors (def \"7 8 9 10 11 12\")\n"
        << "  --sweep-bw \"x y\"     bandwidths (def \"125 250 500\")\n"
        << "  --sweep-cr \"n m\"     coding rates, n=5..8 (def \"5 6 7 8\")\n"
        << "\n"
        << "Common\n"
        << "  --count <n>      frames per config (def 10)\n"
        << "  --interval-ms <n> gap between frames, ms (def 200)\n"
        << "  --seq <n>        first sequence number (def 0)\n"
        << "  --spi <path>     SPI device (def /dev/spidev0.0)\n"
        << "  --speed <hz>     SPI clock (def 1000000)\n"
        << "  --chip <name>    gpiochip (def gpiochip0)\n"
        << "  --rst <pin>      SX1272 reset pin, BCM (def 25)\n"
        << "  --rxsw <pin>     antenna RX switch, BCM (def 23)\n"
        << "  --txsw <pin>     antenna TX switch, BCM (def 24)\n"
        << "  --help\n";
}

std::vector<int> parseList(const std::string &s) {
    std::vector<int> out;
    std::istringstream is(s);
    int v;
    while (is >> v) out.push_back(v);
    return out;
}

} // namespace

int main(int argc, char **argv) {
    LoRaSX1272::Settings cfg;
    bool sweep_mode = false;
    int count = 10;
    int interval_ms = 200;
    uint16_t seq = 0;
    std::vector<int> sweep_sf, sweep_bw, sweep_cr;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char *name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << std::endl;
                std::exit(2);
            }
            return argv[++i];
        };

        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--sweep") sweep_mode = true;
        else if (a == "--freq") cfg.freq_hz = (uint64_t)std::stoull(need("--freq"));
        else if (a == "--sf") cfg.sf = std::stoi(need("--sf"));
        else if (a == "--bw") cfg.bw_khz = std::stoi(need("--bw"));
        else if (a == "--cr") cfg.cr = std::stoi(need("--cr"));
        else if (a == "--sync") cfg.sync_word = (uint8_t)std::stoul(need("--sync"), nullptr, 0);
        else if (a == "--preamble") cfg.preamble = std::stoi(need("--preamble"));
        else if (a == "--power") cfg.tx_power_dbm = std::stoi(need("--power"));
        else if (a == "--rssi-append") cfg.rssi_append = true;
        else if (a == "--count") count = std::stoi(need("--count"));
        else if (a == "--interval-ms") interval_ms = std::stoi(need("--interval-ms"));
        else if (a == "--seq") seq = (uint16_t)std::stoul(need("--seq"));
        else if (a == "--spi") cfg.spi_dev = need("--spi");
        else if (a == "--speed") cfg.spi_speed_hz = (uint32_t)std::stoul(need("--speed"));
        else if (a == "--chip") cfg.chip = need("--chip");
        else if (a == "--rst") cfg.rst_pin = std::stoi(need("--rst"));
        else if (a == "--rxsw") cfg.rx_switch_pin = std::stoi(need("--rxsw"));
        else if (a == "--txsw") cfg.tx_switch_pin = std::stoi(need("--txsw"));
        else if (a == "--sweep-sf") sweep_sf = parseList(need("--sweep-sf"));
        else if (a == "--sweep-bw") sweep_bw = parseList(need("--sweep-bw"));
        else if (a == "--sweep-cr") sweep_cr = parseList(need("--sweep-cr"));
        else {
            std::cerr << "unknown option " << a << std::endl;
            usage();
            return 2;
        }
    }

    if (count < 1) count = 1;

    auto burst = [&](LoRaSX1272 &radio, const char *marker) {
        // Announce the on-air parameters as a status frame the GS decodes.
        if (marker) {
            uint8_t buf[FIRELINK_PACKET_SIZE];
            packetEncodeStatus(seq++, 1, 0, marker, buf);
            radio.send(buf, sizeof(buf));
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
        TelemetryFields f;
        f.state = 1;
        for (int k = 0; k < count; ++k) {
            uint8_t buf[FIRELINK_PACKET_SIZE];
            packetEncodeTelemetry(seq, f, buf);
            if (!radio.send(buf, sizeof(buf))) {
                std::cerr << "send failed, stopping" << std::endl;
                return false;
            }
            ++seq;
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
        return true;
    };

    if (!sweep_mode) {
        LoRaSX1272 radio(cfg);
        if (!radio.begin()) return 1;
        std::ostringstream marker;
        marker << "SWEP SF" << cfg.sf << " BW" << cfg.bw_khz
               << " CR" << cfg.cr << " S" << (int)cfg.sync_word;
        std::cerr << "TX " << cfg.freq_hz << "Hz SF" << cfg.sf << " BW"
                  << cfg.bw_khz << " CR4/" << cfg.cr << " sync 0x"
                  << std::hex << (int)cfg.sync_word << std::dec
                  << " @" << cfg.tx_power_dbm << "dBm, " << count
                  << " frames x ~" << radio.airtimeMs(FIRELINK_PACKET_SIZE)
                  << "ms\n";
        return burst(radio, marker.str().c_str()) ? 0 : 1;
    }

    if (sweep_sf.empty())   sweep_sf = {7, 8, 9, 10, 11, 12};
    if (sweep_bw.empty())   sweep_bw = {125, 250, 500};
    if (sweep_cr.empty())   sweep_cr = {5, 6, 7, 8};

    std::cerr << "SWEEP " << sweep_sf.size() << "x" << sweep_bw.size() << "x"
              << sweep_cr.size() << " tuples, " << count
              << " frames each, sync 0x" << std::hex << (int)cfg.sync_word
              << std::dec << "\n";

    for (int sf : sweep_sf) {
        for (int bw : sweep_bw) {
            for (int cr : sweep_cr) {
                cfg.sf = sf;
                cfg.bw_khz = bw;
                cfg.cr = cr;
                LoRaSX1272 radio(cfg);
                if (!radio.begin()) continue;
                std::ostringstream marker;
                marker << "SWEP SF" << sf << " BW" << bw << " CR" << cr;
                std::cerr << "--- " << marker.str()
                          << " sync 0x" << std::hex << (int)cfg.sync_word
                          << std::dec << " (~"
                          << radio.airtimeMs(FIRELINK_PACKET_SIZE)
                          << "ms/frame) ---\n";
                if (!burst(radio, marker.str().c_str())) return 1;
            }
        }
    }
    std::cerr << "SWEEP complete\n";
    return 0;
}