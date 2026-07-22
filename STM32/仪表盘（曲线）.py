"""
实时传感器仪表盘 — matplotlib.animation 动态刷新
左侧: 4个子图 (温度/距离/光照/MPU三轴)
右侧: 数值面板
数据源: 串口 COM12
"""
import re
import time
import threading
import queue
from collections import deque
import serial
import numpy as np
import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.gridspec import GridSpec

# ====== 配置 ======
COM_PORT = "COM12"
BAUD = 115200
HISTORY = 100
INTERVAL = 200

# ====== 数据缓冲 ======
data_q = queue.Queue()
ticks   = deque(maxlen=HISTORY)
temps   = deque(maxlen=HISTORY)
dists   = deque(maxlen=HISTORY)
lights  = deque(maxlen=HISTORY)
angles  = deque(maxlen=HISTORY)
mpu_xs  = deque(maxlen=HISTORY)
mpu_ys  = deque(maxlen=HISTORY)
mpu_zs  = deque(maxlen=HISTORY)

cur = {"sr_cm": -1, "light_lux": 0.0, "temp_c": 0.0, "sv_angle": 30,
       "mpu_x": 0, "mpu_y": 0, "mpu_z": 0, "alarm": "N", "tick": 0}

PAT = re.compile(
    r"\[(\d+)\]\s+"
    r"SR:(-?\d+)cm\s+"
    r"L:(\d+)\.(\d+)lux\s+"
    r"T:(-?\d+)\.(\d+)C\s+"
    r"MPU:(-?\d+)/(-?\d+)/(-?\d+)\s+"
    r"ALM:(\w)\s+"
    r"SV:(\d+)"
)

# ====== 串口读取线程 ======
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
    s = line.strip()
    m = PAT.match(s)
    if not m:
        if s:
            print(f"[NO MATCH] {s}")
        return None
    return {
        "tick": int(m.group(1)), "sr_cm": int(m.group(2)),
        "light_lux": float(m.group(3)) + float(m.group(4)) / 10.0,
        "temp_c": float(m.group(5)) + float(m.group(6)) / 10.0,
        "mpu_x": int(m.group(7)), "mpu_y": int(m.group(8)), "mpu_z": int(m.group(9)),
        "alarm": m.group(10),
        "sv_angle": int(m.group(11)),
    }

# ====== 布局 ======
fig = plt.figure("STM32 Sensor Dashboard", figsize=(14, 8))
fig.patch.set_facecolor("#1a1a2e")
gs = GridSpec(2, 3, figure=fig, width_ratios=[1, 1, 0.5],
              left=0.05, right=0.95, top=0.93, bottom=0.06,
              hspace=0.40, wspace=0.32)

ax_t = fig.add_subplot(gs[0, 0])   # 温度
ax_d = fig.add_subplot(gs[0, 1])   # 距离
ax_l = fig.add_subplot(gs[1, 0])   # 光照
ax_m = fig.add_subplot(gs[1, 1])   # MPU 三轴
ax_v = fig.add_subplot(gs[:, 2])   # 数值面板

for ax in [ax_t, ax_d, ax_l, ax_m]:
    ax.set_facecolor("#16213e")
    ax.tick_params(colors="#a0a0a0", labelsize=8)
    for s in ax.spines.values():
        s.set_color("#333")
    ax.grid(True, alpha=0.2, color="#a0a0a0")

ax_v.set_facecolor("#16213e")
ax_v.set_xticks([]); ax_v.set_yticks([])
for s in ax_v.spines.values():
    s.set_visible(False)

lt,  = ax_t.plot([], [], "#00e676", lw=1.5)
ld,  = ax_d.plot([], [], "#ff9100", lw=1.5)
ll,  = ax_l.plot([], [], "#40c4ff", lw=1.5)
lmx, = ax_m.plot([], [], "#ff5252", lw=1.2, label="AX")
lmy, = ax_m.plot([], [], "#00e676", lw=1.2, label="AY")
lmz, = ax_m.plot([], [], "#40c4ff", lw=1.2, label="AZ")
ax_m.legend(loc="upper right", fontsize=7, labelcolor="#a0a0a0",
            facecolor="#16213e", edgecolor="#333")

