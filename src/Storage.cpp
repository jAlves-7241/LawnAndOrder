#include "i18n.h"
#include "Storage.h"
#include <Preferences.h>
#include <stddef.h>
#include "log.h"


// ── NVS namespace (max 15 chars) ──────────────────────────
static const char* NVS_NS       = "rega";
static const char* KEY_CFG      = "cfg";
static const char* KEY_CFG_B    = "cfg_b";
static const char* KEY_HBLOB    = "hblob";
static const char* KEY_SCHED_TR = "sched_tr";
static const char* KEY_RECV     = "recv";
static const uint8_t CFG_SLOT_MAGIC = 0xA5;
static const size_t HIST_CACHE_MAX_BYTES = 64;

struct __attribute__((packed)) HistoryNvsHeader {
    uint8_t  version;
    uint16_t lineCount;
    uint32_t fileSize;
    uint16_t dataSize;
};

struct __attribute__((packed)) SchedTriggersBlob {
    uint8_t  version;
    uint8_t  count;
    uint32_t ts[MAX_SLOTS_PER_MODE];
    uint16_t crc16;
};

static Preferences prefs;
Storage storage;

// ─────────────────────────────────────────────────────────
void Storage::begin() {
    if (_ready) return;
    _savePending = false;
    _lastSaveRetryMs = 0;
    _lastLoadResult = ConfigLoadResult::EMPTY;
    _configSeq = 0;
    _activeSlot = 0;
    _backupSeeded = false;
    prefs.end(); // Ensure a clean NVS handle if re-initializing after a reset
    // Open in read-write mode; creates namespace if it doesn't exist.
    _ready = prefs.begin(NVS_NS, false);
    if (!_ready) {
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_NS_FAIL);
    } else {
        LOG_D(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_READY);
    }
}

uint16_t Storage::_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// AppConfigBlob is a packed wire/storage format. Validate bool bytes before
// reading them as bools so malformed NVS/import data cannot create invalid
// C++ bool representations.
static bool _blobHasValidBooleans(const AppConfigBlob& blob) {
    static_assert(sizeof(bool) == 1, "AppConfigBlob expects 1-byte bools");
    static_assert(sizeof(AppConfigBlob) == 36, "AppConfigBlob must be 36 bytes");
    static_assert(offsetof(AppConfigBlob, crc16) == 34,
                  "AppConfigBlob must have no padding before crc16");
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&blob);

    if (raw[offsetof(AppConfigBlob, auto_dst)] > 1 ||
        raw[offsetof(AppConfigBlob, setup_done)] > 1) {
        return false;
    }

    const size_t zonesOffset = offsetof(AppConfigBlob, zones);
    for (size_t i = 0; i < NUM_ZONES; ++i) {
        const size_t zoneOffset = zonesOffset + i * sizeof(blob.zones[0]);
        if (raw[zoneOffset] > 1) return false;
    }
    return true;
}

static bool _validateImportBlob(const AppConfigBlob& blob) {
    if (blob.mode >= (uint8_t)AppMode::_COUNT) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_MODE, (unsigned)blob.mode);
        return false;
    }
    if (blob.backlight_timeout_ms != BACKLIGHT_TIMEOUT_NEVER &&
        (blob.backlight_timeout_ms < 30000UL || blob.backlight_timeout_ms > 3600000UL)) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_BACKLIGHT);
        return false;
    }
    if (blob.suspended_until != 0 &&
        (blob.suspended_until < UNIX_MIN_VALID || blob.suspended_until > UNIX_MAX_VALID)) { // 2020..2099 UTC
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_SUSP_END);
        return false;
    }
    if (blob.custom_ref_day != 0xFFFFFFFFUL && blob.custom_ref_day > CUSTOM_REF_DAY_MAX) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_REF_DAY);
        return false;
    }
    for (int i = 0; i < NUM_ZONES; i++) {
        if (blob.zones[i].duration_min > 20 ||
            (blob.zones[i].enabled && blob.zones[i].duration_min == 0)) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_ZONE_DUR);
            return false;
        }
    }
    if (blob.custom_interval_days == 0 || blob.custom_interval_days > 14) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_INTERVAL);
        return false;
    }
    if (blob.custom_slot_count == 0 || blob.custom_slot_count > MAX_SLOTS_PER_MODE) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_SLOTS);
        return false;
    }
    for (int i = 0; i < MAX_SLOTS_PER_MODE; i++) {
        if (blob.custom_slots[i].hour > 23 || blob.custom_slots[i].minute > 59) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_FIELD, TXT_IMP_FLD_TIME);
            return false;
        }
    }
    return true;
}

