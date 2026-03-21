#include "config.h"
#include "app_init.h"
#include "app_state_machine.h"

// Check if Bluetooth configs are enabled
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

void setup()
{
    initializeApp();
}

void loop()
{
    runStateMachine();
}