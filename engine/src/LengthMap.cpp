#include "midiseer/LengthMap.h"

#include <algorithm>

namespace midiseer
{

namespace
{
bool before (const LearnedNote& a, const LearnedNote& b)
{
    return a.tick != b.tick ? a.tick < b.tick : a.pitch < b.pitch;
}
} // namespace

LengthMap::LengthMap (std::size_t capacity) : maxNotes (capacity)
{
    notes.reserve (maxNotes);
}

std::vector<LearnedNote>::const_iterator LengthMap::nearest (std::int64_t tick, int pitch) const
{
    auto best = notes.end();
    std::int64_t bestDistance = matchTolerance + 1;

    const auto first = std::lower_bound (notes.begin(), notes.end(), tick - matchTolerance,
                                         [] (const LearnedNote& n, std::int64_t t) { return n.tick < t; });
    for (auto it = first; it != notes.end() && it->tick <= tick + matchTolerance; ++it)
    {
        if (it->pitch != pitch)
            continue;
        const auto distance = it->tick > tick ? it->tick - tick : tick - it->tick;
        if (distance < bestDistance)
        {
            best = it;
            bestDistance = distance;
        }
    }
    return best;
}

std::optional<std::int32_t> LengthMap::find (std::int64_t tick, int pitch) const
{
    if (const auto it = nearest (tick, pitch); it != notes.end())
        return it->length;
    return std::nullopt;
}

bool LengthMap::learn (std::int64_t tick, int pitch, std::int32_t length)
{
    const LearnedNote note { tick, length, static_cast<std::uint8_t> (pitch) };

    if (const auto it = nearest (tick, pitch); it != notes.end())
    {
        // The same note, perhaps moved by a few ticks: take its newest position and length,
        // keeping the order.
        const auto index = it - notes.begin();
        notes.erase (notes.begin() + index);
        notes.insert (std::upper_bound (notes.begin(), notes.end(), note, before), note);
        return true;
    }

    if (notes.size() >= maxNotes)
        return false;

    notes.insert (std::upper_bound (notes.begin(), notes.end(), note, before), note);
    return true;
}

bool LengthMap::forget (std::int64_t tick, int pitch)
{
    const LearnedNote probe { tick, 0, static_cast<std::uint8_t> (pitch) };
    const auto it = std::lower_bound (notes.begin(), notes.end(), probe, before);
    if (it == notes.end() || it->tick != tick || it->pitch != pitch)
        return false;
    notes.erase (it);
    return true;
}

void LengthMap::assign (std::span<const LearnedNote> newNotes)
{
    notes.assign (newNotes.begin(), newNotes.begin() + static_cast<std::ptrdiff_t> (std::min (newNotes.size(), maxNotes)));
    std::stable_sort (notes.begin(), notes.end(), before);

    // Of notes at the same place, the last one given wins.
    const auto same = [] (const LearnedNote& a, const LearnedNote& b) { return a.tick == b.tick && a.pitch == b.pitch; };
    auto out = notes.begin();
    for (auto it = notes.begin(); it != notes.end(); ++it)
    {
        if (out != notes.begin() && same (*(out - 1), *it))
            *(out - 1) = *it;
        else
            *out++ = *it;
    }
    notes.erase (out, notes.end());
}

} // namespace midiseer
