#include "stepCounter.h"

// Global variables
uint32_t currentStepCount = 0;
volatile bool stepInterruptFlag = false;

// External references (defined in main.cpp)
extern BMA *sensor;
extern volatile bool irqBMA;

void initStepCounter()
{
    Serial.println("initStepCounter: starting");
    // Accel parameter structure
    Acfg cfg;
    cfg.odr = BMA423_ODR;
    cfg.range = BMA423_RANGE;
    cfg.bandwidth = BMA423_BANDWIDTH;
    cfg.perf_mode = BMA423_PERF_MODE;

    // Configure the BMA423 accelerometer
    sensor->accelConfig(cfg);

    Serial.println("initStepCounter: accel configured");

    // Enable BMA423 accelerometer
    // Warning : Need to use steps, you must first enable the accelerometer
    sensor->enableAccel();
    Serial.println("initStepCounter: accel enabled");

    // Setup BMA423 interrupt pin
    // Use INPUT_PULLUP to avoid floating pin and spurious interrupts
    pinMode(BMA423_INT1, INPUT_PULLUP);
    attachInterrupt(BMA423_INT1, [] {
        // Set interrupt to set irqBMA value to 1
        irqBMA = true;
    }, RISING); //It must be a rising edge

    Serial.println("initStepCounter: interrupt attached (irq pin set)");

    // Enable BMA423 step count feature
    sensor->enableFeature(BMA423_STEP_CNTR, true);
    Serial.println("initStepCounter: step counter feature enabled");

    // Reset steps
    sensor->resetStepCounter();
    Serial.println("initStepCounter: hardware step counter reset");

    // Turn on step interrupt
    sensor->enableStepCountInterrupt();
    Serial.println("initStepCounter: step count interrupt enabled");

    currentStepCount = 0;
    stepInterruptFlag = false;
    Serial.println("initStepCounter: done");
}

void handleStepCounterInterrupt()
{
    if (irqBMA) {
        Serial.println("handleStepCounterInterrupt: irqBMA detected");
        irqBMA = false;
        bool rlst;
        do {
            // Read the BMA423 interrupt status,
            // need to wait for it to return to true before continuing
            rlst = sensor->readInterrupt();
            Serial.print("handleStepCounterInterrupt: readInterrupt returned ");
            Serial.println(rlst);
        } while (!rlst);

        // Check if it is a step interrupt
        if (sensor->isStepCounter()) {
            // Get step data from register
            uint32_t newCount = sensor->getCounter();
            Serial.print("handleStepCounterInterrupt: step interrupt, sensor counter=");
            Serial.println(newCount);
            currentStepCount = newCount;
            stepInterruptFlag = true;
            Serial.println("handleStepCounterInterrupt: updated currentStepCount and set flag");
        } else {
            Serial.println("handleStepCounterInterrupt: interrupt not step_counter");
        }
    }
}

uint32_t getStepCount()
{
    // Return last known count (debug print kept minimal)
    return currentStepCount;
}

void resetStepCount()
{
    Serial.println("resetStepCount: resetting currentStepCount and hardware counter");
    currentStepCount = 0;
    sensor->resetStepCounter();
    // Read back to confirm
    uint32_t after = sensor->getCounter();
    Serial.print("resetStepCount: hardware counter after reset=");
    Serial.println(after);
}

float stepsToKilometers(uint32_t steps)
{
    return (float)steps / STEPS_PER_KM;
}
