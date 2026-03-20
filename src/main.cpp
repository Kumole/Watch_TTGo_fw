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
uint32_t activeSessionBaseSteps = 0;   // used for resume-after-reboot base
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

bool waitingForAck = false;
String pendingSessionId = "";

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

String normalizeFsPath(const String &name)
{
    if (name.startsWith("/")) {
        return name;
    }
    return "/" + name;
}

String readFileAsString(fs::FS &fs, const char *path)
{
    if (!fs.exists(path)) {
        Serial.print("READ: file does not exist: ");
        Serial.println(path);
        return "";
    }

    fs::File file = fs.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        Serial.print("READ: failed to open file: ");
        Serial.println(path);
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
    writeFile(LittleFS, SESSION_STATE_PATH, serialized.c_str());
}

uint32_t getJsonUintValue(const String &json, const char *key)
{
    String pattern = String("\"") + key + "\":";
    int start = json.indexOf(pattern);
    if (start < 0) {
        return 0;
    }

    start += pattern.length();

    while (start < json.length() && isspace(json.charAt(start))) {
        start++;
    }

    int end = start;
    while (end < json.length() && isDigit(json.charAt(end))) {
        end++;
    }

    if (end == start) {
        return 0;
    }

    return static_cast<uint32_t>(json.substring(start, end).toInt());
}

void loadRuntimeState()
{
    Serial.println("LOAD_STATE: entered");

    if (!LittleFS.exists(SESSION_STATE_PATH)) {
        Serial.println("LOAD_STATE: state file missing, skipping");
        return;
    }

    String content = readFileAsString(LittleFS, SESSION_STATE_PATH);
    Serial.println("LOAD_STATE: raw content:");
    Serial.println(content);

    if (content.length() == 0) {
        Serial.println("LOAD_STATE: state file empty");
        return;
    }

    String nextIdxRaw = getStateValue(content, "next_idx");
    if (nextIdxRaw.length() > 0) {
        int parsedNextIdx = nextIdxRaw.toInt();
        if (parsedNextIdx >= 0 && parsedNextIdx < MAX_SESSIONS) {
            currentSessionIdx = parsedNextIdx;
        }
    }

    String activeRaw = getStateValue(content, "active");
    Serial.print("LOAD_STATE: active=");
    Serial.println(activeRaw);

    if (activeRaw.toInt() != 1) {
        Serial.println("LOAD_STATE: active is not 1");
        return;
    }

    int restoredIdx = getStateValue(content, "active_idx").toInt();
    Serial.print("LOAD_STATE: restoredIdx=");
    Serial.println(restoredIdx);

    if (restoredIdx < 0 || restoredIdx >= MAX_SESSIONS) {
        Serial.println("LOAD_STATE: active_idx invalid");
        return;
    }

    char activePath[20];
    sprintf(activePath, "/session_%d.json", restoredIdx);
    Serial.print("LOAD_STATE: checking ");
    Serial.println(activePath);

    if (!LittleFS.exists(activePath)) {
        Serial.println("LOAD_STATE: active session file missing");
        return;
    }

    String sessionJson = readFileAsString(LittleFS, activePath);
    if (sessionJson.length() == 0) {
        Serial.println("LOAD_STATE: active session file empty");
        return;
    }

    activeSessionFileIdx = restoredIdx;
    activeSessionId = getStateValue(content, "session_id");
    activeSessionStartTime = getStateValue(content, "start_time");
    activeSessionElapsedBeforeResume =
        static_cast<uint32_t>(getStateValue(content, "elapsed_s").toInt());

    // Restore step base from the actual checkpoint file, not session_state.txt
    activeSessionBaseSteps = getJsonUintValue(sessionJson, "steps");

    Serial.print("LOAD_STATE: restored steps from session file=");
    Serial.println(activeSessionBaseSteps);

    if (sensor != nullptr) {
        resetStepCount();
        Serial.println("LOAD_STATE: hardware counter reset after restoring state");
    }

    activeSessionStartMillis = millis();
    lastSessionCheckpointAt = millis();
    resumeSessionOnBoot = true;

    Serial.println("LOAD_STATE: resumeSessionOnBoot set to true");
    Serial.print("Resuming active session from file index: ");
    Serial.println(activeSessionFileIdx);
    Serial.print("Resuming base steps: ");
    Serial.println(activeSessionBaseSteps);
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
    if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
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
    if (storedSessionCount == 0 || waitingForAck) {
        return false;
    }

    fs::File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("SYNC: failed to open LittleFS root");
        return false;
    }

    fs::File file = root.openNextFile();
    while (file) {
        String name = file.name();
        String path = normalizeFsPath(name);
        int parsedIdx = -1;

        if (!file.isDirectory() && parseSessionFileIndex(name, parsedIdx)) {
            String payload = readFileAsString(LittleFS, path.c_str());

            if (payload.length() > 0 && payload.indexOf("\"in_progress\":true") < 0) {
                String sessionId = "";
                int start = payload.indexOf("\"session_id\":\"");
                if (start >= 0) {
                    start += strlen("\"session_id\":\"");
                    int end = payload.indexOf("\"", start);
                    if (end > start) {
                        sessionId = payload.substring(start, end);
                    }
                }

                Serial.print("SYNC: sending one session from ");
                Serial.println(path);

                SerialBT.print(payload);
                SerialBT.print('\n');

                waitingForAck = true;
                pendingSessionId = sessionId;

                file.close();
                root.close();
                return true;
            }
        }

        file = root.openNextFile();
    }

    root.close();
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

