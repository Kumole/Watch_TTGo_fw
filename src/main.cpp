#include "config.h"
#include "stepCounter.h"
#include <BluetoothSerial.h>
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

// Classic Bluetooth serial (RFCOMM) for compatibility with RPi receiver
BluetoothSerial SerialBT;

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

bool sessionSent = false;

String activeSessionId;
String activeSessionStartTime;
unsigned long activeSessionStartMillis = 0;
uint32_t activeSessionElapsedBeforeResume = 0;
uint32_t activeSessionBaseSteps = 0;
int activeSessionFileIdx = -1;
bool resumeSessionOnBoot = false;
const unsigned long SESSION_CHECKPOINT_INTERVAL_MS = 10000;
unsigned long lastSessionCheckpointAt = 0;

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

const int16_t STEPS_LABEL_X = 45;
const int16_t STEPS_Y = 70;
const int16_t STEPS_VALUE_X = 120;
const int16_t DIST_LABEL_X = 45;
const int16_t DIST_Y = 100;
const int16_t DIST_VALUE_X = 95;

int currentSessionIdx = 0;
int storedSessionCount = 0;
const int MAX_SESSIONS = 100;
const char *SESSION_STATE_PATH = "/session_state.txt";

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
    json += "\"duration_s\":" + String(record.durationSeconds) + ",";
    json += "\"in_progress\":" + String(record.endTime.length() == 0 ? "true" : "false");
    json += "}";
    return json;
}