static int _hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}


// One-step N-1 schema migration hook.
// When bumping NVS_VERSION (e.g. v5 -> v6), decode the previous version's packed
// blob here and populate outBlob (version = NVS_VERSION, recomputed CRC-16) so
// firmware updates preserve user settings instead of wiping to defaults.
// v4 used individual NVS keys rather than a packed blob and has no migration.
bool Storage::_migratePrevVersionBlob(const uint8_t* raw, size_t len, AppConfigBlob& outBlob) {
    (void)raw;
    (void)len;
    (void)outBlob;
    return false;
}

bool Storage::_readConfigSlot(const char* key, AppConfigBlob& outBlob,
                              uint32_t& outSeq, bool& outEnveloped,
                              ConfigLoadResult& outErr) {
    static_assert(sizeof(AppConfigSlot) == 44, "AppConfigSlot must be 44 bytes");
    static_assert(offsetof(AppConfigSlot, slot_crc16) == 42,
                  "AppConfigSlot must have no padding before slot_crc16");
    outSeq = 0;
    outEnveloped = false;
    outErr = ConfigLoadResult::EMPTY;

    const size_t storedLen = prefs.getBytesLength(key);
    if (storedLen == 0) {
        return false;
    }

    auto validateBlob = [&](const AppConfigBlob& b, ConfigLoadResult& err) -> bool {
        if (b.version != NVS_VERSION) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_VER_MISMATCH,
                  (unsigned)b.version, (unsigned)NVS_VERSION);
            err = ConfigLoadResult::VERSION_MISMATCH;
            return false;
        }
        if (!_blobHasValidBooleans(b)) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_BOOL_INV);
            err = ConfigLoadResult::CORRUPT;
            return false;
        }
        uint16_t expectedCrc = _crc16(reinterpret_cast<const uint8_t*>(&b),
                                      offsetof(AppConfigBlob, crc16));
        if (b.crc16 != expectedCrc) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_CFG);
            err = ConfigLoadResult::CORRUPT;
            return false;
        }
        return true;
    };

    if (storedLen == sizeof(AppConfigSlot)) {
        AppConfigSlot slot = {};
        if (prefs.getBytes(key, &slot, sizeof(slot)) != sizeof(slot)) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_SIZE_MISMATCH);
            outErr = ConfigLoadResult::CORRUPT;
            return false;
        }
        if (slot.magic != CFG_SLOT_MAGIC) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_CFG);
            outErr = ConfigLoadResult::CORRUPT;
            return false;
        }
        uint16_t expectedSlotCrc = _crc16(reinterpret_cast<const uint8_t*>(&slot),
                                          offsetof(AppConfigSlot, slot_crc16));
        if (slot.slot_crc16 != expectedSlotCrc) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_CFG);
            outErr = ConfigLoadResult::CORRUPT;
            return false;
        }
        if (slot.seq == 0) {
            outErr = ConfigLoadResult::CORRUPT;
            return false;
        }
        if (slot.version != NVS_VERSION) {
            if (slot.version == (uint8_t)(NVS_VERSION - 1U) &&
                _migratePrevVersionBlob(reinterpret_cast<const uint8_t*>(&slot.blob),
                                        sizeof(slot.blob), outBlob) &&
                validateBlob(outBlob, outErr)) {
                outSeq = slot.seq;
                outEnveloped = true;
                outErr = ConfigLoadResult::OK;
                return true;
            }
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_VER_MISMATCH,
                  (unsigned)slot.version, (unsigned)NVS_VERSION);
            outErr = ConfigLoadResult::VERSION_MISMATCH;
            return false;
        }
        if (!validateBlob(slot.blob, outErr)) {
            return false;
        }
        outBlob = slot.blob;
        outSeq = slot.seq;
        outEnveloped = true;
        outErr = ConfigLoadResult::OK;
        return true;
    }

    if (storedLen == sizeof(AppConfigBlob)) {
        AppConfigBlob blob = {};
        if (prefs.getBytes(key, &blob, sizeof(blob)) != sizeof(blob)) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_SIZE_MISMATCH);
            outErr = ConfigLoadResult::CORRUPT;
            return false;
        }
        if (blob.version == (uint8_t)(NVS_VERSION - 1U) &&
            _migratePrevVersionBlob(reinterpret_cast<const uint8_t*>(&blob),
                                    sizeof(blob), outBlob) &&
            validateBlob(outBlob, outErr)) {
            outSeq = 0;
            outEnveloped = false;
            outErr = ConfigLoadResult::OK;
            return true;
        }
        if (!validateBlob(blob, outErr)) {
            return false;
        }
        outBlob = blob;
        outSeq = 0;
        outEnveloped = false;
        outErr = ConfigLoadResult::OK;
        return true;
    }

    // Attempt N-1 migration for future schema size changes before failing size check
    if (storedLen <= 128U) {
        uint8_t rawBuf[128];
        if (prefs.getBytes(key, rawBuf, storedLen) == storedLen) {
            const size_t kEnvelopeHeader = offsetof(AppConfigSlot, blob);
            const size_t kEnvelopeOverhead = kEnvelopeHeader + sizeof(uint16_t);
            if (storedLen > kEnvelopeOverhead &&
                rawBuf[0] == CFG_SLOT_MAGIC &&
                rawBuf[1] == (uint8_t)(NVS_VERSION - 1U)) {
                uint16_t storedSlotCrc = 0;
                memcpy(&storedSlotCrc, rawBuf + storedLen - sizeof(uint16_t), sizeof(storedSlotCrc));
                uint32_t seq = 0;
                memcpy(&seq, rawBuf + offsetof(AppConfigSlot, seq), sizeof(seq));
                if (seq != 0 &&
                    storedSlotCrc == _crc16(rawBuf, storedLen - sizeof(uint16_t)) &&
                    _migratePrevVersionBlob(rawBuf + kEnvelopeHeader,
                                            storedLen - kEnvelopeOverhead, outBlob) &&
                    validateBlob(outBlob, outErr)) {
                    outSeq = seq;
                    outEnveloped = true;
                    outErr = ConfigLoadResult::OK;
                    return true;
                }
            } else if (_migratePrevVersionBlob(rawBuf, storedLen, outBlob) &&
                       validateBlob(outBlob, outErr)) {
                outSeq = 0;
                outEnveloped = false;
                outErr = ConfigLoadResult::OK;
                return true;
            }
        }
    }

    LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_SIZE_MISMATCH);
    outErr = ConfigLoadResult::CORRUPT;
    return false;
}

