#!/usr/bin/env python3
"""Package Full and Video Lite, with matching symbols and build manifests."""
import argparse
import hashlib
from pathlib import Path
import shutil
import zipfile


VARIANTS = (("", "Full: all video/audio codecs, USB storage/audio, covers and visualizations"),
            ("-lite", "Video Lite: local H.264, subtitles and encryption; no audio/USB"))


def module_symbols(players: list[Path], dist: Path) -> list[Path]:
    """Verify bundled payloads against the exact images kept for debugging."""
    from bundle_codec_modules import CODECS as IDS, ENTRY, FOOTER, MAGIC

    required = set()
    names = {value: name for name, value in IDS.items()}
    for player in players:
        with player.open("rb") as stream:
            stream.seek(-FOOTER.size, 2)
            footer = stream.read(FOOTER.size)
            if footer[:8] != MAGIC:
                continue  # Single-codec and explicitly static builds.
            _, version, count, offset, size, total, reserved, digest = FOOTER.unpack(footer)
            if (version != 1 or not 0 < count <= len(IDS) or reserved or
                    total != player.stat().st_size or size != count * ENTRY.size or
                    offset + size + FOOTER.size != total):
                raise ValueError(f"Invalid codec bundle: {player}")
            stream.seek(offset)
            index = stream.read(size)
            if hashlib.sha256(index).digest() != digest:
                raise ValueError(f"Corrupt codec directory: {player}")
            seen = set()
            for i in range(count):
                codec, abi, start, length, _, _, flags, reserved, digest = ENTRY.unpack_from(index, i * ENTRY.size)
                if (codec not in names or codec in seen or abi != 1 or flags or reserved or
                        start > offset or length > offset - start):
                    raise ValueError(f"Invalid codec entry: {player}")
                seen.add(codec)
                image = dist / "modules" / (names[codec] + ".zehn")
                payload = image.read_bytes()
                stream.seek(start)
                if (len(payload) != length or hashlib.sha256(payload).digest() != digest or
                        stream.read(length) != payload):
                    raise ValueError(f"Codec symbols do not match {player}: {image}")
                required.add(names[codec])
    return [dist / "modules" / f"{codec}.{ext}"
            for codec in sorted(required) for ext in ("elf", "zehn")]


def package(dist: Path, output: Path, notes: Path | None = None) -> None:
    root = Path(__file__).resolve().parent.parent
    players = [dist / f"_nMedia{suffix}.tns" for suffix, _ in VARIANTS]
    manifests = [dist / f"nMedia{suffix}-build.json" for suffix,_ in VARIANTS]
    symbols = [dist / f"nMedia{suffix}.{ext}"
               for suffix, _ in VARIANTS for ext in ("elf", "zehn")]
    licenses = [root / "LICENSE", root / "THIRD_PARTY_NOTICES"]
    if dist.resolve() == output.resolve():
        raise ValueError("The release output must be separate from the build directory.")
    for path in players + symbols + licenses + manifests:
        if not path.is_file() or not path.stat().st_size:
            raise ValueError(f"Missing or empty release input: {path}")
    modules = module_symbols(players, dist)
    for path in modules:
        if not path.is_file() or not path.stat().st_size:
            raise ValueError(f"Missing or empty codec symbol input: {path}")

    output.mkdir(parents=True, exist_ok=True)
    for path in players + licenses + manifests:
        shutil.copy2(path, output / path.name)
    archive = output / "nMedia-symbols.zip"
    temporary = archive.with_suffix(".zip.tmp")
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path in symbols:
            zf.write(path, path.name)
        for path in modules:
            zf.write(path, "modules/" + path.name)
    temporary.replace(archive)

    notices = output / "nMedia-licenses.zip"
    vendor = root / "src/audio/vendor"
    license_sources = [*licenses,root/'src/player/vendor/stb_image.h',vendor/'dr_flac.h',vendor/'dr_wav.h',
        vendor/'minimp3-LICENSE',vendor/'minimp4/LICENSE',vendor/'libmad/COPYRIGHT',
        *sorted(vendor.rglob('COPYING'))]
    with zipfile.ZipFile(notices,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as zf:
        for path in license_sources:zf.write(path,path.relative_to(root).as_posix())
    assets = [output / path.name for path in players + licenses + manifests] + [archive,notices]
    checksums = [f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
                 for path in sorted(assets, key=lambda path: path.name)]
    (output / "SHA256SUMS.txt").write_text("".join(checksums), encoding="utf-8")
    rows = [f"| `{path.name}` | {codecs} | {path.stat().st_size / 1024:.1f} KiB |"
            for path, (_, codecs) in zip(players, VARIANTS)]
    if notes:
        notes.parent.mkdir(parents=True, exist_ok=True)
        notes.write_text(
            "Choose `_nMedia.tns` for the Full player, or `_nMedia-lite.tns` for local H.264 video without audio/USB.\n\n" +
            ("Each player is a single file. Full loads video and extra audio decoders on demand.\n\n" if modules else "") +
            "Normal playback writes no diagnostic logs. D explicitly enables recording; errors retain bounded reports.\n\n" +
            "| Player | Codecs | Size |\n| --- | --- | ---: |\n" + "\n".join(rows) +
            "\n\n`nMedia-symbols.zip` contains matching player/decoder ELF and Zehn files.\n"
            "Build JSON files list the exact features. `SHA256SUMS.txt` verifies downloads.\n"
            "`nMedia-licenses.zip` contains third-party license texts.\n",
            encoding="utf-8",
        )
    print("\n".join(rows))
    print(f"Release files: {output.resolve()}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dist", type=Path, default=Path("dist"))
    parser.add_argument("--output", type=Path, default=Path("release"))
    parser.add_argument("--notes", type=Path, help="Optional release-description file outside the assets")
    args = parser.parse_args()
    try:
        package(args.dist, args.output, args.notes)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
