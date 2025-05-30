# OpenCBP - Game-Theoretic Bidding Strategies for Solar Battery Demand Response

OpenCBP enables solar battery systems to participate in utility Demand Response (DR) and Time-of-Use (TOU) programs using sophisticated game-theoretic bidding strategies. Built on **OpenADR 2.0** and **Modbus communication**, the system runs on a **Raspberry Pi Zero with FreeRTOS** to deliver automated, intelligent responses to grid signals.

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

---

## Key Features

### Advanced Bidding Strategies
- **Game-Theoretic Nash Equilibrium Bidding**: Optimizes bid pricing based on market competition and grid demand dynamics
- **Non-Linear Battery Degradation Modeling**: Incorporates physics-based degradation costs using rainflow cycle counting
- **Dynamic Opportunity Cost Calculation**: Considers potential revenue from reserving capacity for high-value future DR events
- **Multi-Program Optimization**: Simultaneously optimizes across fast DR dispatch and capacity bidding markets

### Grid Integration
- **OpenADR 2.0 Compliance**: Seamless integration with utility Virtual Top Nodes (VTNs) for demand response programs
- **Modbus (RS485) Communication**: Direct interface with solar battery systems for real-time monitoring and control
- **Flexible DR Program Support**: Compatible with CPP, CBP, Fast DR, and DER-based programs

### Battery Protection
- **Anti-Flutter Timer**: Prevents rapid cycling with a 3600-second (1-hour) minimum interval between DR events
- **20% SOC Safety Latch**: Ensures the battery never discharges below 20% SOC to protect battery health
- **Non-Linear Degradation Protection**: Makes economically efficient decisions that preserve battery lifespan

### Real-Time Capabilities
- **FreeRTOS Implementation**: Reliable real-time task scheduling on resource-constrained hardware
- **Optimized Algorithms**: SQP solver and efficient implementation for low-power devices
- **Adaptive Response**: Continuously adjusts strategy based on market conditions and battery state

---

## Performance Benchmarks

| Strategy | Annual Revenue ($) | Battery Cycles | Effective $/kWh | Profit Margin (%) |
|----------|-------------------|----------------|-----------------|-------------------|
| OpenCBP Nash Equilibrium | XXX.XX | XXX | X.XXX | XX.X |
| Fixed-margin (10%) | XXX.XX | XXX | X.XXX | XX.X |
| Price-threshold | XXX.XX | XXX | X.XXX | XX.X |
| Naive peak-shaving | XXX.XX | XXX | X.XXX | XX.X |

**Note**: Performance results are pending completion of empirical simulation studies using LFP-specific degradation parameters and 2023 California ISO (CAISO) market data. Preliminary analysis indicates significant revenue improvements and reduced battery cycling compared to traditional bidding strategies, with statistical significance testing underway.

---

## Theoretical Foundations

The OpenCBP model leverages several key theoretical insights:

- **Strategic Pricing in Non-Cooperative Markets**: Our non-cooperative game formulation models the strategic interaction between market participants, allowing the BESS operator to adapt bidding based on competition levels and grid demand patterns.

- **Dynamic Bid Pricing Formula**:
  ```
  p_bid = max(MC, p_min) * (1 + μ(D_grid, N))
  ```
  Where MC is the marginal cost, p_min is the minimum acceptable price, and μ(D_grid, N) is a markup function based on grid demand and competition.

- **Capacity Allocation Function**:
  ```
  φ(h) = (e^(γ · R(h))) / (∑_{h' ∈ H} e^(γ · R(h')))
  ```
  This distributes available capacity across hours based on expected profitability, with γ determining allocation aggressiveness.

---

## Hardware Requirements

- **Solar Battery System**: Must support Modbus (RS485) communication
- **Gridlink OpenADR Gateway**: Acts as a Virtual End Node (VEN) to communicate with utility VTNs
- **Raspberry Pi Zero**: For running the RTOS application and interfacing with the Gridlink gateway
- **RS485 Interface**: For communication with the battery's BMS

---

## Software Components

The implementation consists of several key files:

1. **demand_response.h/c**: Core bidding strategy implementation
   - Non-linear battery degradation model
   - Nash equilibrium price calculation
   - Capacity allocation algorithms
   - Opportunity cost estimation