// ─────────────────────────────────────────────────────────
bool Storage::load() {
    if (!_ready) {
        _lastLoadResult = ConfigLoadResult::NOT_READY;
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_LOAD_FAILED);
        return false;
    }

    AppConfigBlob blobA = {}, blobB = {};
    uint32_t seqA = 0, seqB = 0;
    bool envA = false, envB = false;
    ConfigLoadResult errA = ConfigLoadResult::EMPTY, errB = ConfigLoadResult::EMPTY;

    const bool okA = _readConfigSlot(KEY_CFG,   blobA, seqA, envA, errA);
    const bool okB = _readConfigSlot(KEY_CFG_B, blobB, seqB, envB, errB);

    if (!okA && !okB) {
        _configSeq = 0;
        _activeSlot = 0;
        _backupSeeded = false;
        if (errA == ConfigLoadResult::EMPTY && errB == ConfigLoadResult::EMPTY) {
            _lastLoadResult = ConfigLoadResult::EMPTY;
            LOG_I(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_EMPTY);
            return false;
        }
        _lastLoadResult = (errA == ConfigLoadResult::VERSION_MISMATCH ||
                           errB == ConfigLoadResult::VERSION_MISMATCH) &&
                          (errA != ConfigLoadResult::CORRUPT && errB != ConfigLoadResult::CORRUPT)
                          ? ConfigLoadResult::VERSION_MISMATCH
                          : ConfigLoadResult::CORRUPT;
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_LOAD_FAILED);
        return false;
    }

    // Choose order: newer sequence first (wrap-safe signed delta); enveloped preferred over legacy
    uint8_t firstIdx = 0;
    if (okA && okB) {
        if (envA != envB) {
            firstIdx = envB ? 1 : 0;
        } else if ((int32_t)(seqB - seqA) > 0) {
            firstIdx = 1;
        } else {
            firstIdx = 0;
        }
    } else {
        firstIdx = okA ? 0 : 1;
    }

    const bool slotOk[2] = { okA, okB };
    const uint32_t slotSeq[2] = { seqA, seqB };
    const AppConfigBlob* slotBlob[2] = { &blobA, &blobB };
    const ConfigLoadResult slotErr[2] = { errA, errB };

    uint8_t chosen = 0xFF;
    bool usedBackup = false;
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        uint8_t idx = (attempt == 0) ? firstIdx : (uint8_t)(1U - firstIdx);
        if (!slotOk[idx]) continue;
        if (_blobToState(*slotBlob[idx])) {
            chosen = idx;
            const uint8_t other = (uint8_t)(1U - idx);
            if (attempt == 1 || (slotErr[other] != ConfigLoadResult::OK &&
                                 slotErr[other] != ConfigLoadResult::EMPTY)) {
                usedBackup = true;
            }
            break;
        }
    }

    if (chosen == 0xFF) {
        _configSeq = 0;
        _activeSlot = 0;
        _backupSeeded = false;
        _lastLoadResult = ConfigLoadResult::CORRUPT;
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_LOAD_FAILED);
        return false;
    }

    _activeSlot = chosen;
    uint32_t maxSeq = slotSeq[chosen];
    const uint8_t otherSlot = (uint8_t)(1U - chosen);
    if (slotOk[otherSlot] && slotSeq[otherSlot] != 0 &&
        (maxSeq == 0 || (int32_t)(slotSeq[otherSlot] - maxSeq) > 0)) {
        maxSeq = slotSeq[otherSlot];
    }
    _configSeq = maxSeq;
    _backupSeeded = (okA && okB && envA && envB && !usedBackup);
    _lastLoadResult = ConfigLoadResult::OK;

    if (usedBackup) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_BACKUP_USED);
    }

    LOG_I(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_LOADED,
          serialModeName(gState.mode),
          gState.suspended ? TXT_SER_YES : TXT_SER_NO);
    return true;
}

