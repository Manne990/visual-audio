#!/usr/bin/env python3
import csv
import argparse
import json
import math
import subprocess
from pathlib import Path


VIDEOS = [
    ("Untitled1", Path("/Users/manne990/Downloads/Untitled1.mp4")),
    ("Untitled2", Path("/Users/manne990/Downloads/Untitled2.mp4")),
    ("Untitled3", Path("/Users/manne990/Downloads/Untitled3.mp4")),
]

OUT_DIR = Path("build/video-analysis")
ANALYSIS_WIDTH = 120
ANALYSIS_HEIGHT = 90


def ffprobe(path):
    data = subprocess.check_output(
        [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height,avg_frame_rate,r_frame_rate,nb_frames,duration",
            "-of",
            "json",
            str(path),
        ],
        text=True,
    )
    stream = json.loads(data)["streams"][0]
    return stream


def parse_rate(rate):
    num, den = rate.split("/")
    num = float(num)
    den = float(den)
    return num / den if den else 0.0


def hue_bucket(r, g, b):
    hi = max(r, g, b)
    lo = min(r, g, b)
    if hi < 42:
        return "black"
    if hi - lo < 28:
        return "white"
    if r >= g and r >= b:
        if g > b + 24:
            return "yellow"
        if b > g + 18:
            return "magenta"
        return "red"
    if g >= r and g >= b:
        if b > r + 18:
            return "cyan"
        return "green"
    if r > g + 18:
        return "magenta"
    return "blue"


