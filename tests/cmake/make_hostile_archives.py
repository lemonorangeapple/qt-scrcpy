"""Writes archives with entries that try to escape the extraction directory into <outdir>.

Only used by test_bundle_module.cmake. Every escape target is a path under <outdir> named
escape-*, so a successful escape is visible without touching anything else.
"""
import io
import os
import sys
import tarfile
import zipfile

out = os.path.abspath(sys.argv[1])
escape_dir = os.path.join(out, "escape-dir")
os.makedirs(escape_dir, exist_ok=True)


def regular(name, data=b"x"):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = 0o644
    return info, io.BytesIO(data)


def link(name, target, hard=False):
    info = tarfile.TarInfo(name)
    info.type = tarfile.LNKTYPE if hard else tarfile.SYMTYPE
    info.linkname = target
    return info, None


def write_tar(filename, entries):
    with tarfile.open(os.path.join(out, filename), "w") as archive:
        for info, data in entries:
            archive.addfile(info, data)


write_tar("dotdot-first.tar", [regular("../escape-dotdot.txt")])
write_tar("dotdot-later.tar", [regular("top/ok"), regular("../escape-dotdot2.txt")])
write_tar("dotdot-dot-prefix.tar", [regular("./../escape-dotdot3.txt")])
write_tar("dotdot-inside.tar", [regular("a/../../escape-dotdot4.txt")])
write_tar("absolute.tar", [regular(os.path.join(out, "escape-abs.txt"))])
write_tar("symlink-then-file.tar", [link("top/link", escape_dir), regular("top/link/pwned.txt")])
write_tar("symlink-relative.tar", [link("top/link", "../../.."), regular("top/link/escape-rel.txt")])
write_tar("hardlink.tar", [link("top/hard", os.path.join(out, "escape-hard-target.txt"), hard=True)])
write_tar("newline-name.tar", [regular("top/ok\n../escape-newline.txt")])

with zipfile.ZipFile(os.path.join(out, "backslash.zip"), "w") as archive:
    archive.writestr("..\\escape-zip.txt", "x")
with zipfile.ZipFile(os.path.join(out, "drive.zip"), "w") as archive:
    archive.writestr("C:/escape-drive.txt", "x")
with zipfile.ZipFile(os.path.join(out, "dotdot.zip"), "w") as archive:
    archive.writestr("../escape-zipdotdot.txt", "x")
