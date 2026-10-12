#include "ulog_parser.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>

namespace {

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t read_u32(const std::uint8_t* p) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    }
    return value;
}

// Size in bytes of a basic type, or 0 if it's not a basic type
std::size_t basic_size(const std::string& type) {
    static const std::map<std::string, std::size_t> sizes = {
        {"int8_t", 1},  {"uint8_t", 1},  {"bool", 1},  {"char", 1},
        {"int16_t", 2}, {"uint16_t", 2},
        {"int32_t", 4}, {"uint32_t", 4}, {"float", 4},
        {"int64_t", 8}, {"uint64_t", 8}, {"double", 8},
    };
    auto it = sizes.find(type);
    return it == sizes.end() ? 0 : it->second;
}

// Turns the bytes at p into a number, based on the type
double decode_value(const std::uint8_t* p, const std::string& type) {
    if (type == "float") {
        std::uint32_t bits = read_u32(p);
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }
    if (type == "double") {
        std::uint64_t bits = read_u64(p);
        double d;
        std::memcpy(&d, &bits, sizeof d);
        return d;
    }
    if (type == "uint8_t" || type == "bool" || type == "char") return p[0];
    if (type == "int8_t")   return static_cast<std::int8_t>(p[0]);
    if (type == "uint16_t") return read_u16(p);
    if (type == "int16_t")  return static_cast<std::int16_t>(read_u16(p));
    if (type == "uint32_t") return read_u32(p);
    if (type == "int32_t")  return static_cast<std::int32_t>(read_u32(p));
    if (type == "uint64_t") return static_cast<double>(read_u64(p));
    if (type == "int64_t")  return static_cast<double>(static_cast<std::int64_t>(read_u64(p)));
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

void ULogParser::request(const std::string& topic, std::uint8_t multi_id,
                         const std::vector<std::string>& fields) {
    requests_[{topic, multi_id}].wanted = fields;
}

const TopicData* ULogParser::data(const std::string& topic, std::uint8_t multi_id) const {
    auto it = requests_.find({topic, multi_id});
    return it == requests_.end() ? nullptr : &it->second.data;
}

bool ULogParser::parse(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error_ = "Could not open " + path;
        return false;
    }

    // ----- File header -----
    std::array<std::uint8_t, 16> header{};
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    if (file.gcount() != static_cast<std::streamsize>(header.size())) {
        error_ = "File is too short to be a ULog file";
        return false;
    }

    const std::array<std::uint8_t, 7> magic = {0x55, 0x4C, 0x6F, 0x67, 0x01, 0x12, 0x35};
    if (std::memcmp(header.data(), magic.data(), magic.size()) != 0) {
        error_ = "Not a ULog file (magic bytes don't match)";
        return false;
    }
    start_time_us_ = read_u64(header.data() + 8);

    // ----- Message loop -----
    std::vector<std::uint8_t> payload;
    while (true) {
        std::array<std::uint8_t, 3> msg_header{};
        file.read(reinterpret_cast<char*>(msg_header.data()), msg_header.size());
        std::streamsize got = file.gcount();
        if (got == 0) break;
        if (got < 3) { truncated_ = true; break; }

        std::uint16_t size = read_u16(msg_header.data());
        char type = static_cast<char>(msg_header[2]);

        payload.resize(size);
        file.read(reinterpret_cast<char*>(payload.data()), size);
        if (file.gcount() != size) { truncated_ = true; break; }

        handle_message(type, payload);
    }
    return true;
}

void ULogParser::handle_message(char type, const std::vector<std::uint8_t>& payload) {
    message_counts_[type]++;
    switch (type) {
        case 'F': parse_format(payload); break;
        case 'A': parse_subscription(payload); break;
        case 'D': handle_data(payload); break;
        default: break;
    }
}

void ULogParser::parse_format(const std::vector<std::uint8_t>& payload) {
    std::string text(payload.begin(), payload.end());

    std::size_t colon = text.find(':');
    if (colon == std::string::npos) return;

    Format fmt;
    fmt.name = text.substr(0, colon);

    std::size_t pos = colon + 1;
    while (pos < text.size()) {
        std::size_t semi = text.find(';', pos);
        if (semi == std::string::npos) semi = text.size();
        std::string field_text = text.substr(pos, semi - pos);
        pos = semi + 1;

        std::size_t space = field_text.find(' ');
        if (space == std::string::npos) continue;

        Field field;
        std::string type_part = field_text.substr(0, space);
        field.name = field_text.substr(space + 1);

        std::size_t bracket = type_part.find('[');
        if (bracket != std::string::npos) {
            field.array_length = std::stoi(type_part.substr(bracket + 1));
            type_part = type_part.substr(0, bracket);
        }
        field.type = type_part;
        fmt.fields.push_back(field);
    }
    formats_[fmt.name] = fmt;
}

