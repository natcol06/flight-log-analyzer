#include "checks.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace {

// Turns a list of seconds into microsecond timestamps
std::vector<std::uint64_t> seconds(const std::vector<double>& s) {
    std::vector<std::uint64_t> out;
    for (double v : s) out.push_back(static_cast<std::uint64_t>(v * 1e6));
    return out;
}

const double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

// ---------- find_events ----------

TEST(FindEventsTest, FindsOneEventAboveLimit) {
    InAir always_flying(nullptr);
    auto times = seconds({0, 1, 2, 3, 4, 5, 6});
    std::vector<double> values = {1, 1, 12, 15, 11, 1, 1};

    auto events = find_events("Test", "u", times, values,
                              Direction::Above, 10.0, always_flying, 0, 2.0);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_DOUBLE_EQ(events[0].start_s, 2.0);
    EXPECT_DOUBLE_EQ(events[0].end_s, 4.0);
    EXPECT_DOUBLE_EQ(events[0].worst, 15.0);
    EXPECT_EQ(events[0].samples, 3);
}

TEST(FindEventsTest, MergesBadReadingsCloseTogether) {
    InAir always_flying(nullptr);
    auto times = seconds({0, 1, 2.5, 3.5});
    std::vector<double> values = {1, 12, 13, 1};  // bad readings 1.5 s apart

    auto events = find_events("Test", "u", times, values,
                              Direction::Above, 10.0, always_flying, 0, 2.0);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_DOUBLE_EQ(events[0].start_s, 1.0);
    EXPECT_DOUBLE_EQ(events[0].end_s, 2.5);
}

TEST(FindEventsTest, SplitsBadReadingsFarApart) {
    InAir always_flying(nullptr);
    auto times = seconds({0, 1, 5, 6});
    std::vector<double> values = {1, 12, 13, 1};  // bad readings 4 s apart

    auto events = find_events("Test", "u", times, values,
                              Direction::Above, 10.0, always_flying, 0, 2.0);

    EXPECT_EQ(events.size(), 2u);
}

TEST(FindEventsTest, BelowLimitTracksLowestValue) {
    InAir always_flying(nullptr);
    auto times = seconds({0, 1, 2, 3, 4});
    std::vector<double> values = {5, 2, 1, 3, 5};

    auto events = find_events("Test", "V", times, values,
                              Direction::Below, 4.0, always_flying, 0, 2.0);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_DOUBLE_EQ(events[0].worst, 1.0);
    EXPECT_DOUBLE_EQ(events[0].start_s, 1.0);
    EXPECT_DOUBLE_EQ(events[0].end_s, 3.0);
}

TEST(FindEventsTest, SkipsMissingValues) {
    InAir always_flying(nullptr);
    auto times = seconds({0, 1, 2, 3, 4});
    std::vector<double> values = {1, kNaN, 12, kNaN, 1};

    auto events = find_events("Test", "u", times, values,
                              Direction::Above, 10.0, always_flying, 0, 2.0);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].samples, 1);
}

TEST(FindEventsTest, IgnoresReadingsOnTheGround) {
    TopicData land;
    land.timestamps = seconds({0, 5});
    land.values["landed"] = {1, 0};  // on the ground until 5 s, then flying
    InAir in_air(&land);

    auto times = seconds({2, 6, 7});
    std::vector<double> values = {20, 20, 20};

    auto events = find_events("Test", "u", times, values,
                              Direction::Above, 10.0, in_air, 0, 2.0);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_DOUBLE_EQ(events[0].start_s, 6.0);  // the reading at 2 s was ignored
}

// ---------- InAir ----------

TEST(InAirTest, UsesMostRecentLandedReading) {
    TopicData land;
    land.timestamps = seconds({0, 5, 10});
    land.values["landed"] = {1, 0, 1};
    InAir in_air(&land);

    EXPECT_FALSE(in_air.at(2000000));   // 2 s: still on the ground
    EXPECT_TRUE(in_air.at(6000000));    // 6 s: flying
    EXPECT_FALSE(in_air.at(11000000));  // 11 s: landed again
    EXPECT_DOUBLE_EQ(in_air.airborne_seconds(), 5.0);
}

TEST(InAirTest, NoLandingDataMeansCheckEverything) {
    InAir in_air(nullptr);
    EXPECT_TRUE(in_air.at(123));
}