// ─────────────────────────────────────────────────────────
bool Storage::save() {
    if (!_ready) {
        _savePending = true;
        _lastSaveRetryMs = millis();
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_CFG);
        return false;
    }

    AppConfigBlob blob = {};
    _stateToBlob(blob);

    const char* slotKeys[2] = { KEY_CFG, KEY_CFG_B };

    // Avoid unnecessary flash writes if active slot already holds identical, valid enveloped config
    // and the backup slot is already seeded.
    if (_backupSeeded) {
        AppConfigSlot current = {};
        if (prefs.getBytes(slotKeys[_activeSlot], &current, sizeof(current)) == sizeof(current) &&
            current.magic == CFG_SLOT_MAGIC &&
            current.version == NVS_VERSION &&
            current.seq != 0 &&
            current.slot_crc16 == _crc16(reinterpret_cast<const uint8_t*>(&current),
                                         offsetof(AppConfigSlot, slot_crc16)) &&
            memcmp(&blob, &current.blob, sizeof(blob)) == 0) {
            _savePending = false;
            _lastLoadResult = ConfigLoadResult::OK;
            return true;
        }
    }

    uint32_t nextSeq = _configSeq + 1UL;
    if (nextSeq == 0) nextSeq = 1UL;

    AppConfigSlot slot = {};
    slot.magic   = CFG_SLOT_MAGIC;
    slot.version = NVS_VERSION;
    slot.seq     = nextSeq;
    slot.blob    = blob;
    slot.slot_crc16 = _crc16(reinterpret_cast<const uint8_t*>(&slot),
                             offsetof(AppConfigSlot, slot_crc16));

    // Alternate slots so the previous valid copy stays intact while writing the new one.
    uint8_t target = 0;
    if (_backupSeeded) {
        target = (uint8_t)(1U - _activeSlot);
    } else if (_activeSlot == 0 && prefs.getBytesLength(KEY_CFG) > 0) {
        target = 1; // Keep existing KEY_CFG intact while seeding KEY_CFG_B first
    } else {
        target = 0;
    }

    if (prefs.putBytes(slotKeys[target], &slot, sizeof(slot)) != sizeof(slot)) {
        _savePending = true;
        _lastSaveRetryMs = millis();
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_CFG);
        return false;
    }

    _configSeq = nextSeq;
    _activeSlot = target;
    _savePending = false;
    _lastLoadResult = ConfigLoadResult::OK;

    // Seed the alternate slot on first save (or after recovering from a damaged slot)
    // so two valid copies exist immediately without waiting for a second user edit.
    if (!_backupSeeded) {
        uint8_t mirror = (uint8_t)(1U - target);
        if (prefs.putBytes(slotKeys[mirror], &slot, sizeof(slot)) == sizeof(slot)) {
            _backupSeeded = true;
        }
    }

    LOG_D(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_UPDATED, serialModeName(gState.mode));
    return true;
}

