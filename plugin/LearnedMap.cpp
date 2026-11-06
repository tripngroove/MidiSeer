#include "LearnedMap.h"

#include "midiseer/Seer.h"

namespace midiseer
{

LearnedMap::LearnedMap() : audio (std::make_unique<Numbered>())
{
}

LearnedMap::~LearnedMap() = default;

LengthMap& LearnedMap::beginBlock()
{
    if (hasPending.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType lock (pendingLock);
        if (lock.isLocked() && pending != nullptr)
        {
            std::swap (audio, pending); // the old map is freed later, off the audio thread
            hasPending = false;
        }
    }
    return audio->map;
}

void LearnedMap::learned (const LearnedNote& note)
{
    // When the queue is full (nothing has read it for a long while) the note is dropped from
    // the copy; the audio thread still knows it.
    if (queue.getFreeSpace() > 0)
        queue.write (1).forEach ([&] (int index) { queued[static_cast<size_t> (index)] = { note, audio->generation }; });
}

bool LearnedMap::sync()
{
    const std::scoped_lock lock (copyLock);
    queue.read (queue.getNumReady()).forEach ([this] (int index) {
        const auto& q = queued[static_cast<size_t> (index)];
        if (q.generation == copyGeneration && copy.learn (q.note.tick, q.note.pitch, q.note.length))
            changed = true;
    });
    return std::exchange (changed, false);
}

std::vector<LearnedNote> LearnedMap::notes()
{
    sync();
    const std::scoped_lock lock (copyLock);
    return { copy.all().begin(), copy.all().end() };
}

std::size_t LearnedMap::size()
{
    sync();
    const std::scoped_lock lock (copyLock);
    return copy.size();
}

void LearnedMap::replace (const std::vector<LearnedNote>& newNotes)
{
    auto next = std::make_unique<Numbered>();
    next->map.assign (newNotes);
    {
        const std::scoped_lock lock (copyLock);
        copy.assign (newNotes);
        next->generation = ++copyGeneration;
        changed = true;
    }

    // Swapped in at the audio thread's next block. Whatever was waiting before (or the map the
    // audio thread let go of) is freed here, at the end of this function.
    {
        const juce::SpinLock::ScopedLockType lock (pendingLock);
        std::swap (pending, next);
        hasPending = true;
    }
}

juce::String LearnedMap::toText()
{
    juce::MemoryOutputStream out;
    for (const auto& n : notes())
    {
        out.writeInt64 (n.tick);
        out.writeInt (n.length);
        out.writeByte (static_cast<char> (n.pitch));
    }
    return out.getMemoryBlock().toBase64Encoding();
}

void LearnedMap::fromText (const juce::String& text)
{
    juce::MemoryBlock data;
    std::vector<LearnedNote> loaded;
    if (text.isNotEmpty() && data.fromBase64Encoding (text))
    {
        juce::MemoryInputStream in (data, false);
        while (in.getNumBytesRemaining() >= 13)
        {
            LearnedNote n;
            n.tick = in.readInt64();
            n.length = in.readInt();
            n.pitch = static_cast<std::uint8_t> (in.readByte() & 0x7f);
            if (n.length > 0)
                loaded.push_back (n);
        }
    }
    replace (loaded);
}

std::optional<std::vector<LearnedNote>> readMidiFile (const juce::File& file, double loopBeats)
{
    juce::FileInputStream stream (file);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream))
        return std::nullopt;

    // Timings in ticks per quarter note; files timed in SMPTE frames are not musical positions.
    const double ticksPerQuarter = midi.getTimeFormat();
    if (ticksPerQuarter <= 0.0)
        return std::nullopt;

    std::vector<LearnedNote> notes;
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence track (*midi.getTrack (t));
        track.updateMatchedPairs();
        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            const auto& m = track.getEventPointer (i)->message;
            if (! m.isNoteOn())
                continue;
            const double start = m.getTimeStamp() / ticksPerQuarter;
            const double end = track.getTimeOfMatchingKeyUp (i) / ticksPerQuarter;
            const auto length = std::llround ((end - start) * ticksPerBeat);
            if (length > 0)
                notes.push_back ({ positionKey (start, loopBeats), static_cast<std::int32_t> (length), static_cast<std::uint8_t> (m.getNoteNumber()) });
        }
    }
    if (notes.empty())
        return std::nullopt;
    return notes;
}

} // namespace midiseer
