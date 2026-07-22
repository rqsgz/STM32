"""
仪表盘式传感器可视化
- 温度表盘 (matplotlib polar gauge)
- 光照进度条 (tkinter Canvas)
- 姿态3D视图 (matplotlib 3D)
- 距离刻度尺 (tkinter Canvas)
- CSV 数据保存
数据源: COM12 串口
"""
import re
import time
import csv
import threading
import queue
from datetime import datetime
from collections import deque
import serial
import numpy as np
import tkinter as tk
from tkinter import ttk
import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import matplotlib.pyplot as plt

# ====== 配置 ======
COM_PORT = "COM12"
BAUD = 115200
INTERVAL = 200  # ms

# ====== 数据解析 ======
PAT = re.compile(
    r"\[(\d+)\]\s+"
    r"SR:(-?\d+)cm\s+"
    r"L:(\d+)\.(\d+)lux\s+"
    r"T:(-?\d+)\.(\d+)C\s+"
    r"ADC:(\d+)mV"
)

data_q = queue.Queue()
cur = {"tick": 0, "sr_cm": -1, "light_lux": 0.0, "temp_c": 0.0, "adc_mv": 0,
       "mpu_ax": 0, "mpu_ay": 0, "mpu_az": 0}
log_enabled = False
log_count = 0
log_file = None
log_writer = None

def serial_thread():
    ser = serial.Serial(COM_PORT, BAUD, timeout=1)
    ser.dtr = True
    while True:
        try:
            line = ser.readline().decode("utf-8", errors="replace")
            if line.strip():
                data_q.put(line)
        except Exception:
            time.sleep(0.1)

def parse_line(line):
    m = PAT.match(line.strip())
    if not m:
        return None
    # 尝试解析可选 MPU 字段
    mpu_match = re.search(r"MPU:(-?\d+)/(-?\d+)/(-?\d+)", line)
    return {
        "tick": int(m.group(1)), "sr_cm": int(m.group(2)),
        "light_lux": float(m.group(3)) + float(m.group(4)) / 10.0,
        "temp_c": float(m.group(5)) + float(m.group(6)) / 10.0,
        "adc_mv": int(m.group(7)),
        "mpu_ax": int(mpu_match.group(1)) if mpu_match else 0,
        "mpu_ay": int(mpu_match.group(2)) if mpu_match else 0,
        "mpu_az": int(mpu_match.group(3)) if mpu_match else 0,
    }

# ====== 温度表盘 ======
class TempGauge:
    def __init__(self, parent):
        self.fig, self.ax = plt.subplots(figsize=(2.8, 2.4), subplot_kw={"projection": "polar"})
        self.fig.patch.set_facecolor("#1a1a2e")
        self.ax.set_facecolor("#16213e")
        self.ax.set_theta_offset(np.pi)       # 半圆: 180° 开始
        self.ax.set_theta_direction(-1)       # 顺时针
        self.ax.set_thetamin(0)
        self.ax.set_thetamax(180)
        self.ax.set_yticks([])
        self.ax.set_xticks(np.linspace(0, np.pi, 7))
        self.ax.set_xticklabels(["-10", "0", "10", "20", "30", "40", "50"], color="#a0a0a0", fontsize=8)
        self.ax.set_rlim(0, 1)
        for spine in self.ax.spines.values():
            spine.set_visible(False)

        # 颜色弧段
        for angle, color in [(0, "#00e676"), (30, "#76ff03"), (60, "#ffeb3b"),
                               (90, "#ff9800"), (120, "#ff5252")]:
            self.ax.fill_between(np.linspace(np.deg2rad(angle), np.deg2rad(angle + 30), 20), 0.4, 0.8,
                                 color=color, alpha=0.6)
        self.needle, = self.ax.plot([0, 0], [0, 0.75], "white", lw=3)
        self.val_text = self.ax.text(0, -0.5, "--.- C", ha="center", va="center",
                                      color="white", fontsize=16, fontweight="bold",
                                      fontfamily="monospace")
        self.canvas = FigureCanvasTkAgg(self.fig, parent)
        self.canvas.get_tk_widget().pack()

    def update(self, temp):
        angle = np.deg2rad(np.clip((temp + 10) / 60 * 180, 0, 180))
        self.needle.set_data([angle, angle], [0, 0.75])
        self.val_text.set_text(f"{temp:.1f} C")
        self.canvas.draw_idle()

