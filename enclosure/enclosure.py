"""Enclosure for the Waveshare ESP32-C6-Touch-LCD-1.47 desk toy: the board on top (screen up), 2xAA below,
a sliding battery lid with a snap latch. FreeCAD 1.1 script, run headless:

    /Applications/FreeCAD.app/Contents/Resources/bin/freecadcmd enclosure/enclosure.py

Coordinates (mm): x across (0 = centre, +x = BOOT / switch side), y along (0 = USB end "A", L = end "B"),
z up (0 = bottom of the lid). Parts: tray (battery bay + board shelf), frame (top with the screen window),
lid (slides in from end B). Outputs go to enclosure/out/.
"""
import math
import os

import FreeCAD as App
import Part

V = App.Vector

# ---------------- measured / datasheet ----------------
CELL_D, CELL_L = 14.5, 50.5          # AA, length incl. the + nub
CONTACT_W, CONTACT_H = 9.65, 9.65    # single + / - plates (9.55 x 9.65 measured: use the larger for the slot)
BRIDGE_W, BRIDGE_H = 20.55, 9.55     # series plate: spring and bump 11.2 apart
BRIDGE_PITCH = 11.2
PLATE_SLOT = 0.9                     # slot for the plates (they are ~0.3-0.5 thick)
SWITCH_L, SWITCH_H = 9.0, 3.5        # slide switch body face (along y, along z)
LEVER_SLOT_L, LEVER_SLOT_H = 5.0, 2.2

# board (Waveshare drawing): plastic bezel case, PCB, brass M2 standoffs on the back
BEZ_W, BEZ_L, BEZ_R = 24.55, 44.50, 5.75
GLASS_W, GLASS_L = 22.05, 42.0
BEZ_T = 5.40          # bezel front -> PCB front
PCB_BACK = 6.60       # bezel front -> PCB back
STACK = 10.60         # bezel front -> end of the standoffs
HOLES = [(-8.89, 2.75), (8.89, 2.75), (-8.50, 41.75), (8.50, 41.75)]  # from the USB edge, x from the centre
BTN_FROM_USB = 7.8    # BOOT / RST side buttons: centre from the USB edge
BTN_BELOW_PCB = 0.8   # their centre below the PCB back
BTN_PROTRUDE = 0.5    # actuator beyond the board edge
USB_H = 3.2           # receptacle height on the PCB back

# ---------------- design ----------------
WALL = 2.4            # side walls
END_W = 2.4           # end walls (outer part, behind the plates)
LIP = 0.8             # stop in front of the plates
LIP_W = 0.9           # how far the stop covers the plate's top and bottom edges
SPRING_R = 4.0        # half width of the window in front of a spring / bump (spring base up to 8 mm)
CELL_PITCH = 14.8
CLR = 0.25
CRADLE_CLR = 0.15                              # radial play in the cradles: the cells sit in them without rattling
R_CRADLE = CELL_D / 2 + CRADLE_CLR
CAV_W = CELL_PITCH + CELL_D + 2 * CLR          # 29.8
LID_T = 2.0
CAV_H = 15.0                                   # lid top -> shelf underside
SHELF_T = 1.6
PLATE_GAP = 55.0                               # between the inner faces of the two plate slots
TOP_T, POCKET = 2.0, 0.8                       # top plate, recess for the bezel
CLAMP = 0.2                                    # the board is squeezed this much between the shelf and the top
R_OUT = 4.0                                    # outer vertical edge radius
LAP = 1.2                                      # tray/frame lap joint: outer part removed from the tray top
LAP_CLR = 0.15
BOARD_GAP = 0.3                                # bezel edge to the end A wall
# board snaps (no screws): cantilever hooks from the top plate catch the PCB back on both long sides
HOOK_T, HOOK_W = 1.0, 6.0                      # arm thickness (flexes outwards), width along y
HOOK_LIP, HOOK_TIP = 0.7, 1.6                  # catch depth under the PCB, arm length below the PCB back
HOOK_Y = (20.0, 36.0)                          # from the USB edge of the board: clear of the side buttons at 7.8
# lanyard eyelet: a diagonal channel through the corner at end B, RST side (away from USB and the switch)
EYE_W, EYE_H, EYE_BAR = 2.5, 6.5, 2.0          # rectangular channel (across, tall), bar left outside it at the corner
EYE_R_IN, EYE_R_MOUTH = 0.8, 0.8               # rounded channel corners; rounded window edges so the cord does not chafe
# engraved name in the top margin next to the screen (end B), filled with paint afterwards
LOGO_TEXT, LOGO_W, LOGO_DEPTH = "Tiden.ai", 26.0, 0.6
LOGO_FONT, LOGO_TRACK = "/Library/Fonts/SF-Pro-Rounded-Bold.otf", 0.6   # rounded strokes take the paint well

L = PLATE_GAP + 2 * (END_W + PLATE_SLOT)       # outer length
W = CAV_W + 2 * WALL                           # outer width
Z_CELL = LID_T + CELL_D / 2 + 0.1              # cell axis
Z_SHELF_BOT = LID_T + CAV_H
Z_SHELF_TOP = Z_SHELF_BOT + SHELF_T
H = Z_SHELF_TOP + STACK - CLAMP + (TOP_T - POCKET)
Z_BEZ_FRONT = H - (TOP_T - POCKET)             # where the bezel front sits
Z_PCB_BACK = Z_BEZ_FRONT - PCB_BACK
Y_BOARD = END_W + BOARD_GAP                    # USB edge of the bezel
ZONE = END_W + PLATE_SLOT + LIP                # end zone depth (wall + slot + stop)
PLATE_Z0 = Z_CELL - max(CONTACT_H, BRIDGE_H) / 2 - 0.1   # plates rest on this

# dovetail for the lid (half widths at z = 0 and z = LID_T)
DT_LID = (13.7, 15.7)
DT_CUT = (13.9, 15.9)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")


# ---------------- helpers ----------------
def box(x0, x1, y0, y1, z0, z1):
    return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, V(x0, y0, z0))


def rrect(x0, x1, y0, y1, z0, z1, r):
    """Box with rounded vertical edges."""
    r = min(r, (x1 - x0) / 2 - 0.01, (y1 - y0) / 2 - 0.01)
    s = box(x0 + r, x1 - r, y0, y1, z0, z1).fuse(box(x0, x1, y0 + r, y1 - r, z0, z1))
    for cx, cy in ((x0 + r, y0 + r), (x1 - r, y0 + r), (x0 + r, y1 - r), (x1 - r, y1 - r)):
        s = s.fuse(Part.makeCylinder(r, z1 - z0, V(cx, cy, z0)))
    return s.removeSplitter()


