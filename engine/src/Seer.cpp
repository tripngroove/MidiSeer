#include "midiseer/Seer.h"

#include <algorithm>
#include <cmath>

namespace midiseer
{

namespace
{
constexpr int noteOffType = 0x80, noteOnType = 0x90, controllerType = 0xb0;
constexpr int allSoundOffCc = 120, allNotesOffCc = 123;
constexpr int masterChannel = 1, firstMemberChannel = 2, lastMemberChannel = 16;

constexpr double toleranceBeats = static_cast<double> (matchTolerance) / ticksPerBeat;

std::uint8_t statusByte (int type, int channel)
{
    return static_cast<std::uint8_t> (type | (channel - 1));
}
} // namespace

double rampShape (double p, double curve)
{
    p = std::clamp (p, 0.0, 1.0);
    // An exponential bend; at 0 it is a straight line.
    const double k = std::clamp (curve, -1.0, 1.0) * 5.0;
    if (std::abs (k) < 1.0e-4)
        return p;
    return std::expm1 (k * p) / std::expm1 (k);
}

void Seer::prepare (double sampleRate)
{
    const double rate = std::max (sampleRate, 1000.0);
    gapSamples = std::max<std::int64_t> (1, std::llround (rate * minGapMs / 1000.0));
    glideSamples = std::max<std::int64_t> (1, std::llround (rate * glideMs / 1000.0));

    voices = {};
    held = {};
    newestVoice = -1;
    guessBeats = firstGuessBeats;
    block = {};
    blockStartSample = 0;
    blockStartBeat = 0.0;
    renderedTo = 0;
    lastStatus = {};
    sweptTo = 0.0;
    expectedSongBeat = 0.0;
    wasPlaying = false;
    numStarted = 0;
}

std::int64_t positionKey (double songBeat, double loopBeats)
{
    const auto tick = std::llround (songBeat * ticksPerBeat);
    const auto loop = std::llround (loopBeats * ticksPerBeat);
    if (loop <= 0)
        return tick;
    return ((tick % loop) + loop) % loop;
}

std::int64_t Seer::sampleAtBeat (double beat) const
{
    return blockStartSample + static_cast<std::int64_t> (std::ceil ((beat - blockStartBeat) * block.samplesPerBeat));
}

void Seer::beginBlock (const BlockInfo& info)
{
    block = info;
    block.samplesPerBeat = std::max (block.samplesPerBeat, 1.0);

    // Playback started or jumped: tidy the map only from here on.
    const bool playingOn = info.playing && wasPlaying && std::abs (info.songBeat - expectedSongBeat) < toleranceBeats;
    if (info.playing && ! playingOn)
    {
        sweptTo = info.songBeat;
        numStarted = 0;
    }
}

void Seer::endBlock (Output& out)
{
    const auto end = sampleOf (block.numSamples);
    advanceTo (end, out);

    if (newestVoice >= 0)
    {
        const auto& v = voices[static_cast<size_t> (newestVoice)];
        lastStatus.active = true;
        lastStatus.moving = v.phase == Phase::ramping || v.phase == Phase::gliding;
        lastStatus.known = v.known;
        lastStatus.progress = v.phase == Phase::ramping ? std::clamp ((beatAt (end) - v.startBeat) / v.lengthBeats, 0.0, 1.0) : 1.0;
        lastStatus.value = std::max (v.lastValue, 0);
    }

    // The sweep stays a little behind the playhead, so a note that starts a few ticks late still
    // counts as its learned one.
    if (block.playing)
    {
        expectedSongBeat = block.songBeat + block.numSamples / block.samplesPerBeat;
        sweepTo (expectedSongBeat - toleranceBeats, out);
    }
    wasPlaying = block.playing;

    blockStartSample = end;
    blockStartBeat += block.numSamples / block.samplesPerBeat;
}

void Seer::sweepTo (double songBeat, Output& out)
{
    if (map == nullptr || songBeat <= sweptTo)
        return;

    const std::int64_t loop = std::llround (settings.loopBeats * ticksPerBeat);
    const auto distance = [loop] (std::int64_t a, std::int64_t b) -> std::int64_t {
        const auto d = a > b ? a - b : b - a;
        return loop > 0 ? std::min (d, loop - d) : d;
    };
    const auto keep = [&] (const LearnedNote& n) {
        for (std::size_t i = 0; i < numStarted; ++i)
            if (started[i].pitch == n.pitch && distance (started[i].key, n.tick) <= matchTolerance)
                return true;
        return false;
    };
    const auto removed = [&out] (const LearnedNote& n) { out.changes.push_back ({ n, true }); };

    const auto from = positionKey (sweptTo), to = positionKey (songBeat);
    if (loop <= 0)
        map->sweep (from, to, keep, removed);
    else if (songBeat - sweptTo >= settings.loopBeats)
        map->sweep (0, loop, keep, removed);
    else if (from <= to)
        map->sweep (from, to, keep, removed);
    else // across the loop's end
    {
        map->sweep (from, loop, keep, removed);
        map->sweep (0, to, keep, removed);
    }
    sweptTo = songBeat;

    // Notes that started before the sweep can no longer save anything.
    std::size_t kept = 0;
    for (std::size_t i = 0; i < numStarted; ++i)
        if (started[i].songBeat >= sweptTo - 2.0 * toleranceBeats)
            started[kept++] = started[i];
    numStarted = kept;
}

void Seer::handle (const MidiEvent& in, Output& out)
{
    const auto t = sampleOf (std::clamp (in.sample, 0, block.numSamples));
    advanceTo (t, out);

    const int type = in.type();
    if (type == noteOnType && in.data2 > 0)
        noteOn (t, in, out);
    else if (type == noteOffType || type == noteOnType)
        noteOff (t, in, out);
    else if (type == controllerType && (in.data1 == allNotesOffCc || in.data1 == allSoundOffCc))
        allNotesOff (t, in, out);
    else
        out.events.push_back (in);
}

int Seer::memberChannelFor (int inChannel) const
{
    if (inChannel != masterChannel)
        return inChannel; // already MPE

    // The free channel that has been free longest, else the one whose last note is oldest.
    int best = -1;
    for (const bool needFree : { true, false })
    {
        for (int ch = firstMemberChannel; ch <= lastMemberChannel; ++ch)
        {
            const auto& v = voices[static_cast<size_t> (ch - 1)];
            if (needFree && v.heldNotes > 0)
                continue;
            if (best < 0 || v.lastUsed < voices[static_cast<size_t> (best - 1)].lastUsed)
                best = ch;
        }
        if (best >= 0)
            break;
    }
    return best;
}

void Seer::noteOn (std::int64_t t, const MidiEvent& in, Output& out)
{
    const int inChannel = in.channel();
    const int pitch = in.data1 & 0x7f;
    const int outChannel = settings.mpe ? memberChannelFor (inChannel) : inChannel;
    const int voiceIndex = settings.mpe ? outChannel - 1 : 0;
    auto& v = voices[static_cast<size_t> (voiceIndex)];

    // A key struck again without a note off first: it is the same note going on.
    auto& h = heldNote (inChannel, pitch);
    if (h.on)
    {
        voices[h.voice].heldNotes = std::max (0, voices[h.voice].heldNotes - 1);
        if (h.outChannel != outChannel) // it moves channel, so end it on the old one
            out.events.push_back ({ in.sample, statusByte (noteOffType, h.outChannel), in.data1, 0 });
    }

    const double now = beatAt (t);
    h = Held { true, block.playing, now, block.playing ? block.songBeat + (now - blockStartBeat) : 0.0,
               static_cast<std::uint8_t> (outChannel), static_cast<std::uint8_t> (voiceIndex) };

    // Remember it started, for tidying the map. (When full, the oldest goes.)
    if (h.positioned)
    {
        if (numStarted == started.size())
        {
            std::move (started.begin() + 1, started.end(), started.begin());
            --numStarted;
        }
        started[numStarted++] = { h.songBeat, positionKey (h.songBeat), static_cast<std::uint8_t> (pitch) };
    }

    // How long it will last: learned, or a guess.
    double length = guessBeats;
    bool known = false;
    if (map != nullptr && h.positioned)
        if (const auto learned = map->find (positionKey (h.songBeat), pitch))
        {
            length = static_cast<double> (*learned) / ticksPerBeat;
            known = true;
        }

    const auto& owner = heldNote (std::max (v.ownerChannel, 1), std::max (v.ownerPitch, 0));
    const bool ownerHeld = v.ownerPitch >= 0 && owner.on && owner.voice == voiceIndex && &owner != &h;
    const bool sameStart = v.phase != Phase::idle && ownerHeld && v.channel == outChannel && std::abs (now - v.startBeat) <= toleranceBeats;

    if (sameStart)
    {
        // A chord: follow its longest note.
        if (length > v.lengthBeats)
        {
            v.lengthBeats = length;
            v.known = known;
            v.ownerChannel = inChannel;
            v.ownerPitch = pitch;
        }
    }
    else
    {
        v.phase = Phase::ramping;
        v.channel = outChannel;
        v.ownerChannel = inChannel;
        v.ownerPitch = pitch;
        v.startBeat = now;
        v.lengthBeats = std::max (length, 1.0 / ticksPerBeat);
        v.known = known;
        send (v, t, settings.startValue, out); // always, so the note starts from it
        v.nextCheck = t + gapSamples;
    }

    ++v.heldNotes;
    v.lastUsed = t;
    newestVoice = voiceIndex;

    out.events.push_back ({ in.sample, statusByte (noteOnType, outChannel), in.data1, in.data2 });
}

void Seer::noteOff (std::int64_t t, const MidiEvent& in, Output& out)
{
    const int inChannel = in.channel();
    const int pitch = in.data1 & 0x7f;
    auto& h = heldNote (inChannel, pitch);
    if (! h.on)
    {
        out.events.push_back (in); // not one we saw start
        return;
    }
    h.on = false;

    // Learn its length. Only while the song plays from start to end, so a note cut short by
    // stopping is not remembered.
    const double length = beatAt (t) - h.onBeat;
    if (length > 0.0)
    {
        guessBeats = length;
        if (map != nullptr && h.positioned && block.playing)
        {
            const LearnedNote note { positionKey (h.songBeat), static_cast<std::int32_t> (std::llround (length * ticksPerBeat)),
                                     static_cast<std::uint8_t> (pitch) };
            if (note.length > 0 && map->learn (note.tick, note.pitch, note.length))
                out.changes.push_back ({ note });
        }
    }

    auto& v = voices[h.voice];
    v.heldNotes = std::max (0, v.heldNotes - 1);
    v.lastUsed = t;
    if (v.ownerChannel == inChannel && v.ownerPitch == pitch)
        release (v, t, out);

    out.events.push_back ({ in.sample, statusByte (in.type(), h.outChannel), in.data1, in.data2 });
}

void Seer::allNotesOff (std::int64_t t, const MidiEvent& in, Output& out)
{
    // Notes moved to other channels for MPE would be missed by the message, so end them here.
    const int inChannel = in.channel();
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        auto& h = heldNote (inChannel, pitch);
        if (! h.on)
            continue;
        h.on = false;
        auto& v = voices[h.voice];
        v.heldNotes = std::max (0, v.heldNotes - 1);
        if (v.ownerChannel == inChannel && v.ownerPitch == pitch)
            release (v, t, out);
        if (h.outChannel != inChannel)
            out.events.push_back ({ in.sample, statusByte (noteOffType, h.outChannel), static_cast<std::uint8_t> (pitch), 0 });
    }
    out.events.push_back (in);
}

