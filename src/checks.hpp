#pragma once

#include "ulog_parser.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Limits for each check. Change these to make the analyzer stricter or looser.
struct Thresholds {
    double vibration_red = 9.81;      // m/s^2, red zone in PX4 Flight Review (1 g)
    double vibration_orange = 4.905;  // m/s^2, orange zone in PX4 Flight Review (0.5 g)
    double min_fix_type = 3;          // below 3 means no 3D GPS fix
    double min_satellites = 6;
    double max_eph_m = 3.0;           // horizontal GPS accuracy, meters
    double max_descent_m_s = 3.0;     // vz is positive when going down
    double min_cell_voltage = 3.5;    // volts per battery cell
    double min_remaining = 0.20;      // 20% battery left
    double merge_gap_s = 2.0;         // join problems less than 2 s apart
    int min_samples = 2;              // ignore events with fewer bad readings than this
};

// Is a value bad when it goes above the limit, or below it?
enum class Direction { Above, Below };

// One stretch of time where a check failed
struct Event {
    std::string name;
    std::string unit;
    double start_s = 0;
    double end_s = 0;
    double worst = 0;
    double limit = 0;
    int samples = 0;
    Direction dir = Direction::Above;
};

// Answers "was the drone in the air at time t?" using vehicle_land_detected
class InAir {
public:
    explicit InAir(const TopicData* land_detected);
    bool at(std::uint64_t t) const;
    double airborne_seconds() const;

private:
    std::vector<std::uint64_t> times_;
    std::vector<bool> landed_;
};

// Finds stretches where a value breaks its limit while the drone is flying
std::vector<Event> find_events(const std::string& name, const std::string& unit,
                               const std::vector<std::uint64_t>& times,
                               const std::vector<double>& values,
                               Direction dir, double limit,
                               const InAir& in_air, std::uint64_t log_start_us,
                               double merge_gap_s);

// Returns one field's values from a topic, or nullptr if it's missing
const std::vector<double>* field(const TopicData* d, const std::string& name);