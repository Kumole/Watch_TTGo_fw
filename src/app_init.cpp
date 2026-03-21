#include "app_init.h"
#include "app_context.h"
#include "app_storage.h"

#include "step_counter.h"

void initHikeWatch()
{
    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

    initStepCounter();

    pinMode(AXP202_INT, INPUT_PULLUP);
    attachInterrupt(AXP202_INT, [] {
        irqButton = true;
    }, FALLING);

    watch->power->enableIRQ(AXP202_PEK_SHORTPRESS_IRQ, true);
    watch->power->clearIRQ();
}

void initializeApp()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("BOOT OK");

    watch = TTGOClass::getWatch();
    Serial.println("Before watch->begin()");
    watch->begin();
    Serial.println("After watch->begin()");
    Serial.println("Before watch->openBL()");
    watch->openBL();
    Serial.println("After watch->openBL()");

    tft = watch->tft;
    sensor = watch->bma;

    tft->fillScreen(TFT_BLACK);
    tft->setTextFont(4);
    tft->setTextSize(1);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);

    initHikeWatch();
    updateSessionCount();
    loadRuntimeState();

    // The watch loses trusted wall clock time across reboot.
    clockSynced = false;

    if (resumeSessionOnBoot) {
        state = WatchState::RESUME_PENDING_TIME_SYNC;
    } else {
        state = WatchState::IDLE_UNSYNCED;
    }

    SerialBT.begin(WATCH_BLUETOOTH_NAME);
    Serial.println("SETUP DONE");
}