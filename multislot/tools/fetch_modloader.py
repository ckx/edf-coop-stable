"""Puts the official EDFModLoader v1.0.10 winmm.dll (BlueAmulet, MIT) at third_party/EDFModLoader/winmm.dll.

The build turns it into the race-fixed loader (fix_winmm_proxy.py) that the package ships; the original is not kept
in the repository. Both the release zip and the DLL in it are checked by SHA-256. HTTPS_PROXY is honoured.
"""
import hashlib
import io
import pathlib
import sys
import urllib.request
import zipfile

ZIP_URL = 'https://github.com/BlueAmulet/EDFModLoader/releases/download/v1.0.10/EDFModLoader.zip'
ZIP_SHA256 = '850be7567954ee319dc2b65e9174d1eec70791ba9a43d2508a40b22b98bb4b08'
DLL_SHA256 = 'b80e4da6ae7264f0e9774c992de3c5f9b6e9bc9422146adeeb5bb2bd681e112e'
TARGET = pathlib.Path(__file__).resolve().parents[1] / 'third_party' / 'EDFModLoader' / 'winmm.dll'


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main():
    if TARGET.exists() and sha256(TARGET.read_bytes()) == DLL_SHA256:
        print('EDFModLoader winmm.dll already in place')
        return 0
    with urllib.request.urlopen(ZIP_URL, timeout=60) as response:
        archive = response.read()
    if sha256(archive) != ZIP_SHA256:
        sys.exit(f'EDFModLoader.zip hash {sha256(archive)} does not match the pinned one')
    with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
        dll = zipped.read('winmm.dll')
    if sha256(dll) != DLL_SHA256:
        sys.exit(f'winmm.dll hash {sha256(dll)} does not match the pinned one')
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    TARGET.write_bytes(dll)
    print(f'EDFModLoader v1.0.10 winmm.dll written to {TARGET}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
