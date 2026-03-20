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

    if (sensor == nullptr) {
        Serial.println("initStepCounter: sensor is null");
        return;
    }

    // Accel parameter structure
    Acfg cfg;
    cfg.odr = BMA423_ODR;
    cfg.range = BMA423_RANGE;
    cfg.bandwidth = BMA423_BANDWIDTH;
    cfg.perf_mode = BMA423_PERF_MODE;

    // Configure the BMA423 accelerometer
    sensor->accelConfig(cfg);
    Serial.println("initStepCounter: accel configured");

    // Warning: step counting requires accelerometer enabled first
    sensor->enableAccel();
    Serial.println("initStepCounter: accel enabled");

    // Setup BMA423 interrupt pin
    pinMode(BMA423_INT1, INPUT_PULLUP);
    attachInterrupt(BMA423_INT1, [] {
        irqBMA = true;
    }, RISING);

    Serial.println("initStepCounter: interrupt attached (irq pin set)");

    // Enable BMA423 step count feature
    sensor->enableFeature(BMA423_STEP_CNTR, true);
    Serial.println("initStepCounter: step counter feature enabled");

    // Reset hardware counter once during initialization
    sensor->resetStepCounter();
    delay(20);
    Serial.println("initStepCounter: hardware step counter reset");

    // Turn on step interrupt
    sensor->enableStepCountInterrupt();
    Serial.println("initStepCounter: step count interrupt enabled");

    currentStepCount = 0;
    stepInterruptFlag = false;
    irqBMA = false;

    Serial.println("initStepCounter: done");
}

void handleStepCounterInterrupt()
{
    if (!irqBMA) {
        return;
    }

    Serial.println("handleStepCounterInterrupt: irqBMA detected");
    irqBMA = false;

    if (sensor == nullptr) {
        Serial.println("handleStepCounterInterrupt: sensor is null");
        return;
    }

    bool interruptRead = sensor->readInterrupt();
    Serial.print("handleStepCounterInterrupt: readInterrupt returned ");
    Serial.println(interruptRead);

    if (!interruptRead) {
        return;
    }

    if (sensor->isStepCounter()) {
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

uint32_t getStepCount()
{
    return currentStepCount;
}

void resetStepCount()
{
    Serial.println("resetStepCount: resetting currentStepCount and hardware counter");

    currentStepCount = 0;
    stepInterruptFlag = false;

    if (sensor == nullptr) {
        Serial.println("resetStepCount: sensor is null");
        return;
    }

    sensor->resetStepCounter();
    delay(20);

    uint32_t after = sensor->getCounter();
    Serial.print("resetStepCount: hardware counter after reset=");
    Serial.println(after);
}

float stepsToKilometers(uint32_t steps)
{
    return (float)steps / STEPS_PER_KM;
}
