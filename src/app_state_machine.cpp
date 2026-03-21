#include "app_state_machine.h"
#include "app_bluetooth.h"
#include "app_context.h"
#include "app_session.h"
#include "app_storage.h"
#include "app_time.h"
#include "app_ui.h"
#include "step_counter.h"

static void runIdleScreen()
{
    updateSessionCount();

    watch->power->readIRQ();
    watch->power->clearIRQ();
    irqButton = false;

    watch->tft->fillScreen(TFT_BLACK);
    watch->tft->setTextFont(4);
    watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
    drawClockBanner(true);
    watch->tft->drawString("Hiking Assistant", 25, 35, 4);
    watch->tft->drawString("Press side button", 18, 88);
    watch->tft->drawString("or tap screen", 37, 118);
    drawTouchButton(START_TOUCH_BUTTON);

    bool exitIdleScreen = false;
    bool startTouchActive = false;
    int16_t startTouchX = 0;
    int16_t startTouchY = 0;

    while (!exitIdleScreen &&
           (state == WatchState::IDLE_UNSYNCED || state == WatchState::IDLE_READY))
    {
        drawClockBanner();
        pollBluetooth();

        if (touchButtonReleased(START_TOUCH_BUTTON, startTouchActive, startTouchX, startTouchY)) {
            updateSessionCount();

            if (storedSessionCount >= MAX_SESSIONS) {
                showMemoryFullMessage();
                exitIdleScreen = true;
            } else if (!isClockCurrentlyValid()) {
                showClockNotSyncedMessage();
                exitIdleScreen = true;
            } else {
                state = WatchState::SESSION_STARTING;
                exitIdleScreen = true;
            }
        }

        if (irqButton) {
            irqButton = false;
            watch->power->readIRQ();

            if (watch->power->isPEKShortPressIRQ()) {
                updateSessionCount();

                if (storedSessionCount >= MAX_SESSIONS) {
                    showMemoryFullMessage();
                    exitIdleScreen = true;
                } else if (!isClockCurrentlyValid()) {
                    showClockNotSyncedMessage();
                    exitIdleScreen = true;
                } else {
                    state = WatchState::SESSION_STARTING;
                    exitIdleScreen = true;
                }
            }

            watch->power->clearIRQ();
        }

        delay(10);
    }
}

static void runResumePendingTimeSync()
{
    watch->tft->fillScreen(TFT_BLACK);
    watch->tft->setTextFont(4);
    watch->tft->setTextSize(1);
    watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
    drawClockBanner(true);
    watch->tft->drawString("Recovered hike", 38, 70, 4);
    watch->tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    watch->tft->drawString("Time sync needed", 22, 105, 4);
    watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
    watch->tft->drawString("Connect to RPi", 40, 145, 2);

    while (state == WatchState::RESUME_PENDING_TIME_SYNC)
    {
        drawClockBanner();
        pollBluetooth();

        if (irqButton) {
            irqButton = false;
            watch->power->readIRQ();
            watch->power->clearIRQ();
        }

        delay(10);
    }
}

static void runActiveSession()
{
    watch->tft->setTextFont(4);
    watch->tft->setTextSize(1);
    watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
    watch->tft->fillScreen(TFT_BLACK);

    drawClockBanner(true);
    if (resumeSessionOnBoot) {
        watch->tft->drawString("Resuming hike", 45, 100);
        delay(1000);
        resumeSessionOnBoot = false;
    } else {
        watch->tft->drawString("Starting hike", 45, 100);
        delay(1000);
    }

    watch->tft->fillScreen(TFT_BLACK);

    watch->tft->setCursor(STEPS_LABEL_X, STEPS_Y);
    watch->tft->print("Steps:");

    watch->tft->setCursor(DIST_LABEL_X, DIST_Y);
    watch->tft->print("Dist:");

    watch->tft->setCursor(DUR_LABEL_X, DUR_Y);
    watch->tft->print("Time:");

    drawTouchButton(END_TOUCH_BUTTON);

    if (activeSessionFileIdx < 0 || activeSessionFileIdx >= MAX_SESSIONS) {
        activeSessionFileIdx = currentSessionIdx;
    }

    bool endTouchActive = false;
    int16_t endTouchX = 0;
    int16_t endTouchY = 0;

    time_t lastRenderedEpochSecond = -1;
    uint32_t lastRenderedSteps = UINT32_MAX;
    uint32_t lastRenderedDuration = UINT32_MAX;

    while (state == WatchState::SESSION_ACTIVE)
    {
        // Consume any pending step-counter IRQ and update cached count.
        handleStepCounterInterrupt();

        uint32_t stepCount =
            activeSessionBaseSteps + static_cast<uint32_t>(getStepCount());
        uint32_t durationSeconds = getCurrentElapsedSeconds();
        time_t nowEpoch = time(nullptr);

        bool shouldRedrawClock = (nowEpoch != lastRenderedEpochSecond);
        bool shouldRedrawMetrics =
            (stepCount != lastRenderedSteps) ||
            (durationSeconds != lastRenderedDuration);

        if (shouldRedrawClock) {
            drawClockBanner(true);
            lastRenderedEpochSecond = nowEpoch;
        }

        if (shouldRedrawMetrics) {
            renderSessionMetrics(stepCount, durationSeconds);
            lastRenderedSteps = stepCount;
            lastRenderedDuration = durationSeconds;
        }

        checkpointActiveSessionIfNeeded();
        pollBluetooth();

        if (touchButtonReleased(END_TOUCH_BUTTON, endTouchActive, endTouchX, endTouchY)) {
            state = WatchState::SESSION_ENDING;
            break;
        }

        if (irqButton) {
            irqButton = false;
            watch->power->readIRQ();

            if (watch->power->isPEKShortPressIRQ()) {
                state = WatchState::SESSION_ENDING;
                watch->power->clearIRQ();
                break;
            }

            watch->power->clearIRQ();
        }

        delay(30);
    }
}

void runStateMachine()
{
    switch (state)
    {
        case WatchState::BOOTING:
            state = resumeSessionOnBoot
                ? WatchState::RESUME_PENDING_TIME_SYNC
                : WatchState::IDLE_UNSYNCED;
            break;

        case WatchState::IDLE_UNSYNCED:
        case WatchState::IDLE_READY:
            runIdleScreen();
            break;

        case WatchState::RESUME_PENDING_TIME_SYNC:
            runResumePendingTimeSync();
            break;

        case WatchState::SESSION_STARTING:
            beginNewSession();
            state = WatchState::SESSION_ACTIVE;
            break;

        case WatchState::SESSION_ACTIVE:
            runActiveSession();
            break;

        case WatchState::SESSION_ENDING:
            finalizeSession();
            state = WatchState::SESSION_SAVED;
            break;

        case WatchState::SESSION_SAVED:
            watch->tft->fillScreen(TFT_BLACK);
            drawClockBanner(true);
            watch->tft->drawString("Session saved", 45, 100, 4);
            delay(1000);
            transitionToIdleState();
            break;
    }
}