#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "pyarrow",
# ]
# ///

import argparse
import time
from pathlib import Path

import pyarrow.feather as feather
import pyarrow.json as paj


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert Databento NDJSON .mbo.json files to Feather."
    )
    parser.add_argument("input", type=Path, help="Input .mbo.json file or directory")
    parser.add_argument(
        "-o",
        "--output-dir",
        type=Path,
        default=Path("."),
        help="Directory for generated .feather files",
    )
    parser.add_argument(
        "--benchmark",
        action="store_true",
        help="Print JSON and Feather read timings",
    )
    return parser.parse_args()


def input_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    if path.is_dir():
        return sorted(path.glob("*.mbo.json"))
    raise FileNotFoundError(f"input path does not exist: {path}")


def output_path(input_file: Path, output_dir: Path) -> Path:
    return output_dir / input_file.with_suffix(".feather").name


def convert_file(input_file: Path, output_file: Path, benchmark: bool) -> None:
    started = time.perf_counter()
    table = paj.read_json(input_file)
    json_seconds = time.perf_counter() - started

    output_file.parent.mkdir(parents=True, exist_ok=True)
    feather.write_feather(table, output_file, compression="lz4")

    message = (
        f"{input_file} -> {output_file}: {table.num_rows} rows, "
        f"json_read={json_seconds:.6f}s"
    )

    if benchmark:
        started = time.perf_counter()
        feather.read_table(output_file)
        feather_seconds = time.perf_counter() - started
        message += f", feather_read={feather_seconds:.6f}s"

    print(message)


def main() -> int:
    args = parse_args()
    files = input_files(args.input)
    if not files:
        raise FileNotFoundError(f"no *.mbo.json files found in {args.input}")

    for file in files:
        convert_file(file, output_path(file, args.output_dir), args.benchmark)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
