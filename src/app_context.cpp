#include "app_context.h"

TTGOClass *watch = nullptr;
TFT_eSPI *tft = nullptr;
BMA *sensor = nullptr;
BluetoothSerial SerialBT;

volatile WatchState state = WatchState::BOOTING;
volatile bool irqBMA = false;
volatile bool irqButton = false;

String activeSessionId;
String activeSessionStartTime;
unsigned long activeSessionStartMillis = 0;
uint32_t activeSessionElapsedBeforeResume = 0;
uint32_t activeSessionBaseSteps = 0;
int activeSessionFileIdx = -1;
bool resumeSessionOnBoot = false;
unsigned long lastSessionCheckpointAt = 0;

const TouchButton START_TOUCH_BUTTON = {30, 145, 180, 60, TFT_DARKGREEN, TFT_WHITE, "Start session"};
const TouchButton END_TOUCH_BUTTON   = {30, 185, 180, 50, TFT_RED, TFT_WHITE, "End session"};

const int16_t STEPS_LABEL_X = 45;
const int16_t STEPS_Y = 78;
const int16_t DIST_LABEL_X = 45;
const int16_t DIST_Y = 108;
const int16_t DUR_LABEL_X = 45;
const int16_t DUR_Y = 138;
const int16_t METRIC_VALUE_X = 125;

int currentSessionIdx = 0;
int storedSessionCount = 0;
const int MAX_SESSIONS = 100;
const char *SESSION_STATE_PATH = "/session_state.txt";
bool waitingForAck = false;
String pendingSessionId;

bool clockSynced = false;
unsigned long lastRenderedClockEpoch = ULONG_MAX;

const unsigned long SESSION_CHECKPOINT_INTERVAL_MS = 10000;