for ax, title, yl in [
    (ax_t, "Temperature (DS18B20)", "C"),
    (ax_d, "Distance (HC-SR04)", "cm"),
    (ax_l, "Light (BH1750)", "lux"),
    (ax_m, "MPU6050 Accel", "raw"),
]:
    ax.set_title(title, color="#e0e0e0", fontsize=10, fontweight="bold")
    ax.set_ylabel(yl, color="#a0a0a0", fontsize=8)
    ax.set_xlabel("Tick", color="#a0a0a0", fontsize=8)

# 数值面板
val_texts = []
y_pos = [0.94, 0.83, 0.72, 0.61, 0.50, 0.39, 0.28, 0.17, 0.06]
labels = [
    ("Tick", "#e0e0e0", False), ("Temp", "#00e676", True),
    ("Dist", "#ff9100", True), ("Light", "#40c4ff", True),
    ("AX", "#ff5252", False), ("AY", "#00e676", False),
    ("AZ", "#40c4ff", False), ("Servo", "#ff9800", True),
    ("Alarm", "#555", False),
]
for i, (label, color, big) in enumerate(labels):
    fs = 16 if big else 10
    t = ax_v.text(0.5, y_pos[i], f"{label}\n--", transform=ax_v.transAxes,
                  ha="center", va="center", color=color, fontsize=fs,
                  fontfamily="monospace", fontweight="bold" if big else "normal")
    val_texts.append(t)

# ====== 更新函数 ======
def update(_frame):
    while not data_q.empty():
        line = data_q.get_nowait()
        p = parse_line(line)
        if p is None:
            continue
        for k, v in p.items():
            cur[k] = v
        ticks.append(p["tick"])
        temps.append(p["temp_c"])
        dists.append(p["sr_cm"])
        lights.append(p["light_lux"])
        angles.append(p["sv_angle"])
        mpu_xs.append(p["mpu_x"])
        mpu_ys.append(p["mpu_y"])
        mpu_zs.append(p["mpu_z"])

    if not ticks:
        return [lt, ld, ll, lmx, lmy, lmz] + val_texts

    tx = list(ticks)
    lt.set_data(tx, list(temps))
    ld.set_data(tx, list(dists))
    ll.set_data(tx, list(lights))
    lmx.set_data(tx, list(mpu_xs))
    lmy.set_data(tx, list(mpu_ys))
    lmz.set_data(tx, list(mpu_zs))
    for a in [ax_t, ax_d, ax_l, ax_m]:
        a.relim(); a.autoscale_view()

    alarm = cur["alarm"] in ("T", "B")
    vals = [
        f"Tick\n{cur['tick']}",
        f"Temp\n{cur['temp_c']:.1f} C",
        f"Dist\n{cur['sr_cm']} cm",
        f"Light\n{cur['light_lux']:.1f} lux",
        f"AX\n{cur['mpu_x']}",
        f"AY\n{cur['mpu_y']}",
        f"AZ\n{cur['mpu_z']}",
        f"Servo\n{cur['sv_angle']}°",
        f"{'⚠ ALARM' if alarm else 'Normal'}",
    ]
    for t, v in zip(val_texts, vals):
        t.set_text(v)
    val_texts[-1].set_color("#ff5252" if alarm else "#555")

    return [lt, ld, ll, lmx, lmy, lmz] + val_texts

# ====== 启动 ======
def main():
    t = threading.Thread(target=serial_thread, daemon=True)
    t.start()
    print(f"Serial {COM_PORT} @ {BAUD}")

    ani = animation.FuncAnimation(fig, update, interval=INTERVAL,
                                   blit=False, cache_frame_data=False)
    fig.suptitle("STM32 Sensor Dashboard", color="#e0e0e0",
                 fontsize=14, fontweight="bold", y=0.97)
    plt.show()

if __name__ == "__main__":
    main()
