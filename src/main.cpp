#include "config.h"
#include "stepCounter.h"
#include "ble.h"
#include <esp_system.h>
#include <time.h>

// Check if Bluetooth configs are enabled
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// Watch objects
TTGOClass *watch;
TFT_eSPI *tft;
BMA *sensor;

struct SessionRecord {
    String sessionId;
    String startTime;
    String endTime;
    uint32_t steps;
    uint32_t distanceMeters;
    uint32_t durationSeconds;
};

volatile uint8_t state;
volatile bool irqBMA = false;
volatile bool irqButton = false;

bool sessionStored = false;
bool sessionSent = false;

String activeSessionId;
String activeSessionStartTime;
unsigned long activeSessionStartMillis = 0;

struct TouchButton {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint16_t fillColor;
    uint16_t textColor;
    const char *label;
};

const TouchButton START_TOUCH_BUTTON = {40, 150, 160, 50, TFT_DARKGREEN, TFT_WHITE, "Start session"};
const TouchButton END_TOUCH_BUTTON = {40, 170, 160, 50, TFT_RED, TFT_WHITE, "End session"};

int currentSessionIdx = 0;
int storedSessionCount = 0;
const int MAX_SESSIONS = 5;

// Timer variables
unsigned long last = 0;
unsigned long updateTimeout = 0;

String generateUuidV4()
{
    uint8_t bytes[16];
    for (uint8_t i = 0; i < sizeof(bytes); i++) {
        bytes[i] = static_cast<uint8_t>(esp_random() & 0xFF);
    }

    bytes[6] = (bytes[6] & 0x0F) | 0x40; // version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80; // variant RFC 4122

    char uuid[37];
    snprintf(
        uuid,
        sizeof(uuid),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5],
        bytes[6], bytes[7],
        bytes[8], bytes[9],
        bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]
    );
    return String(uuid);
}

String currentIso8601()
{
    time_t now = time(nullptr);
    if (now < 946684800) {
        return "1970-01-01T00:00:00Z";
    }

    struct tm utcTime;
    gmtime_r(&now, &utcTime);

    char timestamp[25];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
    return String(timestamp);
}

uint32_t stepsToMeters(uint32_t steps)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(steps) * 1000ULL) / STEPS_PER_KM);
}

String buildSessionPayload(const SessionRecord &record)
{
    String json = "{";
    json += "\"session_id\":\"" + record.sessionId + "\",";
    json += "\"start_time\":\"" + record.startTime + "\",";
    json += "\"end_time\":\"" + record.endTime + "\",";
    json += "\"steps\":" + String(record.steps) + ",";
    json += "\"distance_m\":" + String(record.distanceMeters) + ",";
    json += "\"duration_s\":" + String(record.durationSeconds);
    json += "}";
    return json;
}

String readFileAsString(fs::FS &fs, const char *path)
{
    fs::File file = fs.open(path);
    if (!file || file.isDirectory()) {
        return "";
    }

    String content;
    while (file.available()) {
        content += static_cast<char>(file.read());
    }
    file.close();
    return content;
}

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
    tft->drawCentreString(button.label, button.x + (button.w / 2), button.y + 16, 2);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
}

bool touchButtonReleased(const TouchButton &button, bool &touchActive, int16_t &lastTouchX, int16_t &lastTouchY)
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

void initHikeWatch()
{
    // LittleFS
    if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
        Serial.println("LittleFS Mount Failed");
        return;
    }

    // Stepcounter
    // Configure IMU
    // Enable BMA423 step count feature
    // Reset steps
    // Turn on step interrupt
    // Initialize step counter
    initStepCounter();

    // Side button
    pinMode(AXP202_INT, INPUT_PULLUP);
    attachInterrupt(AXP202_INT, [] {
        irqButton = true;
    }, FALLING);

    //!Clear IRQ unprocessed first
    watch->power->enableIRQ(AXP202_PEK_SHORTPRESS_IRQ, true);
    watch->power->clearIRQ();

    return;
}

void updateSessionCount() {
    storedSessionCount = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        char path[20];
        sprintf(path, "/session_%d.json", i);
        if (LittleFS.exists(path)) {
            storedSessionCount++;
        }
    }
}

