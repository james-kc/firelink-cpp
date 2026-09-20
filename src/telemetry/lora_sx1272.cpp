#include "telemetry/lora_sx1272.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cmath>
#include <iostream>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>

#include "telemetry/packet.h"

namespace {

// SX1272 (868 MHz HF, LoRa mode) register map. The 1272 layout differs from
// the SX1276 in the modem-config bits - do not copy 1276 values blindly.
constexpr uint8_t REG_FIFO                   = 0x00;
constexpr uint8_t REG_OP_MODE                = 0x01;
constexpr uint8_t REG_FRF_MSB                = 0x06;
constexpr uint8_t REG_FRF_MID                = 0x07;
constexpr uint8_t REG_FRF_LSB                = 0x08;
constexpr uint8_t REG_PA_CONFIG              = 0x09;
constexpr uint8_t REG_FIFO_ADDR_PTR          = 0x0D;
constexpr uint8_t REG_FIFO_TX_BASE_ADDR      = 0x0E;
constexpr uint8_t REG_FIFO_RX_BASE_ADDR      = 0x0F;
constexpr uint8_t REG_FIFO_RX_CURRENT_ADDR   = 0x10;
constexpr uint8_t REG_IRQ_FLAGS_MASK         = 0x11;
constexpr uint8_t REG_IRQ_FLAGS              = 0x12;
constexpr uint8_t REG_RX_NB_BYTES            = 0x13;
constexpr uint8_t REG_PKT_RSSI               = 0x1A;
constexpr uint8_t REG_MODEM_CONFIG_1         = 0x1D;
constexpr uint8_t REG_MODEM_CONFIG_2         = 0x1E;
constexpr uint8_t REG_PREAMBLE_MSB           = 0x20;
constexpr uint8_t REG_PREAMBLE_LSB           = 0x21;
constexpr uint8_t REG_PAYLOAD_LENGTH         = 0x22;
constexpr uint8_t REG_DETECT_OPTIMIZE        = 0x31;
constexpr uint8_t REG_DETECTION_THRESHOLD    = 0x37;
constexpr uint8_t REG_SYNC_WORD              = 0x39;
constexpr uint8_t REG_VERSION                = 0x42;
constexpr uint8_t REG_PA_DAC                 = 0x5A;

// SX1272 LoRa op-mode encoding: bit 7 selects LoRa (vs FSK), bits [2:0] the
// operating state.
constexpr uint8_t OP_MODE_LORA_BIT           = 0x80;
constexpr uint8_t OP_MODE_SLEEP              = 0x00;
constexpr uint8_t OP_MODE_STANDBY            = 0x01;
constexpr uint8_t OP_MODE_TX                 = 0x03;
constexpr uint8_t OP_MODE_RX_CONTINUOUS      = 0x05;

// SX1272 IRQ flag bits (REG_IRQ_FLAGS, write-1-to-clear).
constexpr uint8_t IRQ_TX_DONE                = 0x08;
constexpr uint8_t IRQ_VALID_HEADER           = 0x10;
constexpr uint8_t IRQ_PAYLOAD_CRC_ERROR      = 0x20;
constexpr uint8_t IRQ_RX_DONE                = 0x40;
constexpr uint8_t IRQ_ALL                    = 0xFF;

// SX1272 HF packet RSSI reference: RSSI_dBm = -139 + RegPktRssiValue.
constexpr int    SX1272_RSSI_OFFSET_DBM      = -139;
constexpr double SX1272_FST_HZ               = 61.03515625;  // 32 MHz / 2^19

// SX1272 detection configuration for SF 7..12 (0x05/0x0C are SF6).
constexpr uint8_t DETECT_OPTIMIZE_SF_7_12    = 0x03;
constexpr uint8_t DETECTION_THRESHOLD_SF_7_12 = 0x0A;

// SX1272 22-pin package exposes only PA_BOOST; PA_DAC boost adds +3 dB
// beyond 17 dBm. (RF Solutions RF-LORA-868-SO uses this package.)
constexpr uint8_t PA_DAC_BOOST_OFF           = 0x04;
constexpr uint8_t PA_DAC_BOOST_ON            = 0x07;

} // namespace

