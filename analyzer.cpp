#include "analyzer.h"

#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <array>
#include <string>
#include <string_view>
#include <memory>
#include <cctype>

// ======= FAST HELPERS (senin üst koddakiyle aynı mantık) =======

static inline int fastParseHour(const char* p, size_t len) noexcept {
    // Expected: "YYYY-MM-DD HH:MM"
    // Hour at positions 11-12 (0-based) within this field.
    if (len < 13) return -1;
    if (p[10] != ' ') return -1;

    unsigned char c1 = (unsigned char)p[11];
    unsigned char c2 = (unsigned char)p[12];
    if (!std::isdigit(c1) || !std::isdigit(c2)) return -1;

    int h = (p[11] - '0') * 10 + (p[12] - '0');
    return (h >= 0 && h <= 23) ? h : -1;
}

static inline size_t findComma(const std::string& s, size_t start) noexcept {
    return s.find(',', start);
}

// ======= PIMPL-LIKE STORAGE (GitHub’da çalışan stil) =======

class TripAnalyzerImpl {
public:
    std::unordered_map<std::string, int> zoneIndex;
    std::vector<std::string> zones;
    std::vector<long long> zoneCounts;
    std::vector<std::array<long long, 24>> hourCounts;

    void clearAll() {
        zoneIndex.clear();
        zones.clear();
        zoneCounts.clear();
        hourCounts.clear();
    }
};

static std::unordered_map<const TripAnalyzer*, std::unique_ptr<TripAnalyzerImpl>> implMap;

static TripAnalyzerImpl* getImpl(const TripAnalyzer* ta) {
    auto it = implMap.find(ta);
    if (it == implMap.end()) {
        auto ptr = std::make_unique<TripAnalyzerImpl>();
        TripAnalyzerImpl* raw = ptr.get();
        implMap.emplace(ta, std::move(ptr));
        return raw;
    }
    return it->second.get();
}

// Basit temizlik (çok testte object birikir diye)
static void cleanupIfNeeded() {
    if (implMap.size() > 200) implMap.clear();
}

// ======= REQUIRED METHODS (GitHub testlerinin çağırdığı) =======

void TripAnalyzer::ingestFile(const std::string& csvPath) {
    cleanupIfNeeded();
    TripAnalyzerImpl* impl = getImpl(this);

    impl->clearAll();

    std::ifstream file(csvPath);
    if (!file.is_open()) return;

    std::string line;
    // header skip
    if (!std::getline(file, line)) return;

    impl->zoneIndex.reserve(4096);
    impl->zones.reserve(4096);
    impl->zoneCounts.reserve(4096);
    impl->hourCounts.reserve(4096);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // Need at least 6 fields:
        // 0 TripID
        // 1 PickupZoneID
        // 2 DropoffZoneID
        // 3 PickupDateTime
        // 4 DistanceKm
        // 5 FareAmount
        size_t c0 = findComma(line, 0);
        if (c0 == std::string::npos) continue;
        size_t c1 = findComma(line, c0 + 1);
        if (c1 == std::string::npos) continue;
        size_t c2 = findComma(line, c1 + 1);
        if (c2 == std::string::npos) continue;
        size_t c3 = findComma(line, c2 + 1);
        if (c3 == std::string::npos) continue;
        size_t c4 = findComma(line, c3 + 1);
        if (c4 == std::string::npos) continue;

        // zone field [1]
        size_t zStart = c0 + 1;
        size_t zLen = (c1 > zStart) ? (c1 - zStart) : 0;
        if (zLen == 0) continue;

        std::string_view zoneSv(line.data() + zStart, zLen);

        // datetime field [3]
        size_t dtStart = c2 + 1;
        size_t dtLen = (c3 > dtStart) ? (c3 - dtStart) : 0;
        if (dtLen == 0) continue;

        // Eğer datetime tırnaklı geliyorsa: "YYYY-MM-DD HH:MM"
        // üst algoritmayı bozmadan sadece offset düzeltelim
        const char* dtPtr = line.data() + dtStart;
        size_t dtRealLen = dtLen;
        if (dtRealLen >= 2 && dtPtr[0] == '"' && dtPtr[dtRealLen - 1] == '"') {
            dtPtr += 1;
            dtRealLen -= 2;
        }

        int hour = fastParseHour(dtPtr, dtRealLen);
        if (hour < 0) continue;

        // Zone key (string)
        std::string zoneKey(zoneSv);

        auto it = impl->zoneIndex.find(zoneKey);
        int idx;
        if (it == impl->zoneIndex.end()) {
            idx = (int)impl->zones.size();
            impl->zones.emplace_back(std::move(zoneKey));
            impl->zoneCounts.push_back(0);

            impl->hourCounts.push_back({});
            impl->hourCounts.back().fill(0);

            // key olarak zones.back() kullan (string lifetime garanti)
            impl->zoneIndex.emplace(impl->zones.back(), idx);
        } else {
            idx = it->second;
        }

        impl->zoneCounts[idx] += 1;
        impl->hourCounts[idx][hour] += 1;
    }
}

std::vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    TripAnalyzerImpl* impl = getImpl(this);

    std::vector<ZoneCount> result;
    result.reserve(impl->zones.size());

    for (size_t i = 0; i < impl->zones.size(); ++i) {
        result.push_back({impl->zones[i], impl->zoneCounts[i]});
    }

    auto cmp = [](const ZoneCount& a, const ZoneCount& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.zone < b.zone;
    };

    if (k <= 0) return {};
    if ((int)result.size() <= k) {
        std::sort(result.begin(), result.end(), cmp);
        return result;
    }

    std::nth_element(result.begin(), result.begin() + k, result.end(), cmp);
    result.resize(k);
    std::sort(result.begin(), result.end(), cmp);
    return result;
}

std::vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    TripAnalyzerImpl* impl = getImpl(this);

    std::vector<SlotCount> all;
    all.reserve(impl->zones.size() * 4);

    for (size_t i = 0; i < impl->zones.size(); ++i) {
        for (int h = 0; h < 24; ++h) {
            long long c = impl->hourCounts[i][h];
            if (c) all.push_back({impl->zones[i], h, c});
        }
    }

    auto cmp = [](const SlotCount& a, const SlotCount& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.zone != b.zone) return a.zone < b.zone;
        return a.hour < b.hour;
    };

    if (k <= 0) return {};
    if ((int)all.size() <= k) {
        std::sort(all.begin(), all.end(), cmp);
        return all;
    }

    std::nth_element(all.begin(), all.begin() + k, all.end(), cmp);
    all.resize(k);
    std::sort(all.begin(), all.end(), cmp);
    return all;
}
