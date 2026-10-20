#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <algorithm>

typedef bool boolean;
typedef uint8_t byte;

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

// Mock Timing
extern uint32_t _mock_millis;
extern uint32_t _mock_micros;

inline uint32_t millis() { return _mock_millis; }
inline uint32_t micros() { return _mock_micros; }
inline void delay(uint32_t ms) { _mock_millis += ms; }
inline void setMockMillis(uint32_t ms) { _mock_millis = ms; }
inline void advanceMockMillis(uint32_t ms) { _mock_millis += ms; }

// Mock GPIO
inline void pinMode(uint8_t pin, uint8_t mode) { (void)pin; (void)mode; }
inline void digitalWrite(uint8_t pin, uint8_t val) { (void)pin; (void)val; }
inline int digitalRead(uint8_t pin) { (void)pin; return 0; }

// Math macros / functions
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef abs
#define abs(x) ((x)>0?(x):-(x))
#endif
#ifndef constrain
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#endif

// BSD / Arduino string utilities
inline size_t strlcpy(char* dst, const char* src, size_t size) {
    if (!dst || !src || size == 0) return 0;
    size_t srclen = strlen(src);
    size_t toCopy = srclen < size - 1 ? srclen : size - 1;
    memcpy(dst, src, toCopy);
    dst[toCopy] = '\0';
    return srclen;
}

// Arduino String class mock (minimal adapter)
class String : public std::string {
public:
    String() : std::string() {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(int val) : std::string(std::to_string(val)) {}
    String(unsigned int val) : std::string(std::to_string(val)) {}
    String(long val) : std::string(std::to_string(val)) {}
    String(unsigned long val) : std::string(std::to_string(val)) {}
    
    int toInt() const { return atoi(c_str()); }
    void toLowerCase() {
        for (auto& c : *this) c = tolower((unsigned char)c);
    }
    void toUpperCase() {
        for (auto& c : *this) c = toupper((unsigned char)c);
    }
    void trim() {
        size_t start = find_first_not_of(" \t\r\n");
        if (start == std::string::npos) { clear(); return; }
        size_t end = find_last_not_of(" \t\r\n");
        *this = substr(start, end - start + 1);
    }
};

// Mock Serial implementation
class MockSerial {
public:
    std::string outputBuffer;
    std::string inputBuffer;
    size_t inputPos = 0;

    void reset() {
        outputBuffer.clear();
        inputBuffer.clear();
        inputPos = 0;
    }

    void feedInput(const std::string& s) {
        inputBuffer += s;
    }

    int available() const {
        return (int)(inputBuffer.size() - inputPos);
    }

    int read() {
        if (inputPos < inputBuffer.size()) {
            return (unsigned char)inputBuffer[inputPos++];
        }
        return -1;
    }

    size_t write(uint8_t c) {
        outputBuffer += (char)c;
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) {
        if (buffer && size) {
            outputBuffer.append((const char*)buffer, size);
            return size;
        }
        return 0;
    }

    size_t print(const char* s) {
        if (!s) return 0;
        outputBuffer += s;
        return strlen(s);
    }

    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int n, int base = DEC) {
        char buf[32];
        if (base == HEX) snprintf(buf, sizeof(buf), "%X", (unsigned int)n);
        else snprintf(buf, sizeof(buf), "%d", n);
        return print(buf);
    }
    size_t print(unsigned int n, int base = DEC) {
        char buf[32];
        if (base == HEX) snprintf(buf, sizeof(buf), "%X", n);
        else snprintf(buf, sizeof(buf), "%u", n);
        return print(buf);
    }
    size_t print(unsigned long n, int base = DEC) {
        char buf[32];
        if (base == HEX) snprintf(buf, sizeof(buf), "%lX", n);
        else snprintf(buf, sizeof(buf), "%lu", n);
        return print(buf);
    }
    size_t print(const String& s) { return print(s.c_str()); }

    size_t println(const char* s) {
        size_t n = print(s);
        outputBuffer += "\r\n";
        return n + 2;
    }
    size_t println(const String& s) { return println(s.c_str()); }
    size_t println(int n, int base = DEC) {
        size_t r = print(n, base);
        outputBuffer += "\r\n";
        return r + 2;
    }
    size_t println(unsigned int n, int base = DEC) {
        size_t r = print(n, base);
        outputBuffer += "\r\n";
        return r + 2;
    }
    size_t println(unsigned long n, int base = DEC) {
        size_t r = print(n, base);
        outputBuffer += "\r\n";
        return r + 2;
    }
    size_t println() {
        outputBuffer += "\r\n";
        return 2;
    }

    size_t printf(const char* format, ...) {
        char buf[512];
        va_list args;
        va_start(args, format);
        int len = vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        if (len > 0) {
            outputBuffer.append(buf, len);
            return (size_t)len;
        }
        return 0;
    }
};

extern MockSerial Serial;
