#!/usr/bin/env bash
#
# extract-sysroot.sh — pull a usable sysroot for the LoongArch cross-toolchain
# from a running Loongnix 20 machine (over SSH) or from a local rootfs tree.
#
# Reference source: 龙芯 2K3000 EV 开发板, Loongnix 20 (DaoXiangHu).
# Anything else running Loongnix 20 works too (3A5000 / 3C5000 / ...).
#
# Why this script (and why no sysroot is shipped with the toolchain):
#   The toolchain itself is redistributable, but Loongnix 20's library tree
#   contains Loongson-proprietary GPU drivers (libEGL_loonggpu, libGLX_loonggpu)
#   and a closed-source crypto library (libloongson_crypto). We can't ship
#   those in a public GitHub repo — but you can lawfully use them on your own
#   Loongnix system. So we extract on demand from a source you control.
#
# Full guide with the apt-install prerequisites: see SYSROOT.md at repo root.
#
# Usage:
#   ./extract-sysroot.sh --from-ssh root@<loongnix-host> [--out <sysroot-dir>]
#   ./extract-sysroot.sh --from-rootfs <path-to-rootfs> [--out <sysroot-dir>]
#
# Default --out is /opt/loongarch-toolchain/sysroot (requires sudo).
# Pass --out somewhere under your $HOME to avoid sudo.
#
# Requires: rsync, ssh (for --from-ssh), bash 4+.
#
# What it does:
#   1) rsync the canonical paths (lib/, lib64/, usr/include/, usr/lib/, ...)
#   2) rewrite absolute symlinks to be relative (so they resolve inside sysroot)
#   3) rewrite GNU ld linker scripts that reference absolute /lib/... paths
#   4) drop APFS case-insensitive collisions (xtables CONNMARK vs connmark, etc.)
#
# Test after extraction:
#   loongarch64-linux-gnu-gcc -O2 examples/hello.c -o /tmp/hello
#   file /tmp/hello   # → ELF 64-bit LSB executable, LoongArch, ...

set -euo pipefail

# ---------- arg parsing ----------

SRC_KIND=""        # "ssh" or "rootfs"
SRC=""             # host:user or path
OUT="/opt/loongarch-toolchain/sysroot"

usage() {
    sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//' | head -n -2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --from-ssh)      SRC_KIND="ssh"; SRC="$2"; shift 2 ;;
        --from-rootfs)   SRC_KIND="rootfs"; SRC="$2"; shift 2 ;;
        --out)           OUT="$2"; shift 2 ;;
        -h|--help)       usage ;;
        *)               echo "unknown arg: $1"; usage ;;
    esac
done

[[ -z "$SRC_KIND" ]] && { echo "must pass --from-ssh or --from-rootfs"; usage; }

# ---------- sanity ----------

command -v rsync >/dev/null || { echo "rsync not installed (brew install rsync)"; exit 1; }
if [[ "$SRC_KIND" == "ssh" ]]; then
    command -v ssh >/dev/null || { echo "ssh not installed"; exit 1; }
fi

# We need to refuse to write to root-owned dirs without sudo, and refuse to
# write into a non-empty dir that doesn't look like ours.
if [[ -d "$OUT" && -n "$(ls -A "$OUT" 2>/dev/null || true)" ]]; then
    if [[ ! -d "$OUT/usr" && ! -d "$OUT/lib" ]]; then
        echo "Refusing to write into non-empty unrelated dir: $OUT"
        echo "Pass --out <empty-or-sysroot-dir>"
        exit 1
    fi
fi

mkdir -p "$OUT" || { echo "cannot mkdir $OUT (try sudo or --out somewhere under \$HOME)"; exit 1; }

echo "==> Extracting sysroot from $SRC_KIND:$SRC"
echo "==> Output: $OUT"

# ---------- paths to pull ----------
#
# We need:
#   /lib64/ld.so.1                                   (dynamic linker, 老世界)
#   /lib/loongarch64-linux-gnu/                      (libc, libm, libpthread, libdl, librt, ...)
#   /usr/lib/loongarch64-linux-gnu/                  (more libs + crt*.o)
#   /usr/include/                                    (glibc + kernel headers, multiarch subdirs)
#
# Excludes are conservative — keep build artifacts, drop dev cruft and
# bind-mounted runtime state if present.

RSYNC_OPTS=(
    -aHP
    --delete-after
    --exclude="*.pyc"
    --exclude="__pycache__"
    --exclude="lost+found"
)

# Build the source-side path prefix.
case "$SRC_KIND" in
    ssh)    SRC_PREFIX="$SRC:" ;;
    rootfs) SRC_PREFIX="$SRC" ;;
esac

pull() {
    local src_path="$1"
    local dst_path="$2"
    mkdir -p "$(dirname "$dst_path")"
    case "$SRC_KIND" in
        ssh)    rsync "${RSYNC_OPTS[@]}" "${SRC_PREFIX}${src_path}/" "$dst_path/" ;;
        rootfs) rsync "${RSYNC_OPTS[@]}" "${SRC_PREFIX}${src_path}/" "$dst_path/" ;;
    esac
}

