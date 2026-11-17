"""The kinds of engine events, by name: the numbers of `Event.kind`.

The binding gives events as flat records whose `kind` is a number (engine/src/ffi/convert.cpp,
and the event table of the engine's README, section 6). This is the one place that names them.
"""

from enum import IntEnum


class EventKind(IntEnum):
    MOVE_USED = 0
    DAMAGE_DEALT = 1
    FAINTED = 2
    MISSED = 3
    STATUS_APPLIED = 4
    STATUS_FAILED = 5
    STATUS_DAMAGE = 6
    STATUS_CURED = 7
    MOVE_SKIPPED = 8
    STAT_STAGE_CHANGED = 9
    STAT_CHANGE_FAILED = 10
    SWITCHED_OUT = 11
    SWITCHED_IN = 12
    ABILITY_TRIGGERED = 13
    MOVE_FAILED = 14
    WEATHER_STARTED = 15
    WEATHER_ENDED = 16
    WEATHER_DAMAGE = 17
    HAZARD_SET = 18
    HAZARD_DAMAGE = 19
    HAZARDS_CLEARED = 20
    TOXIC_SPIKES_ABSORBED = 21
    HEALED = 22
    RECOIL_DAMAGE = 23
    CHARGING = 24
    PROTECTED = 25
    ITEM_TRIGGERED = 26
    ITEM_CONSUMED = 27
    ITEM_DAMAGE = 28
    ITEM_KNOCKED_OFF = 29
    TERRAIN_STARTED = 30
    TERRAIN_ENDED = 31
    SCREEN_STARTED = 32
    SCREEN_ENDED = 33
    DESTINY_BOND_TRIGGERED = 34
    ABILITY_DAMAGE = 35
