#include "stepCounter.h"

// Global variables
uint32_t currentStepCount = 0;
volatile bool stepInterruptFlag = false;

// External references (defined in main.cpp)
extern BMA *sensor;
extern volatile bool irqBMA;

void initStepCounter()
{
    // Accel parameter structure
    Acfg cfg;
    cfg.odr = BMA423_ODR;
    cfg.range = BMA423_RANGE;
    cfg.bandwidth = BMA423_BANDWIDTH;
    cfg.perf_mode = BMA423_PERF_MODE;

    // Configure the BMA423 accelerometer
    sensor->accelConfig(cfg);

    // Enable BMA423 accelerometer
    // Warning : Need to use steps, you must first enable the accelerometer
    // Warning : Need to use steps, you must first enable the accelerometer
    // Warning : Need to use steps, you must first enable the accelerometer
    sensor->enableAccel();

    // Setup BMA423 interrupt pin
    pinMode(BMA423_INT1, INPUT);
    attachInterrupt(BMA423_INT1, [] {
        // Set interrupt to set irqBMA value to 1
        irqBMA = true;
    }, RISING); //It must be a rising edge

    // Enable BMA423 step count feature
    sensor->enableFeature(BMA423_STEP_CNTR, true);

    // Reset steps
    sensor->resetStepCounter();

    // Turn on step interrupt
    sensor->enableStepCountInterrupt();

    currentStepCount = 0;
    stepInterruptFlag = false;
}

void handleStepCounterInterrupt()
{
    if (irqBMA) {
        irqBMA = false;
        bool rlst;
        do {
            // Read the BMA423 interrupt status,
            // need to wait for it to return to true before continuing
            rlst = sensor->readInterrupt();
        } while (!rlst);

        // Check if it is a step interrupt
        if (sensor->isStepCounter()) {
            // Get step data from register
            currentStepCount = sensor->getCounter();
            stepInterruptFlag = true;
        }
    }
}

uint32_t getStepCount()
{
    return currentStepCount;
}

void resetStepCount()
{
    currentStepCount = 0;
    sensor->resetStepCounter();
}

float stepsToKilometers(uint32_t steps)
{
    return (float)steps / STEPS_PER_KM;
}
