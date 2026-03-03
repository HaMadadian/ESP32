import pygame
import serial
import sys
import time
import math
from collections import deque

# ========================= CONFIGURATION =========================
SERIAL_PORT = "COM9"          # ← CHANGE TO YOUR ACTUAL COM PORT
BAUD_RATE   = 115200

WINDOW_WIDTH  = 1280
WINDOW_HEIGHT = 720

# Graph layout
GRAPH_COLS   = 3
GRAPH_ROWS   = 2
GRAPH_W      = 380
GRAPH_H      = 160
GRAPH_MARGIN = 40

# Fixed scales (matches your MPU6050 settings)
ACCEL_SCALE = 8.0     # ±8 g
GYRO_SCALE  = 500.0   # ±500 °/s

# Matchbox dimensions
MATCHBOX_W = 220
MATCHBOX_H = 90
MATCHBOX_D = 40

# Colors
FACE_COLORS = [
    (255, 60, 60),    # red
    (60, 255, 60),    # green
    (60, 60, 255),    # blue
    (255, 255, 60),   # yellow
    (60, 255, 255),   # cyan
    (255, 60, 255)    # magenta
]

# ========================= INITIALIZATION =========================
pygame.init()
screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
pygame.display.set_caption("ESP32-C3 MPU-9250 Real-Time Visualizer")
clock = pygame.time.Clock()
font_small = pygame.font.SysFont("consolas", 14)
font_large = pygame.font.SysFont("consolas", 22, bold=True)

# Serial
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
    print(f"Connected to {SERIAL_PORT} at {BAUD_RATE} baud")
except Exception as e:
    print(f"Failed to open {SERIAL_PORT}: {e}")
    sys.exit(1)

# Data buffers
history = {k: deque(maxlen=180) for k in ['ax', 'ay', 'az', 'gx', 'gy', 'gz']}

# Gyro bias calibration
gyro_bias = [0.0, 0.0, 0.0]
calibrated = False
calibration_start = time.time()
calibration_samples = []

# Orientation angles
angles = [0.0, 0.0, 0.0]

mode = 0   # 0=graphs, 1=matchbox
running = True

# ========================= CALIBRATION =========================
def calibrate_gyro():
    global gyro_bias, calibrated
    print("Calibration: Keep sensor **completely still** for 3 seconds...")
    pygame.display.set_caption("CALIBRATING – Keep still!")

    calib_start = time.time()
    calib_samples = {'gx': [], 'gy': [], 'gz': []}

    while time.time() - calib_start < 3.0 and running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit()
                sys.exit(0)

        if ser.in_waiting > 0:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line.startswith("MPU,"):
                    parts = line.split(',')
                    if len(parts) == 7:
                        gx = float(parts[4])
                        gy = float(parts[5])
                        gz = float(parts[6])
                        calib_samples['gx'].append(gx)
                        calib_samples['gy'].append(gy)
                        calib_samples['gz'].append(gz)
            except:
                pass

        # Progress bar
        screen.fill((10, 10, 15))
        progress = (time.time() - calib_start) / 3.0
        pygame.draw.rect(screen, (80, 180, 255), (200, 300, 880 * progress, 80))
        pygame.draw.rect(screen, (80, 80, 80), (200, 300, 880, 80), 4)
        text = font_large.render("Calibrating gyro bias... Keep still!", True, (220, 220, 255))
        screen.blit(text, (200, 220))
        pygame.display.flip()
        clock.tick(30)

    if len(calib_samples['gx']) > 20:
        gyro_bias[0] = sum(calib_samples['gx']) / len(calib_samples['gx'])
        gyro_bias[1] = sum(calib_samples['gy']) / len(calib_samples['gy'])
        gyro_bias[2] = sum(calib_samples['gz']) / len(calib_samples['gz'])
        calibrated = True
        print(f"Calibration complete → bias: {gyro_bias}")
    else:
        print("Calibration failed – not enough samples. Restart with sensor still.")

calibrate_gyro()