LoRaSX1272::LoRaSX1272(const Settings &settings) : s_(settings) {}

LoRaSX1272::~LoRaSX1272() {
    if (spi_fd_ >= 0) close(spi_fd_);
    if (rx_sw_) gpiod_line_release(rx_sw_);
    if (tx_sw_) gpiod_line_release(tx_sw_);
    if (rst_) gpiod_line_release(rst_);
    if (chip_) gpiod_chip_close(chip_);
}

bool LoRaSX1272::openSpi() {
    spi_fd_ = ::open(s_.spi_dev.c_str(), O_RDWR);
    if (spi_fd_ < 0) {
        std::cerr << "SX1272: failed to open " << s_.spi_dev << ": "
                  << std::strerror(errno) << std::endl;
        return false;
    }
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    if (ioctl(spi_fd_, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &s_.spi_speed_hz) < 0) {
        std::cerr << "SX1272: SPI config failed: " << std::strerror(errno)
                  << std::endl;
        return false;
    }
    return true;
}

bool LoRaSX1272::requestPins() {
    chip_ = gpiod_chip_open_by_name(s_.chip.c_str());
    if (!chip_) {
        std::cerr << "SX1272: failed to open " << s_.chip << std::endl;
        return false;
    }
    rst_ = gpiod_chip_get_line(chip_, s_.rst_pin);
    rx_sw_ = gpiod_chip_get_line(chip_, s_.rx_switch_pin);
    tx_sw_ = gpiod_chip_get_line(chip_, s_.tx_switch_pin);
    if (!rst_ || !rx_sw_ || !tx_sw_) {
        std::cerr << "SX1272: failed to get rst/rx_switch/tx_switch lines ("
                  << s_.rst_pin << "/" << s_.rx_switch_pin << "/"
                  << s_.tx_switch_pin << ")" << std::endl;
        return false;
    }
    if (gpiod_line_request_output(rst_, "firelink-sx1272-rst", 0) < 0 ||
        gpiod_line_request_output(rx_sw_, "firelink-sx1272-rxsw", 0) < 0 ||
        gpiod_line_request_output(tx_sw_, "firelink-sx1272-txsw", 0) < 0) {
        std::cerr << "SX1272: failed to request reset/antenna outputs"
                  << std::endl;
        return false;
    }
    return true;
}

void LoRaSX1272::setAntenna(bool tx) {
    if (!rx_sw_ || !tx_sw_) return;
    gpiod_line_set_value(rx_sw_, tx ? 0 : 1);
    gpiod_line_set_value(tx_sw_, tx ? 1 : 0);
    // Let the switch settle before any RF activity.
    usleep(500);
}

void LoRaSX1272::writeReg(uint8_t reg, uint8_t value) {
    spi_ioc_transfer tr{};
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), value };
    uint8_t rx[2] = { 0, 0 };
    tr.tx_buf = (uint64_t)tx;
    tr.rx_buf = (uint64_t)rx;
    tr.len = 2;
    tr.speed_hz = s_.spi_speed_hz;
    tr.bits_per_word = 8;
    ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &tr);
}

uint8_t LoRaSX1272::readReg(uint8_t reg) {
    spi_ioc_transfer tr{};
    uint8_t tx[2] = { (uint8_t)((reg & 0x7F) | 0x80), 0x00 };
    uint8_t rx[2] = { 0, 0 };
    tr.tx_buf = (uint64_t)tx;
    tr.rx_buf = (uint64_t)rx;
    tr.len = 2;
    tr.speed_hz = s_.spi_speed_hz;
    tr.bits_per_word = 8;
    if (ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &tr) < 0) return 0xFF;
    return rx[1];
}