void Storage::update() {
    if (!_savePending || !_ready) return;
    uint32_t nowMs = millis();
    if ((nowMs - _lastSaveRetryMs) >= 5000UL) {
        _lastSaveRetryMs = nowMs;
        save();
    }
}

// ─────────────────────────────────────────────────────────
bool Storage::clear() {
    if (!_ready) {
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_NS_FAIL);
        return false;
    }
    if (!prefs.clear()) {   // erases all keys in the namespace
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_CFG);
        return false;
    }
    _savePending = false;
    _configSeq = 0;
    _activeSlot = 0;
    _backupSeeded = false;
    _lastLoadResult = ConfigLoadResult::EMPTY;
    LOG_I(TXT_LOG_TAG_STORAGE, TXT_LOG_MEM_CLEARED);
    return true;
}

// ─────────────────────────────────────────────────────────
bool Storage::loadHistoryCache(void* dest, size_t size, uint16_t& lineCount, uint32_t* outFileSize) {
    lineCount = 0;
    if (outFileSize) *outFileSize = 0;
    if (!_ready || !dest || size == 0 || size > HIST_CACHE_MAX_BYTES) return false;

    const size_t expectedLen = sizeof(HistoryNvsHeader) + size + sizeof(uint16_t);
    const size_t storedLen = prefs.getBytesLength(KEY_HBLOB);
    if (storedLen == 0) return false;
    if (storedLen != expectedLen) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_HIST_INV);
        return false;
    }
    uint8_t buf[sizeof(HistoryNvsHeader) + HIST_CACHE_MAX_BYTES + sizeof(uint16_t)];
    size_t readBytes = prefs.getBytes(KEY_HBLOB, buf, expectedLen);
    if (readBytes != expectedLen) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_HIST_INV);
        return false;
    }

    HistoryNvsHeader hdr = {};
    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.version != NVS_VERSION || hdr.dataSize != size) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_HIST_INV);
        return false;
    }

    uint16_t storedCrc = 0;
    memcpy(&storedCrc, buf + sizeof(hdr) + size, sizeof(storedCrc));
    uint16_t calcCrc = _crc16(buf, sizeof(hdr) + size);
    if (storedCrc != calcCrc) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_HIST);
        return false;
    }

    memcpy(dest, buf + sizeof(hdr), size);
    lineCount = hdr.lineCount;
    if (outFileSize) *outFileSize = hdr.fileSize;
    return true;
}

void Storage::saveHistoryCache(const void* src, size_t size, uint16_t lineCount, uint32_t fileSize) {
    if (!_ready || !src || size == 0 || size > HIST_CACHE_MAX_BYTES) return;

    const size_t totalLen = sizeof(HistoryNvsHeader) + size + sizeof(uint16_t);
    uint8_t buf[sizeof(HistoryNvsHeader) + HIST_CACHE_MAX_BYTES + sizeof(uint16_t)];
    HistoryNvsHeader hdr = {};
    hdr.version   = NVS_VERSION;
    hdr.lineCount = lineCount;
    hdr.fileSize  = fileSize;
    hdr.dataSize  = (uint16_t)size;

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), src, size);
    uint16_t crc = _crc16(buf, sizeof(hdr) + size);
    memcpy(buf + sizeof(hdr) + size, &crc, sizeof(crc));

    uint8_t existing[sizeof(HistoryNvsHeader) + HIST_CACHE_MAX_BYTES + sizeof(uint16_t)];
    if (prefs.getBytes(KEY_HBLOB, existing, sizeof(existing)) == totalLen &&
        memcmp(buf, existing, totalLen) == 0) {
        return;
    }

    if (prefs.putBytes(KEY_HBLOB, buf, totalLen) != totalLen) {
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_HIST);
    }
}