String readFileAsString(fs::FS &fs, const char *path)
{
    if (!fs.exists(path)) {
        return "";
    }

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

bool parseSessionFileIndex(const String &rawName, int &idx)
{
    String name = rawName;
    if (name.startsWith("/")) {
        name.remove(0, 1);
    }

    if (!name.startsWith("session_") || !name.endsWith(".json")) {
        return false;
    }

    int prefixLen = strlen("session_");
    int suffixPos = name.lastIndexOf(".json");
    if (suffixPos <= prefixLen) {
        return false;
    }

    String numberPart = name.substring(prefixLen, suffixPos);
    for (int i = 0; i < numberPart.length(); i++) {
        if (!isDigit(numberPart.charAt(i))) {
            return false;
        }
    }

    idx = numberPart.toInt();
    return idx >= 0 && idx < MAX_SESSIONS;
}

String getStateValue(const String &content, const char *key)
{
    String prefix = String(key) + "=";
    int start = content.indexOf(prefix);
    if (start < 0) {
        return "";
    }

    start += prefix.length();
    int end = content.indexOf('\n', start);
    if (end < 0) {
        end = content.length();
    }

    String value = content.substring(start, end);
    value.trim();
    return value;
}

void saveRuntimeState(bool sessionActive)
{
    int nextIdx = currentSessionIdx;
    if (nextIdx < 0 || nextIdx >= MAX_SESSIONS) {
        nextIdx = 0;
    }

    String serialized;
    serialized += "active=" + String(sessionActive ? 1 : 0) + "\n";
    serialized += "next_idx=" + String(nextIdx) + "\n";
    serialized += "active_idx=" + String(activeSessionFileIdx) + "\n";
    serialized += "session_id=" + activeSessionId + "\n";
    serialized += "start_time=" + activeSessionStartTime + "\n";
    serialized += "elapsed_s=" + String(activeSessionElapsedBeforeResume) + "\n";
    serialized += "steps=" + String(activeSessionBaseSteps) + "\n";
    writeFile(LittleFS, SESSION_STATE_PATH, serialized.c_str());
}

void loadRuntimeState()
{
    if (!LittleFS.exists(SESSION_STATE_PATH)) {
        return;
    }

    String content = readFileAsString(LittleFS, SESSION_STATE_PATH);
    if (content.length() == 0) {
        return;
    }

    String nextIdxRaw = getStateValue(content, "next_idx");
    if (nextIdxRaw.length() > 0) {
        int parsedNextIdx = nextIdxRaw.toInt();
        if (parsedNextIdx >= 0 && parsedNextIdx < MAX_SESSIONS) {
            currentSessionIdx = parsedNextIdx;
        }
    }

    if (getStateValue(content, "active").toInt() != 1) {
        return;
    }

    int restoredIdx = getStateValue(content, "active_idx").toInt();
    if (restoredIdx < 0 || restoredIdx >= MAX_SESSIONS) {
        return;
    }

    char activePath[20];
    sprintf(activePath, "/session_%d.json", restoredIdx);
    if (!LittleFS.exists(activePath)) {
        return;
    }

    activeSessionFileIdx = restoredIdx;
    activeSessionId = getStateValue(content, "session_id");
    activeSessionStartTime = getStateValue(content, "start_time");
    activeSessionElapsedBeforeResume = static_cast<uint32_t>(getStateValue(content, "elapsed_s").toInt());
    activeSessionBaseSteps = static_cast<uint32_t>(getStateValue(content, "steps").toInt());
    // Ensure sensor hardware counter is accounted for: if the hardware counter
    // contains any counts accumulated while the device was booting (or before
    // we reset it), add them to the base and then reset the hardware counter.
    uint32_t hwCounter = 0;
    if (sensor != nullptr) {
        hwCounter = sensor->getCounter();
        if (hwCounter != 0) {
            Serial.print("LOAD_STATE: hardware counter non-zero on load: ");
            Serial.println(hwCounter);
            Serial.print("LOAD_STATE: adding to activeSessionBaseSteps (before)=");
            Serial.println(activeSessionBaseSteps);
            activeSessionBaseSteps += hwCounter;
            Serial.print("LOAD_STATE: activeSessionBaseSteps (after)=");
            Serial.println(activeSessionBaseSteps);
        }
        // Reset the hardware counter so subsequent readings start from zero
        resetStepCount();
        Serial.println("LOAD_STATE: hardware counter reset after restoring state");
    }
    activeSessionStartMillis = millis();
    // Prevent immediate checkpointing flood by initializing checkpoint timestamp
    lastSessionCheckpointAt = millis();
    resumeSessionOnBoot = true;

    Serial.print("Resuming active session from file index: ");
    Serial.println(activeSessionFileIdx);
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

void renderSessionMetrics(uint32_t stepCount)
{
    float distanceKm = stepsToKilometers(stepCount);

    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setCursor(STEPS_VALUE_X, STEPS_Y);
    tft->print(stepCount);
    tft->print("   ");

    tft->fillRect(DIST_VALUE_X, DIST_Y, 110, 20, TFT_BLACK);
    tft->setCursor(DIST_VALUE_X, DIST_Y);
    tft->print(distanceKm, 1);
    tft->print(" km");
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
    int highestSessionIdx = -1;

    fs::File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        return;
    }

    fs::File file = root.openNextFile();
    while (file) {
        String name = file.name();
        int parsedIdx = -1;
        if (!file.isDirectory() && parseSessionFileIndex(name, parsedIdx)) {
            storedSessionCount++;
            if (parsedIdx > highestSessionIdx) {
                highestSessionIdx = parsedIdx;
            }
        }
        file = root.openNextFile();
    }

    if (!LittleFS.exists(SESSION_STATE_PATH) && highestSessionIdx >= 0) {
        currentSessionIdx = highestSessionIdx + 1;
        if (currentSessionIdx >= MAX_SESSIONS) {
            currentSessionIdx = 0;
        }
    }

    Serial.print("Current session count: ");
    Serial.println(storedSessionCount);
    Serial.print("Next session index: ");
    Serial.println(currentSessionIdx);
}

bool sendNextFinishedSessionBT() {
    if (storedSessionCount == 0) {
        Serial.println("SYNC: no stored sessions to send");
        return false;
    }

    for (int i = 0; i < MAX_SESSIONS; i++) {
        char path[20];
        sprintf(path, "/session_%d.json", i);

        if (!LittleFS.exists(path)) {
            continue;
        }

        String payload = readFileAsString(LittleFS, path);
        if (payload.length() == 0) {
            continue;
        }

        if (payload.indexOf("\"in_progress\":true") >= 0) {
            continue;
        }

        Serial.print("SYNC: sending one session from ");
        Serial.println(path);

        SerialBT.print(payload);
        SerialBT.print('\n');
        return true;
    }

    Serial.println("SYNC: no finished sessions found");
    return false;
}

void saveSessionData(int idx, const SessionRecord &record) {
    char path[20];

    sprintf(path, "/session_%d.json", idx);
    String payload = buildSessionPayload(record);
    Serial.println("SAVE_SESSION");
    writeFile(LittleFS, path, payload.c_str());
}

void deleteSession() {
    Serial.println("Deleting all sessions...");
    for (int i = 0; i < MAX_SESSIONS; i++) {
        char path[20];
        sprintf(path, "/session_%d.json", i);
        if (LittleFS.exists(path)) {
            LittleFS.remove(path);
        }
    }
    
    // Force reset variables
    storedSessionCount = 0; 
    currentSessionIdx = 0;
    activeSessionFileIdx = -1;
    activeSessionElapsedBeforeResume = 0;
    activeSessionBaseSteps = 0;
    activeSessionId = "";
    activeSessionStartTime = "";
    resumeSessionOnBoot = false;
    sessionSent = false;

    if (LittleFS.exists(SESSION_STATE_PATH)) {
        LittleFS.remove(SESSION_STATE_PATH);
    }

    updateSessionCount();
    Serial.print("Verification - Stored sessions: ");
    Serial.println(storedSessionCount);
}

void setup()
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

    //Receive objects for easy writing
    tft = watch->tft;
    sensor = watch->bma;
    
    tft->fillScreen(TFT_BLACK);
    tft->setTextFont(4);
    tft->setTextSize(1);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    
    initHikeWatch();
    updateSessionCount();
    loadRuntimeState();
    state = resumeSessionOnBoot ? 3 : 1;

    // Classic Bluetooth serial
    SerialBT.begin("Hiking Watch");

    Serial.println("SETUP DONE");
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
                // Classic Bluetooth sync via SerialBT
                if (SerialBT.available()) {
                    char incoming = SerialBT.read();
                    if (incoming == 'c' && storedSessionCount > 0 && !sessionSent) {
                        sendNextFinishedSessionBT();
                    }
                    
                    if (incoming == 'r') {
                        Serial.println("Received R - Sync Complete");
                        sessionSent = true;
                        deleteSession();

                        // Reset hardware flags so button works after sync
                        watch->power->readIRQ();
                        watch->power->clearIRQ();
                        irqButton = false;

                        exitSync = true;
                        break;
                    }
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
            resetStepCount();
            activeSessionId = generateUuidV4();
            activeSessionStartTime = currentIso8601();
            activeSessionStartMillis = millis();
            activeSessionElapsedBeforeResume = 0;
            activeSessionBaseSteps = 0;
            activeSessionFileIdx = currentSessionIdx;

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
            
            state = 3;
            break;
        }
        case 3:
        {
            /* Hiking session ongoing */
            watch->tft->setTextFont(4);
            watch->tft->setTextSize(1);
            watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
            watch->tft->fillScreen(TFT_BLACK);
            
            if (resumeSessionOnBoot) {
                watch->tft->drawString("Resuming hike", 45, 100);
            } else {
                watch->tft->drawString("Starting hike", 45, 100);
            }
            delay(1000);
            watch->tft->fillScreen(TFT_BLACK);

            watch->tft->setCursor(STEPS_LABEL_X, STEPS_Y);
            watch->tft->print("Steps: 0");

            watch->tft->setCursor(DIST_LABEL_X, DIST_Y);
            watch->tft->print("Dist: 0.0 km");
            watch->tft->setCursor(30, 140);
            drawTouchButton(END_TOUCH_BUTTON);

            if (activeSessionFileIdx < 0 || activeSessionFileIdx >= MAX_SESSIONS) {
                activeSessionFileIdx = currentSessionIdx;
            }

            uint32_t stepCount = activeSessionBaseSteps + sensor->getCounter();
            bool endTouchActive = false;
            int16_t endTouchX = 0;
            int16_t endTouchY = 0;

            renderSessionMetrics(stepCount);
            
            // Ensure IRQ is clean before starting the loop
            watch->power->readIRQ();
            watch->power->clearIRQ();
            irqButton = false; 

            while (state == 3) {
                // Handle Step Interrupt
                if (irqBMA) {
                    Serial.println("MAIN: irqBMA true - handling step interrupt");
                    irqBMA = false;
                    if (sensor->readInterrupt() && sensor->isStepCounter()) {
                        stepCount = activeSessionBaseSteps + sensor->getCounter();
                        Serial.print("MAIN: sensor->getCounter()=");
                        Serial.println(sensor->getCounter());
                        Serial.print("MAIN: computed stepCount=");
                        Serial.println(stepCount);
                        renderSessionMetrics(stepCount);
                    } else {
                        Serial.println("MAIN: irqBMA but no step counter interrupt reported by sensor");
                    }
                }
                if (touchButtonReleased(END_TOUCH_BUTTON, endTouchActive, endTouchX, endTouchY)) {
                    SessionRecord record;
                    record.sessionId = activeSessionId;
                    record.startTime = activeSessionStartTime;
                    record.endTime = currentIso8601();
                    record.steps = stepCount;
                    record.distanceMeters = stepsToMeters(stepCount);
                    record.durationSeconds = activeSessionElapsedBeforeResume + ((millis() - activeSessionStartMillis) / 1000);

                    saveSessionData(activeSessionFileIdx, record);

                    currentSessionIdx = activeSessionFileIdx + 1;
                    if (currentSessionIdx >= MAX_SESSIONS) currentSessionIdx = 0;

                    activeSessionFileIdx = -1;
                    activeSessionElapsedBeforeResume = 0;
                    activeSessionBaseSteps = 0;
                    resumeSessionOnBoot = false;

                    saveRuntimeState(false);

                    updateSessionCount();

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
                        record.durationSeconds = activeSessionElapsedBeforeResume + ((millis() - activeSessionStartMillis) / 1000);

                        saveSessionData(activeSessionFileIdx, record);
                        
                        // 2. Increment index for NEXT time (FIFO)
                        currentSessionIdx = activeSessionFileIdx + 1;
                        if (currentSessionIdx >= MAX_SESSIONS) currentSessionIdx = 0;

                        activeSessionFileIdx = -1;
                        activeSessionElapsedBeforeResume = 0;
                        activeSessionBaseSteps = 0;
                        resumeSessionOnBoot = false;

                        saveRuntimeState(false);

                        // 3. Update the total count for Case 1 guard
                        updateSessionCount();
                        
                        state = 4; // Move to save/exit state
                    }
                    watch->power->clearIRQ();
                }

                if (state == 3 && (millis() - lastSessionCheckpointAt) >= SESSION_CHECKPOINT_INTERVAL_MS) {
                    uint32_t elapsed = activeSessionElapsedBeforeResume + ((millis() - activeSessionStartMillis) / 1000);

                    SessionRecord checkpoint;
                    checkpoint.sessionId = activeSessionId;
                    checkpoint.startTime = activeSessionStartTime;
                    checkpoint.endTime = "";
                    checkpoint.steps = stepCount;
                    checkpoint.distanceMeters = stepsToMeters(stepCount);
                    checkpoint.durationSeconds = elapsed;
                    saveSessionData(activeSessionFileIdx, checkpoint);

                    Serial.print("MAIN: checkpoint saved, stepCount=");
                    Serial.println(stepCount);

                    activeSessionBaseSteps = stepCount;
                    activeSessionElapsedBeforeResume = elapsed;
                    activeSessionStartMillis = millis();
                    // Reset the sensor hardware counter after taking a checkpoint. The
                    // code uses: total_steps = activeSessionBaseSteps + sensor->getCounter();
                    // If we don't reset the hardware counter here, sensor->getCounter()
                    // remains cumulative and the next calculation will double-count.
                    resetStepCount();
                    Serial.println("MAIN: resetStepCount called after checkpoint");
                    saveRuntimeState(true);
                    lastSessionCheckpointAt = millis();
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
        {
            // Restart watch
            ESP.restart();
        }
    }
}