#pragma once
// Channels are how a problem tells the engine what can be observed (sensors) and
// what can be done (actions) -- and nothing about how to solve it.
#include <string>

namespace adaptogen {

enum class ChannelKind { Sensor, Action };

struct Channel {
    std::string name;         // e.g. "item.duration" -- reused names make strains transferable
    ChannelKind kind = ChannelKind::Sensor;
    int arity = 0;            // sensors: number of index arguments; actions: number of arguments (0..3)
    std::string description;  // human/LLM readable meaning
    std::string units;        // e.g. "minutes", "" if unitless
};

inline const char* to_string(ChannelKind k) { return k == ChannelKind::Sensor ? "sensor" : "action"; }

}  // namespace adaptogen