2. **sunlight_lut.h/c**: System integration and RTOS tasks
   - Modbus communication with battery
   - OpenADR event handling
   - Anti-flutter protection
   - SOC safety mechanisms

3. **openadr_ven-client.py**: OpenADR client implementation
   - DR event reception and processing
   - Telemetry reporting
   - Bid submission

---

## Supported Demand Response Programs

1. **Critical Peak Pricing (CPP)**: Avoid high electricity costs by discharging the battery during peak pricing events
2. **Capacity Bidding Program (CBP)**: Bid battery capacity to reduce load during high-demand periods
3. **Fast DR Dispatch / Ancillary Services**: Provide rapid grid support during emergencies
4. **Distributed Energy Resources (DER) DR Program**: Smooth integration of solar batteries into the grid

---

## Setup Instructions

1. **Connect the Gridlink Gateway**:
   - Connect the Gridlink OpenADR gateway to your solar battery system using the RS485 interface
   - Configure the gateway with the correct Modbus registers for SOC, charge/discharge status, and other parameters

2. **Install FreeRTOS on Raspberry Pi Zero**:
   - Follow the [FreeRTOS Raspberry Pi Zero guide](https://www.freertos.org/RTOS-Raspberry-Pi-Zero.html) to set up the RTOS environment

3. **Compile and Deploy**:
   - Clone this repository
   - Compile `demand_response.c`, `sunlight_lut.c` and associated headers
   - Deploy the application to the Raspberry Pi Zero

4. **Register with a Utility**:
   - Sign up for a utility DR program that supports OpenADR 2.0
   - Provide the utility with your Gridlink gateway's Virtual End Node (VEN) details

5. **Configure Market Parameters**:
   - Set appropriate values for the markup parameters (α, β) based on your market
   - Configure battery degradation parameters for your specific battery chemistry
   - Set up time-of-use pricing information relevant to your utility

---

## Implementation Details

The system creates three main RTOS tasks:

1. **SpoofSOC Task**: 
   - Manages SOC monitoring and anti-flutter protection
   - Enforces 3600-second minimum interval between DR events
   - Ensures battery never discharges below 20% SOC
   - Tracks battery cycles using rainflow counting

2. **FastDRDispatch Task**:
   - Calculates optimal bid price and capacity for real-time DR events
   - Uses Nash equilibrium pricing to maximize profits
   - Adjusts discharge based on grid signals and battery SOC

3. **CapacityBidding Task**:
   - Calculates day-ahead bids for capacity markets
   - Identifies expected peak hours using price forecasts
   - Optimizes capacity allocation across different hours

---

## Future Directions

- **Adaptation to Evolving Market Structures**: Implement reinforcement learning to adapt to evolving market dynamics
- **Battery Technology Specificity**: Extend degradation models to other chemistries (LFP, NCA, solid-state)
- **Multi-Service Optimization**: Expand to frequency regulation, voltage support, and other ancillary services
- **Coordination with Other DERs**: Develop coordinated bidding strategies for heterogeneous DER fleets

---

## Resources

- **Gridlink Technologies**: [OpenADR Certified Devices](https://gridlinktechnologies.com/open-adr-certified/) - Learn more about OpenADR-certified devices
- **Modbus Protocol Documentation**: [Modbus Protocol](http://www.modbus.org/) - Official documentation for Modbus
- **OpenADR 2.0 Specification**: [OpenADR Alliance](https://www.openadr.org/) - Detailed specifications for OpenADR 2.0
- **FreeRTOS Guide**: [FreeRTOS Raspberry Pi Zero](https://www.freertos.org/RTOS-Raspberry-Pi-Zero.html) - Setup guide

---

## Contributing

Contributions are welcome! Please open an issue or submit a pull request for any improvements or bug fixes.

---

## Bibliography

\bibitem[Millner(2010)]{Millner2010}
Millner, A. (2010).
\newblock Modeling lithium ion battery degradation in electric vehicles.
\newblock In \emph{Innovative Technologies for an Efficient and Reliable Electricity Supply (CITRES), 2010 IEEE Conference on} (pp. 349-356).

---

## License

This project is licensed under the Apache License 2.0 - see the LICENSE file for details.

Copyright 2025 CyberGolem LLC
