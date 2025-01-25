# OpenCPB - Solar Battery Demand Response Integration with RTOS

This project enables a solar battery system to participate in utility Demand Response (DR) and Time-of-Use (TOU) programs using **OpenADR 2.0**, **Modbus communication**, and a **Raspberry Pi Zero running FreeRTOS**. The system integrates with Gridlink Technologies' OpenADR-certified devices to automate responses to grid signals, such as load shedding or peak shaving, while ensuring compliance with utility requirements.

---

## Features

- **OpenADR 2.0 Compliance**: Seamless integration with utility Virtual Top Nodes (VTNs) for demand response programs.
- **Modbus (RS485) Communication**: Direct communication with solar battery systems for real-time monitoring and control.
- **Demand Response Automation**: Automates battery charging/discharging based on DR signals like Critical Peak Pricing (CPP) or Capacity Bidding Programs (CPB).
- **Anti-Flutter Timer**: Prevents rapid cycling of the battery with a 3600-second (1-hour) minimum interval between DR events.
- **20% SOC Safety Latch**: Ensures the battery never discharges below 20% SOC, protecting battery health and longevity.
- **Real-Time Monitoring**: Tracks battery state-of-charge (SOC), power usage, and event responses.
- **Utility Incentives**: Enables participation in DR programs for financial incentives and grid support.

---

## Supported Demand Response Programs

1. **Critical Peak Pricing (CPP)**: Avoid high electricity costs by discharging the battery during peak pricing events.
2. **Capacity Bidding Program (CPB)**: Bid battery capacity to reduce load during high-demand periods.
3. **Fast DR Dispatch / Ancillary Services**: Provide rapid grid support during emergencies.
4. **Distributed Energy Resources (DER) DR Program**: Smooth integration of solar batteries into the grid by offering stored energy during peak demand.

---

## Hardware Requirements

- **Solar Battery System**: Must support Modbus (RS485) communication.
- **Gridlink OpenADR Gateway**: Acts as a Virtual End Node (VEN) to communicate with utility VTNs.
- **Raspberry Pi Zero**: For running the RTOS application and interfacing with the Gridlink gateway.
- **RS485 Interface**: For communication with the battery's BMS.

---

## Software Requirements

- **FreeRTOS**: Real-time operating system for the Raspberry Pi Zero.
- **Modbus Library**: For communication with the solar battery (e.g., `libmodbus`).
- **OpenADR 2.0 Client**: Integrated into the Gridlink gateway for utility communication.
- **C Programming**: For implementing control logic and automation scripts.

---

### Setup Instructions

1. **Connect the Gridlink Gateway**:
   - Connect the Gridlink OpenADR gateway to your solar battery system using the RS485 interface.
   - Configure the gateway with the correct Modbus registers for SOC, charge/discharge status, and other relevant parameters.