def prism_x(pts_yz, x0, x1):
    """Extrude a polygon given in the (y, z) plane along x."""
    w = Part.makePolygon([V(x0, y, z) for y, z in pts_yz] + [V(x0, *pts_yz[0])])
    return Part.Face(w).extrude(V(x1 - x0, 0, 0))


def prism_y(pts_xz, y0, y1):
    w = Part.makePolygon([V(x, y0, z) for x, z in pts_xz] + [V(pts_xz[0][0], y0, pts_xz[0][1])])
    return Part.Face(w).extrude(V(0, y1 - y0, 0))


def cyl_x(r, x0, x1, y, z):
    return Part.makeCylinder(r, x1 - x0, V(x0, y, z), V(1, 0, 0))


def cyl_y(r, y0, y1, x, z):
    return Part.makeCylinder(r, y1 - y0, V(x, y0, z), V(0, 1, 0))


def dovetail(half, y0, y1):
    (b, t) = half
    return prism_y([(-b, 0), (b, 0), (t, LID_T), (-t, LID_T)], y0, y1)


# Looking at the screen with the USB end away from you, BOOT is on the right: that is -x here (x points left).
BOOT_SIDE = -1                               # BOOT pusher and the switch; RST pin hole on the other side
SIDE_VIEW = "east" if BOOT_SIDE > 0 else "west"
CELL_X = (BOOT_SIDE * CELL_PITCH / 2, -BOOT_SIDE * CELL_PITCH / 2)  # cell 1: + at end B (next to the switch)
SNAP_Y = (L * 0.25, L * 0.75)


# ---------------- tray ----------------
def tray():
    s = rrect(-W / 2, W / 2, 0, L, 0, Z_SHELF_TOP, R_OUT)
    # lap joint: the outer part of the walls is removed above the shelf underside
    s = s.cut(rrect(-W / 2 - 1, W / 2 + 1, -1, L + 1, Z_SHELF_BOT, Z_SHELF_TOP + 1, R_OUT).cut(
        rrect(-W / 2 + LAP, W / 2 - LAP, LAP, L - LAP, Z_SHELF_BOT - 1, Z_SHELF_TOP + 2, R_OUT - LAP)))
    # battery bay: a half-round cradle over each cell (like any battery holder), straight sides below the
    # axis so the cells go in from below; the lid holds them in the cradles
    for cx in CELL_X:
        s = s.cut(cyl_y(R_CRADLE, ZONE, L - ZONE, cx, Z_CELL))
        s = s.cut(box(cx - R_CRADLE, cx + R_CRADLE, ZONE, L - ZONE, LID_T, Z_CELL))
    # plate slots (open through the shelf: plates drop in from above) and the windows in the stops
    for end in (0, 1):
        ys = (END_W, END_W + PLATE_SLOT) if end == 0 else (L - END_W - PLATE_SLOT, L - END_W)
        yl = (ys[1], ys[1] + LIP) if end == 0 else (ys[0] - LIP, ys[0])
        plates = [(0.0, BRIDGE_W)] if end == 0 else [(x, CONTACT_W) for x in CELL_X]
        for cx, w in plates:
            s = s.cut(box(cx - w / 2 - 0.2, cx + w / 2 + 0.2, ys[0], ys[1], PLATE_Z0, Z_SHELF_TOP + 1))
            # window in the stop layer: the lips around it hold the plate in its slot from the front
            h = max(CONTACT_H, BRIDGE_H) / 2 - LIP_W
            if end == 0:
                # bridge: a window per cell and a lip in the middle; the outer edges stay open because the
                # + nub of a cell sits 1.8 mm off the bump, almost at the plate edge
                for sg in (-1, 1):
                    xa, xb = sg * (BRIDGE_PITCH / 2 - SPRING_R), sg * (w / 2 + 0.2)
                    s = s.cut(box(min(xa, xb), max(xa, xb), yl[0] - 0.01, yl[1] + 0.01, Z_CELL - h, Z_CELL + h))
            else:  # single plates: lips on all four sides, like a stock holder
                s = s.cut(box(cx - SPRING_R, cx + SPRING_R, yl[0] - 0.01, yl[1] + 0.01, Z_CELL - h, Z_CELL + h))
    # wire channels next to the single plates (end B), up through the shelf
    for cx in CELL_X:
        s = s.cut(box(cx - 1.5, cx + 1.5, L - ZONE, L - END_W, Z_CELL + 2, Z_SHELF_TOP + 1))
    # the board is held by snaps in the frame: the standoffs just rest on the shelf
    # lid: dovetail slot open at end B, pocket for its tip in end A, latch notch under end B
    s = s.cut(dovetail(DT_CUT, 1.2, L + 1))
    s = s.cut(box(-4.3, 4.3, L - 3.8, L - 0.8, LID_T - 0.01, LID_T + 0.9))
    # USB-C cut-out reaches down into the tray at end A
    s = s.cut(usb_cut())
    # snap bumps on the lap (long sides)
    for y in SNAP_Y:
        for sx in (-1, 1):
            s = s.fuse(cyl_y(0.6, y - 4, y + 4, sx * (W / 2 - LAP - 0.3), (Z_SHELF_BOT + Z_SHELF_TOP) / 2))
    return s.removeSplitter()


def usb_cut():
    zc = Z_PCB_BACK - USB_H / 2
    return rrect_x(-6.25, 6.25, zc - 3.5, zc + 3.5, -1, END_W + 0.01, 3.0)


def rrect_x(x0, x1, z0, z1, y0, y1, r):
    """Rounded rectangle in the xz plane, extruded along y."""
    s = box(x0 + r, x1 - r, y0, y1, z0, z1).fuse(box(x0, x1, y0, y1, z0 + r, z1 - r))
    for cx, cz in ((x0 + r, z0 + r), (x1 - r, z0 + r), (x0 + r, z1 - r), (x1 - r, z1 - r)):
        s = s.fuse(cyl_y(r, y0, y1, cx, cz))
    return s.removeSplitter()


