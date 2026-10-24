#pragma once

#include "FS.h"
#include <map>
#include <string>

class MockLittleFS {
public:
    std::map<std::string, std::string> files;

    bool mountFail = false;

    void reset() {
        mountFail = false;
        files.clear();
    }

    bool begin(bool formatOnFail = true) {
        if (mountFail) {
            if (formatOnFail) {
                mountFail = false;
                return true;
            }
            return false;
        }
        return true;
    }

    File open(const char* path, const char* mode = "r") {
        if (!path) return File();
        std::string p(path);
        std::string m(mode ? mode : "r");

        if (m == "w") {
            files[p] = "";
            return File(p, m, &files[p]);
        } else if (m == "a") {
            if (files.find(p) == files.end()) {
                files[p] = "";
            }
            return File(p, m, &files[p]);
        } else {
            // Read mode
            auto it = files.find(p);
            if (it == files.end()) return File();
            return File(p, m, &it->second);
        }
    }

    bool remove(const char* path) {
        if (!path) return false;
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
