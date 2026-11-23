"""Prepare a clean, checksum-verified Mesa 26.2.6 source tree for benchmarks."""
import hashlib
import os
import tarfile
from pathlib import Path, PurePosixPath

ARCHIVE_SHA256 = "b9436b3e1a8be76fbd4823dc884f90596ab7c6ac5e1867a305e66ffccfb31a92"
PRISTINE_HASHES = {
    "src/gallium/drivers/zink/zink_draw.cpp": "d2e9eb00aac90bef60193494c194e982dc1ec90a2906f7c8922e2b399fdb6079",
    "src/gallium/drivers/zink/zink_kopper.c": "54574a34d03d0f68cc9de0f55649cc2230ad2d386d8eb6b9cf14500703862625",
    "src/gallium/drivers/zink/meson.build": "5713293c8f9ea0e6846a1d0d301e9980cf5d4208cc91a5e81194371210999d44",
}


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify(source: Path) -> None:
    if (source / "VERSION").read_text().strip() != "26.2.6":
        raise RuntimeError(f"Unexpected Mesa source version in {source}")
    zink = source / "src/gallium/drivers/zink"
    if any((zink / name).exists() for name in ("vulkanizer-patch.json", "vulkanizer_evidence.c", "vulkanizer_evidence.h")):
        raise RuntimeError("Clean Mesa tree contains Vulkanizer instrumentation")
    for relative, expected in PRISTINE_HASHES.items():
        actual = sha256(source / relative)
        if actual != expected:
            raise RuntimeError(f"Not pristine Mesa source: {relative} hash={actual}")


def prepare(project_root: Path) -> Path:
    tools = project_root / ".tools"
    archive = tools / "mesa-26.2.6.tar.xz"
    if sha256(archive) != ARCHIVE_SHA256:
        raise RuntimeError("Official Mesa archive checksum mismatch")
    destination = tools / "mesa-26.2.6-pristine"
    if destination.exists():
        verify(destination)
        return destination

    temp = tools / f"mesa-pristine-extract-{os.getpid()}"
    if temp.exists():
        raise RuntimeError(f"Refusing to reuse extraction directory: {temp}")
    temp.mkdir()
    with tarfile.open(archive, "r:xz") as tar:
        for member in tar.getmembers():
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts or (path.parts and ":" in path.parts[0]):
                raise RuntimeError(f"Unsafe path in source archive: {member.name}")
        tar.extractall(temp, filter="data")
    source = temp / "mesa-26.2.6"
    verify(source)
    # Same-volume rename is atomic and refuses to overwrite an existing tree.
    source.rename(destination)
    verify(destination)
    return destination


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    print(prepare(root))
