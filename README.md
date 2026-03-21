# Hiking Assistant Watch

Embedded hiking assistant implemented for the **LilyGo T-Watch 2020 V3** using **PlatformIO**.

The watch records hiking sessions locally, keeps unfinished sessions recoverable across reboot, and synchronizes finished sessions to a Raspberry Pi hub over **Classic Bluetooth RFCOMM**. The Raspberry Pi also sends the current Unix time so the watch can restore a trustworthy wall clock after boot.

## Features

- start and end hiking sessions from the watch UI
- count steps using the onboard accelerometer step counter
- estimate distance from step count
- persist sessions locally in LittleFS as JSON
- periodically checkpoint active sessions
- recover an unfinished session after reboot
- require fresh time synchronization before resuming a recovered session
- synchronize completed sessions one by one over Classic Bluetooth
- delete synchronized sessions only after explicit acknowledgment from the hub
- display live clock, steps, distance, and elapsed duration on screen

## Repository structure

```text
src/
  app_bluetooth.*      Classic Bluetooth protocol handling
  app_context.*        Shared global app state, constants, and hardware handles
  app_init.*           Watch and application initialization
  app_session.*        Session lifecycle logic
  app_state_machine.*  Top-level watch state machine
  app_storage.*        LittleFS session persistence and reboot recovery
  app_time.*           UUID, clock, time sync, and elapsed-time helpers
  app_types.h          Shared structs and enums
  app_ui.*             Screen rendering and touch helpers
  config.h             Project-wide constants and hardware configuration
  main.cpp             Minimal entry point
  step_counter.*       Step counter interrupt handling
  utils.*              Utility helpers such as file writing
````

## Build environment

This project uses **PlatformIO**.

### Build

```bash
pio run
```

### Upload firmware

Before uploading, make sure the correct serial port is configured.

You can either:

* set `upload_port = ...` in `platformio.ini`, or
* pass it on the command line

Example:

```bash
pio run --target upload --upload-port /dev/ttyUSB0
```

### Debug with Serial Monitor

Use PlatformIO Serial Monitor to inspect logs from the watch:

```bash
pio device monitor
```

or, with an explicit port:

```bash
pio device monitor --port /dev/ttyUSB0
```

The port shown by PlatformIO or your system when the watch is connected should also be used as the `upload_port`.

Typical startup logs include messages such as:

* `BOOT OK`
* `LOAD_STATE: ...`
* `TIME_SYNC: clock synced to epoch ...`
* `SYNC: received command: ...`

These logs are the main way to debug boot flow, reboot recovery, and Bluetooth synchronization.

## Runtime overview

At a high level, the watch does four things:

1. initializes hardware and local storage
2. loads any unfinished session from LittleFS
3. waits for Bluetooth time synchronization from the Raspberry Pi
4. runs a state machine for idle, active session, save, and recovery flows

The entry point is intentionally small:

* `setup()` calls `initializeApp()`
* `loop()` calls `runStateMachine()`

## Watch state machine

The top-level runtime states are defined in `WatchState`:

### `BOOTING`

Initial transient state during startup.

### `IDLE_UNSYNCED`

Idle screen is shown, but the watch clock is not yet trusted.
A new session cannot be started from this state.

### `IDLE_READY`

Idle screen is shown and the clock has been synchronized.
A new session may be started from this state.

### `RESUME_PENDING_TIME_SYNC`

An unfinished session was restored from storage after reboot, but the watch must receive fresh time sync before it can safely continue.

### `SESSION_STARTING`

Transient state used to create and persist a new in-progress session.

### `SESSION_ACTIVE`

A session is running. The watch updates step count, distance, and duration, checkpoints periodically, and allows the user to end the session.

### `SESSION_ENDING`

Transient state used to finalize and persist the session.

### `SESSION_SAVED`

Temporary confirmation state shown after saving a session.

## Session lifecycle

### Starting a session

When a session starts, the watch:

* resets the hardware step counter
* generates a UUID for the session
* records `start_time`
* initializes elapsed time and step baseline
* creates a JSON file in LittleFS with `in_progress = true`

### While active

While the session is active, the watch:

* consumes step-counter interrupts
* updates displayed metrics only when values actually change
* periodically checkpoints the active session
* listens for Bluetooth commands
* allows the user to end the session from the touch button or side button

### Checkpointing

Checkpointing happens every:

* `SESSION_CHECKPOINT_INTERVAL_MS = 10000`

Each checkpoint updates the session file with:

* current steps
* distance
* elapsed duration
* `in_progress = true`

It also stores enough runtime metadata so the session can be resumed after reboot.

### Ending a session

When the session ends, the watch:

* computes final steps and duration
* records `end_time`
* saves the final session JSON
* clears active-session runtime state
* advances the rotating session file index
* shows a `Session saved` confirmation screen

## Reboot recovery

If the watch reboots during an active session, the code restores the unfinished session from:

* `SESSION_STATE_PATH = "/session_state.txt"`
* the corresponding `session_<idx>.json` checkpoint file

Recovered values include:

* active session file index
* session ID
* session start time
* elapsed duration baseline
* step baseline restored from the checkpoint JSON

After reboot:

* the watch **does not trust its wall clock anymore**
* the watch enters `RESUME_PENDING_TIME_SYNC`
* the Raspberry Pi must send `TIME_SYNC|<unix_epoch>`
* only then does the watch continue in `SESSION_ACTIVE`

This avoids writing misleading timestamps after reboot.

## Storage format

Sessions are stored in LittleFS as individual JSON files:

* `session_0.json`
* `session_1.json`
* ...
* up to `MAX_SESSIONS - 1`

The watch also stores runtime recovery data in:

* `SESSION_STATE_PATH = "/session_state.txt"`

### Session JSON fields

Each session payload contains:

* `session_id`
* `start_time`
* `end_time`
* `steps`
* `distance_m`
* `duration_s`
* `clock_synced`
* `protocol_version`
* `in_progress`

### Important storage behavior

* active sessions are checkpointed as `in_progress = true`
* finished sessions are synchronized only when `in_progress = false`
* finished sessions are deleted only after the hub sends `SESSION_ACK|<session_id>`

## Bluetooth protocol

The watch communicates with the hub over **Classic Bluetooth serial**.

### Incoming commands handled by the watch

#### `HELLO|<protocol_version>`

The watch replies with:

```text
HELLO_ACK|<protocol_version>
```

#### `TIME_SYNC|<unix_epoch>`

The watch applies the received Unix epoch as system time.

Replies:

```text
TIME_SYNC_ACK|<unix_epoch>
```

or

```text
TIME_SYNC_NACK|invalid_epoch
```

#### `SYNC_PULL`

The watch sends the next finished session if one exists.
If no finished sessions are waiting, it replies:

```text
SYNC_DONE
```

#### `SESSION_ACK|<session_id>`

The watch deletes the acknowledged session and continues synchronization.
If no more finished sessions remain, it replies:

```text
SYNC_DONE
```

## UI overview

### Idle screen

Shows:

* clock banner
* title
* instructions
* start session button

From here the user can:

* tap `Start session`
* use the side button to begin

If the clock is not synchronized yet, the watch shows:

* `Clock not synced`
* `Connect to RPi`

If storage is full, the watch shows:

* `MEMORY FULL!`
* `Sync with RPi`

### Resume pending time sync screen

Shown after reboot recovery when the active session exists but wall-clock time is not yet trusted.

Displays:

* `Recovered hike`
* `Time sync needed`
* `Connect to RPi`

### Active session screen

Shows:

* clock banner
* steps
* distance
* elapsed time
* end session button

To reduce flicker, the screen redraws only when:

* the displayed second changes, or
* step count / duration changes

## Key constants and variables

This section summarizes the most important values in the watch code.

### Hardware handles

* `watch` — main T-Watch object
* `tft` — display handle
* `sensor` — accelerometer / step counter handle
* `SerialBT` — Classic Bluetooth serial interface

### State and interrupt flags

* `state` — current top-level `WatchState`
* `irqBMA` — accelerometer interrupt flag
* `irqButton` — side-button interrupt flag

### Active session runtime

* `activeSessionId` — current session UUID
* `activeSessionStartTime` — ISO-8601 start timestamp
* `activeSessionStartMillis` — `millis()` baseline for elapsed time
* `activeSessionElapsedBeforeResume` — elapsed duration restored from checkpoints
* `activeSessionBaseSteps` — restored step baseline after reboot
* `activeSessionFileIdx` — current LittleFS session file index
* `resumeSessionOnBoot` — whether an unfinished session was restored at startup
* `lastSessionCheckpointAt` — last checkpoint time in milliseconds

### Storage and sync

* `currentSessionIdx` — next rotating session file index
* `storedSessionCount` — number of session files currently present
* `MAX_SESSIONS = 100` — maximum session capacity
* `SESSION_STATE_PATH = "/session_state.txt"` — runtime recovery file
* `waitingForAck` — whether the watch is waiting for hub acknowledgment
* `pendingSessionId` — session currently awaiting `SESSION_ACK`

### Clock and rendering

* `clockSynced` — whether the watch has received trusted time since boot
* `lastRenderedClockEpoch` — last drawn second on the banner
* `SESSION_CHECKPOINT_INTERVAL_MS = 10000` — checkpoint interval

### UI constants

* `START_TOUCH_BUTTON` — start-session button geometry and styling
* `END_TOUCH_BUTTON` — end-session button geometry and styling
* `STEPS_LABEL_X`, `STEPS_Y` — steps label position
* `DIST_LABEL_X`, `DIST_Y` — distance label position
* `DUR_LABEL_X`, `DUR_Y` — duration label position
* `METRIC_VALUE_X` — x-position for metric values

## Debugging checklist

If the watch does not behave as expected, check Serial Monitor for these areas:

### Boot and initialization

Look for:

* `BOOT OK`
* `LOAD_STATE: ...`
* step counter initialization logs

### Time synchronization

Look for:

* `SYNC: received command: TIME_SYNC|...`
* `TIME_SYNC: clock synced to epoch ...`

### Session recovery

Look for:

* `LOAD_STATE: restored steps from session file=...`
* `LOAD_STATE: resumeSessionOnBoot set to true`

### Synchronization

Look for:

* `SYNC: received command: HELLO|...`
* `SYNC: received command: SYNC_PULL`
* `SYNC: sending one session from ...`
* `SYNC: all finished sessions acknowledged`

## Notes

* The watch loses trusted wall-clock time after reboot and must be synchronized again by the hub.
* Step counting relies on the step-counter interrupt path in `step_counter.*`.
* The project uses **Classic Bluetooth**, not BLE.
* Finished sessions are transferred one by one with explicit acknowledgment.