# ========================= MAIN LOOP =========================
while running:
    dt = clock.tick(60) / 1000.0

    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False
        if event.type == pygame.KEYDOWN:
            if event.key == pygame.K_SPACE:
                mode = 1 - mode

    # Read serial
    while ser.in_waiting > 0:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line.startswith("MPU,"):
                parts = line.split(',')
                if len(parts) == 7:
                    vals = [float(p) for p in parts[1:]]
                    ax, ay, az, gx, gy, gz = vals

                    history['ax'].append(ax)
                    history['ay'].append(ay)
                    history['az'].append(az)
                    history['gx'].append(gx)
                    history['gy'].append(gy)
                    history['gz'].append(gz)

                    if calibrated:
                        gain = 0.92
                        angles[0] += (gx - gyro_bias[0]) * dt * gain
                        angles[1] += (gy - gyro_bias[1]) * dt * gain
                        angles[2] += (gz - gyro_bias[2]) * dt * gain

                        for i in range(3):
                            angles[i] = ((angles[i] + 180) % 360) - 180

        except:
            pass

    screen.fill((10, 10, 15))

    if mode == 0:  # ── GRAPH MODE ──
        pygame.display.set_caption("MPU Visualizer – Graphs")

        sensors = ['ax', 'ay', 'az', 'gx', 'gy', 'gz']
        colors = [(255,80,80), (80,255,80), (80,80,255), (255,255,80), (80,255,255), (255,80,255)]
        labels = ["Accel X (g)", "Accel Y (g)", "Accel Z (g)", "Gyro X (°/s)", "Gyro Y (°/s)", "Gyro Z (°/s)"]
        scales = [ACCEL_SCALE, ACCEL_SCALE, ACCEL_SCALE, GYRO_SCALE, GYRO_SCALE, GYRO_SCALE]

        for i, key in enumerate(sensors):
            row = i // GRAPH_COLS
            col = i % GRAPH_COLS
            x = GRAPH_MARGIN + col * (GRAPH_W + GRAPH_MARGIN)
            y = GRAPH_MARGIN + row * (GRAPH_H + GRAPH_MARGIN)

            # Background & grid
            pygame.draw.rect(screen, (28,28,34), (x, y, GRAPH_W, GRAPH_H))
            for lx in range(0, GRAPH_W+1, 60):
                pygame.draw.line(screen, (40,40,48), (x+lx, y), (x+lx, y+GRAPH_H), 1)
            for ly in range(0, GRAPH_H+1, 40):
                pygame.draw.line(screen, (40,40,48), (x, y+ly), (x+GRAPH_W, y+ly), 1)

            # Zero line – always at true 0
            zero_y = y + GRAPH_H // 2
            pygame.draw.line(screen, (100,100,120), (x, zero_y), (x + GRAPH_W, zero_y), 2)

            # Plot line
            if len(history[key]) > 1:
                points = []
                for j, val in enumerate(history[key]):
                    px = x + j * GRAPH_W / (len(history[key])-1)
                    # Fixed scale – center at 0
                    py = y + GRAPH_H//2 - (val / scales[i]) * (GRAPH_H//2)
                    points.append((px, py))

                pygame.draw.lines(screen, colors[i], False, points, 2)

            # Label + real-time value
            curr = history[key][-1] if history[key] else 0.0
            label_text = f"{labels[i]}   {curr:+.3f}"
            label = font_small.render(label_text, True, (220,220,255))
            screen.blit(label, (x + 8, y - 24))

    else:  # ── MATCHBOX MODE ──
        pygame.display.set_caption("MPU Visualizer – Matchbox")

        cx, cy = WINDOW_WIDTH // 2, WINDOW_HEIGHT // 2

        rx, ry, rz = [math.radians(a) for a in angles]

        vertices = [
            (-MATCHBOX_W/2, -MATCHBOX_H/2, -MATCHBOX_D/2),
            ( MATCHBOX_W/2, -MATCHBOX_H/2, -MATCHBOX_D/2),
            ( MATCHBOX_W/2,  MATCHBOX_H/2, -MATCHBOX_D/2),
            (-MATCHBOX_W/2,  MATCHBOX_H/2, -MATCHBOX_D/2),
            (-MATCHBOX_W/2, -MATCHBOX_H/2,  MATCHBOX_D/2),
            ( MATCHBOX_W/2, -MATCHBOX_H/2,  MATCHBOX_D/2),
            ( MATCHBOX_W/2,  MATCHBOX_H/2,  MATCHBOX_D/2),
            (-MATCHBOX_W/2,  MATCHBOX_H/2,  MATCHBOX_D/2),
        ]

        projected = []
        for x, y, z in vertices:
            x1 = x * math.cos(ry) + z * math.sin(ry)
            z1 = -x * math.sin(ry) + z * math.cos(ry)
            y1 = y * math.cos(rx) - z1 * math.sin(rx)
            z2 = y * math.sin(rx) + z1 * math.cos(rx)
            x2 = x1 * math.cos(rz) - y1 * math.sin(rz)
            y2 = x1 * math.sin(rz) + y1 * math.cos(rz)

            scale = 700 / (z2 + 800)
            px = cx + x2 * scale
            py = cy + y2 * scale
            projected.append((px, py))

        faces = [
            (0,1,2,3), (4,5,6,7),
            (0,1,5,4), (2,3,7,6),
            (0,3,7,4), (1,2,6,5)
        ]

        for i, face in enumerate(faces):
            pts = [projected[j] for j in face]
            pygame.draw.polygon(screen, FACE_COLORS[i], pts)
            pygame.draw.polygon(screen, (240,240,240), pts, 3)

        title = font_large.render("Matchbox follows your sensor movement", True, (220,220,255))
        screen.blit(title, (40, 30))

    # Status bar with clear instructions
    status = f"FPS: {int(clock.get_fps())}   |   PRESS SPACE TO SWITCH BETWEEN GRAPHS ↔ MATCHBOX"
    info = font_small.render(status, True, (160,180,220))
    screen.blit(info, (20, WINDOW_HEIGHT - 35))

    pygame.display.flip()

ser.close()
pygame.quit()