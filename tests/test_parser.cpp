#include "test_helpers.hpp"
#include "ulog_parser.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

std::string temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

}  // namespace

// ---------- Bad files ----------

TEST(ParserTest, RejectsFileWithWrongMagicBytes) {
    std::string path = temp_path("not_ulog.bin");
    {
        std::ofstream out(path, std::ios::binary);
        out << "this is definitely not a ulog file";
    }
    ULogParser parser;
    EXPECT_FALSE(parser.parse(path));
    EXPECT_NE(parser.error().find("magic"), std::string::npos);
}

TEST(ParserTest, RejectsFileThatIsTooShort) {
    std::string path = temp_path("too_short.bin");
    {
        std::ofstream out(path, std::ios::binary);
        out << "ULog";
    }
    ULogParser parser;
    EXPECT_FALSE(parser.parse(path));
}

TEST(ParserTest, RejectsMissingFile) {
    ULogParser parser;
    EXPECT_FALSE(parser.parse(temp_path("this_file_does_not_exist.ulg")));
}

// ---------- Header and formats ----------

TEST(ParserTest, ReadsStartTimeFromHeader) {
    ULogBuilder log(123456789);
    ULogParser parser;
    ASSERT_TRUE(parser.parse(log.save("start_time.ulg")));
    EXPECT_EQ(parser.start_time_us(), 123456789u);
}

TEST(ParserTest, ParsesFormatFields) {
    ULogBuilder log;
    log.add_format("test_msg:uint64_t timestamp;float value;float[3] xyz;");

    ULogParser parser;
    ASSERT_TRUE(parser.parse(log.save("format.ulg")));

    ASSERT_EQ(parser.formats().count("test_msg"), 1u);
    const Format& fmt = parser.formats().at("test_msg");
    ASSERT_EQ(fmt.fields.size(), 3u);
    EXPECT_EQ(fmt.fields[1].type, "float");
    EXPECT_EQ(fmt.fields[1].name, "value");
    EXPECT_EQ(fmt.fields[2].name, "xyz");
    EXPECT_EQ(fmt.fields[2].array_length, 3);
}

// ---------- Decoding values ----------

TEST(ParserTest, DecodesRequestedFields) {
    ULogBuilder log;
    log.add_format("test_msg:uint64_t timestamp;float value;float[3] xyz;");
    log.add_subscription(0, 0, "test_msg");

    std::vector<std::uint8_t> d;
    put_u64(d, 5000000);
    put_float(d, 1.5f);
    put_float(d, 0.0f);
    put_float(d, 0.0f);
    put_float(d, -2.25f);
    log.add_data(0, d);

    ULogParser parser;
    parser.request("test_msg", 0, {"value", "xyz[2]"});
    ASSERT_TRUE(parser.parse(log.save("decode.ulg")));

    const TopicData* data = parser.data("test_msg", 0);
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->timestamps.size(), 1u);
    EXPECT_EQ(data->timestamps[0], 5000000u);
    EXPECT_DOUBLE_EQ(data->values.at("value")[0], 1.5);
    EXPECT_DOUBLE_EQ(data->values.at("xyz[2]")[0], -2.25);
}

TEST(ParserTest, DecodesNegativeIntegers) {
    ULogBuilder log;
    log.add_format("signed_msg:uint64_t timestamp;int16_t a;int32_t b;");
    log.add_subscription(0, 0, "signed_msg");

    std::vector<std::uint8_t> d;
    put_u64(d, 1);
    put_u16(d, static_cast<std::uint16_t>(-300));
    put_u32(d, static_cast<std::uint32_t>(-70000));
    log.add_data(0, d);

    ULogParser parser;
    parser.request("signed_msg", 0, {"a", "b"});
    ASSERT_TRUE(parser.parse(log.save("signed.ulg")));

    const TopicData* data = parser.data("signed_msg", 0);
    ASSERT_NE(data, nullptr);
    EXPECT_DOUBLE_EQ(data->values.at("a")[0], -300.0);
    EXPECT_DOUBLE_EQ(data->values.at("b")[0], -70000.0);
}

