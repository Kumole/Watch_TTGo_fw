#pragma once

#include "app_types.h"

/**
 * Return true when a new session may be started from the idle screen.
 */
bool canStartSession();

/**
 * Return the correct idle state based on whether the clock is valid.
 */
WatchState getIdleStateFromClock();

/**
 * Transition the top-level state machine back to the appropriate idle state.
 */
void transitionToIdleState();

/**
 * Create and persist a new in-progress session.
 */
void beginNewSession();

/**
 * Finalize and persist the currently active session.
 */
void finalizeSession();

/**
 * Periodically checkpoint the active session while it is running.
 */
void checkpointActiveSessionIfNeeded();