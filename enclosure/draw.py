"""Drawing sheets for the enclosure parts: hidden-line views (TechDraw), hatched sections, dimensions, a 3D view."""
import FreeCAD as App
import Part
import TechDraw
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.collections import LineCollection, PolyCollection
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

V = App.Vector

# view name -> (u = screen right, v = screen up, w = towards the viewer), model axes
VIEWS = {
    "top": ((0, 1, 0), (-1, 0, 0), (0, 0, 1)),
    "bottom": ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
    "east": ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
    "west": ((0, -1, 0), (0, 0, 1), (-1, 0, 0)),
    "endA": ((1, 0, 0), (0, 0, 1), (0, -1, 0)),
    "endB": ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
}
TITLES = {"top": "Вид сверху", "bottom": "Вид снизу", "east": "Вид справа (+x, сторона BOOT)",
          "west": "Вид слева (-x)", "endA": "Торец A (USB)", "endB": "Торец B"}


def _matrix(view):
    u, v, w = VIEWS[view]
    m = App.Matrix()
    m.A11, m.A12, m.A13 = u
    m.A21, m.A22, m.A23 = v
    m.A31, m.A32, m.A33 = w
    return m


def to_view(view, p):
    u, v, _ = VIEWS[view]
    return (sum(a * b for a, b in zip(u, p)), sum(a * b for a, b in zip(v, p)))


def _segments(shape, defl=0.05):
    segs = []
    for e in shape.Edges:
        pts = e.discretize(Deflection=defl) if e.Length > 1e-6 else []
        segs += [((a.x, a.y), (b.x, b.y)) for a, b in zip(pts, pts[1:])]
    return segs


def projection(shape, view):
    s = shape.copy()
    s.transformShape(_matrix(view))
    r = TechDraw.project(s, V(0, 0, 1))
    return _segments(r[0]) + _segments(r[1]), _segments(r[2]) + _segments(r[3])


def section(shape, view, normal, d):
    """Faces of `shape` (or a list of shapes) cut by the plane normal . p = d, in `view` coordinates
    (normal must be the view's w axis)."""
    polys, outline = [], []
    for sh in shape if isinstance(shape, (list, tuple)) else [shape]:
        for solid in sh.Solids or [sh]:
            wires = solid.slice(V(*normal), d)
            if not wires:
                continue
            face = None
            for maker in ("Part::FaceMakerBullseye", "Part::FaceMakerCheese"):
                try:
                    face = Part.makeFace(wires, maker)
                    break
                except Exception:
                    pass
            m = _matrix(view)
            if face is not None:
                face.transformShape(m)
                pts, tris = face.tessellate(0.05)
                polys += [[(pts[i].x, pts[i].y) for i in t] for t in tris]
                wires = face.Wires
            else:
                wires = [w.transformGeometry(m) for w in wires]
            for w in wires:
                outline += _segments(w)
    return polys, outline


def dim(ax, p1, p2, off, text, vertical=False, color="#C0392B"):
    """Linear dimension between view points p1, p2; off moves the dimension line (perpendicular)."""
    (x1, y1), (x2, y2) = p1, p2
    kw = dict(color=color, lw=0.6)
    if not vertical:
        y = max(y1, y2) + off if off > 0 else min(y1, y2) + off
        ax.plot([x1, x1], [y1, y], **kw)
        ax.plot([x2, x2], [y2, y], **kw)
        ax.annotate("", (x1, y), (x2, y), arrowprops=dict(arrowstyle="<->", color=color, lw=0.6, shrinkA=0, shrinkB=0))
        ax.text((x1 + x2) / 2, y + (0.6 if off > 0 else -0.6), text, color=color, fontsize=7, ha="center",
                va="bottom" if off > 0 else "top")
    else:
        x = max(x1, x2) + off if off > 0 else min(x1, x2) + off
        ax.plot([x1, x], [y1, y1], **kw)
        ax.plot([x2, x], [y2, y2], **kw)
        ax.annotate("", (x, y1), (x, y2), arrowprops=dict(arrowstyle="<->", color=color, lw=0.6, shrinkA=0, shrinkB=0))
        ax.text(x + (0.6 if off > 0 else -0.6), (y1 + y2) / 2, text, color=color, fontsize=7, rotation=90,
                ha="left" if off > 0 else "right", va="center")


def note(ax, p, text, dx=6, dy=6, color="#1F5FA8"):
    ax.annotate(text, p, (p[0] + dx, p[1] + dy), fontsize=6.5, color=color,
                arrowprops=dict(arrowstyle="-", color=color, lw=0.5))


def draw_view(ax, shape, view, title=None, hidden=True, extra=None):
    vis, hid = projection(shape, view)
    if hidden and hid:
        ax.add_collection(LineCollection(hid, colors="#9AA0A6", linewidths=0.5, linestyles=(0, (2, 1.5))))
    ax.add_collection(LineCollection(vis, colors="#111111", linewidths=0.8))
    if extra:
        for s, col in extra:
            v2, _ = projection(s, view)
            ax.add_collection(LineCollection(v2, colors=col, linewidths=0.7))
    _finish(ax, vis + hid, title or TITLES[view])


