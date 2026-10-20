#include "Devices.hpp"

#include "DeviceTypes.hpp"
#include "NoteOperators.hpp"

#include <algorithm>

namespace tessera::chain {

namespace {

// End messages may use the last slots, which notes can't take, so a full chain still lets every note end.
constexpr int kEndReserve = 32;
constexpr uint32_t kVelocitySalt = 0x5E10C17Fu;

const Scale& chromatic() {
    static const Scale s;
    return s;
}

const std::vector<Scale>& fixedKeys() {
    static const std::vector<Scale> table = [] {
        std::vector<Scale> keys;
        for (const ScaleDef& def : builtinScales())
            for (int root = 0; root < 12; root++)
                keys.emplace_back(root, def.mask);
        return keys;
    }();
    return table;
}

float shown(const DeviceSlot& d, int k) {
    const devices::ParamInfo* p = devices::paramInfo(d.type, k);
    return p && d.knobs ? devices::display(*p, d.knobs[k]) : 0.f;
}

int choiceOf(const DeviceSlot& d, int k) { return int(shown(d, k)); }

uint64_t noteKey(const ChainNote& n) {
    return uint64_t(std::llround(n.origin * 16)) ^ (uint64_t(uint8_t(n.pitch)) << 56);
}

} // namespace

const Scale& Context::keyOf(uint8_t key) const {
    if (key > 0 && clipKeys && key <= clipKeys->size())
        return (*clipKeys)[key - 1];
    return songKey ? *songKey : chromatic();
}

const Scale& fixedKey(int root, int scaleIndex) {
    const std::vector<Scale>& keys = fixedKeys();
    int scales = int(keys.size() / 12);
    if (scales == 0)
        return chromatic();
    size_t i = size_t(std::clamp(scaleIndex, 0, scales - 1) * 12 + std::clamp(root, 0, 11));
    return keys[i];
}

void warmUp() {
    chromatic();
    warmUpChords();
    fixedKeys();
    for (int t = 0; t < kDeviceTypeCount; t++)
        devices::deviceInfo(DeviceType(t));
    for (int t = 0; t < kModulatorTypeCount; t++)
        devices::modulatorInfo(ModulatorType(t));
}

// ---- The message queue ----------------------------------------------------------------------------------------

void Chain::clear() {
    count = 0;
    for (DeviceState& s : states) {
        s.count = 0;
        s.heldCount = 0;
        s.running = false;
        s.steps = 0;
    }
}

void Chain::push(const ChainNote& note) {
    if (count >= kMaxMessages - kEndReserve) {
        overflows++;
        return;
    }
    messages[size_t(count++)] = {note.start, seq++, 0, Kind::On, 0, note};
}

void Chain::end(uint32_t id, double at, Output& out) {
    ChainNote n;
    n.id = id;
    post(0, Kind::End, n, at, out);
}

void Chain::post(int stage, Kind kind, const ChainNote& note, double time, Output& out, uint8_t step) {
    int limit =
        kind == Kind::On || kind == Kind::Echo || kind == Kind::Tick ? kMaxMessages - kEndReserve : kMaxMessages;
    if (count >= limit) {
        overflows++;
        if (kind == Kind::End || kind == Kind::Expire)
            out.end(note.id); // too early beats never
        return;
    }
    messages[size_t(count++)] = {time, seq++, uint8_t(stage), kind, step, note};
}

double Chain::nextDue() const {
    double t = INFINITY;
    for (int i = 0; i < count; i++)
        t = std::min(t, messages[size_t(i)].time);
    return t;
}

void Chain::run(double until, const Context& context, Output& out) {
    for (;;) {
        int best = -1;
        for (int i = 0; i < count; i++) {
            const Message& m = messages[size_t(i)];
            if (m.time >= until)
                continue;
            if (best < 0 || m.time < messages[size_t(best)].time ||
                (m.time == messages[size_t(best)].time && m.seq < messages[size_t(best)].seq))
                best = i;
        }
        if (best < 0)
            return;
        Message m = messages[size_t(best)];
        messages[size_t(best)] = messages[size_t(--count)];
        dispatch(m, context, out);
    }
}

void Chain::dispatch(const Message& m, const Context& c, Output& out) {
    if (m.stage >= c.count) {
        if (m.kind == Kind::On)
            out.on(m.note);
        else
            out.end(m.note.id);
        return;
    }
    const DeviceSlot& d = c.devices[m.stage];
    if (!d.on) {
        forward(m, out);
        return;
    }
    switch (d.type) {
    case DeviceType::Transpose:
        transpose(m, d, c, out);
        break;
    case DeviceType::Scale:
        scale(m, d, c, out);
        break;
    case DeviceType::Velocity:
        velocity(m, d, c, out);
        break;
    case DeviceType::NoteLength:
        noteLength(m, d, c, out);
        break;
    case DeviceType::Chord:
        chord(m, d, c, out);
        break;
    case DeviceType::Arpeggiator:
        arpeggiator(m, d, c, out);
        break;
    case DeviceType::Echo:
        echo(m, d, c, out);
        break;
    case DeviceType::Random:
        random(m, d, c, out);
        break;
    case DeviceType::Humanize:
        humanize(m, d, c, out);
        break;
    case DeviceType::NoteFilter:
        noteFilter(m, d, c, out);
        break;
    default:
        forward(m, out);
        break;
    }
}

void Chain::forward(const Message& m, Output& out) {
    // A device's own reminders mean nothing to the next one: an expiry becomes a plain end, clock ticks and echo
    // repeats stop.
    if (m.kind == Kind::Tick || m.kind == Kind::Echo)
        return;
    post(m.stage + 1, m.kind == Kind::Expire ? Kind::End : m.kind, m.note, m.time, out);
}

void Chain::remember(int stage, const OpenNote& note, double now, Output& out) {
    DeviceState& s = states[size_t(stage)];
    if (s.count == kMaxOpenNotes) {
        // A legato note would never get its end otherwise. The others still get theirs: it passes through.
        const OpenNote& oldest = s.open[0];
        if (oldest.action == Action::Legato) {
            ChainNote n;
            n.id = oldest.id;
            post(stage + 1, Kind::End, n, now, out);
        }
        std::move(s.open.begin() + 1, s.open.begin() + s.count, s.open.begin());
        s.count--;
    }
    s.open[size_t(s.count++)] = note;
}

bool Chain::forget(int stage, uint32_t id, OpenNote* found) {
    DeviceState& s = states[size_t(stage)];
    for (int i = 0; i < s.count; i++) {
        if (s.open[size_t(i)].id != id)
            continue;
        if (found)
            *found = s.open[size_t(i)];
        std::move(s.open.begin() + i + 1, s.open.begin() + s.count, s.open.begin() + i);
        s.count--;
        return true;
    }
    return false;
}

// ---- Devices --------------------------------------------------------------------------------------------------

void Chain::transpose(const Message& m, const DeviceSlot& d, const Context& c, Output& out) {
    // On a drum track a pitch is a lane: moving it would play another drum.
    if (m.kind != Kind::On || (c.drum && !d.affectDrums)) {
        forward(m, out);
        return;
    }
    ChainNote n = m.note;
    int pitch = n.pitch + int(shown(d, knob::kSemitones)) + 12 * int(shown(d, knob::kOctaves));
    int steps = int(shown(d, knob::kKeySteps));
    if (steps != 0)
        pitch = c.keyOf(n.key).stepBy(std::clamp(pitch, 0, 127), steps);
    n.pitch = int8_t(std::clamp(pitch, 0, 127));
    post(m.stage + 1, Kind::On, n, m.time, out);
}

void Chain::scale(const Message& m, const DeviceSlot& d, const Context& c, Output& out) {
    if (m.kind != Kind::On || (c.drum && !d.affectDrums)) {
        forward(m, out);
        return;
    }
    const Scale* key = &c.keyOf(m.note.key);
    switch (KeyFrom(choiceOf(d, knob::kKeyFrom))) {
    case KeyFrom::Song:
        key = c.songKey ? c.songKey : &chromatic();
        break;
    case KeyFrom::Fixed:
        key = &fixedKey(choiceOf(d, knob::kRoot), choiceOf(d, knob::kScale));
        break;
    case KeyFrom::Clip:
    default:
        break;
    }
    if (key->chromatic() || key->inScale(m.note.pitch)) {
        forward(m, out);
        return;
    }
    OffKey mode = OffKey(choiceOf(d, knob::kOffKey));
    if (mode == OffKey::Drop)
        return; // its end, if one follows, finds no note downstream and does nothing
    ChainNote n = m.note;
    n.pitch = int8_t(key->snap(n.pitch, mode == OffKey::Up ? 1 : mode == OffKey::Down ? -1 : 0));
    post(m.stage + 1, Kind::On, n, m.time, out);
}

void Chain::velocity(const Message& m, const DeviceSlot& d, const Context& c, Output& out) {
    if (m.kind != Kind::On) {
        forward(m, out);
        return;
    }
    ChainNote n = m.note;
    float v = std::clamp(n.velocity, 0.f, 1.f);
    float curve = shown(d, knob::kCurve) / 100;
    if (curve != 0)
        v = std::pow(v, std::pow(3.f, -curve)); // above 0 lifts soft notes, below 0 pushes them down
    float lo = shown(d, knob::kMin) / 100, hi = shown(d, knob::kMax) / 100;
    v = lo + v * (hi - lo);
    float spread = shown(d, knob::kRandom) / 100;
    if (spread > 0) {
        double r = operators::random(c.seed, d.id, n.pass, noteKey(n), kVelocitySalt);
        v += float(r * 2 - 1) * spread;
    }
    n.velocity = std::clamp(v, 0.f, 1.f);
    post(m.stage + 1, Kind::On, n, m.time, out);
}

void Chain::noteLength(const Message& m, const DeviceSlot& d, const Context& c, Output& out) {
    int stage = m.stage;
    ChainNote endNote;
    endNote.id = m.note.id;
    if (m.kind == Kind::Expire) {
        if (forget(stage, m.note.id))
            post(stage + 1, Kind::End, endNote, m.time, out);
        return;
    }
    if (m.kind == Kind::End) {
        // What happens was decided when the note came in, so turning the Mode knob meanwhile can't strand it.
        DeviceState& s = states[size_t(stage)];
        for (int i = 0; i < s.count; i++) {
            OpenNote& o = s.open[size_t(i)];
            if (o.id != m.note.id)
                continue;
            if (o.action == Action::Legato)
                return; // a legato note ends when the next note comes
            OpenNote found = o;
            forget(stage, o.id);
            if (found.action == Action::Rescale) {
                double scaled = found.start + (m.time - found.start) * double(shown(d, knob::kScaleBy)) / 100;
                post(stage + 1, Kind::End, endNote, std::max(scaled, m.time), out);
            }
            return;
        }
        forward(m, out);
        return;
    }

    ChainNote n = m.note;
    switch (LengthMode(choiceOf(d, knob::kMode))) {
    case LengthMode::Scale:
        if (n.open())
            remember(stage, {n.id, n.start, Action::Rescale}, m.time, out);
        else
            n.end = n.start + std::max(1.0, (n.end - n.start) * double(shown(d, knob::kScaleBy)) / 100);
        break;
    case LengthMode::Fixed:
        if (n.open())
            remember(stage, {n.id, n.start, Action::Consume}, m.time, out);
        n.end = n.start + double(devices::rateTicks(choiceOf(d, knob::kLength), c.timeSig));
        break;
    case LengthMode::Legato: {
        double overlap = double(shown(d, knob::kOverlap)) / 100 * double(kPpq / 4);
        DeviceState& s = states[size_t(stage)];
        for (int i = 0; i < s.count;) {
            const OpenNote& o = s.open[size_t(i)];
            // Notes starting together (a chord) all hold on; only earlier ones end.
            if (o.action == Action::Legato && o.start < n.start) {
                ChainNote e;
                e.id = o.id;
                forget(stage, o.id);
                post(stage + 1, Kind::End, e, n.start + overlap, out);
            } else {
                i++;
            }
        }
        n.end = INFINITY;
        remember(stage, {n.id, n.start, Action::Legato}, m.time, out);
        // Without a next note it still ends: 4 bars on, so a last note or a stopped clip can't hang.
        ChainNote expire;
        expire.id = n.id;
        post(stage, Kind::Expire, expire, n.start + 4.0 * double(ticksPerBar(c.timeSig)), out);
        break;
    }
    }
    post(stage + 1, Kind::On, n, m.time, out);
}

// ---- Chord ----------------------------------------------------------------------------------------------------

void Chain::chord(const Message& m, const DeviceSlot& d, const Context& c, Output& out) {
    int stage = m.stage;
    if (m.kind == Kind::End) {
        // An open note's chord ends with it.
        OpenNote o;
        if (forget(stage, m.note.id, &o))
            for (int k = 0; k < o.childCount; k++) {
                ChainNote e;
                e.id = o.children[size_t(k)];
                post(stage + 1, Kind::End, e, m.time, out);
            }
        forward(m, out);
        return;
    }
    if (m.kind != Kind::On || (c.drum && !d.affectDrums)) {
        forward(m, out);
        return;
    }
    const std::vector<ChordShapeInfo>& shapes = chordShapes();
    int shape = std::clamp(choiceOf(d, knob::kChord), 0, int(shapes.size()) - 1);
    ChordStamp stamp;
    stamp.shape = shapes[size_t(shape)].shape;
    stamp.inversion = int(shown(d, knob::kInversion));
    stamp.voicing = ChordVoicing(std::clamp(choiceOf(d, knob::kVoicing), 0, 2));
    int pitches[kMaxChordNotes];
    int n =
        chordPitches(m.note.pitch, stamp, c.keyOf(m.note.key), shown(d, knob::kInKey) > 0.5f, pitches, kMaxChordNotes);
    if (n == 0) {
        forward(m, out);
        return;
    }
    // Strum: each note a little after the one below it.
    double strum = double(shown(d, knob::kStrum)) / 100 * double(kPpq / 4);
    OpenNote open{m.note.id, m.note.start, Action::Children};
    for (int k = 0; k < n; k++) {
        ChainNote note = m.note;
        note.pitch = int8_t(pitches[k]);
        note.start = m.note.start + double(k) * strum;
        if (!note.open())
            note.end = std::max(m.note.end, note.start + 1);
        if (k > 0) {
            note.id = newId();
            if (open.childCount < kMaxChordNotes)
                open.children[size_t(open.childCount++)] = note.id;
        }
        post(stage + 1, Kind::On, note, note.start, out);
    }
    if (m.note.open())
        remember(stage, open, m.time, out);
}

// ---- Arpeggiator ----------------------------------------------------------------------------------------------

void Chain::release(int stage, uint32_t id, bool hold) {
    DeviceState& s = states[size_t(stage)];
    for (int i = 0; i < s.heldCount; i++) {
        if (s.held[size_t(i)].note.id != id)
            continue;
        if (hold) {
            s.held[size_t(i)].down = false; // latched: it plays on until new notes come
        } else {
            std::move(s.held.begin() + i + 1, s.held.begin() + s.heldCount, s.held.begin() + i);
            s.heldCount--;
        }
        return;
    }
}

void Chain::arpeggiator(const Message& m, const DeviceSlot& d, const Context& c, Output& out) {
    int stage = m.stage;
    DeviceState& s = states[size_t(stage)];
    bool hold = shown(d, knob::kHold) > 0.5f;
    double rate = double(devices::rateTicks(choiceOf(d, knob::kRate), c.timeSig));
    switch (m.kind) {
    case Kind::End:
    case Kind::Expire:
        release(stage, m.note.id, hold);
        return; // the held notes themselves never sound: the steps do
    case Kind::On: {
        bool anyDown = false;
        for (int i = 0; i < s.heldCount; i++)
            anyDown |= s.held[size_t(i)].down;
        if (hold && !anyDown)
            s.heldCount = 0; // new notes after letting go replace the latched ones
        if (s.heldCount == kMaxHeld) {
            std::move(s.held.begin() + 1, s.held.end(), s.held.begin());
            s.heldCount--;
        }
        s.held[size_t(s.heldCount++)] = {m.note, s.arrivals++, true};
        if (!m.note.open()) {
            ChainNote e;
            e.id = m.note.id;
            post(stage, Kind::Expire, e, m.note.end, out);
        }
        if (!s.running) {
            // Steps fall on the song's grid: the first one on the next line (at once when the note is on one).
            s.running = true;
            s.steps = 0;
            double song = m.time + c.songOffset;
            double line = std::ceil(song / rate - 1e-6) * rate - c.songOffset;
            post(stage, Kind::Tick, ChainNote(), std::max(line, m.time), out);
        }
        return;
    }
    case Kind::Tick:
        break;
    default:
        forward(m, out);
        return;
    }

    if (s.heldCount == 0) {
        s.running = false;
        return;
    }
    // The notes to step through: held notes over the octaves, in the pattern's order.
    int octaves = std::clamp(int(shown(d, knob::kArpOctaves)), 1, 4);
    std::array<const Held*, kMaxHeld> order;
    for (int i = 0; i < s.heldCount; i++)
        order[size_t(i)] = &s.held[size_t(i)];
    ArpPattern pattern = ArpPattern(choiceOf(d, knob::kPattern));
    if (pattern != ArpPattern::AsPlayed)
        std::sort(order.begin(), order.begin() + s.heldCount,
                  [](const Held* a, const Held* b) { return a->note.pitch < b->note.pitch; });
    else
        std::sort(order.begin(), order.begin() + s.heldCount,
                  [](const Held* a, const Held* b) { return a->order < b->order; });
    int count = s.heldCount * octaves;
    auto at = [&](int index) {
        const Held* h = order[size_t(index % s.heldCount)];
        return std::make_pair(h, int(h->note.pitch) + 12 * (index / s.heldCount));
    };

    double t = m.time;
    double swing = (s.steps % 2 == 1) ? double(shown(d, knob::kSwing)) / 100 * rate / 3 : 0;
    double gate = std::max(1.0, rate * double(shown(d, knob::kGate)) / 100);
    ArpVelocity velMode = ArpVelocity(choiceOf(d, knob::kArpVelocity));
    auto play = [&](const Held* h, int pitch) {
        if (pitch < 0 || pitch > 127)
            return;
        ChainNote note = h->note;
        note.id = newId();
        note.pitch = int8_t(pitch);
        note.start = t + swing;
        note.end = note.start + gate;
        note.origin = t + c.songOffset;
        if (velMode == ArpVelocity::Fixed)
            note.velocity = 0.8f;
        else if (velMode == ArpVelocity::Accent)
            note.velocity = std::clamp(note.velocity * (s.steps % 4 == 0 ? 1.25f : 0.85f), 0.f, 1.f);
        post(stage + 1, Kind::On, note, note.start, out);
    };
    int step = int(s.steps % uint64_t(std::max(1, count)));
    switch (pattern) {
    case ArpPattern::Chord:
        for (int i = 0; i < count; i++) {
            auto [h, p] = at(i);
            play(h, p);
        }
        break;
    case ArpPattern::Down: {
        auto [h, p] = at(count - 1 - step);
        play(h, p);
        break;
    }
    case ArpPattern::UpDown:
    case ArpPattern::DownUp: {
        // Up and back without repeating the ends.
        int period = count > 1 ? 2 * count - 2 : 1;
        int j = int(s.steps % uint64_t(period));
        int index = j < count ? j : period - j;
        if (pattern == ArpPattern::DownUp)
            index = count - 1 - index;
        auto [h, p] = at(index);
        play(h, p);
        break;
    }
    case ArpPattern::Random: {
        double r = operators::random(c.seed, d.id, int64_t(s.steps), uint64_t(std::llround(t)), 0xA4B9u);
        auto [h, p] = at(std::min(count - 1, int(r * count)));
        play(h, p);
        break;
    }
    case ArpPattern::Up:
    case ArpPattern::AsPlayed:
    default: {
        auto [h, p] = at(step);
        play(h, p);
        break;
    }
    }
    s.steps++;
    post(stage, Kind::Tick, ChainNote(), t + rate, out);
}

// ---- Echo -----------------------------------------------------------------------------------------------------

void Chain::echo(const Message& m, const DeviceSlot& d, const Context& c, Output& out) {
    int stage = m.stage;
    double time = double(devices::rateTicks(choiceOf(d, knob::kTime), c.timeSig));
    int repeats = std::clamp(int(shown(d, knob::kRepeats)), 0, 16);
    if (m.kind == Kind::On) {
        forward(m, out);
        if (repeats > 0)
            post(stage, Kind::Echo, m.note, m.note.start + time, out, 1);
        return;
    }
    if (m.kind != Kind::Echo) {
        forward(m, out);
        return;
    }
    int k = m.step;
    float decay = shown(d, knob::kDecay) / 100;
    float velocity = m.note.velocity * std::pow(1 - decay, float(k));
    if (velocity < 0.01f)
        return; // faded out
    ChainNote note = m.note;
    note.id = newId();
    note.velocity = velocity;
    if (!c.drum || d.affectDrums)
        note.pitch = int8_t(std::clamp(int(m.note.pitch) + k * int(shown(d, knob::kEchoPitch)), 0, 127));
    double length = m.note.open() ? time : m.note.end - m.note.start;
    note.start = m.time;
    note.end = note.start + std::max(1.0, length * double(shown(d, knob::kEchoGate)) / 100);
    note.origin = m.note.origin + double(k) * time;
    post(stage + 1, Kind::On, note, note.start, out);
    if (k < repeats)
        post(stage, Kind::Echo, m.note, m.time + time, out, uint8_t(k + 1));
}

// ---- Random ---------------------------------------------------------------------------------------------------

void Chain::random(const Message& m, const DeviceSlot& d, const Context& c, Output& out) {
    if (m.kind != Kind::On) {
        forward(m, out);
        return;
    }
    ChainNote n = m.note;
    auto roll = [&](uint32_t salt) { return operators::random(c.seed, d.id, n.pass, noteKey(n), salt); };
    if (roll(0xD509u) * 100 < shown(d, knob::kDrop))
        return; // dropped this pass; its end, if one follows, finds no note downstream
    int steps = int(shown(d, knob::kRandomPitch));
    if (steps > 0 && (!c.drum || d.affectDrums)) {
        int offset = int(std::lround((roll(0x917Cu) * 2 - 1) * steps));
        int pitch = shown(d, knob::kRandomInKey) > 0.5f ? c.keyOf(n.key).stepBy(n.pitch, offset) : n.pitch + offset;
        n.pitch = int8_t(std::clamp(pitch, 0, 127));
    }
    post(m.stage + 1, Kind::On, n, n.start, out);
}

// ---- Humanize -------------------------------------------------------------------------------------------------

void Chain::humanize(const Message& m, const DeviceSlot& d, const Context& c, Output& out) {
    if (m.kind != Kind::On) {
        forward(m, out);
        return;
    }
    ChainNote n = m.note;
    auto roll = [&](uint32_t salt) { return operators::random(c.seed, d.id, n.pass, noteKey(n), salt); };
    float spread = shown(d, knob::kHumanizeVelocity) / 100;
    if (spread > 0)
        n.velocity = std::clamp(n.velocity + float(roll(0x7E10u) * 2 - 1) * spread, 0.f, 1.f);
    // Held keys keep their own start and end: their end is the release, which mustn't come first.
    if (!n.open()) {
        float stretch = shown(d, knob::kHumanizeLength) / 100;
        if (stretch > 0) {
            double length = n.end - n.start;
            n.end = n.start + std::max(1.0, length * (1 + double(roll(0x1E46u) * 2 - 1) * double(stretch)));
        }
        // Only later, never earlier: nothing can play before its time arrives.
        double late = double(shown(d, knob::kHumanizeTiming)) / 100 * double(kPpq / 4) * roll(0x71A1u);
        n.start += late;
        n.end += late;
    }
    post(m.stage + 1, Kind::On, n, n.start, out);
}

// ---- Note Filter ----------------------------------------------------------------------------------------------

void Chain::noteFilter(const Message& m, const DeviceSlot& d, const Context& c, Output& out) {
    if (m.kind != Kind::On) {
        forward(m, out);
        return;
    }
    int lo = int(shown(d, knob::kLowest)), hi = int(shown(d, knob::kHighest));
    if (lo > hi)
        std::swap(lo, hi);
    float v = m.note.velocity * 100;
    float vlo = shown(d, knob::kMinVelocity), vhi = shown(d, knob::kMaxVelocity);
    if (vlo > vhi)
        std::swap(vlo, vhi);
    if (m.note.pitch < lo || m.note.pitch > hi || v < vlo - 0.5f || v > vhi + 0.5f)
        return;
    int channel = choiceOf(d, knob::kChannel); // 0 = any
    if (channel > 0 && m.note.channel != channel)
        return;
    DeviceState& s = states[size_t(m.stage)];
    int every = std::max(1, int(shown(d, knob::kEvery)));
    int offset = int(shown(d, knob::kOffset));
    int64_t k = int64_t(s.steps++) - offset;
    if (((k % every) + every) % every != 0)
        return;
    forward(m, out);
}

} // namespace tessera::chain
