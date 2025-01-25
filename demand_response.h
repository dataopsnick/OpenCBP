#ifndef DEMAND_RESPONSE_H
#define DEMAND_RESPONSE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    double battery_capacity; // Battery capacity in kW
    double efficiency;       // Battery efficiency (0.0 to 1.0)
    double min_soc;          // Minimum state of charge (0.0 to 1.0)
    double max_soc;          // Maximum state of charge (0.0 to 1.0)
    double current_soc;      // Current state of charge (0.0 to 1.0)
    uint32_t cycle_count;    // Number of charge/discharge cycles
    double degradation_rate; // Degradation per cycle
} DemandResponseStrategy;

// Initialize the DR strategy
void DemandResponseStrategy_init(DemandResponseStrategy *strategy, double battery_capacity, double efficiency);

// Calculate Fast DR Dispatch bid
void calculate_fast_dr_bid(DemandResponseStrategy *strategy, double market_price, double grid_demand, double time_window, 
                           double *bid_capacity, double *bid_price);

// Calculate Capacity Bidding Program strategy
void calculate_cbp_strategy(DemandResponseStrategy *strategy, double *day_ahead_prices, int *expected_peak_hours, 
                            int num_hours, double *bid_capacities, double *bid_prices);

// Update state of charge after energy delivery
void update_state_of_charge(DemandResponseStrategy *strategy, double energy_delivered_kwh);

#endif // DEMAND_RESPONSE_H