def draw_section(ax, shape, view, normal, d, title, extra=None, overlay=None):
    polys, outline = section(shape, view, normal, d)
    ax.add_collection(PolyCollection(polys, facecolors="#F3D9B1", edgecolors="none", hatch="////"))
    ax.add_collection(LineCollection(outline, colors="#111111", linewidths=0.8))
    for s, col in overlay or []:  # parts behind the cut, as dashed outlines
        vis, _ = projection(s, view)
        ax.add_collection(LineCollection(vis, colors=col, linewidths=0.8, linestyles=(0, (3, 1.5))))
    if extra:
        for s, col in extra:
            p2, o2 = section(s, view, normal, d)
            ax.add_collection(PolyCollection(p2, facecolors=col, edgecolors="none", alpha=0.35))
            ax.add_collection(LineCollection(o2, colors=col, linewidths=0.6))
    _finish(ax, outline, title)


def _finish(ax, segs, title):
    if segs:
        a = np.array(segs).reshape(-1, 2)
        (x0, y0), (x1, y1) = a.min(0), a.max(0)
        m = 9
        ax.set_xlim(x0 - m, x1 + m)
        ax.set_ylim(y0 - m, y1 + m)
    ax.set_aspect("equal")
    ax.set_title(title, fontsize=9)
    ax.tick_params(labelsize=6)
    ax.grid(True, lw=0.2, alpha=0.5)


def render(parts, elev=28, azim=-58, size=900, ss=2):
    """Orthographic z-buffer render with flat shading and outline edges. Returns an RGB array."""
    e, a = np.radians(elev), np.radians(azim)
    d = np.array([np.cos(e) * np.cos(a), np.cos(e) * np.sin(a), np.sin(e)])   # towards the viewer
    u = np.array([-np.sin(a), np.cos(a), 0.0])
    v = np.cross(d, u)
    light = 0.55 * d + 0.45 * np.array([0.3, -0.4, 0.85])
    light /= np.linalg.norm(light)
    tris, cols = [], []
    for sh, col in parts:
        pts, tr = sh.tessellate(0.05)
        if not tr:
            continue
        p = np.array([(q.x, q.y, q.z) for q in pts])
        t = p[np.array(tr)]
        n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
        k = 0.35 + 0.65 * np.abs(n @ light)
        tris.append(t)
        cols.append(np.clip(np.array(matplotlib.colors.to_rgb(col)) * k[:, None], 0, 1))
    t = np.vstack(tris)
    c = np.vstack(cols)
    P = np.stack([t @ u, t @ v, t @ d], -1)
    lo, hi = P[..., :2].reshape(-1, 2).min(0), P[..., :2].reshape(-1, 2).max(0)
    W = size * ss
    sc = (W - 20 * ss) / (hi - lo).max()
    Hh = int((hi - lo)[1] * sc + 20 * ss)
    Wd = int((hi - lo)[0] * sc + 20 * ss)
    X = (P[..., 0] - lo[0]) * sc + 10 * ss
    Y = Hh - ((P[..., 1] - lo[1]) * sc + 10 * ss)
    Z = P[..., 2]
    zb = np.full((Hh, Wd), -np.inf)
    img = np.ones((Hh, Wd, 3))
    ids = np.full((Hh, Wd), -1)
    for i in range(len(t)):
        x, y, z = X[i], Y[i], Z[i]
        x0, x1 = max(int(x.min()), 0), min(int(x.max()) + 1, Wd)
        y0, y1 = max(int(y.min()), 0), min(int(y.max()) + 1, Hh)
        if x1 <= x0 or y1 <= y0:
            continue
        den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        w0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / den
        w1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / den
        w2 = 1 - w0 - w1
        m = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
        if not m.any():
            continue
        zz = w0 * z[0] + w1 * z[1] + w2 * z[2]
        sub = zb[y0:y1, x0:x1]
        m &= zz > sub
        sub[m] = zz[m]
        img[y0:y1, x0:x1][m] = c[i]
        ids[y0:y1, x0:x1][m] = i
    # outlines: depth jumps and crease edges between faces of different shade
    edge = np.zeros((Hh, Wd), bool)
    fin = np.isfinite(zb)
    zf = np.where(fin, zb, zb[fin].min() - 50)
    for dy, dx in ((0, 1), (1, 0)):
        dz = np.abs(zf - np.roll(np.roll(zf, dy, 0), dx, 1))
        dc = np.abs(img - np.roll(np.roll(img, dy, 0), dx, 1)).sum(-1)
        edge |= (dz > 0.6) | (dc > 0.12)
    img[edge & fin] *= 0.35
    img[edge & ~fin & np.roll(fin, 1, 0)] = 0.2
    if ss > 1:
        img = img[: Hh // ss * ss, : Wd // ss * ss].reshape(Hh // ss, ss, Wd // ss, ss, 3).mean((1, 3))
    return img


def draw_3d(ax, parts, elev=28, azim=-58, title="3D"):
    ax.imshow(render(parts, elev, azim))
    ax.set_axis_off()
    ax.set_title(title, fontsize=9)


def sheet(path, title, rows, cols, size=(16.5, 11)):
    fig = plt.figure(figsize=size, dpi=110)
    fig.suptitle(title, fontsize=12)
    return fig, lambda i, three_d=False: fig.add_subplot(rows, cols, i)


def save(fig, path):
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    fig.savefig(path)
    plt.close(fig)
