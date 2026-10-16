#pragma once

#include "checks.hpp"
#include "ulog_parser.hpp"

#include <cstdint>
#include <string>
#include <vector>

// One line per check: how many events, how long, and the worst value
void print_check_summary(const std::vector<Event>& events);

// Every event in time order
void print_event_list(const std::vector<Event>& events);

// Writes one topic's readings to a CSV file. Returns false if the file can't be written.
bool write_topic_csv(const std::string& path, const TopicData& data, std::uint64_t log_start_us);

// Writes all events to a CSV file
bool write_events_csv(const std::string& path, const std::vector<Event>& events);