# ====== 光照进度条 ======
class LightBar(tk.Canvas):
    def __init__(self, parent):
        super().__init__(parent, width=280, height=80, bg="#1a1a2e", highlightthickness=0)
        self.create_text(140, 18, text="Light (BH1750)", fill="#a0a0a0", font=("Arial", 10, "bold"))
        self.bar_bg = self.create_rectangle(20, 35, 260, 55, fill="#16213e", outline="#333")
        self.bar_fg = self.create_rectangle(20, 35, 20, 55, fill="#40c4ff", outline="")
        self.val_text = self.create_text(140, 65, text="0.0 lux", fill="white",
                                          font=("monospace", 13, "bold"))

    def update(self, lux):
        pct = min(lux / 1000, 1.0)  # 1000 lux = max
        x = 20 + int(pct * 240)
        self.coords(self.bar_fg, 20, 35, x, 55)
        self.itemconfig(self.val_text, text=f"{lux:.1f} lux")

# ====== 距离刻度尺 ======
class DistanceRuler(tk.Canvas):
    def __init__(self, parent):
        super().__init__(parent, width=280, height=80, bg="#1a1a2e", highlightthickness=0)
        self.create_text(140, 18, text="Distance (HC-SR04)", fill="#a0a0a0", font=("Arial", 10, "bold"))
        # 刻度线
        for d in range(0, 401, 50):
            x = 30 + int(d / 400 * 220)
            self.create_line(x, 35, x, 42, fill="#555")
            self.create_text(x, 50, text=str(d), fill="#777", font=("Arial", 7))
        self.cursor = self.create_polygon(30, 32, 24, 42, 36, 42, fill="#ff5252", outline="")
        self.val_text = self.create_text(140, 65, text="-1 cm", fill="white",
                                          font=("monospace", 13, "bold"))

    def update(self, cm):
        if cm < 0: cm = 0
        if cm > 400: cm = 400
        x = 30 + int(cm / 400 * 220)
        self.coords(self.cursor, x, 32, x - 6, 42, x + 6, 42)
        self.itemconfig(self.val_text, text=f"{cm} cm")

# ====== 姿态3D ======
class Attitude3D:
    def __init__(self, parent):
        self.fig = plt.figure(figsize=(2.8, 2.4), facecolor="#1a1a2e")
        self.ax = self.fig.add_subplot(111, projection="3d", facecolor="#16213e")
        self.ax.set_xlim(-1.5, 1.5); self.ax.set_ylim(-1.5, 1.5); self.ax.set_zlim(-1.5, 1.5)
        self.ax.set_xticklabels([]); self.ax.set_yticklabels([]); self.ax.set_zticklabels([])
        self.ax.grid(True, alpha=0.15)
        for spine in self.ax.spines.values():
            try: spine.set_color("#333")
            except: pass
        # 球
        u, v = np.mgrid[0:2 * np.pi:20j, 0:np.pi:10j]
        x = np.cos(u) * np.sin(v); y = np.sin(u) * np.sin(v); z = np.cos(v)
        self.ax.plot_surface(x, y, z, color="#333366", alpha=0.3, linewidth=0)
        self.arrow = None
        self.na_text = self.ax.text2D(0.5, 0.5, "MPU\nN/A", transform=self.ax.transAxes,
                                       ha="center", va="center", color="#555", fontsize=14,
                                       fontweight="bold")
        self.canvas = FigureCanvasTkAgg(self.fig, parent)
        self.canvas.get_tk_widget().pack()

    def update(self, ax_v, ay_v, az_v):
        if self.arrow:
            self.arrow.remove()
            self.arrow = None
        mag = np.sqrt(ax_v * ax_v + ay_v * ay_v + az_v * az_v)
        if mag > 0.01:
            self.na_text.set_text("")
            ax_n, ay_n, az_n = ax_v / mag, ay_v / mag, az_v / mag
            self.arrow = self.ax.quiver(0, 0, 0, ax_n, ay_n, az_n,
                                         color="#ff5252", lw=3, arrow_length_ratio=0.3)
        else:
            self.na_text.set_text("MPU\nN/A")
        self.canvas.draw_idle()

