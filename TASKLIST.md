```xml
<tasklist>
  <task status="PENDING">
    <id>1</id>
    <title>sunlight_lut.c:133,216 & openadr_ven-client.py:44-52 - [bug · critical] Unsynchronized multi-thread/multi-process Modbus RTU access and Holding vs. Input register mismatch</title>
    <description><![CDATA[
### Location: dataopsnick-opencbp/sunlight_lut.c:133, 179, 216, 232 & dataopsnick-opencbp/openadr_ven-client.py:44-52

**Root Cause**:
1. **Register Type Mismatch (`0x210` and `0x220`)**:
   - In `sunlight_lut.c:179` (`SpoofSOC`) and `openadr_ven-client.py:46` (`set_dr_active`), register `0x220` is written as a **Holding Register** via `modbus_write_register` / `WRITE_SINGLE_REGISTER` (Function Code `0x06`). However, `FastDRDispatch` (`sunlight_lut.c:216`) reads `0x220` as an **Input Register** via `modbus_read_input_registers` (Function Code `0x04`).
   - Similarly, `FastDRDispatch` (`sunlight_lut.c:232`) writes the discharge rate to `0x210` as a **Holding Register** (`0x06`), while `openadr_ven-client.py:51` (`get_bid_data`) reads `0x210` as an **Input Register** (`0x04`). Unless the slave device explicitly aliases Holding and Input address spaces, `FastDRDispatch` never sees `isDemandResponseActive == true` and `get_bid_data` never reads the updated bid capacity.
2. **Thread-Unsafe Shared `modbus_t *ctx` in FreeRTOS**:
   - Both `SpoofSOC` and `FastDRDispatch` run every 1 second at identical priority (`1`) and concurrently invoke `libmodbus` functions on the global `modbus_t *ctx` without a mutex. `libmodbus` contexts are not thread-safe; concurrent requests interleave bytes on the RS-485 UART, corrupting CRCs and transaction states.
3. **Missing Slave ID & Serial Port Contention**:
   - `initSystem()` (`sunlight_lut.c:398`) never calls `modbus_set_slave(ctx, 1)` after `modbus_new_rtu(...)`.
   - Both `sunlight_lut.c` and `openadr_ven-client.py` open `/dev/ttyUSB0` as RTU masters simultaneously, causing OS-level serial contention.

**Fix**:
1. Protect all `ctx` operations in `sunlight_lut.c` with a FreeRTOS mutex (`SemaphoreHandle_t modbus_mutex`), call `modbus_set_slave(ctx, 1)` in `initSystem()`, and use `modbus_read_registers` (Function Code `0x03`, Holding Registers) for `0x220` and `0x210`.

```diff
--- a/sunlight_lut.c
+++ b/sunlight_lut.c
@@ -8,11 +8,13 @@
 #include <string.h>
 #include <FreeRTOS.h>
 #include <task.h>
+#include <semphr.h>
 
 // LUT arrays to hold sunrise and sunset times
 double sunriseTable[DAYS_IN_YEAR];
 double sunsetTable[DAYS_IN_YEAR];
 
 // Modbus context
 modbus_t *ctx;