void sendSessionBLE() {
    if (storedSessionCount == 0) {
        return;
    }

    bleSendCommand("SYNC_START");

    for (int i = 0; i < MAX_SESSIONS; i++) {
        char path[20];
        sprintf(path, "/session_%d.json", i);
        
        if (LittleFS.exists(path)) {
            String payload = readFileAsString(LittleFS, path);
            if (payload.length() > 0) {
                bleSendCommand("SESSION_DATA", payload);
            }
        }
    }
}

void saveSessionData(int idx, const SessionRecord &record) {
    char path[20];

    sprintf(path, "/session_%d.json", idx);
    String payload = buildSessionPayload(record);
    writeFile(LittleFS, path, payload.c_str());
}

void deleteSession() {
    Serial.println("Deleting all sessions...");
    for (int i = 0; i < MAX_SESSIONS; i++) {
        char path[20];
        sprintf(path, "/session_%d.json", i);
        LittleFS.remove(path);
    }
    
    // Force reset variables
    storedSessionCount = 0; 
    currentSessionIdx = 0;
    sessionStored = false;
    sessionSent = false;

    updateSessionCount();
    Serial.print("Verification - Stored sessions: ");
    Serial.println(storedSessionCount);
}

void setup()
{
    Serial.begin(115200);
    watch = TTGOClass::getWatch();
    watch->begin();
    watch->openBL();

    //Receive objects for easy writing
    tft = watch->tft;
    sensor = watch->bma;
    
    initHikeWatch();
    updateSessionCount();
    state = 1;

    bleInit("Hiking Watch");
}

