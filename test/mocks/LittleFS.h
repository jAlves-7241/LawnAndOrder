#pragma once

#include "FS.h"
#include <map>
#include <string>

class MockLittleFS {
public:
    std::map<std::string, std::string> files;
    uint64_t generation = 0;

    bool mountFail = false;
    bool writeFail = false;
    bool partialWriteFail = false;
    bool readFail = false;
    bool formatFail = false;
    bool removeFail = false;
    bool renameFail = false;
    bool bufferedSize = false;

    void reset() {
        mountFail = false;
        writeFail = false;
        partialWriteFail = false;
        readFail = false;
        formatFail = false;
        removeFail = false;
        renameFail = false;
        bufferedSize = false;
        files.clear();
        generation++;
    }

    bool begin(bool formatOnFail = true) {
        if (mountFail) {
            if (formatOnFail && !formatFail) {
                files.clear();
                generation++;
                mountFail = false;
                writeFail = false;
                partialWriteFail = false;
                readFail = false;
                removeFail = false;
                renameFail = false;
                return true;
            }
            return false;
        }
        return true;
    }

    bool format() {
        if (formatFail) return false;
        files.clear();
        generation++;
        mountFail = false;
        writeFail = false;
        partialWriteFail = false;
        readFail = false;
        removeFail = false;
        renameFail = false;
        return true;
    }

    void end() {}

    File open(const char* path, const char* mode = "r") {
        if (!path) return File();
        std::string p(path);
        std::string m(mode ? mode : "r");

        if (m == "w") {
            files[p] = "";
            return File(p, m, &files, &generation, generation, &writeFail, nullptr, &bufferedSize, &partialWriteFail);
        } else if (m == "a") {
            if (files.find(p) == files.end()) {
                files[p] = "";
            }
            return File(p, m, &files, &generation, generation, &writeFail, nullptr, &bufferedSize, &partialWriteFail);
        } else {
            // Read mode
            auto it = files.find(p);
            if (it == files.end()) return File();
            return File(p, m, &files, &generation, generation, nullptr, &readFail, &bufferedSize, nullptr);
        }
    }

    bool remove(const char* path) {
        if (!path || removeFail) return false;
        files.erase(path);
        return true;
    }

    bool rename(const char* from, const char* to) {
        if (!from || !to || renameFail) return false;
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
