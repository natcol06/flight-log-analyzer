#include "report.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

namespace {

// Formats a number with 2 decimals, plus a unit only if there is one
std::string with_unit(double value, const std::string& unit) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    if (!unit.empty()) out << " " << unit;
    return out.str();
}

// Running totals for one check
struct CheckTotals {
    int count = 0;
    double seconds = 0;
    double worst = 0;
    double limit = 0;
    std::string unit;
    Direction dir = Direction::Above;
};

}  // namespace

void print_check_summary(const std::vector<Event>& events) {
    std::map<std::string, CheckTotals> totals;
    for (const Event& e : events) {
        CheckTotals& c = totals[e.name];
        if (c.count == 0) {
            c.worst = e.worst;
            c.limit = e.limit;
            c.unit = e.unit;
            c.dir = e.dir;
        } else if (e.dir == Direction::Above) {
            c.worst = std::max(c.worst, e.worst);
        } else {
            c.worst = std::min(c.worst, e.worst);
        }
        c.count++;
        c.seconds += e.end_s - e.start_s;
    }

    std::cout << std::left << std::setw(22) << "Check"
              << std::setw(8) << "Events"
              << std::setw(12) << "Total time"
              << "Worst (limit)\n";
    std::cout << std::string(70, '-') << "\n";

    for (const auto& [name, c] : totals) {
        std::ostringstream time_text;
        time_text << std::fixed << std::setprecision(1) << c.seconds << " s";

        std::cout << std::left << std::setw(22) << name
                  << std::setw(8) << c.count
                  << std::setw(12) << time_text.str()
                  << with_unit(c.worst, c.unit) << " (limit " << with_unit(c.limit, c.unit) << ")\n";
    }
}

void print_event_list(const std::vector<Event>& events) {
    for (const Event& e : events) {
        std::cout << "  " << std::fixed << std::setprecision(1)
                  << std::right << std::setw(7) << e.start_s << " to "
                  << std::setw(7) << e.end_s << " s   "
                  << std::left << std::setw(22) << e.name
                  << "worst " << with_unit(e.worst, e.unit) << "\n";
    }
}

bool write_topic_csv(const std::string& path, const TopicData& data, std::uint64_t log_start_us) {
    std::ofstream out(path);
    if (!out) return false;

    // Header row: time_s,field1,field2,...
    out << "time_s";
    for (const auto& [name, values] : data.values) {
        out << "," << name;
    }
    out << "\n";

    // One row per reading
    out << std::fixed << std::setprecision(4);
    for (std::size_t i = 0; i < data.timestamps.size(); ++i) {
        double t = (static_cast<double>(data.timestamps[i]) - static_cast<double>(log_start_us)) / 1e6;
        out << t;
        for (const auto& [name, values] : data.values) {
            out << ",";
            if (i < values.size() && !std::isnan(values[i])) out << values[i];
        }
        out << "\n";
    }
    return true;
}

bool write_events_csv(const std::string& path, const std::vector<Event>& events) {
    std::ofstream out(path);
    if (!out) return false;

    out << "check,start_s,end_s,duration_s,worst,limit,unit,samples\n";
    out << std::fixed << std::setprecision(3);
    for (const Event& e : events) {
        out << e.name << "," << e.start_s << "," << e.end_s << ","
            << (e.end_s - e.start_s) << "," << e.worst << "," << e.limit << ","
            << e.unit << "," << e.samples << "\n";
    }
    return true;
}