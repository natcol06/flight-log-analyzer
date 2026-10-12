#include "checks.hpp"

#include <algorithm>
#include <cmath>

const std::vector<double>* field(const TopicData* d, const std::string& name) {
    if (d == nullptr) return nullptr;
    auto it = d->values.find(name);
    return it == d->values.end() ? nullptr : &it->second;
}

// ---------- InAir ----------

InAir::InAir(const TopicData* land_detected) {
    const std::vector<double>* landed = field(land_detected, "landed");
    if (landed == nullptr) return;

    times_ = land_detected->timestamps;
    for (double v : *landed) {
        landed_.push_back(v > 0.5);  // 1 = landed, 0 = flying
    }
}

bool InAir::at(std::uint64_t t) const {
    if (times_.empty()) return true;  // no landing data, so check everything

    // Find the last landed reading at or before time t
    auto it = std::upper_bound(times_.begin(), times_.end(), t);
    if (it == times_.begin()) return false;  // before the first reading

    std::size_t i = static_cast<std::size_t>(it - times_.begin()) - 1;
    return !landed_[i];
}

double InAir::airborne_seconds() const {
    double total = 0;
    for (std::size_t i = 0; i + 1 < times_.size(); ++i) {
        if (!landed_[i]) {
            total += (times_[i + 1] - times_[i]) / 1e6;
        }
    }
    return total;
}

// ---------- find_events ----------

std::vector<Event> find_events(const std::string& name, const std::string& unit,
                               const std::vector<std::uint64_t>& times,
                               const std::vector<double>& values,
                               Direction dir, double limit,
                               const InAir& in_air, std::uint64_t log_start_us,
                               double merge_gap_s) {
    std::vector<Event> events;
    Event current;
    bool open = false;

    for (std::size_t i = 0; i < times.size() && i < values.size(); ++i) {
        double v = values[i];
        if (std::isnan(v) || !in_air.at(times[i])) continue;

        bool bad = (dir == Direction::Above) ? v > limit : v < limit;
        if (!bad) continue;

        double t = (static_cast<double>(times[i]) - static_cast<double>(log_start_us)) / 1e6;

        if (open && t - current.end_s <= merge_gap_s) {
            // Close to the last bad reading, so extend the current event
            current.end_s = t;
            current.samples++;
            if (dir == Direction::Above) current.worst = std::max(current.worst, v);
            else                         current.worst = std::min(current.worst, v);
        } else {
            // Too far from the last one (or the first one), so start a new event
            if (open) events.push_back(current);
            current = Event{name, unit, t, t, v, limit, 1};
            open = true;
        }
    }
    if (open) events.push_back(current);
    return events;
}