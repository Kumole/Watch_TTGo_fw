#include "app_bluetooth.h"
#include "app_context.h"
#include "app_storage.h"
#include "app_time.h"

void sendBluetoothLine(const String &line)
{
    SerialBT.print(line);
    SerialBT.print('\n');
}

bool sendNextFinishedSessionBT()
{
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

                sendBluetoothLine("SESSION|" + payload);
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

bool handleBluetoothCommand(const String &incoming)
{
    if (incoming.startsWith("TIME_SYNC|")) {
        String epochRaw = incoming.substring(String("TIME_SYNC|").length());
        epochRaw.trim();
        time_t epochSeconds = static_cast<time_t>(epochRaw.toInt());

        if (syncClockFromUnixEpoch(epochSeconds)) {
            sendBluetoothLine("TIME_SYNC_ACK|" + String(static_cast<unsigned long>(epochSeconds)));

            if (state == WatchState::RESUME_PENDING_TIME_SYNC) {
                activeSessionStartMillis = millis();
                lastSessionCheckpointAt = millis();
                state = WatchState::SESSION_ACTIVE;
            } else if (state == WatchState::IDLE_UNSYNCED) {
                state = WatchState::IDLE_READY;
            }
        } else {
            sendBluetoothLine("TIME_SYNC_NACK|invalid_epoch");
        }

        return true;
    }

    if (incoming == "SYNC_PULL") {
        if (!waitingForAck) {
            if (!sendNextFinishedSessionBT()) {
                Serial.println("SYNC: nothing to send, reporting completion");
                sendBluetoothLine("SYNC_DONE");
            }
        } else {
            Serial.println("SYNC: ignoring SYNC_PULL while waiting for ack");
        }
        return true;
    }

    if (incoming.startsWith("SESSION_ACK|")) {
        String ackId = incoming.substring(String("SESSION_ACK|").length());
        ackId.trim();

        if (!waitingForAck) {
            Serial.println("SYNC: ignoring unexpected session ack");
        } else if (ackId != pendingSessionId) {
            Serial.println("SYNC: ignoring ack for non-pending session");
        } else {
            if (deleteSessionById(ackId)) {
                waitingForAck = false;
                pendingSessionId = "";

                if (!sendNextFinishedSessionBT()) {
                    Serial.println("SYNC: all finished sessions acknowledged");
                    sendBluetoothLine("SYNC_DONE");
                }
            }
        }
        return true;
    }

    if (incoming.startsWith("HELLO|")) {
        sendBluetoothLine("HELLO_ACK|" + String(WATCH_BT_PROTOCOL_VERSION));
        return true;
    }

    return false;
}

void pollBluetooth()
{
    if (!SerialBT.available()) {
        return;
    }

    String incoming = SerialBT.readStringUntil('\n');
    incoming.trim();

    if (incoming.length() > 0) {
        Serial.print("SYNC: received command: ");
        Serial.println(incoming);
    }

    handleBluetoothCommand(incoming);
}