void loop()
{
    switch (state)
    {
    case 1:
{
    /* Initial stage */
    updateSessionCount(); 
    
    // Clear hardware interrupt state before entering the loop
    watch->power->readIRQ();
    watch->power->clearIRQ();
    irqButton = false;

    // Draw your original Start Screen
    watch->tft->fillScreen(TFT_BLACK);
    watch->tft->setTextFont(4);
    watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
    watch->tft->drawString("Hiking Assistant", 25, 25, 4);
    watch->tft->drawString("Press side button", 18, 80);
    watch->tft->drawString("or tap screen", 37, 110);
    drawTouchButton(START_TOUCH_BUTTON);

    bool exitSync = false;
    bool startTouchActive = false;
    int16_t startTouchX = 0;
    int16_t startTouchY = 0;

    while (!exitSync) 
    {
        bleProcess();

        /* BLE sync */
        if (bleIsClientConnected() && sessionStored && !sessionSent) {
            sendSessionBLE();
            sessionSent = true;
        }

        String command = bleTakePendingCommand();
        if (command.length() > 0) {

            if (command == "SYNC_ACK") {
                Serial.println("Received SYNC_ACK - deleting local sessions");
                deleteSession();

                // Reset hardware flags so button works after sync
                watch->power->readIRQ();
                watch->power->clearIRQ();
                irqButton = false;

                exitSync = true;
                break;
            }

            if (command == "SYNC_ERROR") {
                sessionSent = false;
                watch->tft->fillScreen(TFT_RED);
                watch->tft->setTextColor(TFT_WHITE, TFT_RED);
                watch->tft->drawString("SYNC ERROR", 55, 85, 4);
                watch->tft->drawString("Retrying...", 60, 115, 2);
                delay(1200);

                watch->tft->fillScreen(TFT_BLACK);
                watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
                watch->tft->drawString("Hiking Assistant", 45, 25, 4);
                watch->tft->drawString("Press side button", 18, 80);
                watch->tft->drawString("or tap screen", 37, 110);
                drawTouchButton(START_TOUCH_BUTTON);
            }
        }

        if (!bleIsClientConnected()) {
            sessionSent = false;
        }

        if (touchButtonReleased(START_TOUCH_BUTTON, startTouchActive, startTouchX, startTouchY)) {
            updateSessionCount();

            if (storedSessionCount >= MAX_SESSIONS) {
                watch->tft->fillScreen(TFT_RED);
                watch->tft->setTextColor(TFT_WHITE);
                watch->tft->drawString("MEMORY FULL!", 45, 80, 4);
                watch->tft->drawString("Sync with RPi", 45, 110);
                delay(3000);

                exitSync = true;
            } else {
                state = 2;
                exitSync = true;
            }
        }

        /* Button Handling */
        if (irqButton) {
            irqButton = false;
            watch->power->readIRQ(); // Check hardware register
            
            // Only process if it's a short press
            if (watch->power->isPEKShortPressIRQ()) {
                updateSessionCount(); 

                if (storedSessionCount >= MAX_SESSIONS) {
                    // Draw Warning without permanently changing UI
                    watch->tft->fillScreen(TFT_RED);
                    watch->tft->setTextColor(TFT_WHITE);
                    watch->tft->drawString("MEMORY FULL!", 45, 80, 4);
                    watch->tft->drawString("Sync with RPi", 45, 110);
                    delay(3000); 
                    
                    exitSync = true; // Redraw the normal black screen
                } else {
                    state = 2; // Allow starting hike
                    exitSync = true;
                }
            }
            watch->power->clearIRQ(); // Important to release the interrupt line
        }
        delay(10); 
    }
    break; 
}
    case 2:
    {
        /* Hiking session initalisation */
        activeSessionId = generateUuidV4();
        activeSessionStartTime = currentIso8601();
        activeSessionStartMillis = millis();
        
        state = 3;
        break;
    }
    case 3:
    {
        /* Hiking session ongoing */
        watch->tft->fillRect(0, 0, 240, 240, TFT_BLACK);
        watch->tft->drawString("Starting hike", 45, 100);
        delay(1000);
        watch->tft->fillRect(0, 0, 240, 240, TFT_BLACK);

        watch->tft->setCursor(45, 70);
        watch->tft->print("Steps: 0");

        watch->tft->setCursor(45, 100);
        watch->tft->print("Dist: 0.0 km");
        watch->tft->setCursor(30, 140);
        drawTouchButton(END_TOUCH_BUTTON);

        uint32_t stepCount = 0;
        float distanceKm = 0.0f;
        unsigned long lastStepPoll = 0;
        bool endTouchActive = false;
        int16_t endTouchX = 0;
        int16_t endTouchY = 0;
        
        // Ensure IRQ is clean before starting the loop
        watch->power->readIRQ();
        watch->power->clearIRQ();
        irqButton = false; 

        while (state == 3) {
            // Handle Step Interrupt
            if (irqBMA) {
                irqBMA = false;
                if (sensor->readInterrupt() && sensor->isStepCounter()) {
                    stepCount = sensor->getCounter();
                    distanceKm = stepsToKilometers(stepCount);
                    watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
                    watch->tft->setCursor(120, 70); // Update just the number
                    watch->tft->print(stepCount);
                    watch->tft->print("   ");
                    watch->tft->fillRect(95, 100, 110, 20, TFT_BLACK);
                    watch->tft->setCursor(95, 100);
                    watch->tft->print(distanceKm, 1);
                    watch->tft->print(" km");
                }
            }
            if (touchButtonReleased(END_TOUCH_BUTTON, endTouchActive, endTouchX, endTouchY)) {
                SessionRecord record;
                record.sessionId = activeSessionId;
                record.startTime = activeSessionStartTime;
                record.endTime = currentIso8601();
                record.steps = stepCount;
                record.distanceMeters = stepsToMeters(stepCount);
                record.durationSeconds = (millis() - activeSessionStartMillis) / 1000;

                saveSessionData(currentSessionIdx, record);

                currentSessionIdx++;
                if (currentSessionIdx >= MAX_SESSIONS) currentSessionIdx = 0;

                updateSessionCount();

                sessionStored = true;
                state = 4;
            }

            // Handle Button Press to END session
            if (irqButton) {
                irqButton = false;
                watch->power->readIRQ();
                
                // Only act if it was a short press
                if (watch->power->isPEKShortPressIRQ()) {
                    // 1. Save using the current index
                    SessionRecord record;
                    record.sessionId = activeSessionId;
                    record.startTime = activeSessionStartTime;
                    record.endTime = currentIso8601();
                    record.steps = stepCount;
                    record.distanceMeters = stepsToMeters(stepCount);
                    record.durationSeconds = (millis() - activeSessionStartMillis) / 1000;

                    saveSessionData(currentSessionIdx, record);
                    
                    // 2. Increment index for NEXT time (FIFO)
                    currentSessionIdx++;
                    if (currentSessionIdx >= MAX_SESSIONS) currentSessionIdx = 0;
                    
                    // 3. Update the total count for Case 1 guard
                    updateSessionCount();
                    
                    sessionStored = true;
                    state = 4; // Move to save/exit state
                }
                watch->power->clearIRQ();
            }
            delay(50); // Small delay to prevent CPU hogging
        }
        break; 
    }
    case 4:
    {
        //Save hiking session data
        delay(1000);
        state = 1;  
        break;
    }
    default:
        // Restart watch
        ESP.restart();
        break;
    }
}