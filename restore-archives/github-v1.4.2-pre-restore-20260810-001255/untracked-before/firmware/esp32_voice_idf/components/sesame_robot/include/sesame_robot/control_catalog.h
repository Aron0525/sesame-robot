#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace sesame::robot {

// The motion names exposed by the on-device web controller. Voice response
// plans use this same catalog so a name always selects the same pose sequence.
inline constexpr std::array<std::string_view, 19> kWebActions{
    "rest", "stand", "wave", "dance", "swim", "point", "pushup",
    "bow", "cute", "freaky", "worm", "shake", "shrug", "dead",
    "crab", "forward", "backward", "left", "right",
};

// These are the faces with bitmap data currently exposed by /api/catalog.
// `default` remains an input-compatibility alias for idle and is not listed.
inline constexpr std::array<std::string_view, 35> kWebExpressions{
    "walk",          "rest",          "swim",           "dance",
    "wave",          "point",         "cute",           "pushup",
    "freaky",        "bow",           "worm",           "shake",
    "shrug",         "dead",          "crab",           "idle",
    "idle_blink",    "happy",         "talk_happy",     "sad",
    "talk_sad",      "angry",         "talk_angry",     "surprised",
    "talk_surprised", "sleepy",       "talk_sleepy",    "love",
    "talk_love",     "excited",       "talk_excited",   "confused",
    "talk_confused", "thinking",      "talk_thinking",
};

constexpr bool is_web_action(std::string_view action) {
  return std::find(kWebActions.begin(), kWebActions.end(), action) !=
         kWebActions.end();
}

constexpr bool is_web_expression(std::string_view expression) {
  return std::find(kWebExpressions.begin(), kWebExpressions.end(), expression) !=
         kWebExpressions.end();
}

}  // namespace sesame::robot