# ====== 主窗口 ======
class Dashboard(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("STM32 Sensor Dashboard")
        self.configure(bg="#0d0d1a")
        self.geometry("820x520")
        self.resizable(False, False)

        # 顶部标题
        self.title_lbl = tk.Label(self, text="STM32 Sensor Dashboard", bg="#0d0d1a",
                                   fg="#e0e0e0", font=("Arial", 16, "bold"))
        self.title_lbl.pack(pady=4)

        # 上排: 温度表盘 | 姿态3D
        top_frame = tk.Frame(self, bg="#0d0d1a")
        top_frame.pack()
        left_frame = tk.Frame(top_frame, bg="#1a1a2e", padx=5, pady=5)
        left_frame.pack(side=tk.LEFT, padx=4)
        tk.Label(left_frame, text="Temperature", bg="#1a1a2e", fg="#a0a0a0",
                 font=("Arial", 9, "bold")).pack()
        self.temp_gauge = TempGauge(left_frame)

        right_frame = tk.Frame(top_frame, bg="#1a1a2e", padx=5, pady=5)
        right_frame.pack(side=tk.LEFT, padx=4)
        tk.Label(right_frame, text="Attitude (MPU6050)", bg="#1a1a2e", fg="#a0a0a0",
                 font=("Arial", 9, "bold")).pack()
        self.attitude = Attitude3D(right_frame)

        # 下排: 光照 | 距离
        bot_frame = tk.Frame(self, bg="#0d0d1a")
        bot_frame.pack(pady=4)
        self.light_bar = LightBar(bot_frame)
        self.light_bar.pack(side=tk.LEFT, padx=4)
        self.dist_ruler = DistanceRuler(bot_frame)
        self.dist_ruler.pack(side=tk.LEFT, padx=4)

        # 底部状态栏
        status_frame = tk.Frame(self, bg="#0d0d1a")
        status_frame.pack(fill=tk.X, padx=10, pady=4)
        self.adc_lbl = tk.Label(status_frame, text="ADC: ---- mV", bg="#0d0d1a",
                                 fg="#ff5252", font=("monospace", 11, "bold"))
        self.adc_lbl.pack(side=tk.LEFT, padx=10)

        self.log_btn = tk.Button(status_frame, text="开始记录 CSV", command=self.toggle_log,
                                  bg="#16213e", fg="#e0e0e0", font=("Arial", 10),
                                  activebackground="#333", activeforeground="white")
        self.log_btn.pack(side=tk.RIGHT, padx=10)
        self.log_info = tk.Label(status_frame, text="已保存: 0 条", bg="#0d0d1a",
                                  fg="#777", font=("Arial", 9))
        self.log_info.pack(side=tk.RIGHT, padx=10)

        self.tick_lbl = tk.Label(status_frame, text="Tick: 0", bg="#0d0d1a",
                                  fg="#777", font=("Arial", 9))
        self.tick_lbl.pack(side=tk.RIGHT, padx=10)

        self.after(INTERVAL, self.poll_data)

    def toggle_log(self):
        global log_enabled, log_file, log_writer, log_count
        log_enabled = not log_enabled
        if log_enabled:
            fname = f"sensor_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
            log_file = open(fname, "w", newline="", encoding="utf-8")
            log_writer = csv.writer(log_file)
            log_writer.writerow(["timestamp", "tick", "sr_cm", "light_lux",
                                  "temp_c", "adc_mv", "mpu_ax", "mpu_ay", "mpu_az"])
            log_count = 0
            self.log_btn.config(text="停止记录 CSV", bg="#ff5252")
        else:
            if log_file:
                log_file.close()
            self.log_btn.config(text="开始记录 CSV", bg="#16213e")

    def poll_data(self):
        global log_count
        while not data_q.empty():
            line = data_q.get_nowait()
            p = parse_line(line)
            if p is None:
                continue
            for k, v in p.items():
                cur[k] = v

            self.temp_gauge.update(cur["temp_c"])
            self.light_bar.update(cur["light_lux"])
            self.dist_ruler.update(cur["sr_cm"])
            self.attitude.update(cur["mpu_ax"], cur["mpu_ay"], cur["mpu_az"])
            self.adc_lbl.config(text=f"ADC: {cur['adc_mv']} mV")
            self.tick_lbl.config(text=f"Tick: {cur['tick']}")

            if log_enabled and log_writer:
                log_writer.writerow([datetime.now().isoformat(), cur["tick"],
                    cur["sr_cm"], cur["light_lux"], cur["temp_c"], cur["adc_mv"],
                    cur["mpu_ax"], cur["mpu_ay"], cur["mpu_az"]])
                log_count += 1
                self.log_info.config(text=f"已保存: {log_count} 条")

        self.after(INTERVAL, self.poll_data)

if __name__ == "__main__":
    t = threading.Thread(target=serial_thread, daemon=True)
    t.start()
    print(f"Serial {COM_PORT} @ {BAUD}")
    app = Dashboard()
    app.mainloop()
