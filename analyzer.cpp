#include "analyzer.h"

#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <array>
#include <string_view>
#include <cctype>

static inline int fastParseHour(const char* p, size_t len) noexcept {
    // Expected format in field: "YYYY-MM-DD HH:MM"  (at least 16 chars)
    // We only need HH at positions 11-12 (0-based) within that field.
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

void TripAnalyzer::ingestFile(const std::string& csvPath) {
    zoneIndex.clear();
    zones.clear();
    zoneCounts.clear();
    hourCounts.clear();

    std::ifstream file(csvPath);
    if (!file.is_open()) return;

    std::string line;
    // Skip header
    if (!std::getline(file, line)) return;

    zoneIndex.reserve(4096);
    zones.reserve(4096);
    zoneCounts.reserve(4096);
    hourCounts.reserve(4096);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // Need at least first 6 fields:
        // TripID,PickupZoneID,DropoffZoneID,PickupDateTime,DistanceKm,FareAmount
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

        // PickupZoneID is field[1] => between c0 and c1
        size_t zStart = c0 + 1;
        size_t zLen = (c1 > zStart) ? (c1 - zStart) : 0;
        if (zLen == 0) continue;

        std::string_view zoneSv(line.data() + zStart, zLen);

        // PickupDateTime is field[3] => between c2 and c3
        size_t dtStart = c2 + 1;
        size_t dtLen = (c3 > dtStart) ? (c3 - dtStart) : 0;
        if (dtLen == 0) continue;

        int hour = fastParseHour(line.data() + dtStart, dtLen);
        if (hour < 0) continue;

        // Keep the same logic: store zone as string key
        std::string zoneKey(zoneSv);

        auto it = zoneIndex.find(zoneKey);
        int idx;
        if (it == zoneIndex.end()) {
            idx = (int)zones.size();
            zones.emplace_back(std::move(zoneKey));
            zoneCounts.push_back(0);

            hourCounts.push_back({});
            hourCounts.back().fill(0);

            // IMPORTANT: use zones.back() as key to avoid mismatch
            zoneIndex.emplace(zones.back(), idx);
        } else {
            idx = it->second;
        }

        zoneCounts[idx] += 1;
        hourCounts[idx][hour] += 1;
    }
}

std::vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    std::vector<ZoneCount> result;
    result.reserve(zones.size());

    for (size_t i = 0; i < zones.size(); ++i) {
        result.push_back({zones[i], zoneCounts[i]});
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
    std::vector<SlotCount> all;
    all.reserve(zones.size() * 4);

    for (size_t i = 0; i < zones.size(); ++i) {
        for (int h = 0; h < 24; ++h) {
            long long c = hourCounts[i][h];
            if (c) all.push_back({zones[i], h, c});
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
