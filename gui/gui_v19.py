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
BATT_CRITICAL  = 2.6
UPDATE_RATE_MS = 2000     # UI card refresh interval (2 seconds)
GRAPH_RATE_MS  = 30000    # Graph redraw interval (30 seconds)
JSON_FILE      = "../live_data.json"

SENSOR_NAMES  = {1: "Outdoor", 2: "Indoor", 3: "Garden", 4: "Garage", 5: "Attic"}
SENSOR_COLORS = {1: "#ff7f0e", 2: "#1f77b4", 3: "#2ca02c", 4: "#9467bd", 5: "#d62728"}

# Discrete sleep steps in minutes. Multiplier sent to Arduino = round(min * 60 / 8).
# Arduino firmware uses int (16-bit on AVR), so 60 min = 450 multiplier is safe.
SLEEP_STEPS_MIN = [1, 2, 3, 4, 5, 10, 15, 20, 30, 60]

# Time window → bucket width (seconds) and max stored points.
# '15min' uses raw 2s data (bucket_sec=None); others use averaged buckets.
WINDOW_CONFIG = {
    '15min': {'bucket_sec': None, 'maxlen': 450},  # 450 × 2s = 15 min raw
    '1h':    {'bucket_sec': 60,   'maxlen': 60},   # 1-min buckets, 1h
    '12h':   {'bucket_sec': 600,  'maxlen': 72},   # 10-min buckets, 12h
    '24h':   {'bucket_sec': 1200, 'maxlen': 72},   # 20-min buckets, 24h
}
# ==============================================================================

