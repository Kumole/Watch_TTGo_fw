#pragma once

#include <Arduino.h>
#include <stdint.h>

/**
 * One hike session as stored on the watch and synchronized to the hub.
 */
struct SessionRecord {
    String sessionId;
    String startTime;
    String endTime;
    uint32_t steps;
    uint32_t distanceMeters;
    uint32_t durationSeconds;
};

/**
 * Top-level watch UI / runtime state machine.
 *
 * These states are intentionally user-visible or recovery-critical.
 * Transient helper logic should not invent hidden states outside this enum.
 */
enum class WatchState : uint8_t {
    BOOTING = 0,
    IDLE_UNSYNCED = 1,
    IDLE_READY = 2,
    RESUME_PENDING_TIME_SYNC = 3,
    SESSION_STARTING = 4,
    SESSION_ACTIVE = 5,
    SESSION_ENDING = 6,
    SESSION_SAVED = 7
};

/**
 * Generic touchscreen button descriptor.
 */
struct TouchButton {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint16_t fillColor;
    uint16_t textColor;
    const char *label;
};