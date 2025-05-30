#include "sunlight_lut.h"
#include "demand_response.h" // Include the DemandResponseStrategy header
#include <modbus.h> // Include the Modbus library for RS-485 communication
#include <curl/curl.h> // For HTTP API calls to the utility's limit order book
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>

// LUT arrays to hold sunrise and sunset times
double sunriseTable[DAYS_IN_YEAR];
double sunsetTable[DAYS_IN_YEAR];

// Modbus context
modbus_t *ctx;

// DemandResponseStrategy instance
DemandResponseStrategy dr_strategy;

// Price forecast storage
double price_forecast[24] = {0};
double grid_demand_forecast[24] = {0};
int num_competitors = 10; // Default value


// Generate the LUT for sunrise and sunset times
void generateSunlightLUT() {
    for (int day = 0; day < DAYS_IN_YEAR; day++) {
        // Calculate solar declination (in degrees)
        double declination = -23.44 * cos((2 * M_PI / 365.0) * (day + 10));

        // Calculate solar noon (in hours)
        double solarNoon = 12.0 - (LONGITUDE / 15.0) - TIMEZONE_OFFSET;

        // Calculate hour angle (in degrees)
        double hourAngle = acos(-tan(LATITUDE * M_PI / 180.0) * tan(declination * M_PI / 180.0)) * 180.0 / M_PI;

        // Calculate sunrise and sunset times (in hours)
        sunriseTable[day] = solarNoon - (hourAngle / 15.0);
        sunsetTable[day] = solarNoon + (hourAngle / 15.0);
    }
}

// Retrieve sunrise and sunset times for the current day
void getSunlightHours(double *sunrise, double *sunset) {
    // Get the current date
    time_t currentTime = time(NULL);
    struct tm *localTime = localtime(&currentTime);

    // Calculate the day of the year (0-364 for indexing)
    int dayOfYear = localTime->tm_yday;

    // Retrieve values from LUT
    *sunrise = sunriseTable[dayOfYear];
    *sunset = sunsetTable[dayOfYear];
}

// Function to fetch price forecasts from utility API
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    char *data = (char *)contents;
    
    // Very simple parsing (in a real system, use a proper JSON parser)
    char *price_start = strstr(data, "\"prices\":[");
    if (price_start) {
        price_start += 10; // Skip "\"prices\":["
        
        // Parse prices
        for (int i = 0; i < 24 && price_start != NULL; i++) {
            price_forecast[i] = strtod(price_start, &price_start);
            price_start = strchr(price_start, ',');
            if (price_start) price_start++;
        }
    }
    
    // Parse demand forecast
    char *demand_start = strstr(data, "\"demand\":[");
    if (demand_start) {
        demand_start += 10; // Skip "\"demand\":["
        
        // Parse demand values
        for (int i = 0; i < 24 && demand_start != NULL; i++) {
            grid_demand_forecast[i] = strtod(demand_start, &demand_start);
            demand_start = strchr(demand_start, ',');
            if (demand_start) demand_start++;
        }
    }
    
    // Parse competitor count
    char *comp_start = strstr(data, "\"competitors\":");
    if (comp_start) {
        comp_start += 14; // Skip "\"competitors\":"
        num_competitors = (int)strtol(comp_start, NULL, 10);
    }
    
    return realsize;
}

// Fetch market data from utility API
void fetchMarketData() {
    CURL *curl;
    CURLcode res;
    
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://opencbp.api.example.com/market_data");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "Failed to fetch market data: %s\n", curl_easy_strerror(res));
        }
        
        curl_easy_cleanup(curl);
    }
}