class WeatherApp(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("SÄÄASEMA v1.0")
        self.geometry("1250x850")
        self.configure(fg_color="#1a1a1a")

        self.data_store       = {}
        self.last_packet_time = {}
        self.previous_values  = {}
        self.active_nodes     = set()
        self.hidden_nodes     = set()   # nodes toggled off the graph by the user
        self.selected_window  = '15min'
        self.sensor_names     = dict(SENSOR_NAMES)  # mutable copy for renaming

        self.setup_ui()
        self.update_loop()
        self.graph_loop()

    # --------------------------------------------------------------------------
    # UI SETUP
    # --------------------------------------------------------------------------

    def setup_ui(self):
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        self.header = ctk.CTkFrame(self, height=100, fg_color="#1a1a1a", border_width=0, corner_radius=0)
        self.header.grid(row=0, column=0, sticky="nsew", padx=0, pady=5)
        self.node_widgets = {}

        self.setup_plotting_engine()

        self.sidebar = ctk.CTkScrollableFrame(self, width=300, label_text="SYSTEM SETTINGS",
                                              fg_color="#1a1a1a", label_text_color="white",
                                              border_width=0, corner_radius=0)
        self.sidebar.grid(row=0, column=1, rowspan=2, sticky="nsew", padx=5, pady=0)

        self.setup_global_ui_controls()
        self.sleep_sliders = {}

    def setup_plotting_engine(self):
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
        ctk.CTkLabel(self.sidebar, text="Graph Time Window", text_color="white").pack(pady=(10, 0))
        btn_frame = ctk.CTkFrame(self.sidebar, fg_color="#1a1a1a")
        btn_frame.pack(pady=5)
        self.window_buttons = {}
        for label in ['15min', '1h', '12h', '24h']:
            btn = ctk.CTkButton(btn_frame, text=label, width=65,
                                fg_color="#2a2a2a", hover_color="#3a3a3a",
                                command=lambda w=label: self.set_window(w))
            btn.pack(side='left', padx=2)
            self.window_buttons[label] = btn
        self._highlight_window_btn('15min')

        self.timeout_info_lbl = ctk.CTkLabel(self.sidebar, text="Timeout: 15.0 min",
                                             font=("Arial", 12, "bold"), text_color="white")
        self.timeout_info_lbl.pack(pady=(15, 0))
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

    # --------------------------------------------------------------------------
    # SENSOR CARDS
    # --------------------------------------------------------------------------

    def create_sensor_card(self, nid):
        """Creates a clickable header tile. Single click toggles graph visibility."""
        frame = ctk.CTkFrame(self.header, width=150, fg_color="#252525", corner_radius=10)
        frame.pack(side="left", padx=5, pady=5, fill="y")
        frame.configure(cursor="hand2")

        color    = SENSOR_COLORS.get(nid, "white")
        name     = self.sensor_names.get(nid, f"Node {nid}")
        name_lbl = ctk.CTkLabel(frame, text=name, text_color=color, font=("Arial", 11, "bold"))
        name_lbl.pack(pady=(2, 0))
        val_lbl  = ctk.CTkLabel(frame, text="--.-°C", font=("Arial", 20, "bold"), text_color="white")
        val_lbl.pack()
        batt_lbl = ctk.CTkLabel(frame, text="Batt: -.--V", font=("Arial", 10), text_color="white")
        batt_lbl.pack()
        tx_lbl   = ctk.CTkLabel(frame, text="TX: --", font=("Arial", 10), text_color="#888888")
        tx_lbl.pack()

        for widget in [frame, name_lbl, val_lbl, batt_lbl, tx_lbl]:
            widget.bind("<Button-1>", lambda _, n=nid: self.toggle_node_graph(n))

        self.node_widgets[nid] = {
            'val': val_lbl, 'batt': batt_lbl, 'tx': tx_lbl,
            'frame': frame, 'name_lbl': name_lbl,
        }
        self.create_dynamic_node_slider(nid)

    def toggle_node_graph(self, nid):
        """Toggles a node's graph lines on/off. Card remains visible either way."""
        if nid in self.hidden_nodes:
            self.hidden_nodes.discard(nid)
            self._set_card_dimmed(nid, False)
        else:
            self.hidden_nodes.add(nid)
            self._set_card_dimmed(nid, True)
        self.redraw_graph()

    def _set_card_dimmed(self, nid, dimmed):
        w = self.node_widgets[nid]
        name_color = "#555555"                        if dimmed else SENSOR_COLORS.get(nid, "white")
        val_color  = "#666666"                        if dimmed else "white"
        bg_color   = "#1c1c1c"                        if dimmed else "#252525"
        w['name_lbl'].configure(text_color=name_color)
        w['val'].configure(text_color=val_color)
        w['frame'].configure(fg_color=bg_color)

    # --------------------------------------------------------------------------
    # PER-NODE SLEEP SLIDERS + RENAME
    # --------------------------------------------------------------------------

    def create_dynamic_node_slider(self, nid):
        """Sidebar section for a node: sleep slider (discrete steps) and rename button."""
        container = ctk.CTkFrame(self.sidebar, fg_color="#252525", corner_radius=8)
        container.pack(fill="x", pady=5, padx=5)

        info_lbl = ctk.CTkLabel(
            container,
            text=f"{self.sensor_names.get(nid, f'Node {nid}')}: 1 min",
            font=("Arial", 11), text_color="white"
        )
        info_lbl.pack(pady=(4, 0))

        ctk.CTkButton(container, text="Rename", width=80, height=22,
                      fg_color="#444444", hover_color="#555555", font=("Arial", 10),
                      command=lambda n=nid: self.rename_node(n)).pack(pady=2)

        slider = ctk.CTkSlider(
            container,
            from_=0, to=len(SLEEP_STEPS_MIN) - 1,
            number_of_steps=len(SLEEP_STEPS_MIN) - 1,
            command=lambda v, l=info_lbl, n=nid: l.configure(
                text=f"{self.sensor_names.get(n, f'Node {n}')}: {SLEEP_STEPS_MIN[round(float(v))]} min"
            )
        )
        slider.set(0)
        slider.pack(pady=5, padx=10)

        self.sleep_sliders[nid] = slider
        self.node_widgets[nid]['sidebar_lbl'] = info_lbl

    def save_sleep_config(self):
        """Converts discrete slider index → 8s-cycle multiplier and writes to config.txt."""
        settings = []
        for i in range(1, 6):
            if i in self.sleep_sliders:
                idx        = round(float(self.sleep_sliders[i].get()))
                minutes    = SLEEP_STEPS_MIN[idx]
                multiplier = max(1, round(minutes * 60 / 8))
            else:
                multiplier = 1
            settings.append(str(multiplier))
        with open("../config.txt", "w") as f:
            f.write(" ".join(settings))

    def rename_node(self, nid):
        current  = self.sensor_names.get(nid, f"Node {nid}")
        dialog   = ctk.CTkInputDialog(text=f"New name for '{current}':", title="Rename Sensor")
        new_name = dialog.get_input()
        if not new_name or not new_name.strip():
            return
        new_name = new_name.strip()
        self.sensor_names[nid] = new_name
        self.node_widgets[nid]['name_lbl'].configure(text=new_name)
        self._update_sidebar_lbl(nid)
        if nid in self.temp_lines:
            self.temp_lines[nid].set_label(new_name)
        self.redraw_graph()

    def _update_sidebar_lbl(self, nid):
        if nid not in self.sleep_sliders:
            return
        idx     = round(float(self.sleep_sliders[nid].get()))
        minutes = SLEEP_STEPS_MIN[idx]
        name    = self.sensor_names.get(nid, f"Node {nid}")
        self.node_widgets[nid]['sidebar_lbl'].configure(text=f"{name}: {minutes} min")

    # --------------------------------------------------------------------------
    # WINDOW / TIMEOUT CONTROLS
    # --------------------------------------------------------------------------

    def _highlight_window_btn(self, active):
        for label, btn in self.window_buttons.items():
            btn.configure(fg_color="#2ca02c" if label == active else "#2a2a2a")

    def set_window(self, window):
        self.selected_window = window
        self._highlight_window_btn(window)
        self.redraw_graph()

    def update_timeout_label(self, val):
        self.timeout_info_lbl.configure(text=f"Timeout: {float(val):.1f} min")

    # --------------------------------------------------------------------------
    # DATA LAYER
    # --------------------------------------------------------------------------

    def _init_node(self, nid, now_unix, initial_temp):
        raw_maxlen = WINDOW_CONFIG['15min']['maxlen']
        self.data_store[nid] = {
            'raw': {
                'temp': deque(maxlen=raw_maxlen),
                'hum':  deque(maxlen=raw_maxlen),
                't':    deque(maxlen=raw_maxlen),
            },
            'buckets': {
                key: {
                    'temp':      deque(maxlen=cfg['maxlen']),
                    'hum':       deque(maxlen=cfg['maxlen']),
                    't':         deque(maxlen=cfg['maxlen']),
                    'acc_temp':  [],
                    'acc_hum':   [],
                    'acc_start': None,
                }
                for key, cfg in WINDOW_CONFIG.items() if cfg['bucket_sec'] is not None
            }
        }
        self.create_sensor_card(nid)
        self.temp_lines[nid], = self.ax_t.plot([], [], color=SENSOR_COLORS.get(nid),
                                                label=self.sensor_names.get(nid, f"Node {nid}"),
                                                linewidth=1.5)
        self.hum_lines[nid],  = self.ax_h.plot([], [], color=SENSOR_COLORS.get(nid), linewidth=1.5)
        self.last_packet_time[nid] = now_unix
        self.previous_values[nid]  = initial_temp

    def _update_buckets(self, nid, temp, hum, now_unix):
        for key, cfg in WINDOW_CONFIG.items():
            if cfg['bucket_sec'] is None:
                continue
            b = self.data_store[nid]['buckets'][key]
            if b['acc_start'] is None:
                b['acc_start'] = now_unix
            b['acc_temp'].append(temp)
            b['acc_hum'].append(hum)
            if now_unix - b['acc_start'] >= cfg['bucket_sec']:
                avg_temp = sum(b['acc_temp']) / len(b['acc_temp'])
                avg_hum  = sum(b['acc_hum'])  / len(b['acc_hum'])
                mid_dt   = datetime.fromtimestamp(b['acc_start'] + cfg['bucket_sec'] / 2)
                b['temp'].append(avg_temp)
                b['hum'].append(avg_hum)
                b['t'].append(mid_dt)
                b['acc_temp']  = []
                b['acc_hum']   = []
                b['acc_start'] = now_unix

    def _get_plot_data(self, nid):
        if self.selected_window == '15min':
            s = self.data_store[nid]['raw']
            return list(s['t']), list(s['temp']), list(s['hum'])
        b = self.data_store[nid]['buckets'][self.selected_window]
        return list(b['t']), list(b['temp']), list(b['hum'])

    # --------------------------------------------------------------------------
    # LOOPS
    # --------------------------------------------------------------------------

    def update_loop(self):
        """Reads sensor JSON and refreshes UI cards every 2 seconds. No graph redraws here."""
        try:
            if os.path.exists(JSON_FILE):
                with open(JSON_FILE, 'r') as f:
                    data = json.load(f)

                now_dt, now_unix = datetime.now(), time.time()
                timeout_sec = self.timeout_slider.get() * 60

                for entry in data:
                    nid = entry.get('id')
                    if nid is None:
                        continue
                    current_temp = entry.get('temp')
                    current_hum  = entry.get('hum')

                    if nid not in self.data_store:
                        self._init_node(nid, now_unix, current_temp)

                    if current_temp != self.previous_values.get(nid):
                        self.last_packet_time[nid] = now_unix
                        self.previous_values[nid]  = current_temp

                    self.data_store[nid]['raw']['temp'].append(current_temp)
                    self.data_store[nid]['raw']['hum'].append(current_hum)
                    self.data_store[nid]['raw']['t'].append(now_dt)
                    self._update_buckets(nid, current_temp, current_hum, now_unix)

                    self.node_widgets[nid]['val'].configure(text=f"{current_temp:.1f}°C")
                    bv = entry.get('batt', 0)
                    self.node_widgets[nid]['batt'].configure(
                        text=f"Batt: {bv:.2f}V",
                        text_color="#ff4444" if bv < BATT_CRITICAL else "white"
                    )

                for nid in list(self.data_store.keys()):
                    delta = int(now_unix - self.last_packet_time.get(nid, now_unix))
                    self.node_widgets[nid]['tx'].configure(text=f"TX: {delta}s ago")
                    if delta > timeout_sec:
                        self.active_nodes.discard(nid)
                        self.node_widgets[nid]['frame'].pack_forget()
                    else:
                        self.active_nodes.add(nid)
                        self.node_widgets[nid]['frame'].pack(side="left", padx=5, pady=5)

        except Exception as e:
            print(f"Update Error: {e}")

        self.after(UPDATE_RATE_MS, self.update_loop)

    def redraw_graph(self):
        """Redraws Matplotlib canvas. Skips nodes that are timed out or toggled off."""
        visible_temps, visible_hums = [], []

        for nid in list(self.data_store.keys()):
            if nid not in self.active_nodes or nid in self.hidden_nodes:
                self.temp_lines[nid].set_data([], [])
                self.hum_lines[nid].set_data([], [])
                continue
            t_seg, temp_seg, hum_seg = self._get_plot_data(nid)
            visible_temps.extend(temp_seg)
            visible_hums.extend(hum_seg)
            self.temp_lines[nid].set_data(t_seg, temp_seg)
            self.hum_lines[nid].set_data(t_seg, hum_seg)

        if visible_temps:
            t_min, t_max = min(visible_temps), max(visible_temps)
            padding = max((t_max - t_min) * 0.15, 1.0)
            self.ax_t.set_ylim(t_min - padding, t_max + padding)

        if visible_hums:
            h_min, h_max = min(visible_hums), max(visible_hums)
            h_padding = max((h_max - h_min) * 0.15, 5.0)
            self.ax_h.set_ylim(max(0, h_min - h_padding), min(100, h_max + h_padding))

        self.ax_t.relim(); self.ax_t.autoscale_view(scalex=True, scaley=False)
        self.ax_h.relim(); self.ax_h.autoscale_view(scalex=True, scaley=False)
        self.ax_t.legend(loc='upper left', fontsize='7', ncol=3, frameon=False)
        self.canvas.draw()

    def graph_loop(self):
        """Redraws the graph every 30 seconds, independent of the UI card refresh."""
        self.redraw_graph()
        self.after(GRAPH_RATE_MS, self.graph_loop)


if __name__ == "__main__":
    WeatherApp().mainloop()
