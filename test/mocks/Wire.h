#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>

class TwoWire {
public:
    using TxHandler = uint8_t (*)(uint8_t addr, const uint8_t* txBuf, size_t txLen);
    using RxHandler = size_t (*)(uint8_t addr, uint8_t* rxBuf, size_t qty);

    std::vector<uint8_t> detectedAddresses;
    uint32_t beginCalls = 0;

    void begin() { beginCalls++; }
    void begin(int sda, int scl) { (void)sda; (void)scl; beginCalls++; }
    void end() {}
    void setTimeOut(uint16_t ms) { (void)ms; }

    void setSlaveHandlers(uint8_t addr, TxHandler tx, RxHandler rx) {
        _slaveAddr = addr;
        _txHandler = tx;
        _rxHandler = rx;
    }

    void beginTransmission(uint8_t addr) {
        _lastAddr = addr;
        _txBuf.clear();
    }

    size_t write(uint8_t data) {
        _txBuf.push_back(data);
        return 1;
    }

    size_t write(const uint8_t* data, size_t len) {
        if (!data || len == 0) return 0;
        _txBuf.insert(_txBuf.end(), data, data + len);
        return len;
    }

    uint8_t endTransmission() {
        if (!_txBuf.empty() && _txHandler && _lastAddr == _slaveAddr) {
            return _txHandler(_lastAddr, _txBuf.data(), _txBuf.size());
        }
        for (uint8_t a : detectedAddresses) {
            if (a == _lastAddr) return 0;
        }
        return 2; // Not found
    }

    size_t requestFrom(uint8_t addr, size_t qty) {
        _rxBuf.clear();
        _rxPos = 0;
        if (_rxHandler && addr == _slaveAddr && qty > 0) {
            _rxBuf.resize(qty);
            size_t got = _rxHandler(addr, _rxBuf.data(), qty);
            _rxBuf.resize(got);
            return got;
        }
        return 0;
    }

    int read() {
        if (_rxPos < _rxBuf.size()) {
            return _rxBuf[_rxPos++];
        }
        return -1;
    }

    void reset() {
        detectedAddresses.clear();
        beginCalls = 0;
        _txBuf.clear();
        _rxBuf.clear();
        _rxPos = 0;
        _lastAddr = 0;
    }

private:
    uint8_t _lastAddr = 0;
    uint8_t _slaveAddr = 0;
    TxHandler _txHandler = nullptr;
    RxHandler _rxHandler = nullptr;
    std::vector<uint8_t> _txBuf;
    std::vector<uint8_t> _rxBuf;
    size_t _rxPos = 0;
};

extern TwoWire Wire;
inline TwoWire Wire;
