import customtkinter as ctk
import json
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from collections import deque
from datetime import datetime
import os
import time

# ==============================================================================
# DEVELOPER CONFIGURATION - SYSTEM CALIBRATION
# ==============================================================================
BATT_CRITICAL = 2.6       # Voltage threshold for RED warning
UPDATE_RATE_MS = 2000     # GUI refresh interval (2 seconds)
JSON_FILE = "live_data.json" 

# Node Settings: Hard-coded colors and friendly names
SENSOR_NAMES = {1: "Outdoor", 2: "Indoor", 3: "Garden", 4: "Garage", 5: "Attic"}
SENSOR_COLORS = {1: "#ff7f0e", 2: "#1f77b4", 3: "#2ca02c", 4: "#9467bd", 5: "#d62728"}
# ==============================================================================

class WeatherApp(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("GEMINI Control Center v18.1")
        self.geometry("1250x850")
        
        # Force the overall window to the same dark background as the graphs
        self.configure(fg_color="#1a1a1a")

        # --- DATA ARCHITECTURE ---
        self.data_store = {}        # Internal buffer for chart history (Deques)
        self.last_packet_time = {}   # Tracks Unix time of last successful radio contact
        self.previous_values = {}    # Used to detect actual data changes for timer reset
        
        self.setup_ui()
        self.update_loop()

    def setup_ui(self):
        """
        Organizes the GUI into a three-part layout: Header, Sidebar, and Center Graph.
        Background set to #1a1a1a for a seamless "scientific dashboard" aesthetic.
        """
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        # 1. HEADER: Horizontal container for Node Status Cards
        self.header = ctk.CTkFrame(self, height=100, fg_color="#1a1a1a", border_width=0, corner_radius=0)
        self.header.grid(row=0, column=0, sticky="nsew", padx=0, pady=5)
        self.node_widgets = {}

        # 2. GRAPHS: Center plotting area for Temp and Humidity
        self.setup_plotting_engine()

        # 3. SIDEBAR: Scrollable container for global settings and sleep sliders
        self.sidebar = ctk.CTkScrollableFrame(self, width=300, label_text="SYSTEM SETTINGS", 
                                              fg_color="#1a1a1a", label_text_color="white",
                                              border_width=0, corner_radius=0)
        self.sidebar.grid(row=0, column=1, rowspan=2, sticky="nsew", padx=5, pady=0)
        
        self.setup_global_ui_controls()
        self.sleep_sliders = {} 

    def setup_plotting_engine(self):
        """Initializes Matplotlib with a dark theme that blends into the GUI."""
        plt.style.use('dark_background')
        self.fig, (self.ax_t, self.ax_h) = plt.subplots(2, 1, figsize=(8, 6))
        self.fig.patch.set_facecolor('#1a1a1a') 
        
        self.ax_t.set_facecolor('#1a1a1a')
        self.ax_h.set_facecolor('#1a1a1a')
        
        self.ax_t.set_ylabel("Temp (°C)", color="white")
        self.ax_t.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
        
        self.ax_h.set_ylabel("Hum (%)", color="white")
        self.ax_h.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))

        self.canvas = FigureCanvasTkAgg(self.fig, self)
        self.canvas.get_tk_widget().grid(row=1, column=0, sticky="nsew", padx=10, pady=5)
        self.temp_lines, self.hum_lines = {}, {}

    def setup_global_ui_controls(self):
        """Sidebar controls for history window and system timeouts."""
        ctk.CTkLabel(self.sidebar, text="Global History (Samples)", text_color="white").pack(pady=(10,0))
        self.window_slider = ctk.CTkSlider(self.sidebar, from_=10, to=500, number_of_steps=49)
        self.window_slider.set(60)
        self.window_slider.pack(pady=5)

        self.timeout_info_lbl = ctk.CTkLabel(self.sidebar, text="Timeout: 15.0 min", 
                                             font=("Arial", 12, "bold"), text_color="white")
        self.timeout_info_lbl.pack(pady=(15,0))
        self.timeout_slider = ctk.CTkSlider(self.sidebar, from_=1, to=60, 
                                           command=self.update_timeout_label)
        self.timeout_slider.set(15)
        self.timeout_slider.pack(pady=5)

        ctk.CTkLabel(self.sidebar, text="--- PER-NODE SLEEP ---", 
                     font=("Arial", 12, "bold"), text_color="white").pack(pady=20)
        
        self.btn_sync = ctk.CTkButton(self.sidebar, text="Update Arduino Timing", 
                                      fg_color="#2ca02c", hover_color="#1e7a1e",
                                      command=self.save_sleep_config)
        self.btn_sync.pack(pady=5)

    def update_timeout_label(self, val):
        self.timeout_info_lbl.configure(text=f"Timeout: {float(val):.1f} min")

    def create_dynamic_node_slider(self, nid):
        """Creates an individualized slider for each Node ID detected."""
        name = SENSOR_NAMES.get(nid, f"Node {nid}")
        container = ctk.CTkFrame(self.sidebar, fg_color="#252525", corner_radius=8)
        container.pack(fill="x", pady=5, padx=5)

        info_lbl = ctk.CTkLabel(container, text=f"{name}: 0.1 min", font=("Arial", 11), text_color="white")
        info_lbl.pack(pady=2)

        # Calculates minutes based on 8-second watchdog cycles
        slider = ctk.CTkSlider(container, from_=1, to=150, 
                               command=lambda v, l=info_lbl, n=name: l.configure(text=f"{n}: {(float(v)*8/60):.1f} min"))
        slider.set(1)
        slider.pack(pady=5)
        self.sleep_sliders[nid] = slider

    def save_sleep_config(self):
        """Writes current slider values to config.txt for the C++ Server to read/flush."""
        settings = []
        for i in range(1, 6):
            val = int(self.sleep_sliders[i].get()) if i in self.sleep_sliders else 1
            settings.append(str(val))
        with open("config.txt", "w") as f:
            f.write(" ".join(settings))

    def create_sensor_card(self, nid):
        """Creates a horizontal status tile in the header for a new Node."""
        frame = ctk.CTkFrame(self.header, width=150, fg_color="#252525", corner_radius=10)
        frame.pack(side="left", padx=5, pady=5, fill="y")
        
        color = SENSOR_COLORS.get(nid, "white")
        ctk.CTkLabel(frame, text=SENSOR_NAMES.get(nid, f"Node {nid}"), 
                     text_color=color, font=("Arial", 11, "bold")).pack(pady=(2,0))
        
        val_lbl = ctk.CTkLabel(frame, text="--.-°C", font=("Arial", 20, "bold"), text_color="white")
        val_lbl.pack()
        
        batt_lbl = ctk.CTkLabel(frame, text="Batt: -.--V", font=("Arial", 10), text_color="white")
        batt_lbl.pack()
        
        tx_lbl = ctk.CTkLabel(frame, text="TX: --", font=("Arial", 10), text_color="#888888")
        tx_lbl.pack()

        self.node_widgets[nid] = {'val': val_lbl, 'batt': batt_lbl, 'tx': tx_lbl, 'frame': frame}
        self.create_dynamic_node_slider(nid)

    def update_loop(self):
        """
        Main logic loop that handles JSON reading, timeout management, 
        and the DYNAMIC Y-AXIS scaling logic.
        """
        try:
            if os.path.exists(JSON_FILE):
                with open(JSON_FILE, 'r') as f:
                    data = json.load(f)
                
                # Tracking visible points to calculate new scale bounds
                visible_temps = [] 
                visible_hums = []
                now_dt, now_unix = datetime.now(), time.time()

                for entry in data:
                    nid = entry.get('id')
                    if nid is None: continue
                    
                    if nid not in self.data_store:
                        # Initialize history buffers and chart lines
                        self.data_store[nid] = {'temp': deque(maxlen=1000), 'hum': deque(maxlen=1000), 't': deque(maxlen=1000)}
                        self.create_sensor_card(nid)
                        self.temp_lines[nid], = self.ax_t.plot([], [], color=SENSOR_COLORS.get(nid), label=SENSOR_NAMES.get(nid), linewidth=1.5)
                        self.hum_lines[nid], = self.ax_h.plot([], [], color=SENSOR_COLORS.get(nid), linewidth=1.5)
                        self.last_packet_time[nid] = now_unix
                        self.previous_values[nid] = entry.get('temp')

                    # Detect actual data changes to reset "TX: Xs ago"
                    current_temp = entry.get('temp')
                    if current_temp != self.previous_values.get(nid):
                        self.last_packet_time[nid] = now_unix
                        self.previous_values[nid] = current_temp

                    self.data_store[nid]['temp'].append(current_temp)
                    self.data_store[nid]['hum'].append(entry.get('hum'))
                    self.data_store[nid]['t'].append(now_dt)

                    # Update Dashboard Card
                    self.node_widgets[nid]['val'].configure(text=f"{current_temp:.1f}°C")
                    bv = entry.get('batt', 0)
                    self.node_widgets[nid]['batt'].configure(text=f"Batt: {bv:.2f}V", 
                                                            text_color="#ff4444" if bv < BATT_CRITICAL else "white")

                # PROCESS VISIBILITY & DYNAMIC SCALING
                timeout_sec = self.timeout_slider.get() * 60
                window = int(self.window_slider.get()) # Magnitude of samples to show

                for nid in list(self.data_store.keys()):
                    delta = int(now_unix - self.last_packet_time.get(nid, now_unix))
                    self.node_widgets[nid]['tx'].configure(text=f"TX: {delta}s ago")

                    if delta > timeout_sec:
                        # Clear lines if node is offline
                        self.temp_lines[nid].set_data([], [])
                        self.hum_lines[nid].set_data([], [])
                        self.node_widgets[nid]['frame'].pack_forget()
                    else:
                        self.node_widgets[nid]['frame'].pack(side="left", padx=5, pady=5)
                        
                        # Identify only the points visible within the current window
                        t_seg = list(self.data_store[nid]['t'])[-window:]
                        temp_seg = list(self.data_store[nid]['temp'])[-window:]
                        hum_seg = list(self.data_store[nid]['hum'])[-window:]

                        visible_temps.extend(temp_seg)
                        visible_hums.extend(hum_seg)

                        # Update data lines
                        self.temp_lines[nid].set_data(t_seg, temp_seg)
                        self.hum_lines[nid].set_data(t_seg, hum_seg)

                # APPLY ADAPTIVE SCALING
                # This ensures the graph "zooms in" on current data.
                if visible_temps:
                    t_min, t_max = min(visible_temps), max(visible_temps)
                    # Add 15% padding so the line doesn't hug the top/bottom
                    padding = max((t_max - t_min) * 0.15, 1.0)
                    self.ax_t.set_ylim(t_min - padding, t_max + padding)

                if visible_hums:
                    h_min, h_max = min(visible_hums), max(visible_hums)
                    h_padding = max((h_max - h_min) * 0.15, 5.0)
                    # Clamping humidity between 0 and 100
                    self.ax_h.set_ylim(max(0, h_min - h_padding), min(100, h_max + h_padding))

                # Refresh Plot Canvas
                self.ax_t.relim(); self.ax_t.autoscale_view(scalex=True, scaley=False)
                self.ax_h.relim(); self.ax_h.autoscale_view(scalex=True, scaley=False)
                self.ax_t.legend(loc='upper left', fontsize='7', ncol=3, frameon=False)
                self.canvas.draw()

        except Exception as e:
            print(f"Update Error: {e}") 

        self.after(UPDATE_RATE_MS, self.update_loop)

if __name__ == "__main__":
    WeatherApp().mainloop()
