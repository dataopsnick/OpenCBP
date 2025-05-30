#ifndef DEMAND_RESPONSE_H
#define DEMAND_RESPONSE_H

#include <stdint.h>
#include <stdbool.h>

// Forward declaration for rainflow data structure
typedef struct RainflowCycle RainflowCycle;

typedef struct {
    double battery_capacity;        // Battery capacity in kWh
    double efficiency;              // Battery round-trip efficiency (0.0 to 1.0)
    double min_soc;                 // Minimum state of charge (0.0 to 1.0)
    double max_soc;                 // Maximum state of charge (0.0 to 1.0)
    double current_soc;             // Current state of charge (0.0 to 1.0)
    uint32_t cycle_count;           // Number of equivalent full cycles
    
    // Battery degradation parameters
    double replacement_cost;        // Cost of battery replacement ($)
    double k_delta_e1;             // LFP exponential model coefficient 1
    double k_delta_e2;             // LFP exponential model coefficient 2
    double cycles_to_eol;          // Number of cycles to end-of-life at reference conditions
    // Note: Using Millner (2010) exponential model for LFP batteries
    
    // Rainflow counting for degradation
    RainflowCycle* cycles;          // Array of rainflow cycles
    int cycle_array_size;           // Size of rainflow cycles array
    int cycle_count_index;          // Current index in cycle array
    
    // Market parameters
    double risk_factor;             // Risk premium for uncertainty
    double alpha;                   // Scaling parameter for markup function
    double beta;                    // Competition factor for markup function
    double max_grid_demand;         // Maximum historical grid demand
} DemandResponseStrategy;

// Rainflow cycle structure for battery degradation tracking
typedef struct RainflowCycle {
    double depth;                   // Depth of discharge (0.0 to 1.0)
    double mean_soc;                // Mean SOC during cycle (0.0 to 1.0)
    double temperature;             // Temperature during cycle (°C)
    time_t timestamp;               // When the cycle occurred
} RainflowCycle;

// Initialize the DR strategy
void DemandResponseStrategy_init(DemandResponseStrategy *strategy, double battery_capacity, double efficiency);

// Calculate Fast DR Dispatch bid
void calculate_fast_dr_bid(DemandResponseStrategy *strategy, double market_price, double grid_demand, double time_window, 
                          double *bid_capacity, double *bid_price);

// Calculate Capacity Bidding Program strategy
void calculate_cbp_strategy(DemandResponseStrategy *strategy, double *day_ahead_prices, int *expected_peak_hours, 
                           int num_hours, double *bid_capacities, double *bid_prices);

// Update state of charge and track battery degradation
void update_state_of_charge(DemandResponseStrategy *strategy, double energy_delivered_kwh);

// Calculate non-linear degradation cost using rainflow counting
double calculate_degradation_cost(DemandResponseStrategy *strategy, double depth_of_discharge);

// Add a new cycle to the rainflow counting array
void add_rainflow_cycle(DemandResponseStrategy *strategy, double depth, double mean_soc, double temperature);

// Calculate opportunity cost based on future price forecasts
double calculate_opportunity_cost(DemandResponseStrategy *strategy, double *price_forecast, int forecast_hours);

// Find optimal capacity allocation across hours based on expected profitability
void calculate_capacity_allocation(DemandResponseStrategy *strategy, double *day_ahead_prices, int *peak_hours, 
                                 int num_hours, double *capacity_factors);

// Nash equilibrium calculation with competition factors
double find_nash_equilibrium_price(DemandResponseStrategy *strategy, double market_price, double grid_demand, int num_competitors);

#endif // DEMAND_RESPONSE_H
