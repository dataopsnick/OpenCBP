#include "demand_response.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

// Initialize the DR strategy with improved parameters
void DemandResponseStrategy_init(DemandResponseStrategy *strategy, double battery_capacity, double efficiency) {
    strategy->battery_capacity = battery_capacity;
    strategy->efficiency = efficiency;
    strategy->min_soc = 0.1;           // 10% minimum SOC
    strategy->max_soc = 0.9;           // 90% maximum SOC
    strategy->current_soc = 0.5;       // 50% initial SOC
    strategy->cycle_count = 0;
    
    // Battery degradation parameters for LFP chemistry
    // Based on Millner (2010) exponential model: S_delta(δ) = k_delta_e1 * δ * exp(k_delta_e2 * δ)
    // Parameters empirically determined for ExpertPower EP512100 LFP battery system
    strategy->replacement_cost = 4000.0;  // Replacement cost in $
    
    // LFP-specific exponential model parameters
    strategy->k_delta_e1 = 0.693;      // Exponential coefficient 1
    strategy->k_delta_e2 = 3.31;       // Exponential coefficient 2
    
    // Note: LFP batteries have significantly better cycle life than NMC/LMO
    // Manufacturer specs: 5000+ cycles at 95% DoD @ 25°C
    strategy->cycles_to_eol = 5000;    // Cycles to 80% capacity at reference conditions
    
    // Initialize rainflow counting array
    strategy->cycle_array_size = 1000;  // Store up to 1000 cycles
    strategy->cycles = (RainflowCycle*)malloc(strategy->cycle_array_size * sizeof(RainflowCycle));
    strategy->cycle_count_index = 0;
    
    // Market parameters
    strategy->risk_factor = 0.05;       // 5% risk premium
    strategy->alpha = 0.3;              // Markup scaling parameter
    strategy->beta = 0.2;               // Competition factor
    strategy->max_grid_demand = 50000.0; // Maximum grid demand in kW
}

// Calculate non-linear degradation cost using rainflow model
double calculate_degradation_cost(DemandResponseStrategy *strategy, double depth_of_discharge) {
    // Calculate stress factor using Millner (2010) exponential model for LFP
    // S_delta(δ) = k_delta_e1 * δ * exp(k_delta_e2 * δ)
    double stress_factor = strategy->k_delta_e1 * depth_of_discharge * 
                          exp(strategy->k_delta_e2 * depth_of_discharge);
    
    // For LFP batteries, adjust cycles to EOL based on DoD
    // This is a simplified model; real relationship is more complex
    double cycles_at_dod = strategy->cycles_to_eol / stress_factor;
    
    // Calculate degradation cost per cycle
    double degradation_cost = (strategy->replacement_cost / strategy->battery_capacity) * 
                             (1.0 / cycles_at_dod) * depth_of_discharge;
    
    return degradation_cost;
}

// Add a new cycle to the rainflow counting array
void add_rainflow_cycle(DemandResponseStrategy *strategy, double depth, double mean_soc, double temperature) {
    if (strategy->cycle_count_index >= strategy->cycle_array_size) {
        // If array is full, double its size
        strategy->cycle_array_size *= 2;
        strategy->cycles = (RainflowCycle*)realloc(strategy->cycles, 
                                                 strategy->cycle_array_size * sizeof(RainflowCycle));
    }
    
    // Add new cycle
    strategy->cycles[strategy->cycle_count_index].depth = depth;
    strategy->cycles[strategy->cycle_count_index].mean_soc = mean_soc;
    strategy->cycles[strategy->cycle_count_index].temperature = temperature;
    strategy->cycles[strategy->cycle_count_index].timestamp = time(NULL);
    
    strategy->cycle_count_index++;
    
    // Update equivalent full cycle count
    strategy->cycle_count += depth;
}

// Calculate marginal cost with improved model
static double _calculate_marginal_cost(DemandResponseStrategy *strategy, double time_of_day, double depth_of_discharge, double opp_cost) {
    // Time-dependent base cost (day/night)
    double base_cost = (time_of_day >= 6 && time_of_day <= 18) ? 0.29 : 0.10;
    
    // Calculate degradation cost using non-linear model
    double degradation_cost = calculate_degradation_cost(strategy, depth_of_discharge);
    
    // Opportunity cost of using energy now vs. later
    double opportunity_cost = opp_cost;
    
    // Risk premium
    double risk_premium = strategy->risk_factor;
    
    // Calculate total marginal cost
    return (base_cost + degradation_cost + opportunity_cost + risk_premium) / strategy->efficiency;
}

// Find Nash equilibrium price with competition factors
double find_nash_equilibrium_price(DemandResponseStrategy *strategy, double market_price, double grid_demand, int num_competitors) {
    // Calculate demand factor from grid demand
    double demand_factor = fmin(grid_demand / strategy->max_grid_demand, 1.5);
    
    // Calculate markup based on demand and competition
    double markup = strategy->alpha * (demand_factor / (num_competitors * strategy->beta + 1));
    
    // Calculate Nash equilibrium price
    double equilibrium_price = market_price * (1 + markup);
    
    return equilibrium_price;
}

// Calculate opportunity cost based on future price forecasts
double calculate_opportunity_cost(DemandResponseStrategy *strategy, double *price_forecast, int forecast_hours) {
    if (price_forecast == NULL || forecast_hours <= 0) {
        return 0.0;
    }
    
    // Find maximum expected value of future prices
    double max_expected_value = 0.0;
    double discount_factor = 0.9; // Time value discount factor
    
    for (int i = 0; i < forecast_hours; i++) {
        // Apply time discount to future prices (further hours are less certain)
        double expected_value = price_forecast[i] * pow(discount_factor, i);
        if (expected_value > max_expected_value) {
            max_expected_value = expected_value;
        }
    }
    
    return max_expected_value * 0.5; // 50% of max future value as opportunity cost
}