# ---------------- frame ----------------
Y_BTN = Y_BOARD + BTN_FROM_USB
Z_BTN = Z_PCB_BACK - BTN_BELOW_PCB
Y_SW = (Y_BOARD + BEZ_L + L - END_W) / 2
Z_SW = (Z_SHELF_TOP + H - TOP_T) / 2


def frame():
    s = rrect(-W / 2, W / 2, 0, L, Z_SHELF_BOT, H, R_OUT)
    top = [e for e in s.Edges if abs(e.BoundBox.ZMin - H) < 1e-6 and abs(e.BoundBox.ZMax - H) < 1e-6]
    try:
        s = s.makeChamfer(0.8, top)
    except Exception as ex:  # keep a sharp edge rather than fail
        print("top chamfer skipped:", ex)
    s = s.cut(box(-CAV_W / 2, CAV_W / 2, END_W, L - END_W, Z_SHELF_BOT - 1, H - TOP_T))
    # lap: the bottom of the walls goes over the tray's step
    c = LAP - LAP_CLR
    s = s.cut(rrect(-W / 2 + c, W / 2 - c, c, L - c, Z_SHELF_BOT - 1, Z_SHELF_TOP, R_OUT - c))
    for y in SNAP_Y:
        for sx in (-1, 1):
            s = s.cut(cyl_y(0.7, y - 4.5, y + 4.5, sx * (W / 2 - LAP - 0.3), (Z_SHELF_BOT + Z_SHELF_TOP) / 2))
    # screen: window over the glass, recess that centres the bezel
    s = s.cut(rrect(-(BEZ_W / 2 - 1.0), BEZ_W / 2 - 1.0, Y_BOARD + 1.0, Y_BOARD + BEZ_L - 1.0, H - TOP_T - 1, H + 1, BEZ_R - 1.0))
    s = s.cut(rrect(-BEZ_W / 2 - 0.15, BEZ_W / 2 + 0.15, Y_BOARD - 0.15, Y_BOARD + BEZ_L + 0.15, H - TOP_T - 0.01,
                    Z_BEZ_FRONT, BEZ_R + 0.15))
    # ledges that keep the contact plates in their slots (45 deg underneath for printing upside down)
    for y0, sgn in ((END_W, 1), (L - END_W, -1)):
        tri = [(y0, Z_SHELF_TOP), (y0 + sgn * 1.0, Z_SHELF_TOP), (y0, Z_SHELF_TOP + 1.0)]
        s = s.fuse(prism_x(tri, -CAV_W / 2, CAV_W / 2))
    for cx in CELL_X:  # wire channels at end B
        s = s.cut(box(cx - 1.5, cx + 1.5, L - ZONE, L - END_W + 0.01, Z_SHELF_TOP - 1, Z_SHELF_TOP + 1.5))
    s = s.cut(usb_cut())
    # BOOT: a tab in the side wall (hinge at the top), thinned, with a pusher on the inside.
    # Built on the +x wall, mirrored when BOOT_SIDE < 0.
    x_in, x_out = CAV_W / 2, W / 2
    zt0, zt1 = Z_SHELF_TOP + 0.6, H - TOP_T - 0.8
    cuts = [box(x_in - 0.1, x_out + 0.1, y0, y1, Z_SHELF_TOP, zt1) for y0, y1 in ((Y_BTN - 4.1, Y_BTN - 3.5), (Y_BTN + 3.5, Y_BTN + 4.1))]
    cuts += [box(x_in - 0.1, x_out + 0.1, Y_BTN - 4.1, Y_BTN + 4.1, Z_SHELF_TOP, zt0),
             box(x_in - 0.1, x_out - 1.4, Y_BTN - 3.5, Y_BTN + 3.5, zt0, zt1)]
    pusher = box(BEZ_W / 2 + BTN_PROTRUDE + 0.3, x_out - 1.4 + 0.01, Y_BTN - 1.2, Y_BTN + 1.2, Z_BTN - 1.2, Z_BTN + 1.2)
    after = [cyl_x(1.6, x_out - 0.4, x_out + 1, Y_BTN, Z_BTN),               # finger dimple
             cyl_x(0.8, -W / 2 - 1, -CAV_W / 2 + 0.1, Y_BTN, Z_BTN)]         # RST pin hole, opposite wall
    # slide switch: lever slot, recess for the body, I / O marks
    after += [box(x_in - 0.1, x_out + 0.1, Y_SW - LEVER_SLOT_L / 2, Y_SW + LEVER_SLOT_L / 2, Z_SW - LEVER_SLOT_H / 2,
                  Z_SW + LEVER_SLOT_H / 2),
              box(x_in - 0.1, x_in + 0.8, Y_SW - SWITCH_L / 2 - 0.2, Y_SW + SWITCH_L / 2 + 0.2, Z_SW - SWITCH_H / 2 - 0.2,
                  Z_SW + SWITCH_H / 2 + 0.2)]
    zm = Z_SW + LEVER_SLOT_H / 2 + 2.4
    y_o, y_i = (Y_SW - 3.6, Y_SW + 3.6)
    after += [cyl_x(1.3, x_out - 0.5, x_out + 1, y_o, zm).cut(cyl_x(0.65, x_out - 1, x_out + 2, y_o, zm)),
              box(x_out - 0.5, x_out + 1, y_i - 0.35, y_i + 0.35, zm - 1.3, zm + 1.3)]
    flip = (lambda sh: sh.mirror(V(0, 0, 0), V(1, 0, 0))) if BOOT_SIDE < 0 else (lambda sh: sh)
    for c in cuts:
        s = s.cut(flip(c))
    s = s.fuse(flip(pusher))
    for c in after:
        s = s.cut(flip(c))
    for h in board_hooks():
        s = s.fuse(h)
    s = s.fuse(eyelet_block()).cut(eyelet_channel()).removeSplitter()
    s = round_eyelet_mouths(s)
    return s.cut(logo_cut())


Y_LOGO = (Y_BOARD + BEZ_L - 1.0 + L - 0.8) / 2   # middle of the margin between the window and the chamfer at end B


def logo_faces():
    """The name as faces at the bottom of the engraving. It reads upright with the USB end away from you (screen above
    it, like the name under a phone's display): letters run towards -x, their tops point to end A."""
    chars = [Part.makeFace(ch, "Part::FaceMakerBullseye") for ch in Part.makeWireString(LOGO_TEXT, LOGO_FONT, 10.0, LOGO_TRACK) if ch]
    t = Part.makeCompound(chars)
    k = LOGO_W / t.BoundBox.XLength
    b = t.BoundBox
    t.translate(V(-(b.XMin + b.XMax) / 2, -(b.YMin + b.YMax) / 2, 0))
    t.scale(k)
    t.rotate(V(0, 0, 0), V(0, 0, 1), 180)
    t.translate(V(0, Y_LOGO, H - LOGO_DEPTH))
    return t.Faces