def frame_metrics(frame, prev):
    w = ANALYSIS_WIDTH
    h = ANALYSIS_HEIGHT
    n = w * h
    fg = 0
    bright = 0
    color_sum = 0
    lum_sum = 0
    diff_sum = 0
    center_fg = 0
    hue_counts = {
        "white": 0,
        "yellow": 0,
        "green": 0,
        "cyan": 0,
        "blue": 0,
        "magenta": 0,
        "red": 0,
        "black": 0,
    }
    grid = [0] * n
    lum = [0] * n
    cx = (w - 1) / 2.0
    cy = (h - 1) / 2.0
    radius2 = (min(w, h) * 0.32) ** 2

    for i in range(n):
        p = i * 3
        r = frame[p]
        g = frame[p + 1]
        b = frame[p + 2]
        y = (r * 30 + g * 59 + b * 11) // 100
        c = max(r, g, b) - min(r, g, b)
        lum[i] = y
        lum_sum += y
        color_sum += c
        if prev is not None:
            diff_sum += abs(r - prev[p]) + abs(g - prev[p + 1]) + abs(b - prev[p + 2])
        if y > 44:
            fg += 1
            grid[i] = 1
            x = i % w
            yy = i // w
            if ((x - cx) * (x - cx)) + ((yy - cy) * (yy - cy)) <= radius2:
                center_fg += 1
            if y > 115:
                bright += 1
            hue_counts[hue_bucket(r, g, b)] += 1

    vertical_edges = 0
    horizontal_edges = 0
    for y in range(1, h - 1):
        row = y * w
        for x in range(1, w - 1):
            i = row + x
            dx = abs(lum[i + 1] - lum[i - 1])
            dy = abs(lum[i + w] - lum[i - w])
            if dx > 32:
                vertical_edges += 1
            if dy > 32:
                horizontal_edges += 1

    components = 0
    visited = bytearray(n)
    for i in range(n):
        if not grid[i] or visited[i]:
            continue
        components += 1
        stack = [i]
        visited[i] = 1
        while stack:
            cur = stack.pop()
            x = cur % w
            for nb in (cur - 1, cur + 1, cur - w, cur + w):
                if nb < 0 or nb >= n or visited[nb] or not grid[nb]:
                    continue
                if (nb == cur - 1 and x == 0) or (nb == cur + 1 and x == w - 1):
                    continue
                visited[nb] = 1
                stack.append(nb)

    mirror_diff = 0
    mirror_count = 0
    for y in range(h):
        for x in range(w // 2):
            mirror_diff += abs(lum[y * w + x] - lum[y * w + (w - 1 - x)])
            mirror_count += 1

    if fg:
        dominant = max((v, k) for k, v in hue_counts.items() if k != "black")[1]
    else:
        dominant = "black"

    density = fg / float(n)
    bright_density = bright / float(n)
    colorfulness = color_sum / float(n)
    motion = diff_sum / float(n * 3) if prev is not None else 0.0
    edge_total = vertical_edges + horizontal_edges
    vertical_ratio = vertical_edges / float(horizontal_edges + 1)
    horizontal_ratio = horizontal_edges / float(vertical_edges + 1)
    center_ratio = center_fg / float(fg or 1)
    symmetry = 1.0 - (mirror_diff / float((mirror_count or 1) * 255))
    component_density = components / float(fg or 1)

    mode = classify_mode(
        density,
        bright_density,
        colorfulness,
        motion,
        vertical_ratio,
        horizontal_ratio,
        center_ratio,
        symmetry,
        component_density,
        edge_total,
        dominant,
    )

    return {
        "density": density,
        "bright_density": bright_density,
        "mean_luma": lum_sum / float(n),
        "colorfulness": colorfulness,
        "motion": motion,
        "vertical_ratio": vertical_ratio,
        "horizontal_ratio": horizontal_ratio,
        "center_ratio": center_ratio,
        "symmetry": symmetry,
        "components": components,
        "component_density": component_density,
        "dominant": dominant,
        "mode": mode,
    }


def classify_mode(
    density,
    bright_density,
    colorfulness,
    motion,
    vertical_ratio,
    horizontal_ratio,
    center_ratio,
    symmetry,
    component_density,
    edge_total,
    dominant,
):
    if density > 0.50 and horizontal_ratio > 1.18:
        return "raster-noise-field"
    if density > 0.43:
        return "dense-color-noise"
    if vertical_ratio > 1.45 and density > 0.14:
        return "vertical-waterfall"
    if horizontal_ratio > 1.45 and density > 0.12:
        return "horizontal-scanlines"
    if density < 0.20 and edge_total > 500 and bright_density > 0.015:
        return "wireframe-vectors"
    if component_density > 0.045 and density > 0.08:
        return "particle-field"
    if symmetry > 0.72 and center_ratio > 0.28 and density > 0.05:
        return "radial-kaleidoscope"
    if density < 0.09 and motion < 5.0:
        return "dark-hold"
    if dominant in ("cyan", "blue", "green") and density > 0.18:
        return "cool-texture-field"
    return "mixed"


def merge_segments(rows, fps, min_frames=8):
    segments = []
    start = 0
    mode = rows[0]["mode"] if rows else None
    for i, row in enumerate(rows[1:], 1):
        if row["mode"] != mode:
            segments.append((start, i - 1, mode))
            start = i
            mode = row["mode"]
    if rows:
        segments.append((start, len(rows) - 1, mode))

    merged = []
    for start, end, mode in segments:
        if merged and (end - start + 1) < min_frames:
            old_start, _old_end, old_mode = merged[-1]
            merged[-1] = (old_start, end, old_mode)
        else:
            merged.append((start, end, mode))

    out = []
    for start, end, mode in merged:
        seg_rows = rows[start : end + 1]
        out.append(
            {
                "start_frame": start,
                "end_frame": end,
                "start_s": start / fps if fps else 0.0,
                "end_s": end / fps if fps else 0.0,
                "mode": mode,
                "frames": end - start + 1,
                "mean_density": mean(r["density"] for r in seg_rows),
                "mean_motion": mean(r["motion"] for r in seg_rows),
                "dominant": most_common(r["dominant"] for r in seg_rows),
            }
        )
    return out


def mean(values):
    values = list(values)
    return sum(values) / float(len(values) or 1)


def most_common(values):
    counts = {}
    for value in values:
        counts[value] = counts.get(value, 0) + 1
    return max((count, value) for value, count in counts.items())[1] if counts else ""


def analyze_video(name, path):
    stream = ffprobe(path)
    fps = parse_rate(stream.get("avg_frame_rate") or stream.get("r_frame_rate") or "0/1")
    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(path),
        "-vsync",
        "0",
        "-vf",
        "scale=%d:%d" % (ANALYSIS_WIDTH, ANALYSIS_HEIGHT),
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb24",
        "-",
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    frame_size = ANALYSIS_WIDTH * ANALYSIS_HEIGHT * 3
    prev = None
    rows = []
    index = 0
    while True:
        frame = proc.stdout.read(frame_size)
        if len(frame) != frame_size:
            break
        metrics = frame_metrics(frame, prev)
        metrics["frame"] = index
        metrics["time_s"] = index / fps if fps else 0.0
        rows.append(metrics)
        prev = frame
        index += 1
    proc.wait()

    csv_path = OUT_DIR / ("%s-frame-analysis.csv" % name.lower())
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "frame",
                "time_s",
                "mode",
                "dominant",
                "density",
                "bright_density",
                "mean_luma",
                "colorfulness",
                "motion",
                "vertical_ratio",
                "horizontal_ratio",
                "center_ratio",
                "symmetry",
                "components",
                "component_density",
            ],
        )
        writer.writeheader()
        for row in rows:
            writer.writerow(row)

    segments = merge_segments(rows, fps)
    segment_path = OUT_DIR / ("%s-segments.csv" % name.lower())
    with segment_path.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "start_frame",
                "end_frame",
                "start_s",
                "end_s",
                "mode",
                "frames",
                "mean_density",
                "mean_motion",
                "dominant",
            ],
        )
        writer.writeheader()
        for segment in segments:
            writer.writerow(segment)

    summary = {
        "name": name,
        "path": str(path),
        "width": stream.get("width"),
        "height": stream.get("height"),
        "fps": fps,
        "frames": len(rows),
        "duration_s": len(rows) / fps if fps else 0.0,
        "mean_density": mean(r["density"] for r in rows),
        "mean_motion": mean(r["motion"] for r in rows),
        "dominant_modes": sorted(
            mode_counts(rows).items(), key=lambda item: (-item[1], item[0])
        ),
        "dominant_colours": sorted(
            colour_counts(rows).items(), key=lambda item: (-item[1], item[0])
        ),
        "csv": str(csv_path),
        "segments_csv": str(segment_path),
    }
    return summary