TEST(ParserTest, SkipsPaddingButCountsItsBytes) {
    ULogBuilder log;
    log.add_format("pad_msg:uint64_t timestamp;uint8_t a;uint8_t[3] _padding0;float b;");
    log.add_subscription(0, 0, "pad_msg");

    std::vector<std::uint8_t> d;
    put_u64(d, 1);
    put_u8(d, 7);
    put_u8(d, 0);
    put_u8(d, 0);
    put_u8(d, 0);
    put_float(d, 4.0f);
    log.add_data(0, d);

    ULogParser parser;
    parser.request("pad_msg", 0, {"a", "b"});
    ASSERT_TRUE(parser.parse(log.save("padding.ulg")));

    const TopicData* data = parser.data("pad_msg", 0);
    ASSERT_NE(data, nullptr);
    EXPECT_DOUBLE_EQ(data->values.at("a")[0], 7.0);
    EXPECT_DOUBLE_EQ(data->values.at("b")[0], 4.0);
}

TEST(ParserTest, DecodesNestedFormats) {
    ULogBuilder log;
    log.add_format("point:float x;float y;");
    log.add_format("path:uint64_t timestamp;point[2] pts;");
    log.add_subscription(0, 0, "path");

    std::vector<std::uint8_t> d;
    put_u64(d, 1);
    put_float(d, 1.0f);  // pts[0].x
    put_float(d, 2.0f);  // pts[0].y
    put_float(d, 3.0f);  // pts[1].x
    put_float(d, 4.0f);  // pts[1].y
    log.add_data(0, d);

    ULogParser parser;
    parser.request("path", 0, {"pts[0].x", "pts[1].y"});
    ASSERT_TRUE(parser.parse(log.save("nested.ulg")));

    const TopicData* data = parser.data("path", 0);
    ASSERT_NE(data, nullptr);
    EXPECT_DOUBLE_EQ(data->values.at("pts[0].x")[0], 1.0);
    EXPECT_DOUBLE_EQ(data->values.at("pts[1].y")[0], 4.0);
}

// ---------- Unusual situations ----------

TEST(ParserTest, HandlesLogCutOffMidMessage) {
    ULogBuilder log;
    log.add_format("test_msg:uint64_t timestamp;float value;");
    log.add_subscription(0, 0, "test_msg");

    for (int i = 0; i < 2; ++i) {
        std::vector<std::uint8_t> d;
        put_u64(d, 1000000 * (i + 1));
        put_float(d, 1.0f);
        log.add_data(0, d);
    }
    log.cut_last(3);  // chop the end off the second message

    ULogParser parser;
    parser.request("test_msg", 0, {"value"});
    ASSERT_TRUE(parser.parse(log.save("truncated.ulg")));

    EXPECT_TRUE(parser.truncated());
    const TopicData* data = parser.data("test_msg", 0);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->timestamps.size(), 1u);  // only the complete message was kept
}

TEST(ParserTest, WarnsAboutMissingField) {
    ULogBuilder log;
    log.add_format("test_msg:uint64_t timestamp;float value;");
    log.add_subscription(0, 0, "test_msg");

    ULogParser parser;
    parser.request("test_msg", 0, {"does_not_exist"});
    ASSERT_TRUE(parser.parse(log.save("missing_field.ulg")));

    ASSERT_EQ(parser.warnings().size(), 1u);
    EXPECT_NE(parser.warnings()[0].find("does_not_exist"), std::string::npos);
}

TEST(ParserTest, OnlyDecodesRequestedTopics) {
    ULogBuilder log;
    log.add_format("wanted:uint64_t timestamp;float value;");
    log.add_format("other:uint64_t timestamp;float value;");
    log.add_subscription(0, 0, "wanted");
    log.add_subscription(0, 1, "other");

    std::vector<std::uint8_t> d;
    put_u64(d, 1);
    put_float(d, 2.0f);
    log.add_data(0, d);
    log.add_data(1, d);

    ULogParser parser;
    parser.request("wanted", 0, {"value"});
    ASSERT_TRUE(parser.parse(log.save("only_requested.ulg")));

    EXPECT_NE(parser.data("wanted", 0), nullptr);
    EXPECT_EQ(parser.data("other", 0), nullptr);
    EXPECT_EQ(parser.subscriptions().at(1).data_count, 1);  // still counted
}