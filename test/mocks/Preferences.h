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

    static void resetStorage() {
        _storage.clear();
    }

    bool begin(const char* name, bool readOnly = false) {
        (void)readOnly;
        _currentNs = name ? name : "";
        return true;
    }

    void end() {
        _currentNs.clear();
    }

    bool clear() {
        if (!_currentNs.empty()) {
            _storage[_currentNs].clear();
            return true;
        }
        return false;
    }

    size_t putBytes(const char* key, const void* value, size_t len) {
        if (_currentNs.empty() || !key || !value) return 0;
        const uint8_t* p = (const uint8_t*)value;
        _storage[_currentNs][key] = std::vector<uint8_t>(p, p + len);
        return len;
    }

    size_t getBytes(const char* key, void* buf, size_t maxLen) {
        if (_currentNs.empty() || !key || !buf) return 0;
        auto nsIt = _storage.find(_currentNs);
        if (nsIt == _storage.end()) return 0;
        auto keyIt = nsIt->second.find(key);
        if (keyIt == nsIt->second.end()) return 0;
        size_t toCopy = keyIt->second.size() < maxLen ? keyIt->second.size() : maxLen;
        memcpy(buf, keyIt->second.data(), toCopy);
        return toCopy;
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
