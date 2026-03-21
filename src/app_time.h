#pragma once

#include <Arduino.h>
#include <time.h>

/**
 * Generate a random RFC4122 version-4 UUID string for a new session.
 */
String generateUuidV4();

/**
 * Return the current clock time as UTC ISO-8601.
 *
 * If the system clock is invalid, a safe placeholder timestamp is returned.
 */
String currentIso8601();

/**
 * Return true only when the watch clock is considered trustworthy.
 */
bool isClockCurrentlyValid();

/**
 * Return the current clock banner text for the screen header.
 */
String getClockDisplayText();

/**
 * Apply a Unix epoch received from the hub and mark the watch clock as synced.
 */
bool syncClockFromUnixEpoch(time_t epochSeconds);

/**
 * Convert total steps to integer meters.
 */
uint32_t stepsToMeters(uint32_t steps);

/**
 * Return elapsed seconds for the active session, including restored progress.
 */
uint32_t getCurrentElapsedSeconds();