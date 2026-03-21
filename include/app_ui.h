#pragma once

#include "app_types.h"
#include <Arduino.h>

/**
 * Return true if the given touch coordinates fall inside the button rectangle.
 */
bool isTouchInsideButton(const TouchButton &button, int16_t x, int16_t y);

/**
 * Draw a rounded UI button.
 */
void drawTouchButton(const TouchButton &button);

/**
 * Draw the top clock banner. Redraw only when the second changes unless forced.
 */
void drawClockBanner(bool force = false);

/**
 * Draw the active-session metrics area.
 */
void renderSessionMetrics(uint32_t stepCount, uint32_t durationSeconds);

/**
 * Poll touch release and treat it as a button press only when the release
 * happens after a touch that began inside the button area.
 */
bool touchButtonReleased(
    const TouchButton &button,
    bool &touchActive,
    int16_t &lastTouchX,
    int16_t &lastTouchY
);

/**
 * Temporary UI messages shown from the idle screen.
 */
void showClockNotSyncedMessage();
void showMemoryFullMessage();