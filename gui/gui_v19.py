import customtkinter as ctk
import json
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from collections import deque
from datetime import datetime
import os
import time
import threading
import urllib.request
import urllib.parse

# ==============================================================================
# DEVELOPER CONFIGURATION - SYSTEM CALIBRATION
# ==============================================================================
BATT_CRITICAL       = 2.6
UPDATE_RATE_MS      = 2000
GRAPH_RATE_MS       = 30000
WEATHER_INTERVAL_MS = 6 * 60 * 60 * 1000    # re-fetch every 6 hours
JSON_FILE           = "../live_data.json"
DEFAULT_LOCATION    = "Tampere"
PRESSURE_SENTINEL   = 33.33                  # nodes without BMP180 send this value

SENSOR_NAMES  = {1: "Outdoor", 2: "Indoor", 3: "Garden", 4: "Garage", 5: "Attic"}
SENSOR_COLORS = {1: "#ff7f0e", 2: "#1f77b4", 3: "#2ca02c", 4: "#9467bd", 5: "#d62728"}

SLEEP_STEPS_MIN = [0, 1, 2, 3, 4, 5, 10, 15, 20, 30, 60]

FULLSCREEN_PANEL_W = 240   # width of the right card panel in fullscreen mode

CARD_FONTS_NORMAL = {
    'name':  ("Arial", 11, "bold"),
    'val':   ("Arial", 20, "bold"),
    'batt':  ("Arial", 10),
    'press': ("Arial", 10),
    'trend': ("Arial",  9),
    'aq':    ("Arial",  9),
    'tx':    ("Arial", 10),
}
CARD_FONTS_FULL = {
    'name':  ("Arial", 14, "bold"),
    'val':   ("Arial", 28, "bold"),
    'batt':  ("Arial", 13),
    'press': ("Arial", 13),
    'trend': ("Arial", 12),
    'aq':    ("Arial", 12),
    'tx':    ("Arial", 12),
}


def _fmt_sleep(minutes):
    return "Fastest" if minutes == 0 else f"{minutes} min"

WINDOW_CONFIG = {
    '15min': {'bucket_sec': None, 'maxlen': 450},
    '1h':    {'bucket_sec': 60,   'maxlen': 60},
    '12h':   {'bucket_sec': 600,  'maxlen': 72},
    '24h':   {'bucket_sec': 1200, 'maxlen': 72},
}

# Pressure change thresholds in hPa per 3 hours (meteorological standard)
TREND_RISE_THRESHOLD = 1.5
TREND_FALL_THRESHOLD = -1.5
TREND_LABELS = {'▲': 'Improving', '▼': 'Worsening', '─': 'Stable'}
TREND_COLORS = {'▲': '#4CAF50',   '▼': '#f44336',   '─': '#aaaaaa'}

