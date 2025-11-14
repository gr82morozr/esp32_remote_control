#!/usr/bin/env python3
import sys, pygame

# ---- Tuning knobs ----
TARGET_HZ = 500  # try 500–1000; real max depends on OS/driver/device
WIDTH, HEIGHT = 800, 700
BG = (12, 14, 18)
FG = (235, 235, 235)
ACCENT = (120, 170, 255)

def find_first_joystick():
    pygame.joystick.quit()
    pygame.joystick.init()
    if pygame.joystick.get_count() == 0:
        return None
    js = pygame.joystick.Joystick(0)
    js.init()
    return js

def main():
    pygame.init()

    # Pygame 2.1+ supports vsync kwarg; set 0 to avoid vsync capping
    try:
        screen = pygame.display.set_mode((WIDTH, HEIGHT), vsync=0)
    except TypeError:
        screen = pygame.display.set_mode((WIDTH, HEIGHT))  # fallback if older pygame

    pygame.display.set_caption("PS Controller – High-Rate Raw Reader")
    clock = pygame.time.Clock()

    # Only keep essential events to minimize overhead
    pygame.event.set_allowed([pygame.QUIT, pygame.KEYDOWN,
                              pygame.JOYDEVICEADDED, pygame.JOYDEVICEREMOVED])

    # Monospace font for cheap, fixed-width draws
    font = pygame.font.SysFont("Consolas,Menlo,Monaco,Courier New,monospace", 18)
    big  = pygame.font.SysFont("Consolas,Menlo,Monaco,Courier New,monospace", 24, bold=True)

    # Pre-render static labels
    static_lines = []
    def add_static(text, y, color=FG, bigfont=False):
        f = big if bigfont else font
        static_lines.append((f.render(text, True, color), (16, y)))
        return y + f.get_height() + 6

    y = 12
    y = add_static("Sony PlayStation Controller – RAW (High Poll Rate)", y, ACCENT, bigfont=True)
    y = add_static("ESC to quit. Hotplug supported. VSync off. Busy-loop timing.", y)
    base_y = y + 6

    js = find_first_joystick()

    # Layout regions for dynamic text (precompute positions)
    dyn_rows = []  # list of (label, y, type, index) where type in {"axis","btn","hat","hdr","info"}

    def rebuild_layout():
        nonlocal dyn_rows, base_y
        dyn_rows = []
        y = base_y

        if js is None:
            dyn_rows.append(("No joystick detected.", y, "info", -1))
            return

        # Header info
        try: name = js.get_name()
        except: name = "Unknown"
        try: guid = js.get_guid()
        except: guid = "N/A"
        id_ = getattr(js, "get_id", lambda: 0)()

        dyn_rows.append((f"Name : {name}", y, "info", -1)); y += font.get_height()+4
        dyn_rows.append((f"GUID : {guid}", y, "info", -1)); y += font.get_height()+4
        dyn_rows.append((f"ID   : {id_}", y, "info", -1));   y += font.get_height()+8

        na, nb, nh = js.get_numaxes(), js.get_numbuttons(), js.get_numhats()
        dyn_rows.append((f"Axes:{na}  Buttons:{nb}  Hats:{nh}", y, "info", -1)); y += font.get_height()+10

        dyn_rows.append(("Axes (−1.000 … +1.000):", y, "hdr", -1)); y += font.get_height()+4
        for i in range(na):
            dyn_rows.append((f"axis[{i:02d}] = +0.000", y, "axis", i)); y += font.get_height()+2

        y += 6
        dyn_rows.append(("Buttons (0/1):", y, "hdr", -1)); y += font.get_height()+4
        # pack buttons in a couple of fixed rows
        per_row = 16
        rows = (nb + per_row - 1)//per_row
        for r in range(rows):
            dyn_rows.append(("", y, "btnrow", r)); y += font.get_height()+2

        y += 6
        dyn_rows.append(("Hats (D-pad tuples):", y, "hdr", -1)); y += font.get_height()+4
        for i in range(nh):
            dyn_rows.append((f"hat[{i}] = (+0,+0)", y, "hat", i)); y += font.get_height()+2

        y += 8
        dyn_rows.append(("Notes: indices vary by OS/driver; USB usually higher Hz than BT.", y, "info", -1))

    rebuild_layout()

    # Main loop
    while True:
        # Pump minimal events
        for e in pygame.event.get():
            if e.type == pygame.QUIT:
                pygame.quit(); sys.exit(0)
            if e.type == pygame.KEYDOWN and e.key == pygame.K_ESCAPE:
                pygame.quit(); sys.exit(0)
            if e.type == pygame.JOYDEVICEADDED or e.type == pygame.JOYDEVICEREMOVED:
                js = find_first_joystick()
                rebuild_layout()

        screen.fill(BG)

        # Draw static once per frame (fast blits)
        for surf, pos in static_lines:
            screen.blit(surf, pos)

        # Draw dynamic
        if js is None:
            # just one info line
            for text, y, kind, _ in dyn_rows:
                if kind == "info":
                    screen.blit(font.render(text, True, (255,120,120)), (16, y))
            pygame.display.flip()
            clock.tick_busy_loop(TARGET_HZ)
            continue

        # Query joystick fast
        na, nb, nh = js.get_numaxes(), js.get_numbuttons(), js.get_numhats()
        axis_vals = [js.get_axis(i) for i in range(na)]
        btn_vals  = [js.get_button(i) for i in range(nb)]
        hat_vals  = [js.get_hat(i) for i in range(nh)]

        # Render dynamic rows
        for text, y, kind, idx in dyn_rows:
            if kind == "hdr":
                screen.blit(font.render(text, True, ACCENT), (16, y))
            elif kind == "info":
                screen.blit(font.render(text, True, FG), (16, y))
            elif kind == "axis":
                val = axis_vals[idx] if idx < len(axis_vals) else 0.0
                screen.blit(font.render(f"axis[{idx:02d}] = {val:+.3f}", True, FG), (32, y))
            elif kind == "btnrow":
                start = idx * 16
                end = min(start + 16, nb)
                chunk = "  ".join(f"b{b:02d}:{btn_vals[b]}" for b in range(start, end)) if start < end else ""
                screen.blit(font.render(chunk, True, FG), (32, y))
            elif kind == "hat":
                if idx < len(hat_vals):
                    hx, hy = hat_vals[idx]
                else:
                    hx, hy = 0, 0
                screen.blit(font.render(f"hat[{idx}] = ({hx:+d},{hy:+d})", True, FG), (32, y))

        pygame.display.flip()
        # Busy-loop timing gives tighter schedule than tick() (uses sleep)
        clock.tick_busy_loop(TARGET_HZ)

if __name__ == "__main__":
    main()