void LoRaSX1272::readFifo(uint8_t *out, size_t n) {
    if (n > 64) n = 64;
    uint8_t tx[1 + 64] = {0};
    uint8_t rx[1 + 64] = {0};
    tx[0] = (uint8_t)(REG_FIFO | 0x80);  // read FIFO; address auto-increments
    for (size_t i = 0; i < n; ++i) tx[1 + i] = 0x00;
    spi_ioc_transfer tr{};
    tr.tx_buf = (uint64_t)tx;
    tr.rx_buf = (uint64_t)rx;
    tr.len = 1 + n;
    tr.speed_hz = s_.spi_speed_hz;
    tr.bits_per_word = 8;
    if (ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &tr) < 0) return;
    std::memcpy(out, rx + 1, n);
}

void LoRaSX1272::setOpMode(uint8_t mode) {
    writeReg(REG_OP_MODE, OP_MODE_LORA_BIT | mode);
}

bool LoRaSX1272::waitForFlags(uint8_t want, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; ++waited) {
        if (readReg(REG_IRQ_FLAGS) & want) return true;
        usleep(1000);
    }
    return false;
}

void LoRaSX1272::clearFlags(uint8_t flags) {
    writeReg(REG_IRQ_FLAGS, flags);
}

static void writeFifoBytes(int spi_fd, uint32_t speed_hz,
                           const uint8_t *data, size_t n) {
    uint8_t tx[1 + FIRELINK_PACKET_SIZE];
    uint8_t rx[1 + FIRELINK_PACKET_SIZE] = {0};
    tx[0] = REG_FIFO;  // write FIFO; address auto-increments
    std::memcpy(tx + 1, data, n);
    spi_ioc_transfer tr{};
    tr.tx_buf = (uint64_t)tx;
    tr.rx_buf = (uint64_t)rx;
    tr.len = 1 + n;
    tr.speed_hz = speed_hz;
    tr.bits_per_word = 8;
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
}