bool Storage::hasHistoryData() {
    if (!_ready) return false;
    uint8_t buf[sizeof(HistoryNvsHeader) + HIST_CACHE_MAX_BYTES + sizeof(uint16_t)];
    const size_t storedLen = prefs.getBytesLength(KEY_HBLOB);
    if (storedLen < sizeof(HistoryNvsHeader) + sizeof(uint16_t) || storedLen > sizeof(buf)) {
        return false;
    }
    size_t readBytes = prefs.getBytes(KEY_HBLOB, buf, sizeof(buf));
    if (readBytes != storedLen) return false;

    HistoryNvsHeader hdr = {};
    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.version != NVS_VERSION || hdr.dataSize > HIST_CACHE_MAX_BYTES) return false;
    size_t expectedLen = sizeof(HistoryNvsHeader) + hdr.dataSize + sizeof(uint16_t);
    if (readBytes != expectedLen) return false;

    uint16_t storedCrc = 0;
    memcpy(&storedCrc, buf + sizeof(hdr) + hdr.dataSize, sizeof(storedCrc));
    if (storedCrc != _crc16(buf, sizeof(hdr) + hdr.dataSize)) return false;

    return (hdr.lineCount > 0 || hdr.fileSize > 0);
}

// ─────────────────────────────────────────────────────────
bool Storage::loadLastTriggers(uint32_t* out, size_t count) {
    if (!out || count != MAX_SLOTS_PER_MODE) return false;
    for (size_t i = 0; i < count; i++) out[i] = 0;
    if (!_ready) return false;

    SchedTriggersBlob blob = {};
    const size_t storedLen = prefs.getBytesLength(KEY_SCHED_TR);
    if (storedLen == 0) return false;
    if (storedLen != sizeof(blob)) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_TRIG_CORRUPT);
        return false;
    }
    size_t readBytes = prefs.getBytes(KEY_SCHED_TR, &blob, sizeof(blob));
    if (readBytes != sizeof(blob) || blob.version != NVS_VERSION ||
        blob.count != MAX_SLOTS_PER_MODE) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_TRIG_CORRUPT);
        return false;
    }

    uint16_t expectedCrc = _crc16(reinterpret_cast<const uint8_t*>(&blob),
                                  offsetof(SchedTriggersBlob, crc16));
    if (blob.crc16 != expectedCrc) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_TRIG);
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        out[i] = blob.ts[i];
    }
    return true;
}

void Storage::saveLastTriggers(const uint32_t* in, size_t count) {
    if (!_ready || !in || count != MAX_SLOTS_PER_MODE) return;

    SchedTriggersBlob blob = {};
    blob.version = NVS_VERSION;
    blob.count   = MAX_SLOTS_PER_MODE;
    for (size_t i = 0; i < count; i++) {
        blob.ts[i] = in[i];
    }
    blob.crc16 = _crc16(reinterpret_cast<const uint8_t*>(&blob),
                        offsetof(SchedTriggersBlob, crc16));

    SchedTriggersBlob current = {};
    if (prefs.getBytes(KEY_SCHED_TR, &current, sizeof(current)) == sizeof(current) &&
        memcmp(&blob, &current, sizeof(blob)) == 0) {
        return;
    }

    if (prefs.putBytes(KEY_SCHED_TR, &blob, sizeof(blob)) != sizeof(blob)) {
        LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_TRIG);
    }
}

// ─────────────────────────────────────────────────────────
bool Storage::loadRecoveryState(RecoveryState& rs) {
    static_assert(sizeof(RecoveryState) == 36, "RecoveryState must be 36 bytes");
    static_assert(offsetof(RecoveryState, crc16) == 34,
                  "RecoveryState must have no padding before crc16");
    if (!_ready) return false;
    RecoveryState tmp = {};
    const size_t storedLen = prefs.getBytesLength(KEY_RECV);
    if (storedLen == 0) return false;
    if (storedLen != sizeof(tmp)) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_RECV_CORRUPT, TXT_NVS_RSN_SIZE);
        return false;
    }
    size_t readBytes = prefs.getBytes(KEY_RECV, &tmp, sizeof(tmp));
    if (readBytes != sizeof(tmp)) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_RECV_CORRUPT, TXT_NVS_RSN_SIZE);
        return false;
    }
    if (tmp.version != NVS_VERSION) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_RECV_CORRUPT, TXT_NVS_RSN_VER);
        return false;
    }
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&tmp);
    if (raw[offsetof(RecoveryState, active)] > 1) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_RECV_CORRUPT, TXT_NVS_RSN_BOOL);
        return false;
    }
    uint16_t expectedCrc = _crc16(raw, offsetof(RecoveryState, crc16));
    if (tmp.crc16 != expectedCrc) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_RECV);
        return false;
    }
    rs = tmp;
    return true;
}

