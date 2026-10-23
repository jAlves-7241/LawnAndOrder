#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <map>
#include <vector>

#define SeekSet 0
#define SeekCur 1
#define SeekEnd 2

class File {
public:
    std::string path;
    std::string mode;
    std::string* contentPtr = nullptr;
    size_t readPos = 0;
    bool isOpen = false;

    File() : isOpen(false), readPos(0), contentPtr(nullptr) {}
    File(const std::string& p, const std::string& m, std::string* ptr)
        : path(p), mode(m), contentPtr(ptr), readPos(0), isOpen(true) {}

    operator bool() const { return isOpen && contentPtr != nullptr; }

    void close() {
        isOpen = false;
        contentPtr = nullptr;
    }

    bool seek(size_t pos, int seekMode = SeekSet) {
        if (!isOpen || !contentPtr) return false;
        if (seekMode == SeekSet) {
            readPos = pos;
        } else if (seekMode == SeekCur) {
            readPos += pos;
        } else if (seekMode == SeekEnd) {
            readPos = (contentPtr->size() >= pos) ? (contentPtr->size() - pos) : 0;
        }
        if (readPos > contentPtr->size()) readPos = contentPtr->size();
        return true;
    }

    size_t println(const char* s) {
        if (!isOpen || !contentPtr) return 0;
        size_t prevLen = contentPtr->size();
        if (s) contentPtr->append(s);
        contentPtr->append("\r\n");
        return contentPtr->size() - prevLen;
    }

    size_t println(const std::string& s) {
        return println(s.c_str());
    }

    int available() const {
        if (!isOpen || !contentPtr) return 0;
        return (int)(contentPtr->size() - readPos);
    }

    int read() {
        if (!isOpen || !contentPtr || readPos >= contentPtr->size()) return -1;
        return (unsigned char)(*contentPtr)[readPos++];
    }

    int read(uint8_t* buf, size_t size) {
        if (!isOpen || !contentPtr || !buf || size == 0) return 0;
        size_t avail = contentPtr->size() - readPos;
        size_t toRead = avail < size ? avail : size;
        if (toRead > 0) {
            memcpy(buf, contentPtr->data() + readPos, toRead);
            readPos += toRead;
        }
        return (int)toRead;
    }

    size_t size() const {
        return contentPtr ? contentPtr->size() : 0;
    }
};
