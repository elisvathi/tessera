#include "NextAction.hpp"

namespace tessera {

namespace {

/// The slots a pick may land on, as [first, last]: the whole track, or the run of filled slots around `current`.
void pickRange(NextActionScope scope, const std::vector<bool>& filled, int current, int& first, int& last) {
    first = 0;
    last = int(filled.size()) - 1;
    if (scope != NextActionScope::Block)
        return;
    first = current;
    while (first > 0 && filled[size_t(first - 1)])
        first--;
    last = current;
    while (last + 1 < int(filled.size()) && filled[size_t(last + 1)])
        last++;
}

NextActionTarget launch(int slot) { return {NextActionTarget::Kind::Launch, slot}; }
NextActionTarget stopTrack() { return {NextActionTarget::Kind::Stop, -1}; }

/// The `n`th filled slot in [first, last], skipping `skip` (-1 skips none).
int nthFilled(const std::vector<bool>& filled, int first, int last, int n, int skip) {
    for (int i = first; i <= last; i++) {
        if (!filled[size_t(i)] || i == skip)
            continue;
        if (n-- == 0)
            return i;
    }
    return -1;
}

} // namespace

NextActionTarget pickNextAction(NextAction action, NextActionScope scope, const std::vector<bool>& filled, int current,
                                int previous, double random) {
    if (current < 0 || current >= int(filled.size()))
        return {};
    int first = 0, last = 0;
    pickRange(scope, filled, current, first, last);
    auto filledAt = [&](int i) { return i >= first && i <= last && filled[size_t(i)]; };
    random = random < 0 ? 0 : random >= 1 ? 0.999999 : random;

    switch (action) {
    case NextAction::None:
        return {};
    case NextAction::Stop:
        return stopTrack();
    case NextAction::Timeline:
        return {NextActionTarget::Kind::Timeline, -1};
    case NextAction::Next:
    case NextAction::RoundRobin:
        for (int i = current + 1; i <= last; i++)
            if (filledAt(i))
                return launch(i);
        if (action == NextAction::Next)
            return stopTrack();
        return launch(nthFilled(filled, first, last, 0, -1)); // current is filled, so there is one
    case NextAction::Previous:
        for (int i = current - 1; i >= first; i--)
            if (filledAt(i))
                return launch(i);
        return stopTrack();
    case NextAction::First:
        return launch(nthFilled(filled, first, last, 0, -1));
    case NextAction::Last:
        for (int i = last; i >= first; i--)
            if (filledAt(i))
                return launch(i);
        return {};
    case NextAction::Random:
    case NextAction::Other: {
        int skip = action == NextAction::Other ? current : -1;
        int count = 0;
        for (int i = first; i <= last; i++)
            count += filledAt(i) && i != skip;
        if (count == 0)
            return {}; // Other with no other clip: this one plays on
        return launch(nthFilled(filled, first, last, int(random * count), skip));
    }
    case NextAction::Back:
        // The clip before may sit outside the block: going back is about what played, not where it is.
        if (previous >= 0 && previous < int(filled.size()) && filled[size_t(previous)])
            return launch(previous);
        return stopTrack();
    }
    return {};
}

} // namespace tessera
