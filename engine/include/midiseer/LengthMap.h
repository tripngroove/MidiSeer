#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace midiseer
{

// Positions and lengths are counted in ticks, 960 to a quarter note (beat).
inline constexpr int ticksPerBeat = 960;

// Notes that start within this many ticks of each other count as the same position (about a
// 1/48 of a beat, roughly 10 ms at 120 bpm). It absorbs rounding in the host's song position
// without mixing up neighbouring notes.
inline constexpr int matchTolerance = 20;

// One remembered note: where it starts (within the loop, or in the song) and how long it lasts.
struct LearnedNote
{
    std::int64_t tick = 0;
    std::int32_t length = 0;
    std::uint8_t pitch = 0;

    bool operator== (const LearnedNote&) const = default;
};

// The lengths MidiSeer has learned, looked up by start position and pitch.
//
// The audio thread uses it, so it never allocates once made: it holds up to `capacity` notes
// and refuses to learn more (learn returns false).
class LengthMap
{
public:
    static constexpr std::size_t defaultCapacity = 32768;

    explicit LengthMap (std::size_t capacity = defaultCapacity);

    // The length of the note with this pitch that starts nearest `tick`, if one starts within
    // matchTolerance of it.
    std::optional<std::int32_t> find (std::int64_t tick, int pitch) const;

    // Remembers a note's length, replacing the one at about the same position and pitch.
    // False when the map is full (and the note is new).
    bool learn (std::int64_t tick, int pitch, std::int32_t length);

    // Replaces every note with these (in any order), up to the capacity. Notes at the same
    // tick and pitch keep the last one.
    void assign (std::span<const LearnedNote> newNotes);

    // Removes the note at exactly this tick and pitch. False if there is none.
    bool forget (std::int64_t tick, int pitch);

    // Removes the notes starting from `from` up to (not including) `to` that `keep` turns down,
    // calling `removed` with each.
    template <typename Keep, typename Removed>
    void sweep (std::int64_t from, std::int64_t to, Keep keep, Removed removed)
    {
        auto first = std::lower_bound (notes.begin(), notes.end(), from, [] (const LearnedNote& n, std::int64_t t) { return n.tick < t; });
        auto out = first;
        auto it = first;
        for (; it != notes.end() && it->tick < to; ++it)
        {
            if (keep (*it))
                *out++ = *it;
            else
                removed (*it);
        }
        notes.erase (out, it);
    }

    void clear() { notes.clear(); }
    std::size_t size() const { return notes.size(); }
    std::size_t capacity() const { return maxNotes; }
    bool empty() const { return notes.empty(); }

    // Every note, sorted by position, then pitch.
    std::span<const LearnedNote> all() const { return notes; }

private:
    // The note with this pitch nearest `tick` within the tolerance, or end().
    std::vector<LearnedNote>::const_iterator nearest (std::int64_t tick, int pitch) const;

    std::vector<LearnedNote> notes;
    std::size_t maxNotes;
};

} // namespace midiseer