bool LoRaSX1272::begin() {
    if (!openSpi()) return false;
    if (!requestPins()) return false;

    std::lock_guard<std::mutex> lk(mu_);

    // Reset pulse (RadioLib convention: asserted HIGH, then LOW). If the
    // module never answers on the bench (REG_VERSION not 0x22), flip the
    // polarity below - some boards invert.
    gpiod_line_set_value(rst_, 1);
    usleep(500);
    gpiod_line_set_value(rst_, 0);
    usleep(10 * 1000);

    if (readReg(REG_VERSION) != 0x22) {
        std::cerr << "SX1272: REG_VERSION=0x" << std::hex
                  << (int)readReg(REG_VERSION) << std::dec
                  << " (expected 0x22) - is an SX1272 wired to "
                  << s_.spi_dev << "?" << std::endl;
        return false;
    }

    // LoRa mode, leaving FSK: sleep then standby.
    setOpMode(OP_MODE_SLEEP);
    setOpMode(OP_MODE_STANDBY);

    // Carrier: FRF[23:0] = freq / (32 MHz / 2^19).
    uint32_t frf = (uint32_t)std::lround((double)s_.freq_hz / SX1272_FST_HZ);
    writeReg(REG_FRF_MSB, (uint8_t)((frf >> 16) & 0xFF));
    writeReg(REG_FRF_MID, (uint8_t)((frf >> 8) & 0xFF));
    writeReg(REG_FRF_LSB, (uint8_t)(frf & 0xFF));

    // Modem config 1: BW[7:6] CR[5:3] header[2] CRC-on[1] LDRO[0].
    int bw_code = 0;    // SX1272: 125 kHz = 00, 250 kHz = 01, 500 kHz = 10
    if (s_.bw_khz >= 500) bw_code = 2;
    else if (s_.bw_khz >= 250) bw_code = 1;
    int cr_code = std::max(0, s_.cr - 4);   // 4/5 = 001, 4/8 = 100
    if (cr_code > 4) cr_code = 4;
    bool ldro = ((double)(1 << s_.sf) / (s_.bw_khz * 1000.0)) >= 16e-3;
    writeReg(REG_MODEM_CONFIG_1,
             (uint8_t)((bw_code << 6) | (cr_code << 3) | 0x02 /*CRC on*/ |
                       (ldro ? 0x01 : 0x00)));

    // Modem config 2: SF[7:4], TxContinuous[3]=0, AGC auto[2]=1.
    writeReg(REG_MODEM_CONFIG_2, (uint8_t)((s_.sf << 4) | 0x04));

    // SX1272 receiver detection (SF7-12).
    writeReg(REG_DETECT_OPTIMIZE, DETECT_OPTIMIZE_SF_7_12);
    writeReg(REG_DETECTION_THRESHOLD, DETECTION_THRESHOLD_SF_7_12);

    writeReg(REG_SYNC_WORD, s_.sync_word);
    writeReg(REG_PREAMBLE_MSB, (uint8_t)((s_.preamble >> 8) & 0xFF));
    writeReg(REG_PREAMBLE_LSB, (uint8_t)(s_.preamble & 0xFF));
    writeReg(REG_PAYLOAD_LENGTH, FIRELINK_PACKET_SIZE);

    // PA_BOOST only on the 22-pin SX1272. 2..17 dBm use (p-2); 18..20 dBm
    // additionally enable the PA_DAC boost and use (p-5).
    int p = std::max(2, std::min(20, s_.tx_power_dbm));
    uint8_t pa_out, pa_dac;
    if (p <= 17) {
        pa_out = (uint8_t)(0x80 | (p - 2));
        pa_dac = PA_DAC_BOOST_OFF;
    } else {
        pa_out = (uint8_t)(0x80 | (p - 5));
        pa_dac = PA_DAC_BOOST_ON;
    }
    writeReg(REG_PA_CONFIG, pa_out);
    writeReg(REG_PA_DAC, pa_dac);

    // FIFO layout: TX and RX buffers both start at 0.
    writeReg(REG_FIFO_TX_BASE_ADDR, 0x00);
    writeReg(REG_FIFO_RX_BASE_ADDR, 0x00);

    // Unmask all IRQ flags; we poll, no interrupt line is wired.
    writeReg(REG_IRQ_FLAGS_MASK, 0x00);
    clearFlags(IRQ_ALL);

    setAntenna(false);
    setOpMode(OP_MODE_RX_CONTINUOUS);

    std::cout << "SX1272 ready: " << s_.freq_hz << " Hz, SF" << s_.sf
              << ", BW" << s_.bw_khz << " kHz, CR4/" << s_.cr
              << ", sync 0x" << std::hex << (int)s_.sync_word << std::dec
              << ", preamble " << s_.preamble << ", " << p << " dBm"
              << std::endl;
    return true;
}

bool LoRaSX1272::send(const uint8_t *data, size_t len) {
    if (spi_fd_ < 0 || !data) return false;
    if (len == 0 || len > FIRELINK_PACKET_SIZE) return false;

    std::lock_guard<std::mutex> lk(mu_);
    if (readReg(REG_VERSION) != 0x22) return false;

    setAntenna(true);
    setOpMode(OP_MODE_STANDBY);

    clearFlags(IRQ_ALL);
    writeReg(REG_PAYLOAD_LENGTH, (uint8_t)len);
    writeReg(REG_FIFO_ADDR_PTR, 0x00);
    writeFifoBytes(spi_fd_, s_.spi_speed_hz, data, len);

    setOpMode(OP_MODE_TX);
    if (!waitForFlags(IRQ_TX_DONE, airtimeMs(len) + 250)) {
        std::cerr << "SX1272: TX timeout after " << airtimeMs(len)
                  << " ms" << std::endl;
        clearFlags(IRQ_ALL);
        setOpMode(OP_MODE_STANDBY);
        setAntenna(false);
        setOpMode(OP_MODE_RX_CONTINUOUS);
        return false;
    }
    clearFlags(IRQ_TX_DONE);

    // Back to continuous RX so the link thread can listen again.
    setOpMode(OP_MODE_STANDBY);
    setAntenna(false);
    setOpMode(OP_MODE_RX_CONTINUOUS);
    return true;
}

