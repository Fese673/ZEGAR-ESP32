from __future__ import annotations

import re
from pathlib import Path

try:
    Import("env")  # type: ignore[name-defined]
    ROOT = Path(env["PROJECT_DIR"])
except Exception:
    ROOT = Path(__file__).resolve().parents[1] if "__file__" in globals() else Path.cwd()

SOURCE_DIR = ROOT / "DZWIEKI DO WDROZENIA"
OUTPUT_FILE = ROOT / "src" / "AlarmMelodies.generated.inc"

NOTE_DEFINE_RE = re.compile(r"^\s*#define\s+(NOTE_[A-Z0-9]+)\s+(\d+)\s*$", re.MULTILINE)
TEMPO_RE = re.compile(r"int\s+tempo\s*=\s*(\d+)\s*;", re.MULTILINE)
MELODY_RE = re.compile(r"melody\s*\[\s*\]\s*(?:PROGMEM\s*)?=\s*\{(.*?)\};", re.DOTALL | re.MULTILINE)
COMMENT_STRIP_RE = re.compile(r"//.*?$", re.MULTILINE)
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)


def c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def extract_title(text: str, fallback: str) -> str:
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith("/*") or line.startswith("*/"):
            continue
        line = line.lstrip("*").strip()
        if not line:
            continue
        lower = line.lower()
        if lower.startswith("connect ") or lower.startswith("more songs"):
            continue
        if lower.startswith("score available") or lower.startswith("score from"):
            continue
        if lower.startswith("robson couto") or lower.startswith("copyright"):
            continue
        if lower.startswith("change this") or lower.startswith("notes of the moledy"):
            continue
        if "piezo" in lower and "connect" in lower:
            continue
        return line
    return fallback


def parse_song(path: Path) -> dict[str, object]:
    text = path.read_text(encoding="utf-8-sig")
    note_map = {name: int(value) for name, value in NOTE_DEFINE_RE.findall(text)}

    tempo_match = TEMPO_RE.search(text)
    if not tempo_match:
        raise ValueError(f"tempo not found in {path}")
    tempo = int(tempo_match.group(1))

    melody_match = MELODY_RE.search(text)
    if not melody_match:
        raise ValueError(f"melody array not found in {path}")
    melody_block = melody_match.group(1)
    melody_block = BLOCK_COMMENT_RE.sub("", melody_block)
    lines = [COMMENT_STRIP_RE.sub("", line) for line in melody_block.splitlines()]
    cleaned = ",".join(lines)
    tokens = [token.strip() for token in cleaned.split(",") if token.strip()]
    if len(tokens) % 2 != 0:
        raise ValueError(f"odd number of melody tokens in {path}")

    notes: list[int] = []
    divs: list[int] = []
    for index in range(0, len(tokens), 2):
        note_token = tokens[index]
        div_token = tokens[index + 1]

        if note_token == "REST":
            note_value = 0
        elif note_token in note_map:
            note_value = note_map[note_token]
        else:
            note_value = int(note_token)

        notes.append(note_value)
        divs.append(int(div_token))

    title = extract_title(text, path.parent.name)
    return {
        "name": title,
        "stem": re.sub(r"[^A-Za-z0-9_]+", "_", path.stem),
        "notes": notes,
        "divs": divs,
        "tempo": tempo,
    }


def emit_array(values: list[int], value_type: str, name: str) -> str:
    pieces: list[str] = []
    row: list[str] = []
    for index, value in enumerate(values, 1):
        row.append(str(value))
        if index % 12 == 0:
            pieces.append("  " + ", ".join(row) + ",")
            row = []
    if row:
        pieces.append("  " + ", ".join(row) + ",")
    if not pieces:
        pieces.append("  0,")
    return f"static const {value_type} {name}[] = {{\n" + "\n".join(pieces) + "\n};\n"


def main() -> None:
    songs: list[dict[str, object]] = []
    for path in sorted(SOURCE_DIR.rglob("*.ino")):
        songs.append(parse_song(path))

    if not songs:
        raise SystemExit("No songs found")

    out: list[str] = []
    out.append("// Auto-generated from DZWIEKI DO WDROZENIA/*.ino. Do not edit manually.\n\n")
    out.append(f"const uint8_t kCount = {len(songs)};\n\n")
    track_entries: list[str] = []
    for index, song in enumerate(songs):
        stem = song["stem"]
        notes = song["notes"]  # type: ignore[assignment]
        divs = song["divs"]  # type: ignore[assignment]
        title = song["name"]
        tempo = song["tempo"]

        out.append(emit_array(notes, "uint16_t", f"k{stem}Notes"))
        out.append(emit_array(divs, "int16_t", f"k{stem}Divs"))
        out.append("\n")
        track_entries.append(
            f'  {{"{c_string(stem)}", "{c_string(str(title))}", k{stem}Notes, k{stem}Divs, (uint16_t)(sizeof(k{stem}Notes) / sizeof(k{stem}Notes[0])), {tempo}}},'
        )

    out.append("static const SongTrack kTracks[] = {\n")
    out.append("\n".join(track_entries))
    out.append("\n};\n\n")
    OUTPUT_FILE.write_text("".join(out), encoding="utf-8")


if __name__ == "__main__":
    main()
