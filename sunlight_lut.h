#ifndef SUNLIGHT_LUT_H
#define SUNLIGHT_LUT_H

#include <time.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#define DAYS_IN_YEAR 365
#define LATITUDE 37.7749    // Example: San Francisco, CA
#define LONGITUDE -122.4194 // Example: San Francisco, CA
#define TIMEZONE_OFFSET -8  // PST (adjust based on your location)

// LUT for sunrise and sunset times
extern double sunriseTable[DAYS_IN_YEAR];
extern double sunsetTable[DAYS_IN_YEAR];

// Constants for DR logic
#define SPOOF_INTERVAL_SECONDS 3600 // 1-hour anti-flutter timer
#define MIN_SOC 20                  // 20% SOC safety latch
#define MAX_DISCHARGE_RATE 100.0    // Maximum discharge rate in kW
#define BID_PRICE_FACTOR 0.01       // Base price factor ($/kWh)

// Functions
void generateSunlightLUT(void);
void getSunlightHours(double *sunrise, double *sunset);
void SpoofSOC(void *pvParameters);
void FastDRDispatch(void *pvParameters);
void CapacityBidding(void *pvParameters);

#endif // SUNLIGHT_LUT_H
