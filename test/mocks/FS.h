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
    size_t committedSize = 0;
    bool isOpen = false;
    bool* writeFailPtr = nullptr;
    bool* partialWritePtr = nullptr;
    bool* readFailPtr = nullptr;
    bool* bufferedSizePtr = nullptr;

    File() : contentPtr(nullptr), readPos(0), committedSize(0), isOpen(false),
             writeFailPtr(nullptr), partialWritePtr(nullptr),
             readFailPtr(nullptr), bufferedSizePtr(nullptr) {}
    File(const std::string& p, const std::string& m, std::string* ptr,
         bool* wfPtr = nullptr, bool* rfPtr = nullptr,
         bool* bufSizePtr = nullptr, bool* pwPtr = nullptr)
        : path(p), mode(m), contentPtr(ptr), readPos(0),
          committedSize(ptr ? ptr->size() : 0), isOpen(true),
          writeFailPtr(wfPtr), partialWritePtr(pwPtr),
          readFailPtr(rfPtr), bufferedSizePtr(bufSizePtr) {}

    operator bool() const { return isOpen && contentPtr != nullptr; }

    void flush() {
        if (isOpen && contentPtr) {
            committedSize = contentPtr->size();
        }
    }

    void close() {
        if (isOpen && contentPtr) {
            committedSize = contentPtr->size();
        }
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
        if (writeFailPtr && *writeFailPtr) return 0;
        std::string full = (s ? std::string(s) : std::string()) + "\r\n";
        if (partialWritePtr && *partialWritePtr) {
            size_t half = full.size() / 2;
            if (half == 0) half = 1;
            contentPtr->append(full.data(), half);
            if (!(bufferedSizePtr && *bufferedSizePtr)) {
                committedSize = contentPtr->size();
            }
            return half;
        }
        size_t prevLen = contentPtr->size();
        contentPtr->append(full);
        if (!(bufferedSizePtr && *bufferedSizePtr)) {
            committedSize = contentPtr->size();
        }
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
        if (readFailPtr && *readFailPtr) return 0;
        size_t avail = contentPtr->size() - readPos;
        size_t toRead = avail < size ? avail : size;
        if (toRead > 0) {
            memcpy(buf, contentPtr->data() + readPos, toRead);
            readPos += toRead;
        }
        return (int)toRead;
    }

    size_t size() const {
        if (!contentPtr) return 0;
        if (bufferedSizePtr && *bufferedSizePtr) {
            return committedSize;
        }
        return contentPtr->size();
    }
};