def mode_counts(rows):
    counts = {}
    for row in rows:
        counts[row["mode"]] = counts.get(row["mode"], 0) + 1
    return counts


def colour_counts(rows):
    counts = {}
    for row in rows:
        counts[row["dominant"]] = counts.get(row["dominant"], 0) + 1
    return counts


def write_summary(summaries):
    path = OUT_DIR / "summary.json"
    path.write_text(json.dumps(summaries, indent=2), encoding="utf-8")

    md = OUT_DIR / "summary.md"
    lines = [
        "# MindLight video analysis summary",
        "",
        "Analysis resolution: %dx%d. Every decoded frame is included in the CSV files." % (
            ANALYSIS_WIDTH,
            ANALYSIS_HEIGHT,
        ),
        "",
    ]
    for summary in summaries:
        lines.append("## %s" % summary["name"])
        lines.append("")
        lines.append(
            "- %d frames, %.2fs, %.2f fps, source %sx%s"
            % (
                summary["frames"],
                summary["duration_s"],
                summary["fps"],
                summary["width"],
                summary["height"],
            )
        )
        lines.append("- Mean foreground density: %.3f" % summary["mean_density"])
        lines.append("- Mean frame-to-frame motion: %.2f" % summary["mean_motion"])
        lines.append("- Modes:")
        for mode, count in summary["dominant_modes"]:
            lines.append("  - %s: %d frames" % (mode, count))
        lines.append("- Dominant colours:")
        for colour, count in summary["dominant_colours"]:
            lines.append("  - %s: %d frames" % (colour, count))
        lines.append("- Per-frame CSV: `%s`" % summary["csv"])
        lines.append("- Merged segments CSV: `%s`" % summary["segments_csv"])
        lines.append("")
    md.write_text("\n".join(lines), encoding="utf-8")


def main():
    global OUT_DIR

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-dir",
        default=str(OUT_DIR),
        help="directory for CSV and summary outputs",
    )
    parser.add_argument(
        "videos",
        nargs="*",
        help="video files to analyze; defaults to the three Untitled*.mp4 captures",
    )
    args = parser.parse_args()

    OUT_DIR = Path(args.output_dir)
    videos = []
    if args.videos:
        for path in args.videos:
            p = Path(path)
            videos.append((p.stem, p))
    else:
        videos = VIDEOS

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    summaries = []
    for name, path in videos:
        summaries.append(analyze_video(name, path))
    write_summary(summaries)


if __name__ == "__main__":
    main()