void Seer::release (Voice& v, std::int64_t t, Output& out)
{
    v.ownerPitch = -1;
    if (v.phase != Phase::ramping)
        return;

    v.glideFrom = valueAt (v, t);
    if (std::lround (v.glideFrom) == settings.endValue)
    {
        v.phase = Phase::holding;
        if (v.lastValue != settings.endValue)
            send (v, t, settings.endValue, out);
        return;
    }
    v.phase = Phase::gliding;
    v.glideStart = t;
}

double Seer::valueAt (const Voice& v, std::int64_t t) const
{
    const double start = settings.startValue, end = settings.endValue;
    switch (v.phase)
    {
        case Phase::ramping:
            return start + (end - start) * rampShape ((beatAt (t) - v.startBeat) / v.lengthBeats, settings.curve);
        case Phase::gliding:
            return v.glideFrom + (end - v.glideFrom) * std::clamp (static_cast<double> (t - v.glideStart) / static_cast<double> (glideSamples), 0.0, 1.0);
        case Phase::idle:
        case Phase::holding:
            break;
    }
    return end;
}

void Seer::send (Voice& v, std::int64_t t, int value, Output& out)
{
    value = std::clamp (value, 0, 127);
    out.events.push_back ({ static_cast<int> (t - blockStartSample), statusByte (controllerType, v.channel),
                            static_cast<std::uint8_t> (std::clamp (settings.ccNumber, 0, 127)), static_cast<std::uint8_t> (value) });
    v.lastValue = value;
}