WMO_DESCRIPTIONS = {
    0: "Clear sky",      1: "Mainly clear",   2: "Partly cloudy",  3: "Overcast",
    45: "Fog",           48: "Icy fog",
    51: "Lt drizzle",    53: "Drizzle",        55: "Hvy drizzle",
    61: "Light rain",    63: "Rain",           65: "Heavy rain",
    71: "Light snow",    73: "Snow",           75: "Heavy snow",    77: "Snow grains",
    80: "Showers",       81: "Rain showers",   82: "Hvy showers",
    85: "Snow showers",  86: "Hvy snow showers",
    95: "Thunderstorm",  96: "Storm + hail",   99: "Storm + hail",
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
        self.hidden_nodes     = set()
        self.selected_window  = '15min'
        self.sensor_names     = dict(SENSOR_NAMES)
        self.weather_location = DEFAULT_LOCATION
        self.weather_data     = {}

        self.setup_ui()
        self.update_loop()
        self.graph_loop()
        self.weather_loop()

    # --------------------------------------------------------------------------
    # UI SETUP
    # --------------------------------------------------------------------------

    def setup_ui(self):
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)
        self.sidebar_visible = True

        self.header = ctk.CTkFrame(self, height=110, fg_color="#1a1a1a", border_width=0, corner_radius=0)
        self.header.grid(row=0, column=0, sticky="nsew", padx=0, pady=5)
        self.node_widgets = {}

        self._create_sidebar_toggle()
        self.create_weather_card()
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
        self.ax_h.set_ylabel("Hum (%)", color="white")
        for ax in (self.ax_t, self.ax_h):
            ax.xaxis.set_major_locator(mdates.AutoDateLocator(minticks=3, maxticks=7, interval_multiples=True))
            ax.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
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

        ctk.CTkLabel(self.sidebar, text="Weather Location", text_color="white").pack(pady=(15, 0))
        loc_frame = ctk.CTkFrame(self.sidebar, fg_color="#1a1a1a")
        loc_frame.pack(pady=5, fill="x", padx=5)
        self.loc_entry = ctk.CTkEntry(loc_frame, placeholder_text=DEFAULT_LOCATION)
        self.loc_entry.pack(side="left", fill="x", expand=True, padx=(0, 4))
        ctk.CTkButton(loc_frame, text="Update", width=70, height=28,
                      fg_color="#444444", hover_color="#555555",
                      command=self._on_location_update).pack(side="right")

        ctk.CTkLabel(self.sidebar, text="--- PER-NODE SLEEP ---",
                     font=("Arial", 12, "bold"), text_color="white").pack(pady=20)

        self.btn_sync = ctk.CTkButton(self.sidebar, text="Update Arduino Timing",
                                      fg_color="#2ca02c", hover_color="#1e7a1e",
                                      command=self.save_sleep_config)
        self.btn_sync.pack(pady=5)

    # --------------------------------------------------------------------------
    # SIDEBAR TOGGLE
    # --------------------------------------------------------------------------

    def _create_sidebar_toggle(self):
        self.sidebar_btn = ctk.CTkButton(
            self.header, text="▶", width=44, height=44,
            fg_color="#2a2a2a", hover_color="#3a3a3a",
            font=("Arial", 16, "bold"),
            command=self.toggle_sidebar
        )
        self.sidebar_btn.pack(side="right", padx=(0, 4), pady=5)

    def toggle_sidebar(self):
        if self.sidebar_visible:
            self.sidebar.grid_remove()
            self.sidebar_visible = False
            self._enter_fullscreen_layout()
        else:
            self._enter_normal_layout()
            self.sidebar.grid()
            self.sidebar_visible = True

    def _enter_fullscreen_layout(self):
        """Cards panel moves to right column; graph fills full height on the left."""
        self.header.configure(height=1)
        self.header.grid_configure(row=0, column=1, rowspan=2, sticky="nsew", padx=0, pady=0)
        self.canvas.get_tk_widget().grid_configure(row=0, column=0, rowspan=2, sticky="nsew", padx=10, pady=5)
        self.sidebar_btn.configure(width=FULLSCREEN_PANEL_W, text="◀")
        self.wx_frame.configure(width=FULLSCREEN_PANEL_W)
        self._set_all_card_fonts(True)
        self._repack_header("vertical")

    def _enter_normal_layout(self):
        """Restore cards to the top header bar; graph sits below."""
        self.header.configure(height=110)
        self.header.grid_configure(row=0, column=0, rowspan=1, sticky="nsew", padx=0, pady=5)
        self.canvas.get_tk_widget().grid_configure(row=1, column=0, rowspan=1, sticky="nsew", padx=10, pady=5)
        self.sidebar_btn.configure(width=44, text="▶")
        self.wx_frame.configure(width=155)
        self._set_all_card_fonts(False)
        self._repack_header("horizontal")

    def _repack_header(self, mode):
        """Unpack all header children and repack in the given direction."""
        for widget in self.header.winfo_children():
            widget.pack_forget()
        if mode == "vertical":
            self.sidebar_btn.pack(side="top", padx=4, pady=(5, 2), fill="x")
            self.wx_compact_lbl.pack(side="top", padx=8, pady=(0, 6))
            for nid in sorted(self.active_nodes):
                if nid in self.node_widgets:
                    self.node_widgets[nid]['frame'].pack(side="top", padx=4, pady=2, fill="x")
        else:
            # Toggle packs first → rightmost; weather card is second from right
            self.sidebar_btn.pack(side="right", padx=(0, 4), pady=5)
            self.wx_frame.pack(side="right", padx=5, pady=5, fill="y")
            for nid in sorted(self.active_nodes):
                if nid in self.node_widgets:
                    self.node_widgets[nid]['frame'].pack(side="left", padx=5, pady=5, fill="both", expand=True)

    # --------------------------------------------------------------------------
    # WEATHER FORECAST CARD
    # --------------------------------------------------------------------------

    def create_weather_card(self):
        """Fixed forecast card packed to the right end of the header."""
        frame = ctk.CTkFrame(self.header, width=155, fg_color="#2a2a2a", corner_radius=10)
        frame.pack(side="right", padx=5, pady=5, fill="y")
        self.wx_frame = frame
        ctk.CTkLabel(frame, text="TOMORROW", font=("Arial", 10, "bold"),
                     text_color="#888888").pack(pady=(4, 0))
        self.wx_city_lbl = ctk.CTkLabel(frame, text="--", font=("Arial", 10, "bold"), text_color="white")
        self.wx_city_lbl.pack()
        self.wx_temp_lbl = ctk.CTkLabel(frame, text="--/--°C", font=("Arial", 18, "bold"), text_color="white")
        self.wx_temp_lbl.pack()
        self.wx_desc_lbl = ctk.CTkLabel(frame, text="--", font=("Arial", 9), text_color="#aaaaaa")
        self.wx_desc_lbl.pack()
        self.wx_wind_lbl = ctk.CTkLabel(frame, text="Wind max: --", font=("Arial", 9), text_color="#aaaaaa")
        self.wx_wind_lbl.pack()
        self.wx_lightning_lbl = ctk.CTkLabel(frame, text="", font=("Arial", 9), text_color="#555555")
        self.wx_lightning_lbl.pack()
        self.wx_time_lbl = ctk.CTkLabel(frame, text="--", font=("Arial", 8), text_color="#555555")
        self.wx_time_lbl.pack(pady=(0, 4))

        # Compact single-line strip shown in fullscreen mode instead of the full card
        self.wx_compact_lbl = ctk.CTkLabel(
            self.header, text="-- / --°C  ·  --",
            font=("Arial", 12), text_color="#aaaaaa"
        )
        # Not packed here — _repack_header handles it based on mode

    @staticmethod
    def _cape_to_risk(cape):
        if cape >= 1500: return ('⚡⚡⚡ High',     '#ff2222')
        if cape >= 500:  return ('⚡⚡ Moderate', '#ff6600')
        if cape >= 100:  return ('⚡ Low',       '#ffaa00')
        return None

    @staticmethod
    def _aqi_label(aqi):
        return {
            0: ('AQI: —',          '#555555'),
            1: ('AQI 1 Excellent',  '#00c04b'),
            2: ('AQI 2 Good',       '#88c040'),
            3: ('AQI 3 Moderate',   '#ffaa00'),
            4: ('AQI 4 Poor',       '#ff6600'),
            5: ('AQI 5 Unhealthy',  '#ff2222'),
        }.get(aqi, ('AQI ?', '#888888'))

    def _update_weather_card(self):
        if not self.weather_data:
            return
        d    = self.weather_data
        desc = WMO_DESCRIPTIONS.get(d.get('code', -1), "Unknown")
        t_min = d.get('temp_min', 0)
        t_max = d.get('temp_max', 0)
        self.wx_city_lbl.configure(text=d.get('city', '--'))
        self.wx_temp_lbl.configure(text=f"{t_min:.0f}° / {t_max:.0f}°C")
        self.wx_desc_lbl.configure(text=desc)
        self.wx_wind_lbl.configure(text=f"Wind max: {d.get('wind', 0):.0f} m/s")
        risk = self._cape_to_risk(d.get('max_cape', 0))
        if risk:
            self.wx_lightning_lbl.configure(text=risk[0], text_color=risk[1])
        else:
            self.wx_lightning_lbl.configure(text="", text_color="#555555")
        self.wx_time_lbl.configure(text=f"Updated {d.get('updated', '--')}")
        self.wx_compact_lbl.configure(text=f"{t_min:.0f}° / {t_max:.0f}°C  ·  {desc}")

    def _do_weather_fetch(self):
        """Runs in a background thread. Writes to self.weather_data then schedules UI update."""
        try:
            geo_url = ("https://geocoding-api.open-meteo.com/v1/search"
                       f"?name={urllib.parse.quote(self.weather_location)}"
                       "&count=1&language=en&format=json")
            with urllib.request.urlopen(geo_url, timeout=10) as resp:
                geo = json.loads(resp.read())

            results = geo.get('results')
            if not results:
                print(f"Weather: location '{self.weather_location}' not found")
                return

            lat  = results[0]['latitude']
            lon  = results[0]['longitude']
            city = results[0]['name']

            wx_url = (f"https://api.open-meteo.com/v1/forecast"
                      f"?latitude={lat}&longitude={lon}"
                      f"&daily=temperature_2m_max,temperature_2m_min,weather_code,wind_speed_10m_max"
                      f"&hourly=cape&timezone=auto&forecast_days=2")
            with urllib.request.urlopen(wx_url, timeout=10) as resp:
                wx = json.loads(resp.read())

            daily = wx['daily']
            now_iso = datetime.now().strftime('%Y-%m-%dT%H')
            hourly_times = wx.get('hourly', {}).get('time', [])
            h_idx = next((i for i, t in enumerate(hourly_times) if t.startswith(now_iso)), 0)
            cape_values = [v for v in wx.get('hourly', {}).get('cape', [])[h_idx:h_idx + 6] if v is not None]
            max_cape = max(cape_values) if cape_values else 0

            self.weather_data = {
                'city':     city,
                'temp_max': daily['temperature_2m_max'][1],
                'temp_min': daily['temperature_2m_min'][1],
                'code':     daily['weather_code'][1],
                'wind':     daily['wind_speed_10m_max'][1],
                'max_cape': max_cape,
                'updated':  datetime.now().strftime('%H:%M'),
            }
            self.after(0, self._update_weather_card)
        except Exception as e:
            print(f"Weather fetch error: {e}")

    def weather_loop(self):
        """Fetches weather on startup and every 6 hours thereafter."""
        threading.Thread(target=self._do_weather_fetch, daemon=True).start()
        self.after(WEATHER_INTERVAL_MS, self.weather_loop)

    def _on_location_update(self):
        loc = self.loc_entry.get().strip()
        if loc:
            self.weather_location = loc
            threading.Thread(target=self._do_weather_fetch, daemon=True).start()

    # --------------------------------------------------------------------------
    # SENSOR CARDS
    # --------------------------------------------------------------------------

    def create_sensor_card(self, nid, has_pressure, has_aq=False):
        """Creates a clickable header tile. has_aq=True for Sensor HW 2.0 nodes."""
        frame = ctk.CTkFrame(self.header, fg_color="#252525", corner_radius=10)
        if not self.sidebar_visible:
            frame.pack(side="top", padx=4, pady=2, fill="x")
        else:
            frame.pack(side="left", padx=5, pady=5, fill="both", expand=True)
        frame.configure(cursor="hand2")

        fonts    = CARD_FONTS_FULL if not self.sidebar_visible else CARD_FONTS_NORMAL
        color    = SENSOR_COLORS.get(nid, "white")
        name     = self.sensor_names.get(nid, f"Node {nid}")
        name_lbl = ctk.CTkLabel(frame, text=name, text_color=color, font=fonts['name'])
        name_lbl.pack(pady=(2, 0))
        val_lbl  = ctk.CTkLabel(frame, text="--.-°C", font=fonts['val'], text_color="white")
        val_lbl.pack()
        batt_lbl = ctk.CTkLabel(frame, text="Batt: -.--V", font=fonts['batt'], text_color="white")
        batt_lbl.pack()

        press_lbl, trend_lbl = None, None
        if has_pressure:
            press_lbl = ctk.CTkLabel(frame, text="---- hPa ─", font=fonts['press'], text_color="white")
            press_lbl.pack()
            trend_lbl = ctk.CTkLabel(frame, text="Stable", font=fonts['trend'], text_color="#aaaaaa")
            trend_lbl.pack()

        aqi_lbl, eco2_lbl = None, None
        if has_aq:
            aqi_lbl  = ctk.CTkLabel(frame, text="AQI: —", font=fonts['aq'], text_color="#555555")
            aqi_lbl.pack()
            eco2_lbl = ctk.CTkLabel(frame, text="eCO2: —", font=fonts['aq'], text_color="#777777")
            eco2_lbl.pack()

        tx_lbl = ctk.CTkLabel(frame, text="TX: --", font=fonts['tx'], text_color="#888888")
        tx_lbl.pack(pady=(0, 2))

        clickable = [frame, name_lbl, val_lbl, batt_lbl, tx_lbl]
        if press_lbl: clickable.append(press_lbl)
        if trend_lbl: clickable.append(trend_lbl)
        if aqi_lbl:   clickable.append(aqi_lbl)
        if eco2_lbl:  clickable.append(eco2_lbl)
        for widget in clickable:
            widget.bind("<Button-1>", lambda _, n=nid: self.toggle_node_graph(n))

        self.node_widgets[nid] = {
            'val': val_lbl, 'batt': batt_lbl, 'tx': tx_lbl,
            'frame': frame, 'name_lbl': name_lbl,
            'press_lbl': press_lbl, 'trend_lbl': trend_lbl,
            'aqi_lbl': aqi_lbl, 'eco2_lbl': eco2_lbl,
            'has_pressure': has_pressure, 'has_aq': has_aq,
        }
        self.create_dynamic_node_slider(nid)

    def toggle_node_graph(self, nid):
        if nid in self.hidden_nodes:
            self.hidden_nodes.discard(nid)
            self._set_card_dimmed(nid, False)
        else:
            self.hidden_nodes.add(nid)
            self._set_card_dimmed(nid, True)
        self.redraw_graph()

    def _set_card_dimmed(self, nid, dimmed):
        w          = self.node_widgets[nid]
        name_color = "#555555" if dimmed else SENSOR_COLORS.get(nid, "white")
        val_color  = "#666666" if dimmed else "white"
        bg_color   = "#1c1c1c" if dimmed else "#252525"
        w['name_lbl'].configure(text_color=name_color)
        w['val'].configure(text_color=val_color)
        w['frame'].configure(fg_color=bg_color)
        if w.get('press_lbl'):
            w['press_lbl'].configure(text_color="#555555" if dimmed else "white")
        if w.get('trend_lbl'):
            w['trend_lbl'].configure(text_color="#555555" if dimmed else "#aaaaaa")
        if w.get('aqi_lbl'):
            w['aqi_lbl'].configure(text_color="#555555")
        if w.get('eco2_lbl'):
            w['eco2_lbl'].configure(text_color="#555555" if dimmed else "#777777")

    def _set_all_card_fonts(self, fullscreen):
        fonts = CARD_FONTS_FULL if fullscreen else CARD_FONTS_NORMAL
        for w in self.node_widgets.values():
            w['name_lbl'].configure(font=fonts['name'])
            w['val'].configure(font=fonts['val'])
            w['batt'].configure(font=fonts['batt'])
            w['tx'].configure(font=fonts['tx'])
            if w.get('press_lbl'):
                w['press_lbl'].configure(font=fonts['press'])
            if w.get('trend_lbl'):
                w['trend_lbl'].configure(font=fonts['trend'])
            if w.get('aqi_lbl'):
                w['aqi_lbl'].configure(font=fonts['aq'])
            if w.get('eco2_lbl'):
                w['eco2_lbl'].configure(font=fonts['aq'])

    # --------------------------------------------------------------------------
    # PER-NODE SLEEP SLIDERS + RENAME
    # --------------------------------------------------------------------------

    def create_dynamic_node_slider(self, nid):
        container = ctk.CTkFrame(self.sidebar, fg_color="#252525", corner_radius=8)
        container.pack(fill="x", pady=5, padx=5)

        info_lbl = ctk.CTkLabel(
            container,
            text=f"{self.sensor_names.get(nid, f'Node {nid}')}: Fastest",
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
                text=f"{self.sensor_names.get(n, f'Node {n}')}: {_fmt_sleep(SLEEP_STEPS_MIN[round(float(v))])}"
            )
        )
        slider.set(0)
        slider.pack(pady=5, padx=10)

        self.sleep_sliders[nid] = slider
        self.node_widgets[nid]['sidebar_lbl'] = info_lbl

    def save_sleep_config(self):
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
        self.node_widgets[nid]['sidebar_lbl'].configure(text=f"{name}: {_fmt_sleep(minutes)}")

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

    def _get_timeout_sec(self):
        """Timeout = longest active sleep setting × 3, minimum 60 s."""
        if not self.sleep_sliders:
            return 180
        max_min = max(SLEEP_STEPS_MIN[round(float(self.sleep_sliders[nid].get()))]
                      for nid in self.sleep_sliders)
        return max(60, max_min * 3 * 60)

    # --------------------------------------------------------------------------
    # DATA LAYER
    # --------------------------------------------------------------------------

    def _init_node(self, nid, now_unix, initial_temp, initial_press, has_aq=False):
        has_pressure = abs(initial_press - PRESSURE_SENTINEL) > 1.0
        raw_maxlen   = WINDOW_CONFIG['15min']['maxlen']
        self.data_store[nid] = {
            'raw': {
                'temp':  deque(maxlen=raw_maxlen),
                'hum':   deque(maxlen=raw_maxlen),
                'press': deque(maxlen=raw_maxlen),
                't':     deque(maxlen=raw_maxlen),
            },
            'buckets': {
                key: {
                    'temp':      deque(maxlen=cfg['maxlen']),
                    'hum':       deque(maxlen=cfg['maxlen']),
                    'press':     deque(maxlen=cfg['maxlen']),
                    't':         deque(maxlen=cfg['maxlen']),
                    'acc_temp':  [],
                    'acc_hum':   [],
                    'acc_press': [],
                    'acc_start': None,
                }
                for key, cfg in WINDOW_CONFIG.items() if cfg['bucket_sec'] is not None
            }
        }
        self.create_sensor_card(nid, has_pressure, has_aq=has_aq)
        self.temp_lines[nid], = self.ax_t.plot([], [], color=SENSOR_COLORS.get(nid),
                                                label=self.sensor_names.get(nid, f"Node {nid}"),
                                                linewidth=1.5)
        self.hum_lines[nid],  = self.ax_h.plot([], [], color=SENSOR_COLORS.get(nid), linewidth=1.5)
        self.last_packet_time[nid] = now_unix
        self.previous_values[nid]  = initial_temp

    def _update_buckets(self, nid, temp, hum, press, now_unix):
        for key, cfg in WINDOW_CONFIG.items():
            if cfg['bucket_sec'] is None:
                continue
            b = self.data_store[nid]['buckets'][key]
            if b['acc_start'] is None:
                b['acc_start'] = now_unix
            b['acc_temp'].append(temp)
            b['acc_hum'].append(hum)
            b['acc_press'].append(press)
            if now_unix - b['acc_start'] >= cfg['bucket_sec']:
                b['temp'].append(sum(b['acc_temp'])  / len(b['acc_temp']))
                b['hum'].append(sum(b['acc_hum'])    / len(b['acc_hum']))
                b['press'].append(sum(b['acc_press']) / len(b['acc_press']))
                b['t'].append(datetime.fromtimestamp(b['acc_start'] + cfg['bucket_sec'] / 2))
                b['acc_temp']  = []
                b['acc_hum']   = []
                b['acc_press'] = []
                b['acc_start'] = now_unix

    def _get_plot_data(self, nid):
        if self.selected_window == '15min':
            s = self.data_store[nid]['raw']
            return list(s['t']), list(s['temp']), list(s['hum'])
        b = self.data_store[nid]['buckets'][self.selected_window]
        return list(b['t']), list(b['temp']), list(b['hum'])

    def _calc_pressure_trend(self, nid):
        """Returns ▲, ▼, or ─ based on pressure change rate normalised to hPa/3h."""
        if self.selected_window == '15min':
            press_list = list(self.data_store[nid]['raw']['press'])
            t_list     = list(self.data_store[nid]['raw']['t'])
        else:
            b          = self.data_store[nid]['buckets'][self.selected_window]
            press_list = list(b['press'])
            t_list     = list(b['t'])

        valid = [(t, p) for t, p in zip(t_list, press_list) if abs(p - PRESSURE_SENTINEL) > 1.0]
        if len(valid) < 2:
            return '─'

        t_first, p_first = valid[0]
        t_last,  p_last  = valid[-1]
        elapsed_h = (t_last - t_first).total_seconds() / 3600
        if elapsed_h < 0.05:
            return '─'

        change_per_3h = (p_last - p_first) / elapsed_h * 3
        if change_per_3h > TREND_RISE_THRESHOLD:
            return '▲'
        elif change_per_3h < TREND_FALL_THRESHOLD:
            return '▼'
        return '─'

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
                timeout_sec = self._get_timeout_sec()

                for entry in data:
                    nid = entry.get('id')
                    if nid is None:
                        continue
                    current_temp  = entry.get('temp')
                    current_hum   = entry.get('hum')
                    current_press = entry.get('press', PRESSURE_SENTINEL)

                    if nid not in self.data_store:
                        self._init_node(nid, now_unix, current_temp, current_press,
                                        has_aq='eco2' in entry)

                    if current_temp != self.previous_values.get(nid):
                        self.last_packet_time[nid] = now_unix
                        self.previous_values[nid]  = current_temp

                    self.data_store[nid]['raw']['temp'].append(current_temp)
                    self.data_store[nid]['raw']['hum'].append(current_hum)
                    self.data_store[nid]['raw']['press'].append(current_press)
                    self.data_store[nid]['raw']['t'].append(now_dt)
                    self._update_buckets(nid, current_temp, current_hum, current_press, now_unix)

                    self.node_widgets[nid]['val'].configure(text=f"{current_temp:.1f}°C")
                    bv = entry.get('batt', 0)
                    if bv <= 0:
                        self.node_widgets[nid]['batt'].configure(text="USB Pwr", text_color="white")
                    else:
                        self.node_widgets[nid]['batt'].configure(
                            text=f"Batt: {bv:.2f}V",
                            text_color="#ff4444" if bv < BATT_CRITICAL else "white"
                        )

                    if self.node_widgets[nid].get('has_aq') and 'eco2' in entry:
                        aqi_text, aqi_color = self._aqi_label(entry.get('aqi', 0))
                        self.node_widgets[nid]['aqi_lbl'].configure(
                            text=aqi_text, text_color=aqi_color
                        )
                        self.node_widgets[nid]['eco2_lbl'].configure(
                            text=f"eCO2:{entry.get('eco2',0)} TVOC:{entry.get('tvoc',0)}ppb"
                        )

                    if self.node_widgets[nid].get('has_pressure'):
                        valid_press = abs(current_press - PRESSURE_SENTINEL) > 1.0
                        if valid_press:
                            trend       = self._calc_pressure_trend(nid)
                            trend_label = TREND_LABELS.get(trend, 'Stable')
                            self.node_widgets[nid]['press_lbl'].configure(
                                text=f"{current_press:.0f} hPa {trend}"
                            )
                            self.node_widgets[nid]['trend_lbl'].configure(
                                text=trend_label,
                                text_color=TREND_COLORS.get(trend, '#aaaaaa')
                            )
                        else:
                            self.node_widgets[nid]['press_lbl'].configure(text="---- hPa ─")
                            self.node_widgets[nid]['trend_lbl'].configure(text="--", text_color="#aaaaaa")

                for nid in list(self.data_store.keys()):
                    delta = int(now_unix - self.last_packet_time.get(nid, now_unix))
                    self.node_widgets[nid]['tx'].configure(text=f"TX: {delta}s ago")
                    if delta > timeout_sec:
                        if nid in self.active_nodes:
                            self.active_nodes.discard(nid)
                            self.node_widgets[nid]['frame'].pack_forget()
                    elif nid not in self.active_nodes:
                        self.active_nodes.add(nid)
                        if not self.sidebar_visible:
                            self._repack_header("vertical")
                        else:
                            self.node_widgets[nid]['frame'].pack(side="left", padx=5, pady=5, fill="both", expand=True)

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
            self.temp_lines[nid].set_data(t_seg, temp_seg)
            # Filter out -99.9 humidity sentinel (sensor error)
            valid_hum = [(t, h) for t, h in zip(t_seg, hum_seg) if h >= 0]
            hum_t = [p[0] for p in valid_hum]
            hum_h = [p[1] for p in valid_hum]
            visible_hums.extend(hum_h)
            self.hum_lines[nid].set_data(hum_t, hum_h)

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
