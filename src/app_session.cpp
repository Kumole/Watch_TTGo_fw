#include "app_session.h"
#include "app_context.h"
#include "app_storage.h"
#include "app_time.h"
#include "step_counter.h"

bool canStartSession()
{
    return storedSessionCount < MAX_SESSIONS && isClockCurrentlyValid();
}

WatchState getIdleStateFromClock()
{
    return isClockCurrentlyValid()
        ? WatchState::IDLE_READY
        : WatchState::IDLE_UNSYNCED;
}

void transitionToIdleState()
{
    state = getIdleStateFromClock();
}

void beginNewSession()
{
    resetStepCount();

    activeSessionId = generateUuidV4();
    activeSessionStartTime = currentIso8601();
    activeSessionStartMillis = millis();
    activeSessionElapsedBeforeResume = 0;
    activeSessionBaseSteps = 0;
    activeSessionFileIdx = currentSessionIdx;
    waitingForAck = false;
    pendingSessionId = "";

    SessionRecord initialRecord;
    initialRecord.sessionId = activeSessionId;
    initialRecord.startTime = activeSessionStartTime;
    initialRecord.endTime = "";
    initialRecord.steps = 0;
    initialRecord.distanceMeters = 0;
    initialRecord.durationSeconds = 0;
    saveSessionData(activeSessionFileIdx, initialRecord);

    saveRuntimeState(true);
    lastSessionCheckpointAt = millis();
    resumeSessionOnBoot = false;
}

void finalizeSession()
{
    uint32_t stepCount = activeSessionBaseSteps + static_cast<uint32_t>(getStepCount());
    uint32_t durationSeconds = getCurrentElapsedSeconds();

    SessionRecord finalRecord;
    finalRecord.sessionId = activeSessionId;
    finalRecord.startTime = activeSessionStartTime;
    finalRecord.endTime = currentIso8601();
    finalRecord.steps = stepCount;
    finalRecord.distanceMeters = stepsToMeters(stepCount);
    finalRecord.durationSeconds = durationSeconds;

    saveSessionData(activeSessionFileIdx, finalRecord);

    activeSessionElapsedBeforeResume = 0;
    activeSessionBaseSteps = 0;
    activeSessionFileIdx = -1;
    activeSessionId = "";
    activeSessionStartTime = "";
    resumeSessionOnBoot = false;

    currentSessionIdx++;
    if (currentSessionIdx >= MAX_SESSIONS) {
        currentSessionIdx = 0;
    }

    saveRuntimeState(false);
    updateSessionCount();
}

void checkpointActiveSessionIfNeeded()
{
    unsigned long nowMillis = millis();
    if (nowMillis - lastSessionCheckpointAt < SESSION_CHECKPOINT_INTERVAL_MS) {
        return;
    }

    uint32_t stepCount = activeSessionBaseSteps + static_cast<uint32_t>(getStepCount());
    uint32_t durationSeconds = getCurrentElapsedSeconds();

    SessionRecord checkpoint;
    checkpoint.sessionId = activeSessionId;
    checkpoint.startTime = activeSessionStartTime;
    checkpoint.endTime = "";
    checkpoint.steps = stepCount;
    checkpoint.distanceMeters = stepsToMeters(stepCount);
    checkpoint.durationSeconds = durationSeconds;

    saveSessionData(activeSessionFileIdx, checkpoint);

    // After checkpointing, elapsed time becomes the new persisted baseline.
    activeSessionElapsedBeforeResume = durationSeconds;
    activeSessionStartMillis = millis();
    saveRuntimeState(true);
    lastSessionCheckpointAt = nowMillis;
}