bool Storage::saveRecoveryState(const RecoveryState& rs) {
    if (!_ready) return false;
    RecoveryState copy;
    memset(&copy, 0, sizeof(RecoveryState));
    copy = rs;
    copy.version = NVS_VERSION;
    copy.crc16 = _crc16(reinterpret_cast<const uint8_t*>(&copy),
                        offsetof(RecoveryState, crc16));
    
    RecoveryState current = {};
    if (prefs.getBytes(KEY_RECV, &current, sizeof(current)) != sizeof(current) ||
        memcmp(&copy, &current, sizeof(copy)) != 0) {
        if (prefs.putBytes(KEY_RECV, &copy, sizeof(copy)) != sizeof(copy)) {
            LOG_E(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_WRITE_FAIL, TXT_LOG_NVS_PART_RECV);
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────
void Storage::exportConfigHex(char* hexOut, size_t maxLen) {
    if (!hexOut || maxLen == 0) return;
    if (maxLen < sizeof(AppConfigBlob) * 2 + 1) {
        hexOut[0] = '\0';
        return;
    }

    AppConfigBlob blob = {};
    _stateToBlob(blob);

    // Convert to hex string
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&blob);
    static const char hexChars[] = "0123456789ABCDEF";
    for (size_t i = 0; i < sizeof(AppConfigBlob); i++) {
        hexOut[i * 2]     = hexChars[ptr[i] >> 4];
        hexOut[i * 2 + 1] = hexChars[ptr[i] & 0x0F];
    }
    hexOut[sizeof(AppConfigBlob) * 2] = '\0';
}

// ─────────────────────────────────────────────────────────
bool Storage::importConfigHex(const char* hexIn) {
    if (!hexIn) return false;
    if (gState.watering.active) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_ACTIVE);
        return false;
    }

    size_t len = strlen(hexIn);
    if (len != sizeof(AppConfigBlob) * 2) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_LEN,
              (unsigned)len, (unsigned)(sizeof(AppConfigBlob) * 2));
        return false;
    }

    AppConfigBlob blob = {};
    uint8_t* ptr = reinterpret_cast<uint8_t*>(&blob);
    for (size_t i = 0; i < sizeof(AppConfigBlob); i++) {
        const int hi = _hexNibble(hexIn[i * 2]);
        const int lo = _hexNibble(hexIn[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_BAD_HEX,
                  (unsigned)((hi < 0) ? (i * 2) : (i * 2 + 1)));
            return false;
        }
        ptr[i] = (uint8_t)((hi << 4) | lo);
    }

    if (blob.version != NVS_VERSION) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_VER,
              (unsigned)blob.version, (unsigned)NVS_VERSION);
        return false;
    }

    if (!_blobHasValidBooleans(blob)) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_BAD_BOOL);
        return false;
    }

    uint16_t expectedCrc = _crc16(reinterpret_cast<const uint8_t*>(&blob),
                                  offsetof(AppConfigBlob, crc16));
    if (blob.crc16 != expectedCrc) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CRC_FAIL, TXT_LOG_NVS_PART_IMP);
        return false;
    }

    if (!_validateImportBlob(blob)) {
        return false;
    }

    const AppState previousState = gState;
    const uint8_t customMode = (uint8_t)AppMode::PERSONALIZADO;
    const ModeSchedule previousCustomSchedule = MODE_SCHEDULES[customMode];

    if (!_blobToState(blob)) {
        return false;
    }

    if (gState.rtc_valid && gState.suspended && gState.suspended_until > 0 &&
        gState.now.unix >= gState.suspended_until) {
        gState.suspended = false;
        gState.suspended_until = 0;
        LOG_I(TXT_LOG_TAG_SCHEDULE, TXT_LOG_SUSP_EXP_CLR);
    }

    const bool prevSavePending = _savePending;
    if (!save()) {
        gState = previousState;
        MODE_SCHEDULES[customMode] = previousCustomSchedule;
        _savePending = prevSavePending;
        return false;
    }

    LOG_I(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_OK, serialModeName(gState.mode));
    return true;
}