bool deleteSessionById(const String &sessionId) {
    fs::File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("SYNC: failed to open LittleFS root for delete");
        return false;
    }

    fs::File file = root.openNextFile();
    while (file) {
        String name = file.name();
        String path = normalizeFsPath(name);

        int parsedIdx = -1;
        if (!file.isDirectory() && parseSessionFileIndex(name, parsedIdx)) {
            String payload = readFileAsString(LittleFS, path.c_str());
            String pattern = "\"session_id\":\"" + sessionId + "\"";

            if (payload.indexOf(pattern) >= 0) {
                Serial.print("SYNC: deleting acknowledged session from ");
                Serial.println(path);

                file.close();
                root.close();

                if (LittleFS.remove(path)) {
                    updateSessionCount();
                    return true;
                } else {
                    Serial.println("SYNC: failed to delete acknowledged session");
                    return false;
                }
            }
        }

        file = root.openNextFile();
    }

    Serial.print("SYNC: acknowledged session id not found: ");
    Serial.println(sessionId);
    return false;
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

    SerialBT.begin("Hiking Watch");

    Serial.println("SETUP DONE");
}

void loop()
{
    switch (state)
    {
        case 1:
        {
            updateSessionCount();

            watch->power->readIRQ();
            watch->power->clearIRQ();
            irqButton = false;

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
                if (SerialBT.available()) {
                    String incoming = SerialBT.readStringUntil('\n');
                    incoming.trim();

                    if (incoming.length() > 0) {
                        Serial.print("SYNC: received command: ");
                        Serial.println(incoming);
                    }

                    if (incoming == "c") {
                        if (!waitingForAck) {
                            sendNextFinishedSessionBT();
                        } else {
                            Serial.println("SYNC: ignoring c while waiting for ack");
                        }
                    }
                    else if (incoming.startsWith("a:")) {
                        String ackId = incoming.substring(2);
                        ackId.trim();

                        if (!waitingForAck) {
                            Serial.println("SYNC: ignoring unexpected ack");
                        } else if (ackId != pendingSessionId) {
                            Serial.println("SYNC: ignoring ack for non-pending session");
                        } else {
                            if (deleteSessionById(ackId)) {
                                waitingForAck = false;
                                pendingSessionId = "";

                                if (!sendNextFinishedSessionBT()) {
                                    Serial.println("SYNC: all finished sessions acknowledged");
                                    sessionSent = true;

                                    watch->power->readIRQ();
                                    watch->power->clearIRQ();
                                    irqButton = false;

                                    exitSync = true;
                                    break;
                                }
                            }
                        }
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

                if (irqButton) {
                    irqButton = false;
                    watch->power->readIRQ();

                    if (watch->power->isPEKShortPressIRQ()) {
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
                    watch->power->clearIRQ();
                }
                delay(10);
            }
            break;
        }

        case 2:
        {
            resetStepCount();
            activeSessionId = generateUuidV4();
            activeSessionStartTime = currentIso8601();
            activeSessionStartMillis = millis();
            activeSessionElapsedBeforeResume = 0;
            activeSessionBaseSteps = 0;   // brand new session starts from zero base
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

            state = 3;
            break;
        }

        case 3:
        {
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

            watch->power->readIRQ();
            watch->power->clearIRQ();
            irqButton = false;

            while (state == 3) {
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

                if (irqButton) {
                    irqButton = false;
                    watch->power->readIRQ();

                    if (watch->power->isPEKShortPressIRQ()) {
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

                    // Save the latest checkpointed total for reboot recovery.
                    // Do NOT reset the hardware counter here.
                    activeSessionElapsedBeforeResume = elapsed;
                    saveRuntimeState(true);
                    lastSessionCheckpointAt = millis();
                }

                delay(50);
            }
            break;
        }

        case 4:
        {
            delay(1000);
            state = 1;
            break;
        }

        default:
        {
            ESP.restart();
        }
    }
}