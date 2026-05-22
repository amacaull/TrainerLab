#pragma once

namespace engine {

struct Stats {
    int hp = 0;
    int atk = 0;
    int def = 0;
    int specAtk = 0;
    int specDef = 0;
    int speed = 0;
};

// Modern-gen formula, IVs=0, EVs=0, neutral nature.
Stats computeStats(const Stats& base, int level);

} // namespace engine
