#!/usr/bin/env python3
"""
Setup script for VideoLuminanceAnalyzer dependencies.
Downloads and extracts libVLC Windows SDK to a local directory.
Run this script before building the project if you don't have libVLC installed.
"""

import os
import sys
import urllib.request
import zipfile
import shutil
from pathlib import Path

VLC_VERSION = "3.0.21"
VLC_URL = f"https://download.videolan.org/pub/videolan/vlc/{VLC_VERSION}/win64/vlc-{VLC_VERSION}-win64.zip"
DOWNLOAD_NAME = f"vlc-{VLC_VERSION}-win64.zip"
EXTRACT_DIR = Path(__file__).parent / "vlc_sdk"

def download_file(url: str, dest: Path, chunk_size: int = 8192):
    print(f"Downloading {url} ...")
    print(f"  -> {dest}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url) as response, open(dest, "wb") as out_file:
        total = int(response.headers.get("Content-Length", 0))
        downloaded = 0
        while True:
            chunk = response.read(chunk_size)
            if not chunk:
                break
            out_file.write(chunk)
            downloaded += len(chunk)
            if total > 0:
                pct = downloaded * 100 // total
                sys.stdout.write(f"\r  Progress: {pct}% ({downloaded}/{total} bytes)")
                sys.stdout.flush()
    print("\nDownload complete.")

def extract_zip(zip_path: Path, dest_dir: Path):
    print(f"Extracting {zip_path} ...")
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(dest_dir)
    print("Extraction complete.")

def find_vlc_sdk_root(base: Path) -> Path:
    """Find the actual VLC root inside extracted folder."""
    for entry in base.iterdir():
        if entry.is_dir() and (entry / "sdk").exists():
            return entry
    return base

def main():
    script_dir = Path(__file__).parent
    zip_path = script_dir / DOWNLOAD_NAME

    # Clean old extraction
    if EXTRACT_DIR.exists():
        print(f"Removing old extraction: {EXTRACT_DIR}")
        shutil.rmtree(EXTRACT_DIR)

    # Download if needed
    if not zip_path.exists():
        try:
            download_file(VLC_URL, zip_path)
        except Exception as e:
            print(f"ERROR: Failed to download VLC SDK: {e}")
            print("You can manually download it from:")
            print(f"  {VLC_URL}")
            sys.exit(1)
    else:
        print(f"Using existing download: {zip_path}")

    # Extract
    try:
        extract_zip(zip_path, EXTRACT_DIR)
    except Exception as e:
        print(f"ERROR: Failed to extract: {e}")
        sys.exit(1)

    vlc_root = find_vlc_sdk_root(EXTRACT_DIR)
    sdk_include = vlc_root / "sdk" / "include"
    sdk_lib = vlc_root / "sdk" / "lib"

    print("\n" + "=" * 60)
    print("VLC SDK prepared successfully!")
    print("=" * 60)
    print(f"VLC root: {vlc_root.resolve()}")
    print(f"Headers:  {sdk_include.resolve()}")
    print(f"Libs:     {sdk_lib.resolve()}")
    print("\nNext steps:")
    print("1) Install Qt 6 (with Qt Charts) from https://www.qt.io/download")
    print("   Or via aqtinstall:")
    print("     pip install aqtinstall")
    print("     aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 --modules qtcharts")
    print("2) Set environment variables before building:")
    print(f"     set VLC_SDK_PATH={vlc_root.resolve()}")
    print("     set QT6_DIR=C:\\Qt\\6.8.0\\msvc2022_64")
    print("3) Build with CMake:")
    print("     cmake -B build -S .")
    print("     cmake --build build --config Release")
    print("4) Copy VLC DLLs to the output directory (e.g., build\\Release\\):")
    print(f"     copy {vlc_root.resolve()}\\libvlc.dll build\\Release\\")
    print(f"     copy {vlc_root.resolve()}\\libvlccore.dll build\\Release\\")
    print("=" * 60)

if __name__ == "__main__":
    main()