+SemaphoreHandle_t modbus_mutex;
@@ -130,13 +132,17 @@ void SpoofSOC(void *pvParameters) {
         currentTime = time(NULL);
 
         // Read actual SOC from BMS via Modbus
+        xSemaphoreTake(modbus_mutex, portMAX_DELAY);
         if (modbus_read_input_registers(ctx, 0x208, 1, &actualSOC) == -1) {
+            xSemaphoreGive(modbus_mutex);
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
+        xSemaphoreGive(modbus_mutex);
@@ -176,7 +182,9 @@ void SpoofSOC(void *pvParameters) {
             // Disable DR events by writing to register
+            xSemaphoreTake(modbus_mutex, portMAX_DELAY);
             modbus_write_register(ctx, 0x220, 0);
+            xSemaphoreGive(modbus_mutex);
@@ -213,10 +221,12 @@ void FastDRDispatch(void *pvParameters) {
         // Check if DR events are active
         uint16_t dr_status = 0;
-        if (modbus_read_input_registers(ctx, 0x220, 1, &dr_status) == -1) {
+        xSemaphoreTake(modbus_mutex, portMAX_DELAY);
+        if (modbus_read_registers(ctx, 0x220, 1, &dr_status) == -1) {
             fprintf(stderr, "Failed to read DR status register: %s\n", modbus_strerror(errno));
         } else {
             isDemandResponseActive = (dr_status > 0);
         }
+        xSemaphoreGive(modbus_mutex);
@@ -229,9 +239,11 @@ void FastDRDispatch(void *pvParameters) {
             if (bid_capacity > 0) {
                 // Adjust discharge rate based on bid capacity
                 uint16_t discharge_rate = (uint16_t)(bid_capacity * 100); // Scale for Modbus register
+                xSemaphoreTake(modbus_mutex, portMAX_DELAY);
                 if (modbus_write_register(ctx, 0x210, discharge_rate) == -1) {
                     fprintf(stderr, "Failed to write discharge rate: %s\n", modbus_strerror(errno));
                 }
+                xSemaphoreGive(modbus_mutex);
@@ -403,6 +415,8 @@ void initSystem() {
     if (ctx == NULL) {
         fprintf(stderr, "Unable to create the libmodbus context\n");
         return;
     }
+    modbus_set_slave(ctx, 1);
+    modbus_mutex = xSemaphoreCreateMutex();
```

```diff
--- a/openadr_ven-client.py
+++ b/openadr_ven-client.py
@@ -48,5 +48,5 @@ class MyOpenADRClient(OpenADRClient):
     def get_bid_data(self):
         # Read SOC and available capacity from C code via Modbus
         soc = self.master.execute(MODBUS_UNIT, modbus_rtu.READ_INPUT_REGISTERS, 0x208, 1)[0]
-        capacity = self.master.execute(MODBUS_UNIT, modbus_rtu.READ_INPUT_REGISTERS, 0x210, 1)[0]
+        capacity = self.master.execute(MODBUS_UNIT, modbus_rtu.READ_HOLDING_REGISTERS, 0x210, 1)[0] / 100.0
         return (capacity, 0.2)  # Example price
```
]]></description>
  </task>
  <task status="PENDING">
    <id>2</id>
    <title>openadr_ven-client.py:21-38,76-84 - [bug · critical] client.run() is unreachable dead code and on_event() uses invalid openleadr API & race-prone polling</title>
    <description><![CDATA[
### Location: dataopsnick-opencbp/openadr_ven-client.py:21-38, 76-84

**Root Cause**:
1. **Unreachable `await client.run()` (`lines 76-84`)**: `main()` enters an infinite `while True:` loop before `await client.run()`, so the OpenADR client is never started and never polls or receives VTN events.
2. **Invalid `openleadr` Event API (`lines 22, 31-35`)**: In `openleadr`, the `event` parameter passed to `on_event(self, event)` is a plain Python `dict` (not an object with `.event_id`, `.market_context.id`, `.opt_in()`, or `.opt_out()` methods), and the callback must return the string `'optIn'` or `'optOut'`. Accessing `event.event_id` or calling `await event.opt_in()` raises `AttributeError: 'dict' object has no attribute ...`.
3. **Invalid Report Update API (`lines 68-82`)**: `openleadr`'s `client.add_report` takes a `callback` function (`self.get_meter_data`) and `report_specifier_id` rather than `specifier_id`/`measurement_names`, and `client.reports` is a `list` (indexing `client.reports["my_power_report"]` raises `TypeError: list indices must be integers or slices, not str`).
4. **Immediate Flag Reset Race (`lines 25-38`)**: `on_event` sets `0x220` to `True`, immediately reads `0x210` without waiting for the 1-second `FastDRDispatch` RTOS loop to compute and write the bid capacity, and immediately resets `0x220` to `False`.

**Fix**:
Wait for the RTOS task cycle (or trigger calculation) before reading `0x210`, use `openleadr`'s dict-based `on_event` contract returning `'optIn'`/`'optOut'`, register `get_meter_data` as the report callback, and call `await client.run()`.

```diff
--- a/openadr_ven-client.py
+++ b/openadr_ven-client.py
@@ -19,21 +19,23 @@ class MyOpenADRClient(OpenADRClient):
     async def on_event(self, event):
-        print(f"Received event: {event.event_id}, {event.market_context.id}, response_required: {event.response_required}")
+        event_id = event.get("event_descriptor", {}).get("event_id")
+        market_context = event.get("event_descriptor", {}).get("market_context")
+        print(f"Received event: {event_id}, {market_context}")
 
         # Set isDemandResponseActive flag via Modbus
         self.set_dr_active(True)
+        await asyncio.sleep(1.5)  # Allow FastDRDispatch RTOS task (1s period) to update 0x210
 
         # Retrieve bid data from C code via Modbus
         capacity, price = self.get_bid_data()
         print(f"Capacity: {capacity}, Price: {price}")
 
         # Opt-in or opt-out based on bid data
         if capacity > 0:
-            await event.opt_in()
             print("Opted in.")
+            return "optIn"
         else:
-            await event.opt_out()
+            self.set_dr_active(False)
             print("Opted out.")
-
-        # Reset isDemandResponseActive flag after event processing
-        self.set_dr_active(False)
+            return "optOut"
@@ -54,9 +56,9 @@ class MyOpenADRClient(OpenADRClient):
     def get_meter_data(self):
         try:
             soc = self.master.execute(MODBUS_UNIT, modbus_rtu.READ_INPUT_REGISTERS, 0x208, 1)[0]
-            return {"PowerReal": soc}
+            return float(soc)
         except Exception as e:
             print(f"Error reading meter data from modbus: {e}")
-            return {}
+            return 0.0
 
 async def main():
@@ -68,17 +70,14 @@ async def main():
     report_spec_id = "my_power_report"
     client.add_report(
+        callback=client.get_meter_data,
+        resource_id="bess_1",
+        measurement="powerReal",
         report_name="METADATA_TELEMETRY_USAGE",
-        specifier_id=report_spec_id,
+        report_specifier_id=report_spec_id,
         data_collection_mode=enums.DataCollectionMode.FULL_DATA,
-        measurement_names=["PowerReal"],
     )
 
-    while True:
-        # Read meter data from Modbus and update report
-        meter_data = client.get_meter_data()
-        report = client.reports[report_spec_id]
-        if report and meter_data:
-            report.update(meter_data)
-        await asyncio.sleep(5)
-
     await client.run()
+    while True:
+        await asyncio.sleep(5)
```
]]></description>
  </task>
  <task status="PENDING">
    <id>3</id>
    <title>sunlight_lut.c:55-92 - [bug · critical] libcurl write_callback reads un-terminated buffer and overwrites forecast arrays across chunked responses</title>
    <description><![CDATA[
### Location: dataopsnick-opencbp/sunlight_lut.c:55-92

**Root Cause**:
1. **Out-of-Bounds String Read**: `libcurl` does **not** null-terminate the `void *contents` buffer passed to `CURLOPT_WRITEFUNCTION`. Calling `strstr(data, ...)`, `strtod(...)`, `strchr(...)`, and `strtol(...)` directly on `contents` reads past `size * nmemb` into adjacent heap memory whenever the buffer does not happen to end with `'\0'`.
2. **Broken Multi-Chunk Handling**: `libcurl` invokes `write_callback` once per received network chunk (up to `CURL_MAX_WRITE_SIZE`), not necessarily once for the entire HTTP body. If the JSON payload spans two chunks (e.g. split mid-array or mid-number), `strstr` either misses the key or parses a truncated array.
3. **Missing `curl_global_init`**: `initSystem()` calls `fetchMarketData()` (`curl_easy_init()`) before `curl_global_init()` is ever called, and `submitBid()` (`line 389`) calls `curl_global_cleanup()` while other RTOS tasks (`FastDRDispatch`, `CapacityBidding`, `MarketDataUpdate`) continue using `libcurl`.

**Fix**:
Accumulate chunks into a dynamically grown, null-terminated response buffer in `fetchMarketData()`, parse the JSON only after `curl_easy_perform()` completes, and call `curl_global_init(CURL_GLOBAL_DEFAULT)` once in `initSystem()`.

```diff
--- a/sunlight_lut.c
+++ b/sunlight_lut.c
@@ -54,57 +54,75 @@ void getSunlightHours(double *sunrise, double *sunset) {
+typedef struct {
+    char *memory;
+    size_t size;
+} CurlBuffer;
+
 // Function to fetch price forecasts from utility API
 size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
     size_t realsize = size * nmemb;
-    char *data = (char *)contents;
+    CurlBuffer *mem = (CurlBuffer *)userp;
+    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
+    if (!ptr) {
+        return 0;
+    }
+    mem->memory = ptr;
+    memcpy(&(mem->memory[mem->size]), contents, realsize);
+    mem->size += realsize;
+    mem->memory[mem->size] = '\0';
+    return realsize;
+}
+
+static void parse_market_json(const char *data) {
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
-    
-    return realsize;
 }
 
 // Fetch market data from utility API
 void fetchMarketData() {
     CURL *curl;
     CURLcode res;
+    CurlBuffer chunk = { .memory = malloc(1), .size = 0 };
+    if (!chunk.memory) return;
+    chunk.memory[0] = '\0';
     
     curl = curl_easy_init();
     if (curl) {
         curl_easy_setopt(curl, CURLOPT_URL, "https://opencbp.api.example.com/market_data");
         curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
+        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
         
         res = curl_easy_perform(curl);
         if (res != CURLE_OK) {
             fprintf(stderr, "Failed to fetch market data: %s\n", curl_easy_strerror(res));
+        } else {
+            parse_market_json(chunk.memory);
         }
         
         curl_easy_cleanup(curl);
     }
+    free(chunk.memory);
 }
```
]]></description>
  </task>
  <task status="PENDING">
    <id>4</id>
    <title>demand_response.c:178-198 & demand_response.h:15 - [bug · high] Exponential overflow in calculate_capacity_allocation, stack buffer overflow for num_hours > 24, and uint32_t truncation of fractional cycle_count</title>
    <description><![CDATA[
### Location: dataopsnick-opencbp/demand_response.c:66, 178-198, 200-238 & dataopsnick-opencbp/demand_response.h:15

**Root Cause**:
1. **uint32_t Truncation of `cycle_count` (`demand_response.h:15`, `demand_response.c:66`)**:
   `strategy->cycle_count` is declared as `uint32_t`, while `add_rainflow_cycle` updates it via `strategy->cycle_count += depth;`, where `depth` is a fractional `double` in `[0.0, 1.0]` (typically `0.01` to `0.80`). In C, `uint32_t += double` converts `strategy->cycle_count + depth` back to `uint32_t` by truncating the fractional part on every single call! Thus `cycle_count` remains permanently `0` for all sub-100% DoD cycles.
2. **Stack Buffer Overflow when `num_hours > 24` (`demand_response.c:183, 204`)**:
   `calculate_capacity_allocation` and `calculate_cbp_strategy` accept an arbitrary `int num_hours` parameter, but allocate fixed-size local arrays `double expected_revenue[24]` and `double capacity_factors[24]`. Passing `num_hours > 24` corrupts the stack.
3. **Floating-Point Overflow / `NaN` in Softmax (`demand_response.c:188-196`)**:
   `exp(gamma * expected_revenue[h])` is computed without subtracting `max_h(gamma * expected_revenue[h])` (the log-sum-exp trick). For wholesale or scarcity prices (e.g., CAISO prices in `$/MWh` or high `expected_revenue`), `exp(...)` overflows to `INFINITY`, and `capacity_factors[h] /= total_exp_factor` produces `INFINITY / INFINITY = NaN`.

**Fix**:
1. Change `cycle_count` in `DemandResponseStrategy` from `uint32_t` to `double`.
2. Subtract `max_revenue` before exponentiating in `calculate_capacity_allocation` for numerical stability, and guard `num_hours` bounds.

```diff
--- a/demand_response.h
+++ b/demand_response.h
@@ -12,7 +12,7 @@ typedef struct {
     double min_soc;                 // Minimum state of charge (0.0 to 1.0)
     double max_soc;                 // Maximum state of charge (0.0 to 1.0)
     double current_soc;             // Current state of charge (0.0 to 1.0)
-    uint32_t cycle_count;           // Number of equivalent full cycles
+    double cycle_count;             // Number of equivalent full cycles
```

```diff
--- a/demand_response.c
+++ b/demand_response.c
@@ -178,21 +178,31 @@ void calculate_fast_dr_bid(DemandResponseStrategy *strategy, double market_price
 // Calculate capacity allocation across hours
 void calculate_capacity_allocation(DemandResponseStrategy *strategy, double *day_ahead_prices, int *peak_hours, 
                                  int num_hours, double *capacity_factors) {
+    if (num_hours <= 0) return;
+    if (num_hours > 24) num_hours = 24;
+
     double total_exp_factor = 0.0;
     double gamma = 2.0; // Concentration parameter
     
     // Calculate expected revenue for each hour
     double expected_revenue[24] = {0};
+    double max_rev = -INFINITY;
     for (int h = 0; h < num_hours; h++) {
         expected_revenue[h] = day_ahead_prices[h] * (peak_hours[h] ? 1.2 : 1.0);
+        if (expected_revenue[h] > max_rev) {
+            max_rev = expected_revenue[h];
+        }
+    }
+
+    for (int h = 0; h < num_hours; h++) {
         // Calculate numerically stable exponential factor
-        double exp_factor = exp(gamma * expected_revenue[h]);
+        double exp_factor = exp(gamma * (expected_revenue[h] - max_rev));
         capacity_factors[h] = exp_factor;
         total_exp_factor += exp_factor;
     }
     
     // Normalize to get allocation proportions
     for (int h = 0; h < num_hours; h++) {
         capacity_factors[h] /= total_exp_factor;
     }
 }
```
]]></description>
  </task>
  <task status="PENDING">
    <id>5</id>
    <title>demand_response.c:36-49,52-57 - [bug · high] Division by zero when depth_of_discharge is 0.0 and unchecked malloc/realloc in rainflow tracker</title>
    <description><![CDATA[
### Location: dataopsnick-opencbp/demand_response.c:25, 36-49, 52-57

**Root Cause**:
1. **Division by Zero (`demand_response.c:36-49`)**:
   When `current_soc == min_soc` (or `hour_capacity == 0`), `depth_of_discharge` is `0.0`. In `calculate_degradation_cost`:
   - `stress_factor = k_delta_e1 * 0.0 * exp(0.0)` evaluates to `0.0`.
   - `cycles_at_dod = strategy->cycles_to_eol / stress_factor` divides by `0.0`, producing `+INFINITY`.
   - While `1.0 / (+INFINITY) * 0.0` happens to evaluate to `0.0` under normal IEEE-754 rules, under `-ffast-math` or if `depth_of_discharge < 0` (when `current_soc < min_soc`), `depth_of_discharge` is negative and produces a negative `stress_factor` or `NaN` propagation.
2. **Unchecked `realloc` Leak / Crash (`demand_response.c:25, 55-56`)**:
   `strategy->cycles = (RainflowCycle*)realloc(strategy->cycles, ...)` overwrites `strategy->cycles` directly. On embedded targets (Raspberry Pi Zero / FreeRTOS with limited RAM), as `cycle_array_size` doubles repeatedly (`1000 -> 2000 -> 4000 ...`), `realloc` will eventually return `NULL`, leaking the existing buffer and immediately crashing with a null-pointer dereference on line 60.

**Fix**:
1. Return `0.0` immediately in `calculate_degradation_cost` when `depth_of_discharge <= 0.0`.
2. Assign the result of `realloc` to a temporary pointer and handle allocation failure gracefully (e.g., ring-buffer overwrite when at max capacity).

```diff
--- a/demand_response.c
+++ b/demand_response.c
@@ -36,6 +36,9 @@ void DemandResponseStrategy_init(DemandResponseStrategy *strategy, double batter
 // Calculate non-linear degradation cost using rainflow model
 double calculate_degradation_cost(DemandResponseStrategy *strategy, double depth_of_discharge) {
+    if (depth_of_discharge <= 0.0 || strategy->battery_capacity <= 0.0) {
+        return 0.0;
+    }
     // Calculate stress factor using Millner (2010) exponential model for LFP
     // S_delta(δ) = k_delta_e1 * δ * exp(k_delta_e2 * δ)
     double stress_factor = strategy->k_delta_e1 * depth_of_discharge * 
@@ -52,9 +55,14 @@ double calculate_degradation_cost(DemandResponseStrategy *strategy, double depth
 // Add a new cycle to the rainflow counting array
 void add_rainflow_cycle(DemandResponseStrategy *strategy, double depth, double mean_soc, double temperature) {
     if (strategy->cycle_count_index >= strategy->cycle_array_size) {
         // If array is full, double its size
-        strategy->cycle_array_size *= 2;
-        strategy->cycles = (RainflowCycle*)realloc(strategy->cycles, 
-                                                 strategy->cycle_array_size * sizeof(RainflowCycle));
+        int new_size = strategy->cycle_array_size * 2;
+        RainflowCycle *new_cycles = (RainflowCycle*)realloc(
+            strategy->cycles, new_size * sizeof(RainflowCycle));
+        if (new_cycles != NULL) {
+            strategy->cycles = new_cycles;
+            strategy->cycle_array_size = new_size;
+        } else {
+            // Fallback: wrap around as a ring buffer if out of memory on embedded target
+            strategy->cycle_count_index = 0;
+        }
     }
```
]]></description>
  </task>
  <task status="PENDING">
    <id>6</id>
    <title>sunlight_lut.c:189-200,264-321 - [bug · high] Anti-flutter timer is a no-op, CapacityBidding executes 60 times at 2:00 AM, and fast DR bids ignore num_competitors & sunlight LUT</title>
    <description><![CDATA[
### Location: dataopsnick-opencbp/sunlight_lut.c:189-200, 222, 264-321 & dataopsnick-opencbp/demand_response.c:70-72, 130, 159

**Root Cause**:
1. **No-Op Anti-Flutter Timer (`sunlight_lut.c:189-200`)**:
   In `SpoofSOC`, `if (difftime(currentTime, lastSpoofTime) >= SPOOF_INTERVAL_SECONDS)` simply sets `lastSpoofTime = currentTime;` and writes a log message. It never sets or clears any flag or Modbus register, nor is `lastSpoofTime` updated when a DR event actually occurs.
2. **Duplicate Day-Ahead Bid Submissions (`sunlight_lut.c:264-321`)**:
   `CapacityBidding` checks `if (localTime->tm_hour == 2 && localTime->tm_min == 0)` and sleeps `60000 ms`. If the task wakes near the boundary of `02:00:00` or if `fetchMarketData()` + 24 HTTP POSTs take slightly less/more than a tick quantum, there is no tracking of the last day (`tm_yday`) on which bids were submitted, risking either duplicate submissions or skipping the `tm_min == 0` window completely if a prior iteration took >60s.
3. **Unused Sunlight LUT & Hardcoded `num_competitors = 10` (`demand_response.c:72, 159`)**:
   - `generateSunlightLUT()` computes `sunriseTable` and `sunsetTable`, and the paper states that $C_{base}(t)$ uses `getSunlightHours()` to determine daylight solar availability, yet `_calculate_marginal_cost` hardcodes `(time_of_day >= 6 && time_of_day <= 18) ? 0.29 : 0.10` (and sets daytime cost *higher* than nighttime cost instead of reflecting cheap self-generated solar during daylight hours, or vice versa without using the LUT).
   - `calculate_fast_dr_bid` hardcodes `10` competitors on line 159 instead of using the fetched `num_competitors`, and clamps `available_capacity` without `fmax(0.0, ...)` on line 134 (producing negative capacity if `current_soc < min_soc`).
4. **Inconsistent `min_soc` Safety Latch (`demand_response.c:9` vs `sunlight_lut.h:20`)**:
   `sunlight_lut.h` defines `#define MIN_SOC 20` (20%) as documented in the README and paper, but `DemandResponseStrategy_init` sets `strategy->min_soc = 0.1` (10%).

**Fix**:
1. Track `last_bidding_yday` in `CapacityBidding` so day-ahead bidding runs once per calendar day during hour 2 regardless of exact minute alignment.
2. Clamp `available_capacity = fmax(0.0, ...)` in `calculate_fast_dr_bid`, use `getSunlightHours()` in `_calculate_marginal_cost`, and align `min_soc` with `MIN_SOC / 100.0` (`0.20`).

```diff
--- a/demand_response.c
+++ b/demand_response.c
@@ -1,4 +1,5 @@
 #include "demand_response.h"
+#include "sunlight_lut.h"
 #include <math.h>
 #include <stdlib.h>
 #include <time.h>
@@ -6,7 +7,7 @@
 void DemandResponseStrategy_init(DemandResponseStrategy *strategy, double battery_capacity, double efficiency) {
     strategy->battery_capacity = battery_capacity;
     strategy->efficiency = efficiency;
-    strategy->min_soc = 0.1;           // 10% minimum SOC
+    strategy->min_soc = MIN_SOC / 100.0; // 20% minimum SOC safety latch
     strategy->max_soc = 0.9;           // 90% maximum SOC
     strategy->current_soc = 0.5;       // 50% initial SOC
     strategy->cycle_count = 0;
@@ -69,8 +70,11 @@ void add_rainflow_cycle(DemandResponseStrategy *strategy, double depth, double m
 // Calculate marginal cost with improved model
 static double _calculate_marginal_cost(DemandResponseStrategy *strategy, double time_of_day, double depth_of_discharge, double opp_cost) {
-    // Time-dependent base cost (day/night)
-    double base_cost = (time_of_day >= 6 && time_of_day <= 18) ? 0.29 : 0.10;
+    // Time-dependent base cost informed by Sunlight LUT
+    double sunrise = 6.0, sunset = 18.0;
+    getSunlightHours(&sunrise, &sunset);
+    double base_cost = (time_of_day >= sunrise && time_of_day <= sunset) ? 0.10 : 0.29;
@@ -131,7 +135,7 @@ void calculate_fast_dr_bid(DemandResponseStrategy *strategy, double market_price
                           double *bid_capacity, double *bid_price) {
     // Calculate available capacity
-    double available_capacity = (strategy->current_soc - strategy->min_soc) * strategy->battery_capacity;
+    double available_capacity = fmax(0.0, (strategy->current_soc - strategy->min_soc) * strategy->battery_capacity);
```

```diff
--- a/sunlight_lut.c
+++ b/sunlight_lut.c
@@ -260,11 +260,13 @@ void CapacityBidding(void *pvParameters) {
     time_t currentTime;
     bool isDemandResponseActive = false;
     int expected_peak_hours[24] = {0};
+    int last_bid_yday = -1;
 
     for (;;) {
         currentTime = time(NULL);
         struct tm *localTime = localtime(&currentTime);
         
         // Only run capacity bidding once per day at 2 AM
-        if (localTime->tm_hour == 2 && localTime->tm_min == 0) {
+        if (localTime->tm_hour == 2 && localTime->tm_yday != last_bid_yday) {
+            last_bid_yday = localTime->tm_yday;
             // Fetch latest market data
```
]]></description>
  </task>
  <task status="PENDING">
    <id>7</id>
    <title>sunlight_lut.c:28-52 - [bug · medium] Undefined M_PI under strict C standards, leap-year out-of-bounds read on dayOfYear == 365, and thread-unsafe localtime()</title>
    <description><![CDATA[
### Location: dataopsnick-opencbp/sunlight_lut.c:28-52, 208, 265 & dataopsnick-opencbp/demand_response.c:150

**Root Cause**:
1. **Leap-Year Out-of-Bounds Array Access (`sunlight_lut.c:48-51`)**:
   `DAYS_IN_YEAR` is `365`, so `sunriseTable` and `sunsetTable` have valid indices `0..364`. On the 366th day of a leap year (Dec 31), `localTime->tm_yday` is `365`, causing `sunriseTable[dayOfYear]` and `sunsetTable[dayOfYear]` to read out of bounds.
2. **Thread-Unsafe `localtime()` across RTOS Tasks (`sunlight_lut.c:45, 208, 265`, `demand_response.c:150`)**:
   `localtime()` returns a pointer to a single static `struct tm` buffer shared across all threads/tasks. Because `SpoofSOC`, `FastDRDispatch`, and `CapacityBidding` run concurrently under FreeRTOS preemptive scheduling, concurrent calls to `localtime()` race on the static buffer.
3. **Unclamped `acos()` Domain & `M_PI` Portability (`sunlight_lut.c:30-36`)**:
   `M_PI` is a POSIX extension not defined under strict standard C (`-std=c99` / `-std=c11`) unless `_USE_MATH_DEFINES` or a fallback `#ifndef M_PI` is present. Additionally, `-tan(lat) * tan(dec)` is not clamped to `[-1.0, 1.0]` before calling `acos()`, which returns `NaN` for high latitudes.

**Fix**:
1. Define a fallback `M_PI` in `sunlight_lut.h`, clamp the `acos()` argument to `[-1.0, 1.0]`, and clamp `dayOfYear` to `DAYS_IN_YEAR - 1`.
2. Replace `localtime(&t)` with `localtime_r(&t, &tm_buf)` in all RTOS tasks.

```diff
--- a/sunlight_lut.h
+++ b/sunlight_lut.h
@@ -5,6 +5,10 @@
 #include <math.h>
 #include <stdint.h>
 #include <stdbool.h>
+
+#ifndef M_PI
+#define M_PI 3.14159265358979323846
+#endif
```

```diff
--- a/sunlight_lut.c
+++ b/sunlight_lut.c
@@ -33,7 +33,8 @@ void generateSunlightLUT() {
         double solarNoon = 12.0 - (LONGITUDE / 15.0) - TIMEZONE_OFFSET;
 
         // Calculate hour angle (in degrees)
-        double hourAngle = acos(-tan(LATITUDE * M_PI / 180.0) * tan(declination * M_PI / 180.0)) * 180.0 / M_PI;
+        double cos_ha = fmax(-1.0, fmin(1.0, -tan(LATITUDE * M_PI / 180.0) * tan(declination * M_PI / 180.0)));
+        double hourAngle = acos(cos_ha) * 180.0 / M_PI;
 
         // Calculate sunrise and sunset times (in hours)
         sunriseTable[day] = solarNoon - (hourAngle / 15.0);
@@ -43,10 +44,13 @@ void generateSunlightLUT() {
 // Retrieve sunrise and sunset times for the current day
 void getSunlightHours(double *sunrise, double *sunset) {
     // Get the current date
     time_t currentTime = time(NULL);
-    struct tm *localTime = localtime(&currentTime);
+    struct tm localTimeBuf;
+    struct tm *localTime = localtime_r(&currentTime, &localTimeBuf);
 
     // Calculate the day of the year (0-364 for indexing)
     int dayOfYear = localTime->tm_yday;
+    if (dayOfYear >= DAYS_IN_YEAR) {
+        dayOfYear = DAYS_IN_YEAR - 1;
+    }
```
]]></description>
  </task>
  <task status="PENDING">
    <id>8</id>
    <title>demand_response.h:4-5,39 - [bug · medium] Missing &lt;time.h&gt; include in demand_response.h breaks standalone compilation due to time_t usage</title>
    <description><![CDATA[
### Location: dataopsnick-opencbp/demand_response.h:4-5, 39

**Root Cause**:
`demand_response.h` uses `time_t timestamp;` inside `struct RainflowCycle` (line 39), but only includes `<stdint.h>` and `<stdbool.h>`. Any translation unit that includes `demand_response.h` before `<time.h>` (such as `demand_response.c`, where `#include "demand_response.h"` is on line 1 and `#include <time.h>` is on line 4) fails to compile with `error: unknown type name 'time_t'`.

**Fix**:
Add `#include <time.h>` to `demand_response.h`.

```diff
--- a/demand_response.h
+++ b/demand_response.h
@@ -3,6 +3,7 @@
 
 #include <stdint.h>
 #include <stdbool.h>
+#include <time.h>
```
]]></description>
  </task>
  <task status="PENDING">
    <id>9</id>
    <title>sunlight_lut.c:5,134,390 - [bug · medium] Missing &lt;errno.h&gt; include and unsafe curl_global_cleanup() inside submitBid()</title>
    <description><![CDATA[
### Location: dataopsnick-opencbp/sunlight_lut.c:5, 134, 370-391, 395

**Root Cause**:
1. **Missing `<errno.h>` (`sunlight_lut.c:134, 141, 217, 233, 404`)**:
   `sunlight_lut.c` passes `errno` to `modbus_strerror(errno)` in five places, but never includes `<errno.h>`. Depending on whether `<modbus.h>` transitively includes `<errno.h>` on the target toolchain, compilation can fail with `error: 'errno' undeclared`.
2. **Unsafe `curl_global_init` / `curl_global_cleanup` in `submitBid()` (`sunlight_lut.c:370-391`)**:
   `curl_global_init()` and `curl_global_cleanup()` are explicitly documented as **not thread-safe** and must not be called while any other thread is running. Calling them inside `submitBid()` while FreeRTOS tasks (`FastDRDispatch`, `CapacityBidding`, `MarketDataUpdate`) are active can corrupt OpenSSL/cURL global state.

**Fix**:
Include `<errno.h>` at the top of `sunlight_lut.c`, call `curl_global_init(CURL_GLOBAL_DEFAULT)` once inside `initSystem()` before creating FreeRTOS tasks, and remove `curl_global_init` / `curl_global_cleanup` from `submitBid()`.

```diff
--- a/sunlight_lut.c
+++ b/sunlight_lut.c
@@ -5,6 +5,7 @@
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
+#include <errno.h>
 #include <FreeRTOS.h>
 #include <task.h>
@@ -368,7 +369,6 @@ void analyzeHistoricalData() {
 void submitBid(double bidPrice) {
     CURL *curl;
     CURLcode res;
 
-    curl_global_init(CURL_GLOBAL_DEFAULT);
     curl = curl_easy_init();
@@ -387,8 +387,6 @@ void submitBid(double bidPrice) {
         curl_easy_cleanup(curl);
     }
-
-    curl_global_cleanup();
 }
 
 // RTOS Initialization
 void initSystem() {
+    curl_global_init(CURL_GLOBAL_DEFAULT);
     // Generate sunlight LUT
```
]]></description>
  </task>
  <task status="PENDING">
    <id>10</id>
    <title>VTN-server/README.md:1,85 & openadr_ven-client.py:61 - [documentation & bug · low] Protocol version mismatch between OpenADR 2.0b VEN client and OpenADR 3.0 VTN server guide</title>
    <description><![CDATA[
### Location: dataopsnick-opencbp/VTN-server/README.md:1-5, 85-88 & dataopsnick-opencbp/openadr_ven-client.py:61-66

**Root Cause**:
1. **OpenADR 2.0b vs 3.0 Incompatibility**: `openadr_ven-client.py` uses `openleadr`, which implements the XML-based **OpenADR 2.0b** specification (`/OpenADR2/Simple/`), whereas `VTN-server/README.md` instructs deploying `openleadr-rs`, which is a REST/JSON-based **OpenADR 3.0** VTN server. An `openleadr` 2.0b Python VEN cannot communicate directly with an OpenADR 3.0 `openleadr-rs` VTN without a protocol adapter or using a Python OpenADR 2.0b VTN (`openleadr` `OpenADRServer`).
2. **Broken Migration Command in Docker Compose Instructions (`VTN-server/README.md:85`)**:
   Line 85 instructs running `cargo sqlx migrate run` directly on the Lightsail host after only installing `docker.io` and `docker-compose-plugin` (without Rust/`cargo` or `sqlx-cli` installed on the host).

**Fix**:
Clarify the OpenADR 2.0b vs. 3.0 distinction in `VTN-server/README.md` and run database migrations via Docker Compose (or document installing `sqlx-cli`).

```diff
--- a/VTN-server/README.md
+++ b/VTN-server/README.md
@@ -1,6 +1,8 @@
 # Deploying openleadr-rs on AWS Lightsail
 
 This README provides instructions for deploying the `openleadr-rs` (https://github.com/OpenLEADR/openleadr-rs) repository, which implements an OpenADR 3.0 VTN server, on AWS Lightsail. Lightsail offers a simple and affordable way to run virtual private servers, making it suitable for deploying this project.
+
+> **Note on Protocol Compatibility:** `openleadr-rs` implements **OpenADR 3.0** (REST/JSON), whereas `openadr_ven-client.py` uses Python `openleadr` (**OpenADR 2.0b** XML). To connect `openadr_ven-client.py` directly without a 2.0b-to-3.0 bridge, deploy an `openleadr` 2.0b VTN or update the VEN client to OpenADR 3.0.
@@ -83,7 +85,6 @@ Follow these steps to deploy `openleadr-rs` on your AWS Lightsail instance:
     ```bash
     docker compose up -d db # Start the database container in detached mode
-    cargo sqlx migrate run  # Apply database migrations inside the VTN container
     docker compose up -d    # Start the VTN and other services in detached mode
     ```
```
]]></description>
  </task>
</tasklist>
```
