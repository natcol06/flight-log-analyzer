#include "checks.hpp"
#include "ulog_parser.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

struct Wanted {
    std::string topic;
    std::vector<std::string> fields;
};

const std::vector<Wanted> kWanted = {
    {"battery_status",         {"voltage_v", "current_a", "remaining", "cell_count"}},
    {"vehicle_gps_position",   {"fix_type", "satellites_used", "eph"}},
    {"vehicle_local_position", {"z", "vz"}},
    {"vehicle_imu_status",     {"accel_vibration_metric", "gyro_vibration_metric"}},
    {"vehicle_land_detected",  {"landed"}},
};

void print_topics(const ULogParser& parser) {
    std::cout << std::left << std::setw(45) << "Topic" << "Readings\n";
    std::cout << std::string(55, '-') << "\n";
    for (const auto& [id, sub] : parser.subscriptions()) {
        std::string label = sub.format_name + " [" + std::to_string(sub.multi_id) + "]";
        std::cout << std::left << std::setw(45) << label << sub.data_count << "\n";
    }
}

void print_event(const Event& e) {
    std::cout << "  [" << std::right << std::setw(7) << e.start_s << " s to "
              << std::setw(7) << e.end_s << " s]  "
              << e.name << ": worst " << e.worst << " " << e.unit
              << " (limit " << e.limit << ")\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <log.ulg> [--topics]\n";
        return 1;
    }

    ULogParser parser;
    for (const Wanted& w : kWanted) {
        parser.request(w.topic, 0, w.fields);
    }
    if (!parser.parse(argv[1])) {
        std::cerr << "Error: " << parser.error() << "\n";
        return 1;
    }
    if (argc > 2 && std::string(argv[2]) == "--topics") {
        print_topics(parser);
        return 0;
    }

    const Thresholds th;
    const std::uint64_t t0 = parser.start_time_us();

    const TopicData* battery = parser.data("battery_status", 0);
    const TopicData* gps     = parser.data("vehicle_gps_position", 0);
    const TopicData* pos     = parser.data("vehicle_local_position", 0);
    const TopicData* imu     = parser.data("vehicle_imu_status", 0);
    const TopicData* land    = parser.data("vehicle_land_detected", 0);
    InAir in_air(land);

    std::cout << std::fixed << std::setprecision(2);

    // ----- Flight summary -----
    std::cout << "=== Flight summary ===\n";
    if (pos != nullptr && !pos->timestamps.empty()) {
        double length = (pos->timestamps.back() - pos->timestamps.front()) / 1e6;
        std::cout << "Log length:      " << length << " s\n";
    }
    std::cout << "Time in the air: " << in_air.airborne_seconds() << " s\n";

    if (const auto* z = field(pos, "z")) {
        double lowest_z = std::numeric_limits<double>::infinity();
        for (double v : *z) {
            if (!std::isnan(v)) lowest_z = std::min(lowest_z, v);
        }
        std::cout << "Highest point:   " << -lowest_z << " m above takeoff\n";
    }

    // ----- Vibration zones -----
    if (const auto* vib = field(imu, "accel_vibration_metric")) {
        long green = 0, orange = 0, red = 0;
        for (std::size_t i = 0; i < vib->size(); ++i) {
            double v = (*vib)[i];
            if (std::isnan(v) || !in_air.at(imu->timestamps[i])) continue;
            if (v > th.vibration_red)         red++;
            else if (v > th.vibration_orange) orange++;
            else                              green++;
        }
        long total = green + orange + red;
        if (total > 0) {
            std::cout << "\n=== Vibration while flying ===\n";
            std::cout << "Green  (under " << th.vibration_orange << "): " << 100.0 * green / total << "%\n";
            std::cout << "Orange (under " << th.vibration_red << "):  " << 100.0 * orange / total << "%\n";
            std::cout << "Red    (above " << th.vibration_red << "):  " << 100.0 * red / total << "%\n";
        }
    }

    // ----- Run the checks -----
    std::vector<Event> events;
    auto add = [&events](const std::vector<Event>& found) {
        events.insert(events.end(), found.begin(), found.end());
    };

    if (const auto* vib = field(imu, "accel_vibration_metric")) {
        add(find_events("Very high vibration", "m/s^2", imu->timestamps, *vib,
                        Direction::Above, th.vibration_red, in_air, t0, th.merge_gap_s));
    }
    if (const auto* fix = field(gps, "fix_type")) {
        add(find_events("No 3D GPS fix", "", gps->timestamps, *fix,
                        Direction::Below, th.min_fix_type, in_air, t0, th.merge_gap_s));
    }
    if (const auto* sats = field(gps, "satellites_used")) {
        add(find_events("Few satellites", "", gps->timestamps, *sats,
                        Direction::Below, th.min_satellites, in_air, t0, th.merge_gap_s));
    }
    if (const auto* eph = field(gps, "eph")) {
        add(find_events("Poor GPS accuracy", "m", gps->timestamps, *eph,
                        Direction::Above, th.max_eph_m, in_air, t0, th.merge_gap_s));
    }
    if (const auto* vz = field(pos, "vz")) {
        add(find_events("Fast descent", "m/s", pos->timestamps, *vz,
                        Direction::Above, th.max_descent_m_s, in_air, t0, th.merge_gap_s));
    }
    if (const auto* remaining = field(battery, "remaining")) {
        add(find_events("Low battery", "(0 to 1)", battery->timestamps, *remaining,
                        Direction::Below, th.min_remaining, in_air, t0, th.merge_gap_s));
    }

    // Voltage per cell = pack voltage / number of cells
    const auto* volts = field(battery, "voltage_v");
    const auto* cells = field(battery, "cell_count");
    if (volts != nullptr && cells != nullptr) {
        std::vector<double> per_cell;
        for (std::size_t i = 0; i < volts->size() && i < cells->size(); ++i) {
            double n = (*cells)[i];
            per_cell.push_back(n > 0 ? (*volts)[i] / n
                                     : std::numeric_limits<double>::quiet_NaN());
        }
        add(find_events("Low cell voltage", "V/cell", battery->timestamps, per_cell,
                        Direction::Below, th.min_cell_voltage, in_air, t0, th.merge_gap_s));
    }

    // ----- Report -----
    std::sort(events.begin(), events.end(),
              [](const Event& a, const Event& b) { return a.start_s < b.start_s; });

    std::cout << "\n=== Problems found: " << events.size() << " ===\n";
    if (events.empty()) {
        std::cout << "  None. Every check passed while the drone was flying.\n";
    }
    for (const Event& e : events) {
        print_event(e);
    }

    if (!parser.warnings().empty()) {
        std::cout << "\nWarnings:\n";
        for (const std::string& warning : parser.warnings()) {
            std::cout << "  " << warning << "\n";
        }
    }
    if (parser.truncated()) {
        std::cout << "\nNote: the log ended in the middle of a message.\n";
    }
    return 0;
}