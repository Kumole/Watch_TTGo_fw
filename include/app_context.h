#pragma once

#include "config.h"
#include "app_types.h"
#include <BluetoothSerial.h>
#include <limits.h>

/**
 * Global watch hardware handles.
 *
 * These are initialized during setup and then shared across the app modules.
 */
extern TTGOClass *watch;
extern TFT_eSPI *tft;
extern BMA *sensor;
extern BluetoothSerial SerialBT;

/**
 * Global state machine state and interrupt flags.
 */
extern volatile WatchState state;
extern volatile bool irqBMA;
extern volatile bool irqButton;

/**
 * Active-session runtime fields.
 *
 * These describe the currently active or resumed hike session.
 */
extern String activeSessionId;
extern String activeSessionStartTime;
extern unsigned long activeSessionStartMillis;
extern uint32_t activeSessionElapsedBeforeResume;
extern uint32_t activeSessionBaseSteps;
extern int activeSessionFileIdx;
extern bool resumeSessionOnBoot;
extern unsigned long lastSessionCheckpointAt;

/**
 * UI constants.
 */
extern const TouchButton START_TOUCH_BUTTON;
extern const TouchButton END_TOUCH_BUTTON;

extern const int16_t STEPS_LABEL_X;
extern const int16_t STEPS_Y;
extern const int16_t DIST_LABEL_X;
extern const int16_t DIST_Y;
extern const int16_t DUR_LABEL_X;
extern const int16_t DUR_Y;
extern const int16_t METRIC_VALUE_X;

/**
 * Storage and synchronization state.
 */
extern int currentSessionIdx;
extern int storedSessionCount;
extern const int MAX_SESSIONS;
extern const char *SESSION_STATE_PATH;
extern bool waitingForAck;
extern String pendingSessionId;

/**
 * Clock and rendering state.
 */
extern bool clockSynced;
extern unsigned long lastRenderedClockEpoch;

/**
 * Timing constants.
 */
extern const unsigned long SESSION_CHECKPOINT_INTERVAL_MS;