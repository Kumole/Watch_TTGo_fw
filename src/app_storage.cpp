#include "app_storage.h"
#include "app_context.h"
#include "app_time.h"
#include "step_counter.h"

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

String buildSessionPayload(const SessionRecord &record)
{
    String json = "{";
    json += "\"session_id\":\"" + record.sessionId + "\",";
    json += "\"start_time\":\"" + record.startTime + "\",";
    json += "\"end_time\":\"" + record.endTime + "\",";
    json += "\"steps\":" + String(record.steps) + ",";
    json += "\"distance_m\":" + String(record.distanceMeters) + ",";
    json += "\"duration_s\":" + String(record.durationSeconds) + ",";
    json += "\"clock_synced\":" + String(isClockCurrentlyValid() ? "true" : "false") + ",";
    json += "\"protocol_version\":" + String(WATCH_BT_PROTOCOL_VERSION) + ",";
    json += "\"in_progress\":" + String(record.endTime.length() == 0 ? "true" : "false");
    json += "}";
    return json;
}

void updateSessionCount()
{
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

void saveSessionData(int idx, const SessionRecord &record)
{
    char path[20];
    sprintf(path, "/session_%d.json", idx);

    String payload = buildSessionPayload(record);
    Serial.println("SAVE_SESSION");
    writeFile(LittleFS, path, payload.c_str());
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

    // The checkpoint file is the source of truth for restored step count.
    activeSessionBaseSteps = getJsonUintValue(sessionJson, "steps");

    Serial.print("LOAD_STATE: restored steps from session file=");
    Serial.println(activeSessionBaseSteps);

    if (sensor != nullptr) {
        // Reset the hardware counter so future steps start from zero again.
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

bool deleteSessionById(const String &sessionId)
{
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
                }

                Serial.println("SYNC: failed to delete acknowledged session");
                return false;
            }
        }

        file = root.openNextFile();
    }

    Serial.print("SYNC: acknowledged session id not found: ");
    Serial.println(sessionId);
    return false;
}