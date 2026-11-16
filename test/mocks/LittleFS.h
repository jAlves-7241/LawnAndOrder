#pragma once

#include "FS.h"
#include <map>
#include <string>

class MockLittleFS {
public:
    std::map<std::string, std::string> files;

    bool mountFail = false;
    bool writeFail = false;
    bool readFail = false;
    bool formatFail = false;
    bool removeFail = false;

    void reset() {
        mountFail = false;
        writeFail = false;
        readFail = false;
        formatFail = false;
        removeFail = false;
        files.clear();
    }

    bool begin(bool formatOnFail = true) {
        if (mountFail) {
            if (formatOnFail && !formatFail) {
                files.clear();
                mountFail = false;
                writeFail = false;
                readFail = false;
                removeFail = false;
                return true;
            }
            return false;
        }
        return true;
    }

    bool format() {
        if (formatFail) return false;
        files.clear();
        mountFail = false;
        writeFail = false;
        readFail = false;
        removeFail = false;
        return true;
    }

    void end() {}

    File open(const char* path, const char* mode = "r") {
        if (!path) return File();
        std::string p(path);
        std::string m(mode ? mode : "r");

        if (m == "w") {
            files[p] = "";
            return File(p, m, &files[p], &writeFail);
        } else if (m == "a") {
            if (files.find(p) == files.end()) {
                files[p] = "";
            }
            return File(p, m, &files[p], &writeFail);
        } else {
            // Read mode
            auto it = files.find(p);
            if (it == files.end()) return File();
            return File(p, m, &it->second, nullptr, &readFail);
        }
    }

    bool remove(const char* path) {
        if (!path || removeFail) return false;
        files.erase(path);
        return true;
    }

    bool rename(const char* from, const char* to) {
        if (!from || !to) return false;
        auto it = files.find(from);
        if (it == files.end()) return false;
        files[to] = it->second;
        files.erase(it);
        return true;
    }

    bool exists(const char* path) {
        if (!path) return false;
        return files.find(path) != files.end();
    }
};

extern MockLittleFS LittleFS;
