#pragma once

/**
 * Initialize LittleFS, step counter, button IRQ, and power IRQ handling.
 */
void initHikeWatch();

/**
 * Initialize hardware and application state during Arduino setup().
 */
void initializeApp();