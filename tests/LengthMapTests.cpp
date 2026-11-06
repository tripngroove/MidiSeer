#include "midiseer/LengthMap.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace midiseer;

TEST_CASE ("the map finds a note near where it was learned")
{
    LengthMap map;
    REQUIRE (map.learn (960, 60, 480));

    CHECK (map.find (960, 60) == 480);
    CHECK (map.find (960 + matchTolerance, 60) == 480);
    CHECK (map.find (960 - matchTolerance, 60) == 480);
    CHECK_FALSE (map.find (960 + matchTolerance + 1, 60).has_value());
    CHECK_FALSE (map.find (960, 61).has_value()); // another pitch
}

TEST_CASE ("learning the same note again replaces it")
{
    LengthMap map;
    map.learn (960, 60, 480);
    map.learn (965, 60, 240); // a few ticks off: the same note

    REQUIRE (map.size() == 1);
    CHECK (map.all()[0] == LearnedNote { 965, 240, 60 });
}

TEST_CASE ("the map keeps notes in order and picks the nearest")
{
    LengthMap map;
    map.learn (1920, 64, 100);
    map.learn (0, 60, 200);
    map.learn (1920, 60, 300);
    map.learn (1930, 67, 400);

    const auto all = map.all();
    REQUIRE (all.size() == 4);
    CHECK (all[0].tick == 0);
    CHECK (all[1] == LearnedNote { 1920, 300, 60 });
    CHECK (all[2] == LearnedNote { 1920, 100, 64 });
    CHECK (all[3].pitch == 67);

    CHECK (map.find (1925, 64) == 100);
    CHECK (map.find (1925, 67) == 400);
}

TEST_CASE ("a full map refuses new notes but still updates known ones")
{
    LengthMap map (2);
    CHECK (map.learn (0, 60, 10));
    CHECK (map.learn (960, 60, 10));
    CHECK_FALSE (map.learn (1920, 60, 10));
    CHECK (map.learn (960, 60, 20));
    CHECK (map.find (960, 60) == 20);
    CHECK (map.size() == 2);
}

TEST_CASE ("a map can be filled all at once")
{
    LengthMap map (3);
    const std::vector<LearnedNote> notes { { 960, 10, 60 }, { 0, 20, 62 }, { 960, 30, 60 }, { 1920, 40, 64 }, { 2880, 50, 65 } };
    map.assign (notes);

    // Sorted, the repeated note's last length kept, and only as many as fit.
    const std::vector<LearnedNote> expected { { 0, 20, 62 }, { 960, 30, 60 } };
    CHECK (std::vector<LearnedNote> (map.all().begin(), map.all().end()) == expected);
}

TEST_CASE ("notes can be forgotten one by one or by sweeping a stretch")
{
    LengthMap map;
    map.learn (0, 60, 10);
    map.learn (960, 60, 10);
    map.learn (960, 64, 10);
    map.learn (1920, 60, 10);

    CHECK (map.forget (960, 64));
    CHECK_FALSE (map.forget (960, 64));

    std::vector<LearnedNote> removed;
    map.sweep (0, 1920, [] (const LearnedNote& n) { return n.tick == 0; }, [&] (const LearnedNote& n) { removed.push_back (n); });
    CHECK (removed == std::vector<LearnedNote> { { 960, 10, 60 } });
    CHECK (map.size() == 2); // 0 kept, 1920 outside the stretch
}
