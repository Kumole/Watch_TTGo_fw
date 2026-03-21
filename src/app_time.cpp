#include "app_time.h"
#include "app_context.h"
#include "step_counter.h"

#include <esp_system.h>
#include <sys/time.h>

String generateUuidV4()
{
    uint8_t bytes[16];
    for (uint8_t i = 0; i < sizeof(bytes); i++) {
        bytes[i] = static_cast<uint8_t>(esp_random() & 0xFF);
    }

    // RFC4122 version and variant bits.
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

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

bool isClockCurrentlyValid()
{
    time_t now = time(nullptr);
    return clockSynced && now >= 946684800;
}

String getClockDisplayText()
{
    if (!isClockCurrentlyValid()) {
        return "Time: --:--:--";
    }

    time_t now = time(nullptr);
    struct tm localTime;
    localtime_r(&now, &localTime);

    char buffer[20];
    strftime(buffer, sizeof(buffer), "Time: %H:%M:%S", &localTime);
    return String(buffer);
}

bool syncClockFromUnixEpoch(time_t epochSeconds)
{
    if (epochSeconds < 946684800) {
        Serial.println("TIME_SYNC: rejected invalid epoch");
        return false;
    }

    struct timeval tv;
    tv.tv_sec = epochSeconds;
    tv.tv_usec = 0;

    if (settimeofday(&tv, nullptr) != 0) {
        Serial.println("TIME_SYNC: settimeofday failed");
        return false;
    }

    clockSynced = true;
    lastRenderedClockEpoch = ULONG_MAX;

    Serial.print("TIME_SYNC: clock synced to epoch ");
    Serial.println(static_cast<unsigned long>(epochSeconds));
    return true;
}

uint32_t stepsToMeters(uint32_t steps)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(steps) * 1000ULL) / STEPS_PER_KM);
}

uint32_t getCurrentElapsedSeconds()
{
    return activeSessionElapsedBeforeResume +
           static_cast<uint32_t>((millis() - activeSessionStartMillis) / 1000);
}