void ULogParser::parse_subscription(const std::vector<std::uint8_t>& payload) {
    if (payload.size() < 3) return;

    Subscription sub;
    sub.multi_id = payload[0];
    sub.msg_id = read_u16(&payload[1]);
    sub.format_name = std::string(payload.begin() + 3, payload.end());
    subscriptions_[sub.msg_id] = sub;

    // If we asked for this topic, work out its field offsets now
    auto r = requests_.find({sub.format_name, sub.multi_id});
    if (r != requests_.end()) {
        link_request(r->second, sub.format_name);
        active_[sub.msg_id] = &r->second;
    }
}

// Total size in bytes of a type, including nested formats
std::size_t ULogParser::type_size(const std::string& type, int depth) const {
    std::size_t basic = basic_size(type);
    if (basic > 0) return basic;

    auto it = formats_.find(type);
    if (it == formats_.end() || depth > 10) return 0;

    std::size_t total = 0;
    for (const Field& f : it->second.fields) {
        total += type_size(f.type, depth + 1) * static_cast<std::size_t>(f.array_length);
    }
    return total;
}

// Lists every basic value in a format along with its offset
void ULogParser::flatten(const std::string& format_name, const std::string& prefix,
                         std::size_t base, std::vector<FlatField>& out, int depth) const {
    auto it = formats_.find(format_name);
    if (it == formats_.end() || depth > 10) return;

    std::size_t offset = base;
    for (const Field& field : it->second.fields) {
        std::size_t element_size = type_size(field.type);
        bool is_padding = field.name.rfind("_padding", 0) == 0;

        if (!is_padding) {
            for (int i = 0; i < field.array_length; ++i) {
                std::string name = prefix + field.name;
                if (field.array_length > 1) name += "[" + std::to_string(i) + "]";
                std::size_t at = offset + static_cast<std::size_t>(i) * element_size;

                if (basic_size(field.type) > 0) {
                    out.push_back({name, field.type, at});
                } else {
                    flatten(field.type, name + ".", at, out, depth + 1);  // nested format
                }
            }
        }
        offset += element_size * static_cast<std::size_t>(field.array_length);
    }
}

// Matches the field names we asked for to their offsets
void ULogParser::link_request(Request& req, const std::string& format_name) {
    std::vector<FlatField> layout;
    flatten(format_name, "", 0, layout);

    for (const FlatField& f : layout) {
        if (f.name == "timestamp") {
            req.timestamp_offset = f.offset;
            break;
        }
    }

    req.fields.clear();
    for (const std::string& name : req.wanted) {
        bool found = false;
        for (const FlatField& f : layout) {
            if (f.name == name) {
                req.fields.push_back(f);
                req.data.values[name];  // creates an empty list for this field
                found = true;
                break;
            }
        }
        if (!found) {
            warnings_.push_back(format_name + ": field '" + name + "' not found");
        }
    }
}

void ULogParser::handle_data(const std::vector<std::uint8_t>& payload) {
    if (payload.size() < 2) return;
    std::uint16_t msg_id = read_u16(payload.data());

    auto it = subscriptions_.find(msg_id);
    if (it != subscriptions_.end()) it->second.data_count++;

    // Only decode topics we asked for
    auto a = active_.find(msg_id);
    if (a == active_.end()) return;
    Request& req = *a->second;

    const std::uint8_t* msg = payload.data() + 2;  // skip the 2-byte msg_id
    std::size_t len = payload.size() - 2;
    if (req.timestamp_offset + 8 > len) return;

    req.data.timestamps.push_back(read_u64(msg + req.timestamp_offset));
    for (const FlatField& f : req.fields) {
        double value = std::numeric_limits<double>::quiet_NaN();
        if (f.offset + basic_size(f.type) <= len) {
            value = decode_value(msg + f.offset, f.type);
        }
        req.data.values[f.name].push_back(value);
    }
}