def logo_cut():
    return Part.makeCompound([f.extrude(V(0, 0, LOGO_DEPTH + 1)) for f in logo_faces()])


def logo_paint():
    """The paint in the engraving, for the renders."""
    return Part.makeCompound([f.extrude(V(0, 0, LOGO_DEPTH - 0.05)) for f in logo_faces()])


Z_HOOK_CATCH = Z_PCB_BACK - 0.1                 # 0.1 play: the shelf presses the board up against the top anyway


def board_hooks():
    """Four snap arms hanging from the top plate along the board's long sides. Each flexes outwards into the gap
    to the wall while the board is pushed in, then its lip catches the PCB back edge."""
    out = []
    a0 = BEZ_W / 2 + 0.15                       # arm inner face = edge of the bezel recess
    for sx in (-1, 1):
        for hy in HOOK_Y:
            y0, y1 = Y_BOARD + hy - HOOK_W / 2, Y_BOARD + hy + HOOK_W / 2
            arm = box(a0, a0 + HOOK_T, y0, y1, Z_PCB_BACK - HOOK_TIP, H - TOP_T + 0.01)
            lip = prism_y([(a0 + 0.01, Z_HOOK_CATCH), (a0 - HOOK_LIP, Z_HOOK_CATCH), (a0 - HOOK_LIP, Z_HOOK_CATCH - 0.4),
                           (a0 + 0.01, Z_PCB_BACK - HOOK_TIP)], y0, y1)   # flat catch on top, lead-in ramp below
            h = arm.fuse(lip)
            out.append(h if sx > 0 else h.mirror(V(0, 0, 0), V(1, 0, 0)))
    return out


EYE_X = -BOOT_SIDE                               # the eyelet corner: RST side, end B
EYE_Z = (Z_SHELF_TOP + 0.6 + H - TOP_T) / 2
# the channel runs at 45 deg across the corner: x + y = EYE_C (for the +x corner), leaving EYE_BAR of the
# rounded corner outside it
EYE_C = (W / 2 - R_OUT) + (L - R_OUT) + math.sqrt(2) * (R_OUT - EYE_BAR - EYE_W / 2)


def eyelet_block():
    b = box(CAV_W / 2 - 5.0, CAV_W / 2 + 0.01, L - END_W - 6.0, L - END_W + 0.01, Z_SHELF_TOP + 0.6, H - TOP_T + 0.01)
    return b if EYE_X > 0 else b.mirror(V(0, 0, 0), V(1, 0, 0))


def round_eyelet_mouths(s):
    """Fillet the outline of both windows (end face and side face) of the eyelet channel."""
    hw = EYE_W / 2 * math.sqrt(2) + 0.3        # half width of a window on its face (the channel meets it at 45 deg)
    xc, yc = EYE_X * (EYE_C - L), EYE_C - W / 2
    def on_window(e):
        b = e.BoundBox
        if b.ZMin < EYE_Z - EYE_H / 2 - 0.3 or b.ZMax > EYE_Z + EYE_H / 2 + 0.3:
            return False
        end = abs(b.YMin - L) < 1e-4 and abs(b.YMax - L) < 1e-4 and b.XMin > xc - hw and b.XMax < xc + hw
        side = (abs(b.XMin - EYE_X * W / 2) < 1e-4 and abs(b.XMax - EYE_X * W / 2) < 1e-4
                and b.YMin > yc - hw and b.YMax < yc + hw)
        return end or side
    edges = [e for e in s.Edges if on_window(e)]
    try:
        r = s.makeFillet(EYE_R_MOUTH, edges)
        print(f"eyelet mouths: {len(edges)} edges rounded R{EYE_R_MOUTH}")
        return r
    except Exception as ex:
        print("eyelet mouth fillet failed:", ex, f"({len(edges)} edges)")
        return s


def eyelet_channel():
    """A straight rectangular channel at 45 deg: tall rectangular windows on the end face and on the side."""
    x_end, y_side = EYE_C - L, EYE_C - W / 2     # where its axis comes out on the end face and on the side
    length = math.hypot(W / 2 - x_end, L - y_side) + 6
    # rounded rectangle across the channel (x: width, z: height), run along y, then turned onto the 45 deg axis
    c = rrect_x(-EYE_W / 2, EYE_W / 2, EYE_Z - EYE_H / 2, EYE_Z + EYE_H / 2, -length / 2, length / 2, EYE_R_IN)
    c.rotate(V(0, 0, 0), V(0, 0, 1), -135)
    c.translate(V((x_end + W / 2) / 2, (L + y_side) / 2, 0))
    return c if EYE_X > 0 else c.mirror(V(0, 0, 0), V(1, 0, 0))


# ---------------- lid ----------------
TONGUE_W, TONGUE_L = 8.0, 12.0


def lid(with_bump=True):
    s = dovetail(DT_LID, 1.4, L)
    for sx in (-1, 1):  # U-cut: the tongue is free at the end B edge
        x0 = sx * TONGUE_W / 2
        s = s.cut(box(min(x0, x0 + sx * 0.8), max(x0, x0 + sx * 0.8), L - TONGUE_L, L + 1, -1, LID_T + 1))
    if with_bump:  # ramp towards end A, square face towards end B
        s = s.fuse(prism_x([(L - 3.6, LID_T - 0.01), (L - 1.9, LID_T + 0.8), (L - 0.9, LID_T + 0.8), (L - 0.9, LID_T - 0.01)],
                           -TONGUE_W / 2 + 0.4, TONGUE_W / 2 - 0.4))
    for k in range(3):  # finger grip
        y = L - 2.5 - 2.0 * k
        s = s.cut(box(-TONGUE_W / 2 + 0.8, TONGUE_W / 2 - 0.8, y - 0.3, y + 0.3, -0.1, 0.4))
    # polarity marks on the inside: + at the nub end of each cell
    for cx, plus_at_b in ((CELL_X[0], True), (CELL_X[1], False)):
        yp, ym = (L - 10, 12) if plus_at_b else (12, L - 10)
        for y, plus in ((yp, True), (ym, False)):
            s = s.cut(box(cx - 0.4, cx + 0.4, y - 2.2, y + 2.2, LID_T - 0.4, LID_T + 0.1))
            if plus:
                s = s.cut(box(cx - 2.2, cx + 2.2, y - 0.4, y + 0.4, LID_T - 0.4, LID_T + 0.1))
    return s.removeSplitter()


