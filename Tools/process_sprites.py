"""Chroma-key the neon-pink sprite sheets, slice into individual sprites,
write PNGs + manifest JSON into Content/SpriteSrc/, plus labeled contact sheets."""
import json, os, sys
from collections import deque
import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGES = os.path.join(ROOT, "Images")
OUT = os.path.join(ROOT, "Content", "SpriteSrc")
CONTACT = os.path.join(ROOT, "Docs", "ContactSheets")
os.makedirs(OUT, exist_ok=True)
os.makedirs(CONTACT, exist_ok=True)

SHEETS = [
    ("net", "ChatGPT Image Jul 10, 2026, 01_11_34 PM (1).png"),
    ("res", "ChatGPT Image Jul 10, 2026, 01_11_35 PM (2).png"),
    ("veh", "ChatGPT Image Jul 10, 2026, 01_11_35 PM (3).png"),
    ("mac", "ChatGPT Image Jul 10, 2026, 01_11_35 PM (4).png"),
    ("ico", "ChatGPT Image Jul 10, 2026, 01_11_36 PM (5).png"),
]

def chroma_alpha(rgb, key):
    """Smooth alpha from distance to key color."""
    d = np.sqrt(((rgb.astype(np.float32) - key.astype(np.float32)) ** 2).sum(axis=-1))
    t0, t1 = 40.0, 110.0
    a = np.clip((d - t0) / (t1 - t0), 0.0, 1.0)
    return a

def despill(rgb, alpha):
    """Remove magenta spill: magenta has R and B >> G."""
    r = rgb[..., 0].astype(np.float32)
    g = rgb[..., 1].astype(np.float32)
    b = rgb[..., 2].astype(np.float32)
    spill = np.clip(np.minimum(r, b) - g, 0, None)
    # only where partially transparent or spill-heavy
    w = np.clip((1.0 - alpha) * 1.5 + np.clip(spill / 80.0 - 0.4, 0, 1), 0, 1)
    r2 = r - spill * w
    b2 = b - spill * w
    out = rgb.copy().astype(np.float32)
    out[..., 0] = np.clip(r2, 0, 255)
    out[..., 2] = np.clip(b2, 0, 255)
    return out.astype(np.uint8)

def components(mask):
    """Connected components (8-conn) on a downsampled boolean mask; returns boxes in mask coords."""
    h, w = mask.shape
    lab = np.zeros((h, w), np.int32)
    boxes = []
    cur = 0
    for sy in range(h):
        xs = np.nonzero(mask[sy] & (lab[sy] == 0))[0]
        for sx in xs:
            if lab[sy, sx]:
                continue
            cur += 1
            q = deque([(sy, sx)])
            lab[sy, sx] = cur
            x0 = x1 = sx; y0 = y1 = sy; n = 0
            while q:
                y, x = q.popleft(); n += 1
                if x < x0: x0 = x
                if x > x1: x1 = x
                if y < y0: y0 = y
                if y > y1: y1 = y
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        ny, nx = y + dy, x + dx
                        if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not lab[ny, nx]:
                            lab[ny, nx] = cur
                            q.append((ny, nx))
            boxes.append([x0, y0, x1, y1, n])
    return boxes

def merge_boxes(boxes, pad):
    """Merge boxes whose padded rects intersect (rotors/steam detached from bodies)."""
    boxes = [list(b) for b in boxes]
    changed = True
    while changed:
        changed = False
        out = []
        while boxes:
            a = boxes.pop()
            merged = False
            for b in out:
                if not (a[2] + pad < b[0] or b[2] + pad < a[0] or a[3] + pad < b[1] or b[3] + pad < a[1]):
                    b[0] = min(a[0], b[0]); b[1] = min(a[1], b[1])
                    b[2] = max(a[2], b[2]); b[3] = max(a[3], b[3]); b[4] += a[4]
                    merged = True; changed = True
                    break
            if not merged:
                out.append(a)
        boxes = out
    return boxes

manifest = {}
DS = 4  # downsample factor for labeling
for prefix, fname in SHEETS:
    path = os.path.join(IMAGES, fname)
    img = Image.open(path).convert("RGB")
    rgb = np.asarray(img)
    H, W, _ = rgb.shape
    key = np.median(np.concatenate([rgb[0:8].reshape(-1, 3), rgb[-8:].reshape(-1, 3),
                                    rgb[:, 0:8].reshape(-1, 3), rgb[:, -8:].reshape(-1, 3)]), axis=0)
    alpha = chroma_alpha(rgb, key)
    mask = alpha > 0.5
    small = mask[: H // DS * DS, : W // DS * DS].reshape(H // DS, DS, W // DS, DS).any(axis=(1, 3))
    boxes = components(small)
    boxes = [b for b in boxes if b[4] >= 12]  # drop specks (area in ds-pixels)
    boxes = merge_boxes(boxes, pad=3)  # 3 ds-px = 12 px gap merge
    # scale up + pad
    rects = []
    for x0, y0, x1, y1, n in boxes:
        rx0 = max(0, x0 * DS - 4); ry0 = max(0, y0 * DS - 4)
        rx1 = min(W, (x1 + 1) * DS + 4); ry1 = min(H, (y1 + 1) * DS + 4)
        if rx1 - rx0 < 24 or ry1 - ry0 < 24:
            continue
        rects.append((rx0, ry0, rx1, ry1))
    # sort into rows by centroid
    rects.sort(key=lambda r: (r[1] + r[3]) / 2)
    rows = []
    for r in rects:
        cy = (r[1] + r[3]) / 2
        placed = False
        for row in rows:
            if abs(cy - row["cy"]) < (r[3] - r[1]) * 0.6:
                row["items"].append(r)
                row["cy"] = sum((i[1] + i[3]) / 2 for i in row["items"]) / len(row["items"])
                placed = True
                break
        if not placed:
            rows.append({"cy": cy, "items": [r]})
    rows.sort(key=lambda r: r["cy"])
    entries = []
    idx = 0
    rgb_d = despill(rgb, alpha)
    rgba = np.dstack([rgb_d, (alpha * 255).astype(np.uint8)])
    for ri, row in enumerate(rows):
        row["items"].sort(key=lambda r: r[0])
        for ci, (x0, y0, x1, y1) in enumerate(row["items"]):
            crop = rgba[y0:y1, x0:x1]
            name = f"{prefix}_{ri:02d}_{ci:02d}"
            Image.fromarray(crop, "RGBA").save(os.path.join(OUT, name + ".png"))
            entries.append({"id": name, "row": ri, "col": ci,
                            "w": int(x1 - x0), "h": int(y1 - y0)})
            idx += 1
    manifest[prefix] = {"sheet": fname, "count": idx, "sprites": entries}
    # contact sheet with labels
    cs = Image.fromarray(np.dstack([rgb_d, np.full((H, W), 255, np.uint8)]), "RGBA").convert("RGB")
    dr = ImageDraw.Draw(cs)
    for e in entries:
        # find rect again from rows
        pass
    ro = 0
    for ri, row in enumerate(rows):
        for ci, (x0, y0, x1, y1) in enumerate(row["items"]):
            dr.rectangle([x0, y0, x1 - 1, y1 - 1], outline=(0, 255, 0), width=2)
            dr.text((x0 + 3, y0 + 2), f"{ri},{ci}", fill=(255, 255, 0))
    cs.save(os.path.join(CONTACT, f"contact_{prefix}.png"))
    print(f"{prefix}: {idx} sprites, {len(rows)} rows")

with open(os.path.join(OUT, "sprites.json"), "w") as f:
    json.dump(manifest, f, indent=1)
print("done")
