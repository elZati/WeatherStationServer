GEMINI Multi-Sensor Control Center: Technical Documentation (v13.3)
1. System Overview

The GEMINI Weather Station is a multi-tier IoT architecture. It collects high-precision environmental data from remote nodes and visualizes it on a unified Raspberry Pi dashboard.
2. The Data Pipeline

The journey of a single temperature reading follows three distinct stages:

    Arduino Nodes (The Source): Nodes sleep for a defined period (default 8 seconds). Upon waking, they read the HTU21D sensor and the battery voltage. This data is packed into a 64-bit C-Struct and transmitted via the nRF24L01 radio.

    C++ Server (The Bridge):
    Running on the Raspberry Pi, the server acts as a "Listener." When it receives a packet, it identifies the Node ID and updates a local memory array. Every time a new packet arrives, it overwrites live_data.json with the current state of all active sensors.

    Python GUI (The Interface):
    The GUI reads the live_data.json file every 2000ms. It converts the raw JSON numbers into the visual cards and graphs you see on the screen.

3. Advanced GUI Logic
A. The "0s ago" Timer Fix (Change Detection)

In standard GUIs, the "Last Updated" timer resets every time the data is polled. In your system, we use Value-Based Change Detection:

    The GUI keeps a "memory" of the last temperature and battery reading for every node.

    If the GUI reads the JSON and the temperature for Node 1 is exactly the same as 2 seconds ago, it assumes no new radio packet has arrived. The timer continues to count up.

    If the temperature or battery voltage changes by even 0.01, the GUI knows the radio just received a fresh packet. The timer resets to 0.

B. Adaptive Y-Axis Scaling

Because your nodes might be in vastly different environments (e.g., a Freezer at -20°C and an Attic at +45°C), the graph cannot use a fixed scale.

    The GUI iterates through the data buffers of only the active (non-timed-out) sensors.

    It finds the global minimum and maximum across all sensors.

    It dynamically sets the graph limits with a 5°C "breathing room" buffer above and below the highest and lowest readings.

C. Individualized Sleep Management

The GUI now features a dynamic sidebar that grows as sensors check in.

    The Multiplier: Arduinos sleep in increments of 8 seconds (the maximum Watchdog Timer limit).

    The Conversion: When you move a slider in the GUI, it calculates the real-world time:
    Minutes=60Slider Value×8 seconds​

    The Push: When you click "Update Arduino Timing," the GUI writes these multipliers to config.txt. The C++ server picks these up and sends them to the Arduinos via ACK Payloads (data sent back to the Arduino the moment it finishes a transmission).

4. UI Design Philosophy (Seamless Dark Mode)

The v13.3 update focuses on a "Glass" or "Seamless" aesthetic:

    Background Unification: The background of the Matplotlib charts, the Sidebar, and the Header are all hard-coded to #1a1a1a.

    No Dividers: By setting border_width=0, the visual "seams" between the sidebar and the graph area are removed.

    High Contrast: To ensure readability in the dark theme, all primary measurements (Temperature) are forced to Bold White, while secondary info (Battery/TX) uses a subtle grey.

5. Troubleshooting the Data "Chain"

If data stops appearing, check the chain in this order:

    JSON Check: Open a terminal and type cat live_data.json. If it's empty, the C++ server isn't writing data.

    Server Check: Check the C++ terminal for "RX" messages. If none appear, the Arduinos aren't talking to the Pi.

    GUI Timeout: Ensure the Timeout Slider in the sidebar isn't set too low. If a sensor sleeps for 10 minutes but the timeout is set to 5 minutes, the GUI will hide the sensor before it ever wakes up.
