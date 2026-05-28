#include "samplers/papyrus_attribution.h"

#include <algorithm>
#include <cmath>

namespace skygraph::samplers {

PapyrusAttribution::Storage& PapyrusAttribution::Get() {
    static Storage s;
    return s;
}

void PapyrusAttribution::Add(std::string_view a_name, std::uint64_t a_us) {
    if (a_name.empty() || a_us == 0) return;
    auto& s = Get();
    std::lock_guard lk{ s.mtx };
    auto it = s.table.find(std::string{ a_name });
    if (it == s.table.end()) {
        Entry e{ std::string{ a_name }, a_us, 1 };
        s.table.emplace(e.name, std::move(e));
    } else {
        it->second.total_us += a_us;
        ++it->second.calls_in_window;
    }
}

std::vector<PapyrusAttribution::Entry>
PapyrusAttribution::SnapshotTopAndDecay(std::size_t a_topN,
                                        double a_decay,
                                        double /*a_windowSeconds*/) {
    auto& s = Get();
    std::lock_guard lk{ s.mtx };

    std::vector<Entry> snapshot;
    snapshot.reserve(s.table.size());
    for (const auto& [k, e] : s.table) {
        snapshot.push_back(e);
    }
    std::partial_sort(
        snapshot.begin(),
        snapshot.begin() + std::min(a_topN, snapshot.size()),
        snapshot.end(),
        [](const Entry& a, const Entry& b) {
            return a.total_us > b.total_us;
        });
    if (snapshot.size() > a_topN) snapshot.resize(a_topN);

    if (a_decay <= 0.0) {
        s.table.clear();
    } else if (a_decay < 1.0) {
        for (auto it = s.table.begin(); it != s.table.end();) {
            it->second.total_us = static_cast<std::uint64_t>(
                std::round(it->second.total_us * a_decay));
            it->second.calls_in_window = static_cast<std::uint64_t>(
                std::round(it->second.calls_in_window * a_decay));
            // Reap entries that have decayed to nothing to keep the table
            // bounded across long sessions.
            if (it->second.total_us == 0 && it->second.calls_in_window == 0) {
                it = s.table.erase(it);
            } else {
                ++it;
            }
        }
    }
    return snapshot;
}

void PapyrusAttribution::Reset() {
    auto& s = Get();
    std::lock_guard lk{ s.mtx };
    s.table.clear();
}

std::size_t PapyrusAttribution::SizeForDebug() {
    auto& s = Get();
    std::lock_guard lk{ s.mtx };
    return s.table.size();
}

}  // namespace skygraph::samplers
