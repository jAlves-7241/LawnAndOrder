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
    std::map<std::string, std::string>* filesMapPtr = nullptr;
    const uint64_t* fsGenPtr = nullptr;
    uint64_t openGen = 0;
    size_t readPos = 0;
    size_t committedSize = 0;
    bool isOpen = false;
    bool* writeFailPtr = nullptr;
    bool* partialWritePtr = nullptr;
    bool* readFailPtr = nullptr;
    bool* bufferedSizePtr = nullptr;

    File() : filesMapPtr(nullptr), fsGenPtr(nullptr), openGen(0),
             readPos(0), committedSize(0), isOpen(false),
             writeFailPtr(nullptr), partialWritePtr(nullptr),
             readFailPtr(nullptr), bufferedSizePtr(nullptr) {}
    File(const std::string& p, const std::string& m,
         std::map<std::string, std::string>* mapPtr,
         const uint64_t* genPtr, uint64_t gen,
         bool* wfPtr = nullptr, bool* rfPtr = nullptr,
         bool* bufSizePtr = nullptr, bool* pwPtr = nullptr)
        : path(p), mode(m), filesMapPtr(mapPtr), fsGenPtr(genPtr), openGen(gen),
          readPos(0), committedSize(0), isOpen(true),
          writeFailPtr(wfPtr), partialWritePtr(pwPtr),
          readFailPtr(rfPtr), bufferedSizePtr(bufSizePtr) {
        if (std::string* c = _content()) {
            committedSize = c->size();
        }
    }

    operator bool() const { return _content() != nullptr; }

    void flush() {
        if (std::string* c = _content()) {
            committedSize = c->size();
        }
    }

    void close() {
        if (std::string* c = _content()) {
            committedSize = c->size();
        }
        isOpen = false;
        filesMapPtr = nullptr;
        fsGenPtr = nullptr;
    }

    bool seek(size_t pos, int seekMode = SeekSet) {
        std::string* c = _content();
        if (!c) return false;
        if (seekMode == SeekSet) {
            readPos = pos;
        } else if (seekMode == SeekCur) {
            readPos += pos;
        } else if (seekMode == SeekEnd) {
            readPos = (c->size() >= pos) ? (c->size() - pos) : 0;
        }
        if (readPos > c->size()) readPos = c->size();
        return true;
    }

    size_t println(const char* s) {
        std::string* c = _content();
        if (!c) return 0;
        if (writeFailPtr && *writeFailPtr) return 0;
        std::string full = (s ? std::string(s) : std::string()) + "\r\n";
        if (partialWritePtr && *partialWritePtr) {
            size_t half = full.size() / 2;
            if (half == 0) half = 1;
            c->append(full.data(), half);
            if (!(bufferedSizePtr && *bufferedSizePtr)) {
                committedSize = c->size();
            }
            return half;
        }
        size_t prevLen = c->size();
        c->append(full);
        if (!(bufferedSizePtr && *bufferedSizePtr)) {
            committedSize = c->size();
        }
        return c->size() - prevLen;
    }

    size_t println(const std::string& s) {
        return println(s.c_str());
    }

    int available() const {
        std::string* c = _content();
        if (!c || readPos >= c->size()) return 0;
        return (int)(c->size() - readPos);
    }

    int read() {
        std::string* c = _content();
        if (!c || readPos >= c->size()) return -1;
        if (readFailPtr && *readFailPtr) return -1;
        return (unsigned char)(*c)[readPos++];
    }

    int read(uint8_t* buf, size_t size) {
        std::string* c = _content();
        if (!c || !buf || size == 0 || readPos >= c->size()) return 0;
        if (readFailPtr && *readFailPtr) return 0;
        size_t avail = c->size() - readPos;
        size_t toRead = avail < size ? avail : size;
        if (toRead > 0) {
            memcpy(buf, c->data() + readPos, toRead);
            readPos += toRead;
        }
        return (int)toRead;
    }

    size_t size() const {
        std::string* c = _content();
        if (!c) return 0;
        if (bufferedSizePtr && *bufferedSizePtr) {
            return committedSize;
        }
        return c->size();
    }

private:
    std::string* _content() const {
        if (!isOpen || !filesMapPtr) return nullptr;
        if (fsGenPtr && *fsGenPtr != openGen) return nullptr;
        auto it = filesMapPtr->find(path);
        return (it != filesMapPtr->end()) ? &it->second : nullptr;
    }
};