# ---------------- dummies for checks and drawings ----------------
def cells():
    """Cell 1 (east): + nub at end B; cell 2 (west): + nub at end A. Each pressed against its bump plate."""
    out = []
    for cx, nub_at_b in ((CELL_X[0], True), (CELL_X[1], False)):
        if nub_at_b:
            y1 = L - END_W - 0.4 - 0.8           # nub tip on the bump (plate ~0.4 thick, bump ~0.8)
            y0 = y1 - CELL_L
            body = cyl_y(CELL_D / 2, y0, y1 - 1.0, cx, Z_CELL).fuse(cyl_y(2.7, y1 - 1.0, y1, cx, Z_CELL))
        else:
            y0 = END_W + 0.4 + 0.8
            body = cyl_y(2.7, y0, y0 + 1.0, cx, Z_CELL).fuse(cyl_y(CELL_D / 2, y0 + 1.0, y0 + CELL_L, cx, Z_CELL))
        out.append(body)
    return out


def plates():
    out = [box(-BRIDGE_W / 2, BRIDGE_W / 2, END_W + 0.1, END_W + 0.5, Z_CELL - BRIDGE_H / 2, Z_CELL + BRIDGE_H / 2)]
    for cx in CELL_X:
        out.append(box(cx - CONTACT_W / 2, cx + CONTACT_W / 2, L - END_W - 0.5, L - END_W - 0.1,
                       Z_CELL - CONTACT_H / 2, Z_CELL + CONTACT_H / 2))
    return out


def board():
    """Bezel case + PCB + standoffs + USB-C + side buttons, as placed in the enclosure."""
    y0 = Y_BOARD
    bez = rrect(-BEZ_W / 2, BEZ_W / 2, y0, y0 + BEZ_L, Z_BEZ_FRONT - BEZ_T, Z_BEZ_FRONT, BEZ_R)
    pcb = rrect(-BEZ_W / 2, BEZ_W / 2, y0, y0 + BEZ_L, Z_PCB_BACK, Z_BEZ_FRONT - BEZ_T, BEZ_R)
    s = bez.fuse(pcb)
    for hx, hy in HOLES:
        s = s.fuse(Part.makeCylinder(1.75, STACK - PCB_BACK, V(hx, y0 + hy, Z_BEZ_FRONT - STACK)))
    s = s.fuse(box(-4.5, 4.5, y0, y0 + 7.3, Z_PCB_BACK - USB_H, Z_PCB_BACK))
    for sx in (-1, 1):
        x = sx * (BEZ_W / 2 + BTN_PROTRUDE)
        s = s.fuse(box(min(x, sx * (BEZ_W / 2 - 3)), max(x, sx * (BEZ_W / 2 - 3)), y0 + BTN_FROM_USB - 1.5,
                       y0 + BTN_FROM_USB + 1.5, Z_PCB_BACK - BTN_BELOW_PCB - 0.8, Z_PCB_BACK - BTN_BELOW_PCB + 0.8))
    return s.removeSplitter()


# ---------------- sheets ----------------
def tray_sheet(t, path):
    import draw as D
    fig, sub = D.sheet(path, f"ЛОТОК (tray) — {L:.1f} × {W:.1f} × {Z_SHELF_TOP:.1f} мм, PETG, печать вверх дном", 2, 3)
    ax = sub(1)
    D.draw_view(ax, t, "top", "Вид сверху (полка под плату)")
    tv = lambda p: D.to_view("top", p)
    D.dim(ax, tv((W / 2, 0, 0)), tv((W / 2, L, 0)), -4, f"{L:.1f}")
    D.dim(ax, tv((-W / 2, 0, 0)), tv((W / 2, 0, 0)), -4, f"{W:.1f}", vertical=True)
    D.note(ax, tv((HOLES[0][0], Y_BOARD + HOLES[0][1], 0)), "стойки платы лежат на полке (без винтов)", 4, 4)
    D.note(ax, tv((CELL_X[0], L - ZONE + 1, 0)), "окна под провода", -14, 6)
    D.note(ax, tv((0, END_W + 0.4, 0)), "паз переходной пластины", 4, -10)

    ax = sub(2)
    ya = END_W + PLATE_SLOT + LIP / 2
    D.draw_section(ax, t, "endB", (0, 1, 0), ya, "Торец A изнутри: губки переходной пластины",
                   overlay=[(p, "#2E86C1") for p in plates()[:1]])
    av = lambda p: D.to_view("endB", p)
    D.note(ax, av((0, ya, Z_CELL)), "губка посередине", 3, -9)
    D.note(ax, av((BRIDGE_W / 2, ya, Z_CELL + 4.4)), "сверху/снизу 0.9", -4, 7)
    ax.set_xlim(-20, 20)
    ax.set_ylim(-1, 21)
    ax = sub(3)
    yb = L - END_W - PLATE_SLOT - LIP / 2
    D.draw_section(ax, t, "endA", (0, -1, 0), -yb, "Торец B изнутри: контакты «+» и «−» в рамке губок",
                   overlay=[(p, "#2E86C1") for p in plates()[1:]])
    bv2 = lambda p: D.to_view("endA", p)
    D.note(ax, bv2((CELL_X[0] + SPRING_R + 0.4, yb, Z_CELL)), "боковые губки", 4, -9)
    ax.set_xlim(-20, 20)
    ax.set_ylim(-1, 21)

    ax = sub(4)
    D.draw_section(ax, t, "endA", (0, -1, 0), -L / 2, "Разрез А-А (поперёк, по середине)",
                   extra=[(c, "#E2B13C") for c in cells()])
    sv = lambda p: D.to_view("endA", p)
    bw = CELL_PITCH / 2 + R_CRADLE
    D.dim(ax, sv((-bw, 0, LID_T)), sv((bw, 0, LID_T)), -9, f"отсек {2 * bw:.1f}")
    D.dim(ax, sv((-CELL_PITCH / 2, 0, LID_T)), sv((CELL_PITCH / 2, 0, LID_T)), -4, f"шаг {CELL_PITCH}")
    D.dim(ax, sv((CAV_W / 2, 0, LID_T)), sv((CAV_W / 2, 0, Z_SHELF_BOT)), 6, f"{CAV_H:.1f}", vertical=True)
    D.dim(ax, sv((-DT_CUT[0], 0, 0)), sv((DT_CUT[0], 0, 0)), -14, f"паз крышки {2 * DT_CUT[0]:.1f} / {2 * DT_CUT[1]:.1f}")
    D.dim(ax, sv((-W / 2, 0, Z_SHELF_TOP)), sv((-W / 2, 0, Z_SHELF_BOT)), -4, f"{SHELF_T}", vertical=True)
    D.note(ax, sv((CELL_PITCH / 2, 0, Z_CELL + R_CRADLE)), f"ложемент R{R_CRADLE:.2f}", 4, -9)
    D.note(ax, sv((-CELL_PITCH / 2, 0, Z_CELL + R_CRADLE)), f"ложемент R{R_CRADLE:.2f}", -10, -9)
    D.note(ax, sv((W / 2 - LAP - 0.3, 0, (Z_SHELF_BOT + Z_SHELF_TOP) / 2)), "защёлка рамки", 3, 6)
    ax.set_ylim(-17, 27)

    ax = sub(5)
    D.draw_section(ax, t, "east", (1, 0, 0), CELL_X[0], f"Разрез Б-Б (вдоль, по оси ячейки x={CELL_X[0]:.1f})",
                   extra=[(c, "#E2B13C") for c in cells()] + [(p, "#7F8C8D") for p in plates()])
    bv = lambda p: D.to_view("east", p)
    D.dim(ax, bv((0, END_W, PLATE_Z0)), bv((0, L - END_W, PLATE_Z0)), -11,
          f"между пазами {PLATE_GAP:.1f} (+2×{PLATE_SLOT})")
    D.dim(ax, bv((0, 0, Z_CELL)), bv((0, 0, LID_T)), -4, f"ось {Z_CELL - LID_T:.2f}", vertical=True)
    D.note(ax, bv((0, L - 1.4, LID_T + 0.5)), "зуб защёлки", -12, -10)
    D.note(ax, bv((0, 1.8, LID_T / 2)), "карман кончика крышки", 6, -10)

    ax = sub(6, True)
    D.draw_3d(ax, [(t, "#D9D2C5")], elev=-50, azim=-60, title="3D снизу: ложементы под батарейки")
    D.save(fig, path)