2. **Install FreeRTOS on Raspberry Pi Zero**:
   - Follow the [FreeRTOS Raspberry Pi Zero guide](https://www.freertos.org/RTOS-Raspberry-Pi-Zero.html) to set up the RTOS environment.

3. **Compile and Deploy the RTOS Application**:
   - Clone this repository and compile the `sunlight_lut.c` and `sunlight_lut.h` files along with the RTOS application.
   - Deploy the application to the Raspberry Pi Zero.

4. **Register with a Utility**:
   - Sign up for a utility DR program that supports OpenADR 2.0.
   - Provide the utility with your Gridlink gateway's Virtual End Node (VEN) details.

5. **Implement Control Logic**:
   - Use the provided code to automate battery responses to DR signals:
     - Discharge during CPP events.
     - Charge during off-peak periods.
     - Bid capacity for CPB programs.

6. **Monitor and Optimize**:
   - Use the Gridlink gateway's monitoring features to track battery performance and DR event responses.
   - Adjust control logic to maximize financial incentives and grid support.
---

## RTOS Application Overview

The RTOS application consists of three main tasks:

1. **SpoofSOC Task**:
   - Manages SOC spoofing to prevent rapid cycling of the battery.
   - Enforces a 3600-second (1-hour) minimum interval between DR events.
   - Ensures the battery never discharges below 20% SOC.

2. **FastDRDispatch Task**:
   - Adjusts battery discharge based on grid signals and SOC.
   - Uses `calculateDischargeRate` to determine the optimal discharge rate.

3. **CapacityBidding Task**:
   - Calculates a bid price based on SOC and grid demand.
   - Submits the bid to the utility's limit order book using `submitBid`.

---

## Key Code Snippets

### Anti-Flutter Timer and SOC Safety Latch
```c
// RTOS task to handle SOC spoofing and anti-flutter
void SpoofSOC(void *pvParameters) {
    time_t currentTime;
    time_t lastSpoofTime = 0;
    const int SPOOF_INTERVAL_SECONDS = 3600; // 1-hour anti-flutter timer
    const uint16_t MIN_SOC = 20; // 20% SOC safety latch

    for (;;) {
        currentTime = time(NULL);

        // Read actual SOC from BMS via Modbus
        uint16_t actualSOC = modbus_read_input_registers(ctx, SOC_REGISTER, 1, &actualSOC);
        if (actualSOC == -1) {
            fprintf(stderr, "Failed to read SOC register: %s\n", modbus_strerror(errno));
            continue;
        }

        // Enforce 20% SOC safety latch
        if (actualSOC < MIN_SOC) {
            printf("SOC below 20%. Disabling DR events.\n");
            continue;
        }

        // Enforce anti-flutter timer
        if (difftime(currentTime, lastSpoofTime) >= SPOOF_INTERVAL_SECONDS) {
            // Perform DR event (e.g., discharge battery)
            lastSpoofTime = currentTime;
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Run every second
    }
}
```

### Fast DR Dispatch Task
```c
// RTOS task to handle Fast DR Dispatch
void FastDRDispatch(void *pvParameters) {
    time_t currentTime;
    uint16_t actualSOC;
    bool isDemandResponseActive = false; // Replace with actual DR event state

    for (;;) {
        currentTime = time(NULL);

        // Read actual SOC from BMS via Modbus
        actualSOC = modbus_read_input_registers(ctx, SOC_REGISTER, 1, &actualSOC);
        if (actualSOC == -1) {
            fprintf(stderr, "Failed to read SOC register: %s\n", modbus_strerror(errno));
            continue;
        }

        // Fast DR Dispatch Logic
        if (isDemandResponseActive) {
            double dischargeRate = calculateDischargeRate(actualSOC);
            modbus_write_register(ctx, DISCHARGE_RATE_REGISTER, (uint16_t)dischargeRate);
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Run every second
    }
}
```

### Capacity Bidding Task
```c
// RTOS task to handle Capacity Bidding
void CapacityBidding(void *pvParameters) {
    time_t currentTime;
    uint16_t actualSOC;
    bool isDemandResponseActive = false; // Replace with actual DR event state

    for (;;) {
        currentTime = time(NULL);

        // Read actual SOC from BMS via Modbus
        actualSOC = modbus_read_input_registers(ctx, SOC_REGISTER, 1, &actualSOC);
        if (actualSOC == -1) {
            fprintf(stderr, "Failed to read SOC register: %s\n", modbus_strerror(errno));
            continue;
        }

        // Capacity Bidding Logic
        if (isDemandResponseActive) {
            double bidPrice = calculateBidPrice(actualSOC);
            submitBid(bidPrice);
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Run every second
    }
}
```

---

## Resources

- **Gridlink Technologies**: [OpenADR Certified Devices](https://gridlinktechnologies.com/open-adr-certified/) - Learn more about OpenADR-certified devices for demand response integration.
- **Modbus Protocol Documentation**: [Modbus Protocol](http://www.modbus.org/) - Official documentation for the Modbus communication protocol.
- **OpenADR 2.0 Specification**: [OpenADR Alliance](https://www.openadr.org/) - Detailed specifications for OpenADR 2.0.
- **FreeRTOS Raspberry Pi Zero Guide**: [FreeRTOS Raspberry Pi Zero](https://www.freertos.org/RTOS-Raspberry-Pi-Zero.html) - Step-by-step guide to setting up FreeRTOS on a Raspberry Pi Zero.

---

## Contributing

Contributions are welcome! Please open an issue or submit a pull request for any improvements or bug fixes.

---

## License

Apache License
Version 2.0, January 2004
http://www.apache.org/licenses/

TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

1. Definitions.

   "License" shall mean the terms and conditions for use, reproduction,
   and distribution as defined by Sections 1 through 9 of this document.

   "Licensor" shall mean the copyright owner or entity authorized by
   the copyright owner that is granting the License.

   "Legal Entity" shall mean the union of the acting entity and all
   other entities that control, are controlled by, or are under common
   control with that entity. For the purposes of this definition,
   "control" means (i) the power, direct or indirect, to cause the
   direction or management of such entity, whether by contract or
   otherwise, or (ii) ownership of fifty percent (50%) or more of the
   outstanding shares, or (iii) beneficial ownership of such entity.

   "You" (or "Your") shall mean an individual or Legal Entity
   exercising permissions granted by this License.

   "Source" form shall mean the preferred form for making modifications,
   including but not limited to software source code, documentation
   source, and configuration files.

   "Object" form shall mean any form resulting from mechanical
   transformation or translation of a Source form, including but
   not limited to compiled object code, generated documentation,
   and conversions to other media types.

   "Work" shall mean the work of authorship, whether in Source or
   Object form, made available under the License, as indicated by a
   copyright notice that is included in or attached to the work
   (an example is provided in the Appendix below).

   "Derivative Works" shall mean any work, whether in Source or Object
   form, that is based on (or derived from) the Work and for which the
   editorial revisions, annotations, elaborations, or other modifications
   represent, as a whole, an original work of authorship. For the purposes
   of this License, Derivative Works shall not include works that remain
   separable from, or merely link (or bind by name) to the interfaces of,
   the Work and Derivative Works thereof.

   "Contribution" shall mean any work of authorship, including
   the original version of the Work and any modifications or additions
   to that Work or Derivative Works thereof, that is intentionally
   submitted to Licensor for inclusion in the Work by the copyright owner
   or by an individual or Legal Entity authorized to submit on behalf of
   the copyright owner. For the purposes of this definition, "submitted"
   means any form of electronic, verbal, or written communication sent
   to the Licensor or its representatives, including but not limited to
   communication on electronic mailing lists, source code control systems,
   and issue tracking systems that are managed by, or on behalf of, the
   Licensor for the purpose of discussing and improving the Work, but
   excluding communication that is conspicuously marked or otherwise
   designated in writing by the copyright owner as "Not a Contribution."

   "Contributor" shall mean Licensor and any individual or Legal Entity
   on behalf of whom a Contribution has been received by Licensor and
   subsequently incorporated within the Work.

2. Grant of Copyright License. Subject to the terms and conditions of
   this License, each Contributor hereby grants to You a perpetual,
   worldwide, non-exclusive, no-charge, royalty-free, irrevocable
   copyright license to reproduce, prepare Derivative Works of,
   publicly display, publicly perform, sublicense, and distribute the
   Work and such Derivative Works in Source or Object form.

3. Grant of Patent License. Subject to the terms and conditions of
   this License, each Contributor hereby grants to You a perpetual,
   worldwide, non-exclusive, no-charge, royalty-free, irrevocable
   (except as stated in this section) patent license to make, have made,
   use, offer to sell, sell, import, and otherwise transfer the Work,
   where such license applies only to those patent claims licensable
   by such Contributor that are necessarily infringed by their
   Contribution(s) alone or by combination of their Contribution(s)
   with the Work to which such Contribution(s) was submitted. If You
   institute patent litigation against any entity (including a
   cross-claim or counterclaim in a lawsuit) alleging that the Work
   or a Contribution incorporated within the Work constitutes direct
   or contributory patent infringement, then any patent licenses
   granted to You under this License for that Work shall terminate
   as of the date such litigation is filed.

4. Redistribution. You may reproduce and distribute copies of the
   Work or Derivative Works thereof in any medium, with or without
   modifications, and in Source or Object form, provided that You
   meet the following conditions:

   (a) You must give any other recipients of the Work or
       Derivative Works a copy of this License; and

   (b) You must cause any modified files to carry prominent notices
       stating that You changed the files; and

   (c) You must retain, in the Source form of any Derivative Works
       that You distribute, all copyright, patent, trademark, and
       attribution notices from the Source form of the Work,
       excluding those notices that do not pertain to any part of
       the Derivative Works; and

   (d) If the Work includes a "NOTICE" text file as part of its
       distribution, then any Derivative Works that You distribute must
       include a readable copy of the attribution notices contained
       within such NOTICE file, excluding those notices that do not
       pertain to any part of the Derivative Works, in at least one
       of the following places: within a NOTICE text file distributed
       as part of the Derivative Works; within the Source form or
       documentation, if provided along with the Derivative Works; or,
       within a display generated by the Derivative Works, if and
       wherever such third-party notices normally appear. The contents
       of the NOTICE file are for informational purposes only and
       do not modify the License. You may add Your own attribution
       notices within Derivative Works that You distribute, alongside
       or as an addendum to the NOTICE text from the Work, provided
       that such additional attribution notices cannot be construed
       as modifying the License.

   You may add Your own copyright statement to Your modifications and
   may provide additional or different license terms and conditions
   for use, reproduction, or distribution of Your modifications, or
   for any such Derivative Works as a whole, provided Your use,
   reproduction, and distribution of the Work otherwise complies with
   the conditions stated in this License.

5. Submission of Contributions. Unless You explicitly state otherwise,
   any Contribution intentionally submitted for inclusion in the Work
   by You to the Licensor shall be under the terms and conditions of
   this License, without any additional terms or conditions.
   Notwithstanding the above, nothing herein shall supersede or modify
   the terms of any separate license agreement you may have executed
   with Licensor regarding such Contributions.

6. Trademarks. This License does not grant permission to use the trade
   names, trademarks, service marks, or product names of the Licensor,
   except as required for reasonable and customary use in describing the
   origin of the Work and reproducing the content of the NOTICE file.

7. Disclaimer of Warranty. Unless required by applicable law or
   agreed to in writing, Licensor provides the Work (and each
   Contributor provides its Contributions) on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied, including, without limitation, any warranties or conditions
   of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
   PARTICULAR PURPOSE. You are solely responsible for determining the
   appropriateness of using or redistributing the Work and assume any
   risks associated with Your exercise of permissions under this License.

8. Limitation of Liability. In no event and under no legal theory,
   whether in tort (including negligence), contract, or otherwise,
   unless required by applicable law (such as deliberate and grossly
   negligent acts) or agreed to in writing, shall any Contributor be
   liable to You for damages, including any direct, indirect, special,
   incidental, or consequential damages of any character arising as a
   result of this License or out of the use or inability to use the
   Work (including but not limited to damages for loss of goodwill,
   work stoppage, computer failure or malfunction, or any and all
   other commercial damages or losses), even if such Contributor
   has been advised of the possibility of such damages.

9. Accepting Warranty or Additional Liability. While redistributing
   the Work or Derivative Works thereof, You may choose to offer,
   and charge a fee for, acceptance of support, warranty, indemnity,
   or other liability obligations and/or rights consistent with this
   License. However, in accepting such obligations, You may act only
   on Your own behalf and on Your sole responsibility, not on behalf
   of any other Contributor, and only if You agree to indemnify,
   defend, and hold each Contributor harmless for any liability
   incurred by, or claims asserted against, such Contributor by reason
   of your accepting any such warranty or additional liability.

END OF TERMS AND CONDITIONS

APPENDIX: How to apply the Apache License to your work.

   To apply the Apache License to your work, attach the following
   boilerplate notice, with the fields enclosed by brackets "[]"
   replaced with your own identifying information. (Don't include
   the brackets!)  The text should be enclosed in the appropriate
   comment syntax for the file format. We also recommend that a
   file or class name and description of purpose be included on the
   same "printed page" as the copyright notice for easier
   identification within third-party archives.

Copyright 2025 CyberGolem LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
