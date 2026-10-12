#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// One field inside a format, like "float voltage_v" or "float[4] cell_voltage"
struct Field {
    std::string type;      // "float", "uint64_t", or the name of another format
    std::string name;      // "voltage_v"
    int array_length = 1;  // 1 if it's not an array
};

// The full layout of one message type, built from an 'F' message
struct Format {
    std::string name;
    std::vector<Field> fields;
};

// Links an ID number to a format, built from an 'A' message
struct Subscription {
    std::uint16_t msg_id = 0;
    std::uint8_t multi_id = 0;
    std::string format_name;
    long data_count = 0;
};

// Where one value lives inside a data message
struct FlatField {
    std::string name;    // "voltage_v", or "voltage_cell_v[0]" for one item in an array
    std::string type;    // always a basic type like "float"
    std::size_t offset;  // bytes from the start of the message
};

// The decoded readings for one topic
struct TopicData {
    std::vector<std::uint64_t> timestamps;              // microseconds
    std::map<std::string, std::vector<double>> values;  // field name -> one value per reading
};

class ULogParser {
public:
    // Ask for fields from a topic. Call this BEFORE parse().
    void request(const std::string& topic, std::uint8_t multi_id,
                 const std::vector<std::string>& fields);

    bool parse(const std::string& path);

    // Returns nullptr if the topic was never requested
    const TopicData* data(const std::string& topic, std::uint8_t multi_id) const;

    const std::map<std::string, Format>& formats() const { return formats_; }
    const std::map<std::uint16_t, Subscription>& subscriptions() const { return subscriptions_; }
    const std::map<char, long>& message_counts() const { return message_counts_; }
    std::uint64_t start_time_us() const { return start_time_us_; }
    bool truncated() const { return truncated_; }
    const std::string& error() const { return error_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    struct Request {
        std::vector<std::string> wanted;  // field names asked for
        std::vector<FlatField> fields;    // those fields, with offsets filled in
        std::size_t timestamp_offset = 0;
        TopicData data;
    };
    using TopicKey = std::pair<std::string, std::uint8_t>;

    void handle_message(char type, const std::vector<std::uint8_t>& payload);
    void parse_format(const std::vector<std::uint8_t>& payload);
    void parse_subscription(const std::vector<std::uint8_t>& payload);
    void handle_data(const std::vector<std::uint8_t>& payload);

    void link_request(Request& req, const std::string& format_name);
    std::size_t type_size(const std::string& type, int depth = 0) const;
    void flatten(const std::string& format_name, const std::string& prefix,
                 std::size_t base, std::vector<FlatField>& out, int depth = 0) const;

    std::map<std::string, Format> formats_;
    std::map<std::uint16_t, Subscription> subscriptions_;
    std::map<char, long> message_counts_;
    std::uint64_t start_time_us_ = 0;
    bool truncated_ = false;
    std::string error_;

    std::map<TopicKey, Request> requests_;
    std::map<std::uint16_t, Request*> active_;  // msg_id -> the request it feeds
    std::vector<std::string> warnings_;
};