def frame_sheet(f, path):
    import draw as D
    fig, sub = D.sheet(path, f"РАМКА (frame) — {L:.1f} × {W:.1f} × {H - Z_SHELF_BOT:.1f} мм, PETG, печать лицом вниз", 3, 3, size=(16.5, 16))
    ax = sub(1)
    D.draw_view(ax, f, "top", "Вид сверху (окно экрана)")
    tv = lambda p: D.to_view("top", p)
    D.dim(ax, tv((BEZ_W / 2 - 1, Y_BOARD + 1, H)), tv((BEZ_W / 2 - 1, Y_BOARD + BEZ_L - 1, H)), -9,
          f"окно {BEZ_L - 2:.1f}")
    D.dim(ax, tv((-(BEZ_W / 2 - 1), Y_BOARD + BEZ_L - 1, H)), tv((BEZ_W / 2 - 1, Y_BOARD + BEZ_L - 1, H)), 6,
          f"{BEZ_W - 2:.2f}", vertical=True)
    D.dim(ax, tv((W / 2, 0, H)), tv((W / 2, Y_BOARD + 1, H)), -4, f"{Y_BOARD + 1:.1f}")
    D.note(ax, tv((-LOGO_W / 2, Y_LOGO, H)), f"надпись {LOGO_TEXT}, глубина {LOGO_DEPTH}", 4, 6)
    ax = sub(2)
    D.draw_view(ax, f, SIDE_VIEW, "Сторона BOOT: язычок и выключатель I/O")
    ev = lambda p: D.to_view(SIDE_VIEW, p)
    xs = BOOT_SIDE * W / 2
    D.dim(ax, ev((xs, 0, Z_SHELF_BOT)), ev((xs, Y_BTN, Z_SHELF_BOT)), -4, f"BOOT {Y_BTN:.1f}")
    D.dim(ax, ev((xs, Y_SW, Z_SHELF_BOT)), ev((xs, L, Z_SHELF_BOT)), -4, f"{L - Y_SW:.1f}")
    D.dim(ax, ev((xs, L, Z_SHELF_BOT)), ev((xs, L, H)), 4, f"{H - Z_SHELF_BOT:.1f}", vertical=True)
    D.note(ax, ev((xs, Y_SW, Z_SW)), "прорезь 5×2.2", 4, -9)
    ax = sub(3)
    D.draw_view(ax, f, "endA", "Торец A: USB-C")
    av = lambda p: D.to_view("endA", p)
    zc = Z_PCB_BACK - USB_H / 2
    D.dim(ax, av((-6.25, 0, zc + 3.5)), av((6.25, 0, zc + 3.5)), 4, "12.5")
    D.dim(ax, av((6.25, 0, zc - 3.5)), av((6.25, 0, zc + 3.5)), 5, "7.0", vertical=True)
    ax = sub(4)
    bd = board()
    D.draw_section(ax, f, "endA", (0, -1, 0), -Y_BTN, f"Разрез В-В через кнопки (y={Y_BTN:.1f})", extra=[(bd, "#2E86C1")])
    sv = lambda p: D.to_view("endA", p)
    D.note(ax, sv((BOOT_SIDE * (BEZ_W / 2 + 1.5), 0, Z_BTN)), "толкатель BOOT", 4, 8)
    D.note(ax, sv((-BOOT_SIDE * (W / 2 - 1), 0, Z_BTN)), "RST Ø1.6", -10, 8)
    ax = sub(5)
    D.draw_section(ax, f, "east", (1, 0, 0), 0.0, "Разрез Г-Г (вдоль, по центру)", extra=[(bd, "#2E86C1")])
    gv = lambda p: D.to_view("east", p)
    D.dim(ax, gv((0, Y_BOARD, H)), gv((0, Y_BOARD + BEZ_L, H)), 4, f"плата {BEZ_L}")
    D.note(ax, gv((0, END_W + 0.5, Z_SHELF_TOP + 0.5)), "упор контактов", 6, -8)
    ax = sub(6)
    yh = Y_BOARD + HOOK_Y[0]
    D.draw_section(ax, f, "endA", (0, -1, 0), -yh, f"Разрез Д-Д через защёлки платы (y={yh:.1f})", extra=[(bd, "#2E86C1")])
    hv = lambda p: D.to_view("endA", p)
    a0 = BEZ_W / 2 + 0.15
    D.note(ax, hv((a0 - HOOK_LIP, 0, Z_HOOK_CATCH)), f"зуб {HOOK_LIP} под платой", 5, -7)
    D.note(ax, hv((a0 + HOOK_T, 0, (Z_PCB_BACK + H - TOP_T) / 2)), f"язычок {HOOK_T} × {HOOK_W:.0f}", 3, 6)
    D.dim(ax, hv((a0 + HOOK_T, 0, Z_PCB_BACK - HOOK_TIP)), hv((CAV_W / 2, 0, Z_PCB_BACK - HOOK_TIP)), -4,
          f"ход {CAV_W / 2 - a0 - HOOK_T:.2f}")
    ax.set_xlim(2, W / 2 + 4)
    ax.set_ylim(Z_SHELF_BOT - 1, H + 2)
    ax = sub(7)
    D.draw_section(ax, f, "top", (0, 0, 1), EYE_Z, f"Разрез Е-Е: проушина для шнурка (z={EYE_Z:.1f})")
    ev2 = lambda p: D.to_view("top", p)
    xe = EYE_X * (EYE_C - L)
    D.note(ax, ev2((xe, L, EYE_Z)), f"канал {EYE_W} × {EYE_H}, R{EYE_R_IN}; кромки окон R{EYE_R_MOUTH}", 4, 6)
    D.note(ax, ev2((EYE_X * (W / 2 - 1.0), L - 1.0, EYE_Z)), f"перемычка {EYE_BAR}", 6, -4)
    ax.set_xlim(L - 16, L + 5)
    ax.set_ylim(*sorted((-EYE_X * (W / 2 + 4), -EYE_X * (W / 2 - 14))))  # top view: screen up is -x
    ax = sub(8)
    D.draw_view(ax, f, "endB", "Торец B: окно проушины", hidden=False)
    bv3 = lambda p: D.to_view("endB", p)
    xw = EYE_X * (EYE_C - L)
    hw = EYE_W / 2 * math.sqrt(2)                # the 45 deg channel cuts a wider window on the face
    D.dim(ax, bv3((xw - hw, L, EYE_Z + EYE_H / 2)), bv3((xw + hw, L, EYE_Z + EYE_H / 2)), 3, f"{2 * hw:.1f}")
    D.dim(ax, bv3((xw + EYE_X * hw, L, EYE_Z - EYE_H / 2)), bv3((xw + EYE_X * hw, L, EYE_Z + EYE_H / 2)), -3, f"{EYE_H}", vertical=True)
    ax = sub(9, True)
    xa, xb = sorted((EYE_X * (W / 2 - 13), EYE_X * (W / 2 + 1)))
    corner = f.copy().common(box(xa, xb, L - 13, L + 1, Z_SHELF_BOT - 1, H + 1))  # close-up (on a copy: the engraved letters
    # make booleans on the original return nothing)
    D.draw_3d(ax, [(corner, "#D9D2C5")], elev=18, azim=45 if EYE_X > 0 else 135, title="3D крупно: проушина со скруглёнными кромками")
    D.save(fig, path)


