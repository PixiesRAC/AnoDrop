#pragma once


#include <random>

struct FakeTimezone
{
    std::string name;
    int offset_seconds = 0; // Offset from UTC in seconds
    bool operator==(const FakeTimezone &) const = default;
};


namespace FakeTimezonePool
{

// All offsets are real UTC-based offsets, even for fictional locations.
// The fictional names just add plausible deniability and fun.
inline const std::vector<FakeTimezone> POOL = {
    // Real timezones
    {"Europe/Paris (UTC+1)", 3600},
    {"America/New_York (UTC-5)", -18000},
    {"Asia/Tokyo (UTC+9)", 32400},
    {"Europe/London (UTC+0)", 0},
    {"Australia/Sydney (UTC+10)", 36000},
    {"Asia/Kolkata (UTC+5:30)", 19800},
    {"America/Los_Angeles (UTC-8)", -28800},
    {"Africa/Johannesburg (UTC+2)", 7200},
    {"America/Sao_Paulo (UTC-3)", -10800},
    {"Europe/Moscow (UTC+3)", 10800},
    {"Asia/Dubai (UTC+4)", 14400},
    {"Asia/Shanghai (UTC+8)", 28800},
    {"Pacific/Auckland (UTC+12)", 43200},
    {"America/Chicago (UTC-6)", -21600},
    {"Pacific/Honolulu (UTC-10)", -36000},
    // Fictional timezones (with real UTC offsets)
    {"Mars/Olympus_Mons (UTC+2)", 7200},
    {"Tatooine/Mos_Eisley (UTC-4)", -14400},
    {"Terre_du_Milieu/La_Comte (UTC+0)", 0},
    {"Atlantis/Capitale (UTC-1)", -3600},
    {"Poudlard/Tour_Astronomie (UTC+0)", 0},
    {"Westeros/Port_Real (UTC+1)", 3600},
    {"Cyberpunk/Night_City (UTC-8)", -28800},
    {"Pandora/Montagnes_Hallelujah (UTC+4)", 14400},
    {"Narnia/Cair_Paravel (UTC+3)", 10800},
    {"Vulcain/ShiKahr (UTC+5)", 18000},
    {"Krypton/Kryptonopolis (UTC-2)", -7200},
    {"Dune/Arrakeen (UTC+11)", 39600},
    {"Gallifrey/Citadelle (UTC+8)", 28800},
    {"Coruscant/Secteur_Senat (UTC+0)", 0},
    {"Zelda/Chateau_Hyrule (UTC-6)", -21600},
};

inline FakeTimezone pickRandom()
{
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, POOL.size() - 1);
    return POOL[dist(gen)];
}

} // namespace FakeTimezonePool
