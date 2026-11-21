#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <map>
#include <vector>

class Preferences {
public:
    static std::map<std::string, std::map<std::string, std::vector<uint8_t>>> _storage;
    static bool beginFail;
    static bool writeFail;

    static void resetStorage() {
        _storage.clear();
        beginFail = false;
        writeFail = false;
    }

    bool begin(const char* name, bool readOnly = false) {
        (void)readOnly;
        if (!_currentNs.empty() || beginFail || !name || name[0] == '\0') return false;
        _currentNs = name;
        return true;
    }

    void end() {
        _currentNs.clear();
    }

    bool clear() {
        if (!_currentNs.empty() && !writeFail) {
            _storage[_currentNs].clear();
            return true;
        }
        return false;
    }

    size_t putBytes(const char* key, const void* value, size_t len) {
        if (_currentNs.empty() || !key || !value || writeFail) return 0;
        const uint8_t* p = (const uint8_t*)value;
        _storage[_currentNs][key] = std::vector<uint8_t>(p, p + len);
        return len;
    }

    size_t getBytesLength(const char* key) {
        if (_currentNs.empty() || !key) return 0;
        auto nsIt = _storage.find(_currentNs);
        if (nsIt == _storage.end()) return 0;
        auto keyIt = nsIt->second.find(key);
        if (keyIt == nsIt->second.end()) return 0;
        return keyIt->second.size();
    }

    size_t getBytes(const char* key, void* buf, size_t maxLen) {
        size_t len = getBytesLength(key);
        if (!len || !buf || !maxLen) return len;
        if (len > maxLen) return 0; // Matches real ESP32 Arduino Preferences::getBytes()
        memcpy(buf, _storage[_currentNs][key].data(), len);
        return len;
    }

    size_t putUShort(const char* key, uint16_t value) {
        return putBytes(key, &value, sizeof(value));
    }

    uint16_t getUShort(const char* key, uint16_t defaultValue = 0) {
        uint16_t val = 0;
        if (getBytes(key, &val, sizeof(val)) == sizeof(val)) {
            return val;
        }
        return defaultValue;
    }

    size_t putUInt(const char* key, uint32_t value) {
        return putBytes(key, &value, sizeof(value));
    }

    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) {
        uint32_t val = 0;
        if (getBytes(key, &val, sizeof(val)) == sizeof(val)) {
            return val;
        }
        return defaultValue;
    }

    size_t putBool(const char* key, bool value) {
        uint8_t b = value ? 1 : 0;
        return putBytes(key, &b, sizeof(b));
    }

    bool getBool(const char* key, bool defaultValue = false) {
        uint8_t b = 0;
        if (getBytes(key, &b, sizeof(b)) == sizeof(b)) {
            return b != 0;
        }
        return defaultValue;
    }

private:
    std::string _currentNs;
};

inline std::map<std::string, std::map<std::string, std::vector<uint8_t>>> Preferences::_storage;
inline bool Preferences::beginFail = false;
inline bool Preferences::writeFail = false;