def lid_sheet(lw, path):
    import draw as D
    fig, sub = D.sheet(path, f"КРЫШКА (lid) — {L - 1.4:.1f} × {2 * DT_LID[1]:.1f} × {LID_T} мм, PETG, печать плашмя (внутренней стороной вверх)", 2, 3)
    ax = sub(1)
    D.draw_view(ax, lw, "top", "Вид изнутри (метки полярности)")
    tv = lambda p: D.to_view("top", p)
    D.dim(ax, tv((DT_LID[1], 1.4, LID_T)), tv((DT_LID[1], L, LID_T)), -4, f"{L - 1.4:.1f}")
    D.note(ax, tv((CELL_X[0], L - 10, LID_T)), "+ ячейки 1", 4, -8)
    ax = sub(2)
    D.draw_view(ax, lw, "bottom", "Вид снаружи (язычок с рифлением)")
    bv = lambda p: D.to_view("bottom", p)
    D.dim(ax, bv((-TONGUE_W / 2, L - TONGUE_L, 0)), bv((-TONGUE_W / 2, L, 0)), -4, f"язычок {TONGUE_L:.0f}")
    ax = sub(3)
    D.draw_section(ax, lw, "endA", (0, -1, 0), -L / 2, "Разрез: ласточкин хвост 45°")
    sv = lambda p: D.to_view("endA", p)
    D.dim(ax, sv((-DT_LID[0], 0, 0)), sv((DT_LID[0], 0, 0)), -3, f"{2 * DT_LID[0]:.1f}")
    D.dim(ax, sv((-DT_LID[1], 0, LID_T)), sv((DT_LID[1], 0, LID_T)), 3, f"{2 * DT_LID[1]:.1f}")
    ax = sub(4)
    D.draw_section(ax, lw, "east", (1, 0, 0), 0.0, "Разрез по язычку: зуб защёлки")
    ev = lambda p: D.to_view("east", p)
    ax.set_xlim(L - 16, L + 3)
    ax.set_ylim(-3, 5)
    D.note(ax, ev((0, L - 1.4, LID_T + 0.8)), "упор 0.8", -4, 1.5)
    D.note(ax, ev((0, L - 2.8, LID_T + 0.4)), "скос", -5, 1.5)
    ax = sub(5)
    D.draw_view(ax, lw, "east", "Вид сбоку")
    ax = sub(6, True)
    D.draw_3d(ax, [(lw, "#D9D2C5")], elev=35, azim=-60, title="3D")
    D.save(fig, path)


