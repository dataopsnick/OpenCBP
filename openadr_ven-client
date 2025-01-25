import asyncio
import logging
from openleadr import OpenADRClient, enums
import modbus_tk.modbus_rtu as modbus_rtu
import serial

logging.basicConfig(level=logging.DEBUG)

MODBUS_PORT = "/dev/ttyUSB0"  # Update with your Modbus port
MODBUS_BAUD = 9600
MODBUS_UNIT = 1

class MyOpenADRClient(OpenADRClient):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.master = modbus_rtu.RtuMaster(
            serial.Serial(port=MODBUS_PORT, baudrate=MODBUS_BAUD, bytesize=8, parity='N', stopbits=1, xonxoff=0)
        )
        self.master.set_timeout(5.0)
        self.is_dr_active = False

    async def on_event(self, event):
        print(f"Received event: {event.event_id}, {event.market_context.id}, response_required: {event.response_required}")

        # Set isDemandResponseActive flag via Modbus
        self.set_dr_active(True)

        # Retrieve bid data from C code via Modbus
        capacity, price = self.get_bid_data()
        print(f"Capacity: {capacity}, Price: {price}")

        # Opt-in or opt-out based on bid data
        if capacity > 0:
            await event.opt_in()
            print("Opted in.")
        else:
            await event.opt_out()
            print("Opted out.")

        # Reset isDemandResponseActive flag after event processing
        self.set_dr_active(False)

    async def on_update_report(self, report):
        print(f"Updated report: {report.report_name}, {report.specifier_id}")
        print(f"{report.intervals}")
        return await super().on_update_report(report)

    def set_dr_active(self, value):
        self.is_dr_active = value
        self.master.execute(MODBUS_UNIT, modbus_rtu.WRITE_SINGLE_REGISTER, 0x220, output_value=int(value))

    def get_bid_data(self):
        # Read SOC and available capacity from C code via Modbus
        soc = self.master.execute(MODBUS_UNIT, modbus_rtu.READ_INPUT_REGISTERS, 0x208, 1)[0]
        capacity = self.master.execute(MODBUS_UNIT, modbus_rtu.READ_INPUT_REGISTERS, 0x210, 1)[0]
        return (capacity, 0.2)  # Example price

    def get_meter_data(self):
        try:
            soc = self.master.execute(MODBUS_UNIT, modbus_rtu.READ_INPUT_REGISTERS, 0x208, 1)[0]
            return {"PowerReal": soc}
        except Exception as e:
            print(f"Error reading meter data from modbus: {e}")
            return {}

async def main():
    client = MyOpenADRClient(
        ven_name="MyPythonVEN",
        vtn_url="https://your_vtn_host:8080/OpenADR2/Simple/",
        cert="path_to_your_ven_cert.pem",
        key="path_to_your_ven_key.pem",
        allow_jitter=True
    )

    report_spec_id = "my_power_report"
    client.add_report(
        report_name="METADATA_TELEMETRY_USAGE",
        specifier_id=report_spec_id,
        data_collection_mode=enums.DataCollectionMode.FULL_DATA,
        measurement_names=["PowerReal"],
    )

    while True:
        # Read meter data from Modbus and update report
        meter_data = client.get_meter_data()
        report = client.reports[report_spec_id]
        if report and meter_data:
            report.update(meter_data)
        await asyncio.sleep(5)

    await client.run()

if __name__ == "__main__":
    asyncio.run(main())