// ─────────────────────────────────────────────────────────
void Storage::_stateToBlob(AppConfigBlob& blob) {
    memset(&blob, 0, sizeof(blob));
    blob.version              = NVS_VERSION;
    blob.mode                 = (uint8_t)gState.mode;
    blob.backlight_timeout_ms = gState.backlight_timeout_ms;
    blob.suspended_until      = gState.suspended_until;
    blob.custom_ref_day       = gState.custom_ref_day;
    blob.auto_dst             = gState.auto_dst;
    blob.setup_done           = gState.setup_done;

    for (int i = 0; i < NUM_ZONES; i++) {
        blob.zones[i].enabled      = gState.zones[i].enabled && (gState.zones[i].duration_min > 0);
        blob.zones[i].duration_min = gState.zones[i].duration_min;
    }

    const ModeSchedule& cs = MODE_SCHEDULES[(uint8_t)AppMode::PERSONALIZADO];
    blob.custom_interval_days = cs.interval_days;
    blob.custom_slot_count    = cs.slot_count;
    for (int i = 0; i < MAX_SLOTS_PER_MODE; i++) {
        blob.custom_slots[i].hour   = cs.slots[i].hour;
        blob.custom_slots[i].minute = cs.slots[i].minute;
    }

    blob.crc16 = _crc16(reinterpret_cast<const uint8_t*>(&blob),
                        offsetof(AppConfigBlob, crc16));
}

bool Storage::_blobToState(const AppConfigBlob& blob) {
    if (blob.mode >= (uint8_t)AppMode::_COUNT) {
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_IMP_INV_MODE, (unsigned)blob.mode);
        return false;
    }

    const AppState previousState = gState;
    const uint8_t customMode = (uint8_t)AppMode::PERSONALIZADO;
    const ModeSchedule previousCustomSchedule = MODE_SCHEDULES[customMode];

    gState.mode = (AppMode)blob.mode;

    if (blob.backlight_timeout_ms != BACKLIGHT_TIMEOUT_NEVER &&
        (blob.backlight_timeout_ms < 30000UL || blob.backlight_timeout_ms > 3600000UL)) {
        gState.backlight_timeout_ms = 120000UL; // Fallback para 2 minutos
    } else {
        gState.backlight_timeout_ms = blob.backlight_timeout_ms;
    }
    if (blob.suspended_until > UNIX_MAX_VALID) {
        gState.suspended_until = UNIX_MAX_VALID;
    } else if (blob.suspended_until < UNIX_MIN_VALID) {
        gState.suspended_until = 0;
    } else {
        gState.suspended_until = blob.suspended_until;
    }
    gState.custom_ref_day = (blob.custom_ref_day != 0xFFFFFFFFUL && blob.custom_ref_day > CUSTOM_REF_DAY_MAX)
                            ? 0xFFFFFFFFUL : blob.custom_ref_day;
    gState.auto_dst = !!blob.auto_dst;
    gState.setup_done = !!blob.setup_done;
    gState.suspended = (gState.suspended_until > 0);

    for (int i = 0; i < NUM_ZONES; i++) {
        gState.zones[i].enabled = !!blob.zones[i].enabled;
        gState.zones[i].duration_min =
            (blob.zones[i].duration_min <= 20) ? blob.zones[i].duration_min : 8;
        if (gState.zones[i].duration_min == 0) {
            gState.zones[i].enabled = false;
        }
    }

    ModeSchedule& cs = MODE_SCHEDULES[customMode];
    cs.interval_days = blob.custom_interval_days;
    if (cs.interval_days == 0 || cs.interval_days > 14) cs.interval_days = 1;

    cs.slot_count = blob.custom_slot_count;
    if (cs.slot_count == 0 || cs.slot_count > MAX_SLOTS_PER_MODE) cs.slot_count = 1;

    for (int i = 0; i < MAX_SLOTS_PER_MODE; i++) {
        cs.slots[i].hour = (blob.custom_slots[i].hour > 23) ? 0 : blob.custom_slots[i].hour;
        cs.slots[i].minute = (blob.custom_slots[i].minute > 59) ? 0 : blob.custom_slots[i].minute;
    }

    // Keep active slots in chronological order.
    sortCustomSlots(cs);

    // Reject schedules whose watering runs could overlap (enforce >= 1 min even if total_dur == 0).
    if (hasCustomSlotOverlap(cs, computeMinCustomSpacing())) {
        if (gState.mode == AppMode::PERSONALIZADO) {
            LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_OVERLAP);
            gState = previousState;
            MODE_SCHEDULES[customMode] = previousCustomSchedule;
            return false;
        }
        // Dormant custom schedule overlaps current zone durations: respace evenly
        // anchored at slots[0] and keep sorted so user settings are preserved.
        respaceCustomSlots(cs);
        LOG_W(TXT_LOG_TAG_STORAGE, TXT_LOG_NVS_CUSTOM_RESPACED);
    }

    return true;
}