def assembly_sheet(t, f, lw, path):
    import draw as D
    bd, cs, ps = board(), cells(), plates()
    fig, sub = D.sheet(path, f"СБОРКА — {L:.1f} × {W:.1f} × {H:.1f} мм", 2, 3)
    ax = sub(1, True)
    D.draw_3d(ax, [(t, "#C9C2B5"), (f, "#E4DED2"), (lw, "#B8B0A2"), (bd, "#222831")], elev=30, azim=180 - 55 if BOOT_SIDE < 0 else -55, title="Собранный корпус")
    ax = sub(2, True)
    ex = []
    for shp, dz, col in ((f, 22, "#E4DED2"), (bd, 11, "#222831"), (t, 0, "#C9C2B5"), (lw, -14, "#B8B0A2")):
        c = shp.copy()
        c.translate(V(0, 0, dz))
        ex.append((c, col))
    for c in cs:
        c2 = c.copy()
        c2.translate(V(0, 0, -7))
        ex.append((c2, "#E2B13C"))
    D.draw_3d(ax, ex, elev=18, azim=180 - 55 if BOOT_SIDE < 0 else -55, title="Разнесённый вид")
    ax = sub(3)
    D.draw_view(ax, t.fuse(f).fuse(lw), SIDE_VIEW, "Сторона BOOT и выключателя", hidden=False)
    ax = sub(4)
    D.draw_section(ax, [t, f, lw], "endA", (0, -1, 0), -Y_BTN, f"Поперёк через BOOT (y={Y_BTN:.1f})",
                   extra=[(bd, "#2E86C1")] + [(c, "#E2B13C") for c in cs])
    ax = sub(5)
    D.draw_section(ax, [t, f, lw], "east", (1, 0, 0), CELL_X[0], f"Вдоль по ячейке 1 (x={CELL_X[0]:.1f})",
                   extra=[(bd, "#2E86C1")] + [(c, "#E2B13C") for c in cs] + [(p, "#7F8C8D") for p in ps])
    ax = sub(6)
    D.draw_section(ax, [t, f, lw], "endA", (0, -1, 0), -(L - 3.0), "Поперёк у торца B (контакты, защёлка)",
                   extra=[(c, "#E2B13C") for c in cs] + [(p, "#7F8C8D") for p in ps])
    D.save(fig, path)


def top_sheet(t, f, lw, path):
    """The finished top as you hold it: USB end away from you, the name under the screen."""
    import draw as D
    bd = board()
    fig, sub = D.sheet(path, f"ВЕРХ: надпись {LOGO_TEXT} (гравировка {LOGO_DEPTH} мм под краску)", 1, 2, size=(16.5, 8))
    ax = sub(1, True)
    D.draw_3d(ax, [(t, "#C9C2B5"), (f, "#E4DED2"), (lw, "#B8B0A2"), (bd, "#222831")], elev=62, azim=90, title="Сверху: USB от себя")
    ax = sub(2, True)
    xa, xb = -W / 2 - 1, W / 2 + 1
    clip = box(xa, xb, Y_BOARD + BEZ_L - 4, L + 1, H - 3, H + 1)
    top = f.copy().common(clip)
    D.draw_3d(ax, [(top, "#E4DED2"), (logo_paint(), "#3636BB")], elev=55, azim=90, title="Крупно, буквы закрашены")
    D.save(fig, path)


def check(t, f, lw):
    bd = board()
    rows = [("tray x frame", t, f), ("tray x lid", t, lw), ("frame x lid", f, lw), ("frame x board", f, bd),
            ("tray x board (clamp on standoffs)", t, bd)]
    rows += [(f"cell{i} x {n}", c, s) for i, c in enumerate(cells()) for n, s in (("tray", t), ("frame", f), ("lid", lw))]
    rows += [(f"plate{i} x tray", p, t) for i, p in enumerate(plates())]
    slid = lid(False)
    slid.translate(V(0, 12, 0))
    rows.append(("lid (no bump) pulled 12 mm x tray", slid, t))
    # play: a cell shifted 0.3 mm in any direction must hit the cradle or the lid
    for i, c in enumerate(cells()):
        hits = []
        for d in ((0.3, 0, 0), (-0.3, 0, 0), (0, 0, 0.3), (0, 0, -0.3)):
            m = c.copy()
            m.translate(V(*d))
            hits.append(m.common(t).Volume + m.common(lw).Volume > 1e-3)
        print(f"  cell{i} blocked at 0.3 mm (+x, -x, up, down): {hits}")
    for name, a, b in rows:
        print(f"  {name:38s} {a.common(b).Volume:8.3f} mm3")
    nub_tip = BEZ_W / 2 + BTN_PROTRUDE + 0.3
    print(f"  BOOT on {'-x' if BOOT_SIDE < 0 else '+x'}: pusher tip |x|={nub_tip:.2f}, button face |x|={BEZ_W / 2 + BTN_PROTRUDE:.2f}, gap 0.30")
    print(f"  spring working length ~{L - END_W - 0.4 - (END_W + 0.4 + 0.8 + CELL_L):.1f} mm at each - end")


if __name__ == "__main__" or True:
    import sys

    import MeshPart
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    os.makedirs(OUT, exist_ok=True)
    print(f"outer {L:.1f} x {W:.1f} x {H:.1f} mm; shelf {Z_SHELF_BOT:.1f}-{Z_SHELF_TOP:.1f}; cell axis {Z_CELL:.2f}")
    t, f, lw = tray(), frame(), lid()
    for n, s, flip in (("tray", t, True), ("frame", f, True), ("lid", lw, False)):
        print(f"{n}: valid {s.isValid()}, solids {len(s.Solids)}, volume {s.Volume:.0f} mm3")
        p = s.copy()  # STL in print orientation: tray and frame upside down, lid flat
        if flip:
            p.rotate(V(0, 0, 0), V(0, 1, 0), 180)
        p.translate(V(0, 0, -p.BoundBox.ZMin))
        MeshPart.meshFromShape(Shape=p, LinearDeflection=0.02, AngularDeflection=0.15).write(os.path.join(OUT, n + ".stl"))
    Part.makeCompound([t, f, lw]).exportStep(os.path.join(OUT, "enclosure.step"))
    check(t, f, lw)
    only = os.environ.get("SHEETS", "12345")
    if "1" in only: tray_sheet(t, os.path.join(OUT, "1_tray.png"))
    if "2" in only: frame_sheet(f, os.path.join(OUT, "2_frame.png"))
    if "3" in only: lid_sheet(lw, os.path.join(OUT, "3_lid.png"))
    if "4" in only: assembly_sheet(t, f, lw, os.path.join(OUT, "4_assembly.png"))
    if "5" in only: top_sheet(t, f, lw, os.path.join(OUT, "5_top.png"))