void Seer::render (Voice& v, std::int64_t from, std::int64_t to, Output& out)
{
    while (v.phase == Phase::ramping || v.phase == Phase::gliding)
    {
        const auto finish = v.phase == Phase::ramping ? sampleAtBeat (v.startBeat + v.lengthBeats) : v.glideStart + glideSamples;
        const auto check = std::max (v.nextCheck, from);

        if (finish <= check && finish < to)
        {
            // It arrives before the next check: land on the end value exactly then.
            v.phase = Phase::holding;
            if (v.lastValue != settings.endValue)
                send (v, std::max (finish, from), settings.endValue, out);
            return;
        }
        if (check >= to)
            return;

        if (const int value = static_cast<int> (std::lround (valueAt (v, check))); value != v.lastValue)
            send (v, check, value, out);
        v.nextCheck = check + gapSamples;
    }
}

void Seer::advanceTo (std::int64_t t, Output& out)
{
    if (t <= renderedTo)
        return;

    const auto first = out.events.size();
    for (auto& v : voices)
        render (v, renderedTo, t, out);
    renderedTo = t;

    // Voices render one after another; put their CCs back in time order.
    std::stable_sort (out.events.begin() + static_cast<std::ptrdiff_t> (first), out.events.end(),
                      [] (const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });
}

} // namespace midiseer
