#!/usr/bin/env python3
from pathlib import Path
from collections import deque

from PIL import Image


SOURCE = Path("new_src/logo-title.png")
DESTS = (
    Path("data/tyrian2000/logo-gravitium-war.png"),
    Path("data/tyrian2000/logo-title.png"),
)
MAX_W = 160
MAX_H = 21
ALPHA_THRESHOLD = 200
BOTTOM_FRACTION = 0.7
PADDING = 6


def _subtitle_bbox(img: Image.Image) -> tuple[int, int, int, int] | None:
    alpha = img.getchannel("A")
    w, h = img.size
    pix = alpha.load()
    visited = [[False] * w for _ in range(h)]
    components: list[tuple[int, int, int, int, int]] = []

    for y in range(h):
        for x in range(w):
            if visited[y][x] or pix[x, y] < ALPHA_THRESHOLD:
                continue

            q = deque([(x, y)])
            visited[y][x] = True
            min_x = max_x = x
            min_y = max_y = y
            area = 0

            while q:
                cx, cy = q.popleft()
                area += 1
                min_x = min(min_x, cx)
                max_x = max(max_x, cx)
                min_y = min(min_y, cy)
                max_y = max(max_y, cy)

                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < w and 0 <= ny < h and not visited[ny][nx] and pix[nx, ny] >= ALPHA_THRESHOLD:
                        visited[ny][nx] = True
                        q.append((nx, ny))

            components.append((area, min_x, min_y, max_x, max_y))

    subtitle_components = [c for c in components if c[2] >= int(h * BOTTOM_FRACTION)]
    if not subtitle_components:
        return None

    min_x = min(c[1] for c in subtitle_components)
    min_y = min(c[2] for c in subtitle_components)
    max_x = max(c[3] for c in subtitle_components)
    max_y = max(c[4] for c in subtitle_components)

    return (
        max(0, min_x - PADDING),
        max(0, min_y - PADDING),
        min(w, max_x + 1 + PADDING),
        min(h, max_y + 1 + PADDING),
    )


def main() -> None:
    img = Image.open(SOURCE).convert("RGBA")

    subtitle_bbox = _subtitle_bbox(img)
    if subtitle_bbox is not None:
        img = img.crop(subtitle_bbox)

    bbox = img.getbbox()
    if bbox is not None:
        img = img.crop(bbox)

    w, h = img.size
    scale = min(MAX_W / w, MAX_H / h, 1.0)
    out_w = max(1, int(round(w * scale)))
    out_h = max(1, int(round(h * scale)))

    if (out_w, out_h) != (w, h):
        img = img.resize((out_w, out_h), Image.Resampling.LANCZOS)

    for dest in DESTS:
        dest.parent.mkdir(parents=True, exist_ok=True)
        img.save(dest, format="PNG")

    print(f"{img.size[0]}x{img.size[1]}")


if __name__ == "__main__":
    main()
