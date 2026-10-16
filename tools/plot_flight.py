"""Plots height, vibration, and GPS from the analyzer's CSV output."""
import os

import matplotlib.pyplot as plt
import pandas as pd

pos = pd.read_csv("output/vehicle_local_position.csv")
imu = pd.read_csv("output/vehicle_imu_status.csv")
gps = pd.read_csv("output/vehicle_gps_position.csv")

fig, axes = plt.subplots(3, 1, figsize=(11, 8), sharex=True)

# Height (z points down, so flip it)
axes[0].plot(pos["time_s"], -pos["z"], color="tab:blue")
axes[0].set_ylabel("Height (m)")
axes[0].set_title("Flight overview")

# Vibration with PX4 Flight Review's zones
top = max(imu["accel_vibration_metric"].max() * 1.1, 12)
axes[1].axhspan(0, 4.905, color="green", alpha=0.15, label="Green")
axes[1].axhspan(4.905, 9.81, color="orange", alpha=0.15, label="Orange")
axes[1].axhspan(9.81, top, color="red", alpha=0.15, label="Red")
axes[1].plot(imu["time_s"], imu["accel_vibration_metric"], color="black", linewidth=0.8)
axes[1].set_ylim(0, top)
axes[1].set_ylabel("Vibration (m/s²)")
axes[1].legend(loc="upper right")

# GPS satellites and accuracy
axes[2].plot(gps["time_s"], gps["satellites_used"], color="tab:green", label="Satellites")
axes[2].set_ylabel("Satellites")
accuracy = axes[2].twinx()
accuracy.plot(gps["time_s"], gps["eph"], color="tab:purple", linewidth=0.8, label="Accuracy (m)")
accuracy.set_ylabel("Horizontal accuracy (m)")
axes[2].set_xlabel("Time since log start (s)")

fig.tight_layout()
os.makedirs("docs", exist_ok=True)
fig.savefig("docs/flight_overview.png", dpi=150)
print("Saved docs/flight_overview.png")