// RTOS task to handle SOC monitoring and anti-flutter protection
void SpoofSOC(void *pvParameters) {
    time_t currentTime;
    time_t lastSpoofTime = 0;
    uint16_t actualSOC;
    uint16_t batteryTemp;
    double previousSOC = 0.5; // Initial assumption
    
    // Initialize moving average filter for SOC readings
    #define FILTER_SIZE 5
    double socReadings[FILTER_SIZE] = {0.5, 0.5, 0.5, 0.5, 0.5};
    int filterIndex = 0;

    for (;;) {
        currentTime = time(NULL);

        // Read actual SOC from BMS via Modbus
        if (modbus_read_input_registers(ctx, 0x208, 1, &actualSOC) == -1) {
            fprintf(stderr, "Failed to read SOC register: %s\n", modbus_strerror(errno));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        // Read battery temperature (in 0.1°C)
        if (modbus_read_input_registers(ctx, 0x209, 1, &batteryTemp) == -1) {
            fprintf(stderr, "Failed to read temperature register: %s\n", modbus_strerror(errno));
            // Use default temperature of 25°C
            batteryTemp = 250;
        }
        
        // Apply moving average filter to SOC readings
        socReadings[filterIndex] = actualSOC / 100.0;
        filterIndex = (filterIndex + 1) % FILTER_SIZE;
        
        double filteredSOC = 0;
        for (int i = 0; i < FILTER_SIZE; i++) {
            filteredSOC += socReadings[i];
        }
        filteredSOC /= FILTER_SIZE;
        
        // Calculate SOC change for degradation tracking
        double socChange = fabs(filteredSOC - previousSOC);
        
        // Update SOC in the DR strategy
        dr_strategy.current_soc = filteredSOC;
        
        // Add to degradation tracking if significant change occurred
        if (socChange > 0.01) { // >1% SOC change
            // Detect depth of discharge and mean SOC
            double depth = socChange;
            double mean_soc = (filteredSOC + previousSOC) / 2.0;
            double temp_celsius = batteryTemp / 10.0;
            
            // Add to rainflow counting
            add_rainflow_cycle(&dr_strategy, depth, mean_soc, temp_celsius);
            
            // Update previous SOC
            previousSOC = filteredSOC;
        }

        // Enforce minimum SOC safety latch
        if (dr_strategy.current_soc < dr_strategy.min_soc) {
            printf("SOC below minimum threshold (%.1f%%). Disabling DR events.\n", 
                   dr_strategy.min_soc * 100);
                   
            // Disable DR events by writing to register
            modbus_write_register(ctx, 0x220, 0);
            
            // Log event
            FILE *logFile = fopen("/var/log/opencbp.log", "a");
            if (logFile) {
                fprintf(logFile, "[%ld] SOC below minimum threshold. DR events disabled.\n", currentTime);
                fclose(logFile);
            }
            
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Enforce anti-flutter timer
        if (difftime(currentTime, lastSpoofTime) >= SPOOF_INTERVAL_SECONDS) {
            // Allow DR events by setting lastSpoofTime
            lastSpoofTime = currentTime;
            
            // Log event
            FILE *logFile = fopen("/var/log/opencbp.log", "a");
            if (logFile) {
                fprintf(logFile, "[%ld] Anti-flutter timer reset. DR events enabled.\n", currentTime);
                fclose(logFile);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Run every second
    }
}

// RTOS task to handle Fast DR Dispatch
void FastDRDispatch(void *pvParameters) {
    time_t currentTime;
    bool isDemandResponseActive = false;
    double current_market_price = 0.0;
    double current_grid_demand = 0.0;

    for (;;) {
        currentTime = time(NULL);
        struct tm *localTime = localtime(&currentTime);
        int current_hour = localTime->tm_hour;
        
        // Update current price and demand from forecasts for the current hour
        current_market_price = price_forecast[current_hour];
        current_grid_demand = grid_demand_forecast[current_hour];

        // Check if DR events are active
        uint16_t dr_status = 0;
        if (modbus_read_input_registers(ctx, 0x220, 1, &dr_status) == -1) {
            fprintf(stderr, "Failed to read DR status register: %s\n", modbus_strerror(errno));
        } else {
            isDemandResponseActive = (dr_status > 0);
        }

        // Fast DR Dispatch Logic
        if (isDemandResponseActive) {
            double bid_capacity, bid_price;
            calculate_fast_dr_bid(&dr_strategy, current_market_price, current_grid_demand, 1.0, 
                                &bid_capacity, &bid_price);

            printf("Fast DR Dispatch: Capacity: %.2f kWh, Price: $%.4f/kWh\n", bid_capacity, bid_price);

            // Check if bid is valid (capacity > 0)
            if (bid_capacity > 0) {
                // Adjust discharge rate based on bid capacity
                uint16_t discharge_rate = (uint16_t)(bid_capacity * 100); // Scale for Modbus register
                if (modbus_write_register(ctx, 0x210, discharge_rate) == -1) {
                    fprintf(stderr, "Failed to write discharge rate: %s\n", modbus_strerror(errno));
                }
                
                // Send bid price to API
                char url[256];
                snprintf(url, sizeof(url), "https://opencbp.api.example.com/bid?capacity=%.2f&price=%.4f", 
                        bid_capacity, bid_price);
                
                CURL *curl = curl_easy_init();
                if (curl) {
                    curl_easy_setopt(curl, CURLOPT_URL, url);
                    curl_easy_setopt(curl, CURLOPT_POST, 1L);
                    CURLcode res = curl_easy_perform(curl);
                    if (res != CURLE_OK) {
                        fprintf(stderr, "Failed to submit bid: %s\n", curl_easy_strerror(res));
                    }
                    curl_easy_cleanup(curl);
                }
            } else {
                printf("Fast DR Dispatch: Not profitable to participate at current price.\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Run every second
    }
}

// RTOS task to handle Capacity Bidding
void CapacityBidding(void *pvParameters) {
    time_t currentTime;
    bool isDemandResponseActive = false;
    int expected_peak_hours[24] = {0};

    for (;;) {
        currentTime = time(NULL);
        struct tm *localTime = localtime(&currentTime);
        
        // Only run capacity bidding once per day at 2 AM
        if (localTime->tm_hour == 2 && localTime->tm_min == 0) {
            // Fetch latest market data
            fetchMarketData();
            
            // Identify peak hours (simple heuristic: top 6 hours by price)
            // In a real system, use more sophisticated forecasting
            double sorted_prices[24];
            memcpy(sorted_prices, price_forecast, 24 * sizeof(double));
            
            // Sort prices (simple bubble sort for clarity)
            for (int i = 0; i < 24; i++) {
                for (int j = 0; j < 23-i; j++) {
                    if (sorted_prices[j] < sorted_prices[j+1]) {
                        double temp = sorted_prices[j];
                        sorted_prices[j] = sorted_prices[j+1];
                        sorted_prices[j+1] = temp;
                    }
                }
            }
            
            // Set threshold for peak hours (price of 6th highest hour)
            double peak_threshold = sorted_prices[5];
            
            // Mark peak hours
            for (int i = 0; i < 24; i++) {
                expected_peak_hours[i] = (price_forecast[i] >= peak_threshold) ? 1 : 0;
            }
            
            // Calculate bids
            double bid_capacities[24];
            double bid_prices[24];
            
            calculate_cbp_strategy(&dr_strategy, price_forecast, expected_peak_hours, 24, 
                                 bid_capacities, bid_prices);
            
            // Submit bids to the utility
            printf("Capacity Bidding Program: Submitting day-ahead bids\n");
            for (int hour = 0; hour < 24; hour++) {
                if (bid_capacities[hour] > 0) {
                    printf("Hour %d: Capacity: %.2f kWh, Price: $%.4f/kWh\n", 
                           hour, bid_capacities[hour], bid_prices[hour]);
                           
                    // Submit bid via API
                    char url[256];
                    snprintf(url, sizeof(url), 
                             "https://opencbp.api.example.com/day_ahead_bid?hour=%d&capacity=%.2f&price=%.4f", 
                             hour, bid_capacities[hour], bid_prices[hour]);
                    
                    CURL *curl = curl_easy_init();
                    if (curl) {
                        curl_easy_setopt(curl, CURLOPT_URL, url);
                        curl_easy_setopt(curl, CURLOPT_POST, 1L);
                        curl_easy_perform(curl);
                        curl_easy_cleanup(curl);
                    }
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000)); // Check every minute
    }
}


void MarketDataUpdate(void *pvParameters) {
    time_t currentTime;
    time_t lastUpdateTime = 0;
    const int UPDATE_INTERVAL = 3600; // Update hourly
    
    for (;;) {
        currentTime = time(NULL);
        
        // Update market data every hour
        if (difftime(currentTime, lastUpdateTime) >= UPDATE_INTERVAL) {
            printf("Updating market data...\n");
            
            // Fetch latest market data
            fetchMarketData();
            
            // Update last fetch time
            lastUpdateTime = currentTime;
            
            // Log successful update
            FILE *logFile = fopen("/var/log/opencbp.log", "a");
            if (logFile) {
                // Find min and max price
                double min_price = price_forecast[0];
                double max_price = price_forecast[0];
                for (int i = 1; i < 24; i++) {
                    if (price_forecast[i] < min_price) min_price = price_forecast[i];
                    if (price_forecast[i] > max_price) max_price = price_forecast[i];
                }
                fprintf(logFile, "[%ld] Market data updated. Price range: $%.4f-$%.4f/kWh\n", 
                        currentTime, min_price, max_price);
                fclose(logFile);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000)); // Check every minute
    }
}

// Utility function to calculate expected revenue for a given hour
double calculateExpectedRevenue(int hour, double capacity) {
    // Get price forecast for the hour
    double price = price_forecast[hour];
    
    // Calculate expected grid demand
    double demand = grid_demand_forecast[hour];
    
    // Calculate probability of acceptance based on competition
    double acceptance_prob = 1.0 / (1.0 + (num_competitors * 0.1));
    
    // Expected revenue = price * capacity * probability of acceptance
    return price * capacity * acceptance_prob;
}

// Function to analyze historical data and update model parameters
void analyzeHistoricalData() {
    // In a real implementation, this would load historical data from a file
    // and perform statistical analysis to update parameters
    // For this example, we'll just set some reasonable defaults
    
    // Update competition factor based on historical bid acceptance rates
    dr_strategy.beta = 0.2;
    
    // Update markup scaling parameter based on historical profit margins
    dr_strategy.alpha = 0.3;
    
    // Update maximum grid demand based on historical peak
    dr_strategy.max_grid_demand = 50000.0;
    
    printf("Model parameters updated from historical data analysis\n");
}

// Submit the bid to the utility's limit order book
void submitBid(double bidPrice) {
    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        char url[256];
        snprintf(url, sizeof(url), "https://opencbp.api.example.com/api/bid?price=%.2f", bidPrice);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        // Perform the request
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "Failed to submit bid: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
}

// RTOS Initialization
void initSystem() {
    // Generate sunlight LUT
    generateSunlightLUT();

    // Initialize Modbus connection
    ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 1);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to create the libmodbus context\n");
        return;
    }
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return;
    }

    // Initialize DemandResponseStrategy with improved parameters
    DemandResponseStrategy_init(&dr_strategy, 6.5, 0.95);
    
    // Fetch initial market data
    fetchMarketData();

    // Run initial historical data analysis
    analyzeHistoricalData();

    // Create RTOS tasks
    xTaskCreate(SpoofSOC, "SpoofSOC", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(FastDRDispatch, "FastDRDispatch", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(CapacityBidding, "CapacityBidding", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    
    // Create a task for market data updates
    xTaskCreate(MarketDataUpdate, "MarketDataUpdate", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);

    // Start RTOS scheduler
    vTaskStartScheduler();

}



int main() {

    initSystem();

    while (1) {

        // Main loop (not reached in RTOS)

    }

}