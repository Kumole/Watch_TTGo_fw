#include "app_ui.h"
#include "app_context.h"
#include "app_time.h"

bool isTouchInsideButton(const TouchButton &button, int16_t x, int16_t y)
{
    return x >= button.x && x <= (button.x + button.w) &&
           y >= button.y && y <= (button.y + button.h);
}

void drawTouchButton(const TouchButton &button)
{
    tft->fillRoundRect(button.x, button.y, button.w, button.h, 10, button.fillColor);
    tft->drawRoundRect(button.x, button.y, button.w, button.h, 10, TFT_WHITE);
    tft->setTextColor(button.textColor, button.fillColor);
    tft->drawCentreString(button.label, button.x + (button.w / 2), button.y + 18, 4);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
}

void drawClockBanner(bool force)
{
    time_t now = time(nullptr);
    unsigned long nowEpoch = (now >= 0) ? static_cast<unsigned long>(now) : 0;

    if (!force && nowEpoch == lastRenderedClockEpoch) {
        return;
    }

    lastRenderedClockEpoch = nowEpoch;

    tft->fillRect(0, 0, 240, 30, TFT_BLACK);
    tft->setTextColor(isClockCurrentlyValid() ? TFT_CYAN : TFT_YELLOW, TFT_BLACK);
    tft->drawCentreString(getClockDisplayText(), 120, 6, 4);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
}

void renderSessionMetrics(uint32_t stepCount, uint32_t durationSeconds)
{
    float distanceKm = static_cast<float>(stepsToMeters(stepCount)) / 1000.0f;

    uint32_t hours = durationSeconds / 3600;
    uint32_t minutes = (durationSeconds % 3600) / 60;
    uint32_t seconds = durationSeconds % 60;

    char durationBuf[16];
    snprintf(
        durationBuf,
        sizeof(durationBuf),
        "%02lu:%02lu:%02lu",
        static_cast<unsigned long>(hours),
        static_cast<unsigned long>(minutes),
        static_cast<unsigned long>(seconds)
    );

    tft->setTextColor(TFT_WHITE, TFT_BLACK);

    tft->fillRect(METRIC_VALUE_X, STEPS_Y, 100, 20, TFT_BLACK);
    tft->setCursor(METRIC_VALUE_X, STEPS_Y);
    tft->print(stepCount);

    tft->fillRect(METRIC_VALUE_X, DIST_Y, 100, 20, TFT_BLACK);
    tft->setCursor(METRIC_VALUE_X, DIST_Y);
    tft->print(distanceKm, 2);
    tft->print(" km");

    tft->fillRect(METRIC_VALUE_X, DUR_Y, 100, 20, TFT_BLACK);
    tft->setCursor(METRIC_VALUE_X, DUR_Y);
    tft->print(durationBuf);
}

bool touchButtonReleased(
    const TouchButton &button,
    bool &touchActive,
    int16_t &lastTouchX,
    int16_t &lastTouchY
)
{
    int16_t touchX = 0;
    int16_t touchY = 0;
    bool isTouched = watch->getTouch(touchX, touchY);

    if (isTouched) {
        touchActive = true;
        lastTouchX = touchX;
        lastTouchY = touchY;
        return false;
    }

    if (touchActive) {
        touchActive = false;
        return isTouchInsideButton(button, lastTouchX, lastTouchY);
    }

    return false;
}

void showClockNotSyncedMessage()
{
    watch->tft->fillScreen(TFT_BLACK);
    drawClockBanner(true);
    watch->tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    watch->tft->drawString("Clock not synced", 28, 90, 4);
    watch->tft->drawString("Connect to RPi", 38, 120, 2);
    watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
    delay(2000);
}

void showMemoryFullMessage()
{
    watch->tft->fillScreen(TFT_RED);
    watch->tft->setTextColor(TFT_WHITE);
    watch->tft->drawString("MEMORY FULL!", 45, 80, 4);
    watch->tft->drawString("Sync with RPi", 45, 110);
    delay(3000);
    watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
}