size_t LoRaSX1272::receive(uint8_t *out, size_t max) {
    if (spi_fd_ < 0 || !out || max == 0) return 0;
    // A concurrent send() holds the radio (and its SPI bus); do not stall the
    // link thread behind a 300 ms transmission - just report nothing.
    if (!mu_.try_lock()) return 0;
    std::lock_guard<std::mutex> lk(mu_, std::adopt_lock);

    uint8_t flags = readReg(REG_IRQ_FLAGS);
    if (!(flags & IRQ_RX_DONE)) return 0;

    if (flags & IRQ_PAYLOAD_CRC_ERROR) {
        // Pop the corrupt payload so the FIFO resumes at the next frame.
        uint8_t cur = readReg(REG_FIFO_RX_CURRENT_ADDR);
        writeReg(REG_FIFO_ADDR_PTR, cur);
        uint8_t junk[FIRELINK_PACKET_SIZE + 1];
        uint8_t n = readReg(REG_RX_NB_BYTES);
        if (n > 0) readFifo(junk, n);
        clearFlags(IRQ_RX_DONE | IRQ_PAYLOAD_CRC_ERROR | IRQ_VALID_HEADER);
        return 0;
    }

    size_t n = readReg(REG_RX_NB_BYTES);
    if (n == 0 || n > max) {
        clearFlags(IRQ_RX_DONE | IRQ_VALID_HEADER);
        return 0;
    }
    // Point the FIFO read pointer at the received frame.
    uint8_t cur = readReg(REG_FIFO_RX_CURRENT_ADDR);
    writeReg(REG_FIFO_ADDR_PTR, cur);
    readFifo(out, n);
    clearFlags(IRQ_RX_DONE | IRQ_VALID_HEADER);

    if (s_.rssi_append && n + 1 <= max) {
        int rssi_dbm = SX1272_RSSI_OFFSET_DBM + (int)readReg(REG_PKT_RSSI);
        if (rssi_dbm < -128) rssi_dbm = -128;
        if (rssi_dbm > 127) rssi_dbm = 127;
        out[n] = (uint8_t)(int8_t)rssi_dbm;
        return n + 1;
    }
    return n;
}

int LoRaSX1272::airtimeMs(size_t bytes) const {
    const int sf = std::max(7, std::min(12, s_.sf));
    const int cr = std::max(4, std::min(8, s_.cr));
    const bool crc = true;
    const bool implicit_header = false;
    double bw_hz = (double)s_.bw_khz * 1000.0;
    if (bw_hz < 1000.0) bw_hz = 125000.0;
    // Enable LDRO exactly as in begin() (symbol time >= 16 ms).
    const double de = (((double)(1 << sf) / bw_hz) >= 16e-3) ? 1.0 : 0.0;
    const double symbol_s = (double)(1 << sf) / bw_hz;

    double payload = std::ceil(
        (8.0 * (double)bytes - 4.0 * (double)sf + 28.0 +
         (crc ? 16.0 : 0.0) - (implicit_header ? 20.0 : 0.0)) /
        (4.0 * ((double)sf - 2.0 * de)));
    if (payload < 0.0) payload = 0.0;
    payload *= (double)cr;

    double symbols = (double)std::max(1, s_.preamble) + payload + 4.25;
    return (int)std::ceil(symbols * symbol_s * 1000.0);
}