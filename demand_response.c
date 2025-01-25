#include "demand_response.h"
#include <math.h>

// Initialize the DR strategy
void DemandResponseStrategy_init(DemandResponseStrategy *strategy, double battery_capacity, double efficiency) {
    strategy->battery_capacity = battery_capacity;
    strategy->efficiency = efficiency;
    strategy->min_soc = 0.1;  // 10% minimum SOC
    strategy->max_soc = 0.9;  // 90% maximum SOC
    strategy->current_soc = 0.5;  // 50% initial SOC
    strategy->cycle_count = 0;
    strategy->degradation_rate = 0.0002;  // Degradation per cycle
}

// Calculate marginal cost of delivery
static double _calculate_marginal_cost(DemandResponseStrategy *strategy, double time_of_day) {
    double base_cost = (time_of_day == 1) ? 0.29 : 0.00;  // Night vs day charging
    double degradation_cost = 4000 / (strategy->battery_capacity * (5000 - strategy->cycle_count * strategy->degradation_rate));
    double risk_factor = 0.06;  // Default risk factor
    return (base_cost + degradation_cost + risk_factor) / strategy->efficiency;
}

// Find Nash equilibrium price point
static double _find_nash_equilibrium(DemandResponseStrategy *strategy, double market_price, double grid_demand) {
    double demand_factor = fmin(grid_demand / (strategy->battery_capacity * 10), 1.5);
    double equilibrium_price = market_price * demand_factor;
    return fmax(equilibrium_price, _calculate_marginal_cost(strategy, market_price));
}

// Calculate Fast DR Dispatch bid
void calculate_fast_dr_bid(DemandResponseStrategy *strategy, double market_price, double grid_demand, double time_window, 
                           double *bid_capacity, double *bid_price) {
    double available_capacity = (strategy->max_soc - strategy->current_soc) * strategy->battery_capacity;
    double marginal_cost = _calculate_marginal_cost(strategy, market_price);
    double nash_price = _find_nash_equilibrium(strategy, market_price, grid_demand);

    if (nash_price > marginal_cost) {
        *bid_capacity = fmin(available_capacity, strategy->battery_capacity * time_window * strategy->efficiency);
        *bid_price = nash_price * 1.05;  // 5% markup
    } else {
        *bid_capacity = 0;
        *bid_price = 0;
    }
}

// Calculate Capacity Bidding Program strategy
void calculate_cbp_strategy(DemandResponseStrategy *strategy, double *day_ahead_prices, int *expected_peak_hours, 
                            int num_hours, double *bid_capacities, double *bid_prices) {
    for (int hour = 0; hour < num_hours; hour++) {
        bool is_peak_hour = expected_peak_hours[hour];
        double base_cost = _calculate_marginal_cost(strategy, day_ahead_prices[hour]);
        double capacity = strategy->battery_capacity * (strategy->max_soc - strategy->min_soc) * (is_peak_hour ? 1.0 : 0.5);
        double bid_price = fmax(day_ahead_prices[hour] * (is_peak_hour ? 1.15 : 1.05), base_cost * (is_peak_hour ? 1.2 : 1.1));

        bid_capacities[hour] = capacity;
        bid_prices[hour] = bid_price;
    }
}

// Update state of charge after energy delivery
void update_state_of_charge(DemandResponseStrategy *strategy, double energy_delivered_kwh) {
    strategy->current_soc -= energy_delivered_kwh / strategy->battery_capacity;
    strategy->current_soc = fmax(strategy->min_soc, fmin(strategy->max_soc, strategy->current_soc));
    strategy->cycle_count++;
}