pull_file() {
    local src_path="$1"
    local dst_path="$2"
    mkdir -p "$(dirname "$dst_path")"
    rsync "${RSYNC_OPTS[@]}" "${SRC_PREFIX}${src_path}" "$dst_path"
}

echo "==> Pulling /lib/loongarch64-linux-gnu/"
pull /lib/loongarch64-linux-gnu "$OUT/lib/loongarch64-linux-gnu"

echo "==> Pulling /lib64/ (dynamic linker)"
pull /lib64 "$OUT/lib64"

echo "==> Pulling /usr/lib/loongarch64-linux-gnu/"
pull /usr/lib/loongarch64-linux-gnu "$OUT/usr/lib/loongarch64-linux-gnu"

echo "==> Pulling /usr/include/"
pull /usr/include "$OUT/usr/include"

# ---------- post-processing ----------

echo "==> Fixing absolute symlinks (→ relative)"
# Find symlinks that point to absolute paths starting with /lib or /usr,
# rewrite them to be relative to $OUT so they resolve inside the sysroot.
fix_abs_symlinks() {
    # Use find with -print0 to handle spaces (shouldn't happen on Linux paths,
    # but cheap insurance).
    while IFS= read -r -d '' link; do
        local target
        target="$(readlink "$link")"
        # Only act on absolute targets that resolve to inside the sysroot.
        case "$target" in
            /lib/*|/lib64/*|/usr/*)
                local link_dir="$(dirname "$link")"
                # Compute relative target = $OUT$target seen from $link_dir
                local abs_in_sysroot="${OUT}${target}"
                local rel
                # python3 if available (almost certainly is on macOS); else fallback.
                if command -v python3 >/dev/null; then
                    rel="$(python3 -c "import os,sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))" "$abs_in_sysroot" "$link_dir")"
                else
                    # crude fallback — count slashes
                    rel="$abs_in_sysroot"
                fi
                ln -sfn "$rel" "$link"
                ;;
        esac
    done < <(find "$OUT" -type l -print0)
}
fix_abs_symlinks

echo "==> Fixing GNU ld linker scripts referencing absolute paths"
# Some .so files are ld scripts:
#   GROUP ( /lib/loongarch64-linux-gnu/libc.so.6 ... )
# Rewrite to drop the leading slash so the linker finds them inside --sysroot.
fix_linker_scripts() {
    # Look for short text files in lib dirs that mention GROUP and /lib/
    find "$OUT/lib" "$OUT/usr/lib" -maxdepth 4 -type f -name "*.so" -size -8k 2>/dev/null | while read -r f; do
        if head -c 200 "$f" 2>/dev/null | grep -q "^GROUP\|^OUTPUT_FORMAT.*GROUP"; then
            # Rewrite "/lib/loongarch64-linux-gnu/" → "lib/loongarch64-linux-gnu/" etc.
            sed -i.bak \
                -e 's| /lib/loongarch64-linux-gnu/| lib/loongarch64-linux-gnu/|g' \
                -e 's| /usr/lib/loongarch64-linux-gnu/| usr/lib/loongarch64-linux-gnu/|g' \
                -e 's| /lib64/| lib64/|g' \
                "$f"
            rm -f "${f}.bak"
            echo "    rewrote: ${f#$OUT/}"
        fi
    done
}
fix_linker_scripts

echo "==> Cleaning APFS case-insensitive collisions"
# macOS APFS is case-insensitive by default. Loongnix has a handful of files
# that collide when extracted on macOS. These are non-essential (xtables
# kernel-side modules + perl Sys vs sys), drop them.
COLLISION_CANDIDATES=(
    "$OUT/usr/lib/loongarch64-linux-gnu/xtables"
    "$OUT/lib/loongarch64-linux-gnu/xtables"
    "$OUT/usr/lib/loongarch64-linux-gnu/perl"
    "$OUT/usr/share/perl"
)
# Rather than guess which specific files collided, just remove the xtables
# dir entirely (we don't link against firewall modules from cross-build) and
# leave a note about perl Sys/sys if it shows up.
for d in "${COLLISION_CANDIDATES[@]}"; do
    if [[ -d "$d" ]]; then
        case "$d" in
            *xtables*)
                echo "    dropping $d (firewall modules, not needed for cross-build)"
                rm -rf "$d"
                ;;
        esac
    fi
done

# ---------- smoke test ----------

echo "==> Sysroot extraction done."
echo
echo "Sanity check:"
for need in \
    "lib64/ld.so.1" \
    "usr/include/stdio.h" \
    "usr/lib/loongarch64-linux-gnu/libc.so" \
    "usr/lib/loongarch64-linux-gnu/crt1.o" \
    "usr/include/loongarch64-linux-gnu/bits/libc-header-start.h"
do
    if [[ -e "$OUT/$need" ]]; then
        echo "  OK    $need"
    else
        echo "  MISS  $need  ← extraction may be incomplete"
    fi
done

echo
echo "Try compiling examples/hello.c next:"
echo "  loongarch64-linux-gnu-gcc -O2 examples/hello.c -o /tmp/hello"
echo "  file /tmp/hello"
