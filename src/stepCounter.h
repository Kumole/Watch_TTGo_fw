#ifndef STEPCOUNTER_H
#define STEPCOUNTER_H

#include "config.h"

// BMA423 Configuration Parameters

/*!
    Output data rate in Hz, Optional parameters:
        - BMA4_OUTPUT_DATA_RATE_0_78HZ
        - BMA4_OUTPUT_DATA_RATE_1_56HZ
        - BMA4_OUTPUT_DATA_RATE_3_12HZ
        - BMA4_OUTPUT_DATA_RATE_6_25HZ
        - BMA4_OUTPUT_DATA_RATE_12_5HZ
        - BMA4_OUTPUT_DATA_RATE_25HZ
        - BMA4_OUTPUT_DATA_RATE_50HZ
        - BMA4_OUTPUT_DATA_RATE_100HZ
        - BMA4_OUTPUT_DATA_RATE_200HZ
        - BMA4_OUTPUT_DATA_RATE_400HZ
        - BMA4_OUTPUT_DATA_RATE_800HZ
        - BMA4_OUTPUT_DATA_RATE_1600HZ
*/
#define BMA423_ODR          BMA4_OUTPUT_DATA_RATE_100HZ

/*!
    G-range, Optional parameters:
        - BMA4_ACCEL_RANGE_2G
        - BMA4_ACCEL_RANGE_4G
        - BMA4_ACCEL_RANGE_8G
        - BMA4_ACCEL_RANGE_16G
*/
#define BMA423_RANGE        BMA4_ACCEL_RANGE_2G

/*!
    Bandwidth parameter, determines filter configuration, Optional parameters:
        - BMA4_ACCEL_OSR4_AVG1
        - BMA4_ACCEL_OSR2_AVG2
        - BMA4_ACCEL_NORMAL_AVG4
        - BMA4_ACCEL_CIC_AVG8
        - BMA4_ACCEL_RES_AVG16
        - BMA4_ACCEL_RES_AVG32
        - BMA4_ACCEL_RES_AVG64
        - BMA4_ACCEL_RES_AVG128
*/
#define BMA423_BANDWIDTH    BMA4_ACCEL_NORMAL_AVG4

/*!
    Filter performance mode, Optional parameters:
        - BMA4_CIC_AVG_MODE
        - BMA4_CONTINUOUS_MODE
*/
#define BMA423_PERF_MODE    BMA4_CONTINUOUS_MODE

// Step counter reference (steps per km) - adjust based on calibration
#define STEPS_PER_KM        1300

// Global step counter state
extern uint32_t currentStepCount;
extern volatile bool stepInterruptFlag;

/**
 * Initialize the BMA423 step counter
 * Configures accelerometer, enables step counting, and sets up interrupts
 */
void initStepCounter();

/**
 * Handle BMA423 step counter interrupt
 * Reads interrupt status and updates step count if step interrupt detected
 * Should be called when stepInterruptFlag is set to true
 */
void handleStepCounterInterrupt();

/**
 * Get current step count
 * @return Current step count value
 */
uint32_t getStepCount();

/**
 * Reset step counter to zero
 */
void resetStepCount();

/**
 * Convert steps to distance in kilometers
 * @param steps Number of steps
 * @return Distance in kilometers
 */
float stepsToKilometers(uint32_t steps);

#endif // STEPCOUNTER_H
