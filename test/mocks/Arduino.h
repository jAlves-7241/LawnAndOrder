#pragma once

#ifdef unix
#undef unix
#endif
#ifdef linux
#undef linux
#endif

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

#ifdef unix
#undef unix
#endif
#ifdef linux
#undef linux
#endif

typedef bool boolean;
typedef uint8_t byte;

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define OUTPUT_OPEN_DRAIN 3

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
inline void setMockMicros(uint32_t us) { _mock_micros = us; }
inline void advanceMockMicros(uint32_t us) { _mock_micros += us; }

// Interrupt stubs
#define FALLING 2
#define RISING 3
#define CHANGE 4
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int pin, void (*isr)(), int mode) { (void)pin; (void)isr; (void)mode; }
inline void detachInterrupt(int pin) { (void)pin; }

// ESP-IDF Critical Section Mocks
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL_ISR(mux) do { (void)(mux); } while(0)
#define portEXIT_CRITICAL_ISR(mux)  do { (void)(mux); } while(0)
#define portENTER_CRITICAL(mux)     do { (void)(mux); } while(0)
#define portEXIT_CRITICAL(mux)      do { (void)(mux); } while(0)

// Mock GPIO
extern uint8_t _mock_gpio[64];
inline void pinMode(uint8_t pin, uint8_t mode) { (void)pin; (void)mode; }
inline void digitalWrite(uint8_t pin, uint8_t val) {
    if (pin < 64) _mock_gpio[pin] = val;
}
inline int digitalRead(uint8_t pin) {
    return (pin < 64) ? _mock_gpio[pin] : 0;
}

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
#if defined(__linux__) && defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 38))
// glibc 2.38+ provides strlcpy in <string.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
// BSD/macOS provides strlcpy in <string.h>
#else
inline size_t strlcpy(char* dst, const char* src, size_t size) {
    if (!dst || !src || size == 0) return 0;
    size_t srclen = strlen(src);
    size_t toCopy = srclen < size - 1 ? srclen : size - 1;
    memcpy(dst, src, toCopy);
    dst[toCopy] = '\0';
    return srclen;
}
#endif

// Arduino String class mock (minimal adapter)
class String : public std::string {
public:
    String() : std::string() {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(char c) : std::string(1, c) {}
    String(int val) : std::string(std::to_string(val)) {}
    String(unsigned int val) : std::string(std::to_string(val)) {}
    String(long val) : std::string(std::to_string(val)) {}
    String(unsigned long val) : std::string(std::to_string(val)) {}
    String(float val, int precision = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", precision, val);
        assign(buf);
    }
    String(double val, int precision = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", precision, val);
        assign(buf);
    }
    
    int toInt() const { return atoi(c_str()); }
    char charAt(size_t index) const {
        return (index < size()) ? (*this)[index] : 0;
    }
    int indexOf(char c, size_t fromIndex = 0) const {
        size_t pos = find(c, fromIndex);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    int indexOf(const String& str, size_t fromIndex = 0) const {
        size_t pos = find(str, fromIndex);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    String substring(size_t from, size_t to = std::string::npos) const {
        if (from >= size()) return String("");
        if (to == std::string::npos || to > size()) return String(substr(from));
        return String(substr(from, to - from));
    }
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
            size_t toAppend = ((size_t)len < sizeof(buf)) ? (size_t)len : sizeof(buf) - 1;
            outputBuffer.append(buf, toAppend);
            return (size_t)len;
        }
        return 0;
    }

    int availableForWrite() const { return 512; }

    void flush() {}
};

extern MockSerial Serial;

class EspClass {
public:
    bool rebooted = false;
    void restart() { rebooted = true; }
    void reset() { rebooted = false; }
};


inline EspClass ESP;