// Calculate Fast DR Dispatch bid with improved model
void calculate_fast_dr_bid(DemandResponseStrategy *strategy, double market_price, double grid_demand, double time_window, 
                          double *bid_capacity, double *bid_price) {
    // Calculate available capacity
    double available_capacity = (strategy->current_soc - strategy->min_soc) * strategy->battery_capacity;
    
    // Estimate depth of discharge if we were to use all available capacity
    double depth_of_discharge = available_capacity / strategy->battery_capacity;
    
    // For simplicity, using a dummy price forecast array here
    double price_forecast[24] = {0};
    for (int i = 0; i < 24; i++) {
        price_forecast[i] = market_price * (1 + 0.05 * i); // Simple increasing forecast
    }
    
    // Calculate opportunity cost
    double opp_cost = calculate_opportunity_cost(strategy, price_forecast, 24);
    
    // Calculate time of day (hour)
    time_t raw_time;
    struct tm *time_info;
    time(&raw_time);
    time_info = localtime(&raw_time);
    double hour_of_day = time_info->tm_hour;
    
    // Calculate marginal cost
    double marginal_cost = _calculate_marginal_cost(strategy, hour_of_day, depth_of_discharge, opp_cost);
    
    // Calculate Nash equilibrium price (assuming 10 competitors)
    double nash_price = find_nash_equilibrium_price(strategy, market_price, grid_demand, 10);
    
    // Determine optimal bid
    if (nash_price > marginal_cost) {
        // Calculate maximum power based on time window and efficiency
        double max_power = fmin(available_capacity, strategy->battery_capacity * time_window * strategy->efficiency);
        
        // Set bid capacity and price
        *bid_capacity = max_power;
        *bid_price = nash_price;
    } else {
        // Not profitable to participate
        *bid_capacity = 0;
        *bid_price = 0;
    }
}

// Calculate capacity allocation across hours
void calculate_capacity_allocation(DemandResponseStrategy *strategy, double *day_ahead_prices, int *peak_hours, 
                                 int num_hours, double *capacity_factors) {
    double total_exp_factor = 0.0;
    double gamma = 2.0; // Concentration parameter
    
    // Calculate expected revenue for each hour
    double expected_revenue[24] = {0};
    for (int h = 0; h < num_hours; h++) {
        expected_revenue[h] = day_ahead_prices[h] * (peak_hours[h] ? 1.2 : 1.0);
        
        // Calculate exponential factor
        double exp_factor = exp(gamma * expected_revenue[h]);
        capacity_factors[h] = exp_factor;
        total_exp_factor += exp_factor;
    }
    
    // Normalize to get allocation proportions
    for (int h = 0; h < num_hours; h++) {
        capacity_factors[h] /= total_exp_factor;
    }
}

// Calculate Capacity Bidding Program strategy with improved model
void calculate_cbp_strategy(DemandResponseStrategy *strategy, double *day_ahead_prices, int *expected_peak_hours, 
                           int num_hours, double *bid_capacities, double *bid_prices) {
    // Calculate capacity allocation factors
    double capacity_factors[24] = {0};
    calculate_capacity_allocation(strategy, day_ahead_prices, expected_peak_hours, num_hours, capacity_factors);
    
    // Available energy
    double available_energy = strategy->battery_capacity * (strategy->max_soc - strategy->min_soc);
    
    // Calculate bids for each hour
    for (int hour = 0; hour < num_hours; hour++) {
        bool is_peak_hour = expected_peak_hours[hour];
        
        // Dummy price forecast for opportunity cost calculation
        double price_forecast[24] = {0};
        for (int i = 0; i < 24; i++) {
            price_forecast[i] = day_ahead_prices[(hour + i) % 24];
        }
        
        // Calculate opportunity cost
        double opp_cost = calculate_opportunity_cost(strategy, price_forecast, 24);
        
        // Estimate depth of discharge for this hour
        double hour_capacity = available_energy * capacity_factors[hour];
        double depth_of_discharge = hour_capacity / strategy->battery_capacity;
        
        // Calculate marginal cost
        double base_cost = _calculate_marginal_cost(strategy, hour, depth_of_discharge, opp_cost);
        
        // Set bid capacity
        bid_capacities[hour] = hour_capacity;
        
        // Calculate bid price (peak hours get higher markup)
        double markup = is_peak_hour ? 0.15 : 0.05;
        double cost_markup = is_peak_hour ? 0.2 : 0.1;
        
        // Set bid price
        bid_prices[hour] = fmax(day_ahead_prices[hour] * (1 + markup), base_cost * (1 + cost_markup));
    }
}

// Update state of charge and track battery degradation
void update_state_of_charge(DemandResponseStrategy *strategy, double energy_delivered_kwh) {
    // Previous SOC
    double prev_soc = strategy->current_soc;
    
    // Update SOC
    strategy->current_soc -= energy_delivered_kwh / strategy->battery_capacity;
    
    // Ensure SOC stays within bounds
    strategy->current_soc = fmax(strategy->min_soc, fmin(strategy->max_soc, strategy->current_soc));
    
    // Calculate depth of discharge
    double depth = fabs(prev_soc - strategy->current_soc);
    
    // Calculate mean SOC during this operation
    double mean_soc = (prev_soc + strategy->current_soc) / 2.0;
    
    // Dummy temperature (would be measured in a real system)
    double temperature = 25.0; // 25°C
    
    // Add to rainflow counting if depth is significant
    if (depth > 0.01) { // Only count cycles with >1% depth
        add_rainflow_cycle(strategy, depth, mean_soc, temperature);
    }
}
