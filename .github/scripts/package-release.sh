#!/bin/bash
# Build a playable per-game archive: binary, SDL, and that game's data tree.
# Usage: package-release.sh macos-arm64|linux-x64
set -eu
set -o pipefail

platform=${1:-}
root=$(cd "$(dirname "$0")/../.." && pwd)
dist=$root/dist

case "$platform" in
macos-arm64)
    [ "$(uname)" = Darwin ] || { echo "macos-arm64 packages are built on macOS" >&2; exit 1; }
    [ "$(uname -m)" = arm64 ] || { echo "expected arm64, got $(uname -m)" >&2; exit 1; }
    ;;
linux-x64)
    [ "$(uname)" = Linux ] || { echo "linux-x64 packages are built on Linux" >&2; exit 1; }
    [ "$(uname -m)" = x86_64 ] || { echo "expected x86_64, got $(uname -m)" >&2; exit 1; }
    command -v patchelf >/dev/null || { echo "patchelf is required" >&2; exit 1; }
    ;;
*)
    echo "usage: $0 macos-arm64|linux-x64" >&2
    exit 1
    ;;
esac

for bin in dark-reign dark-colony 7legion kknd; do
    [ -x "$root/build/bin/$bin" ] || { echo "missing build/bin/$bin (run make first)" >&2; exit 1; }
done

rm -rf "$dist"
mkdir -p "$dist"

run_within() {
    local secs=$1
    shift
    "$@" &
    local pid=$!
    local i=0
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$i" -ge "$secs" ]; then
            kill -9 "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            echo "timed out after ${secs}s: $*" >&2
            return 124
        fi
        sleep 1
        i=$((i + 1))
    done
    wait "$pid"
}

glibc_lib() {
    case $(basename "$1") in
    libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|libresolv.so.*|libgcc_s.so.*|libstdc++.so.*)
        return 0 ;;
    *) return 1 ;;
    esac
}

bundle_linux() {
    local file=$1 path name
    local deps
    deps=$(ldd "$file" | awk '/=> \// {print $3}')
    for path in $deps; do
        glibc_lib "$path" && continue
        name=$(basename "$path")
        if [ ! -f "$stage/$name" ]; then
            cp -L "$path" "$stage/$name"
            chmod 755 "$stage/$name"
            patchelf --set-rpath '$ORIGIN' "$stage/$name"
            bundle_linux "$stage/$name"
        fi
    done
}

resolve_macho_dep() {
    local src=$1 dep=$2 base rpath expanded found libdir
    case $dep in
    /usr/lib/*|/System/*) return 1 ;;
    @executable_path/*)
        echo "$(dirname "$src")/${dep#@executable_path/}"
        ;;
    @loader_path/*)
        echo "$(dirname "$src")/${dep#@loader_path/}"
        ;;
    @rpath/*)
        base=${dep#@rpath/}
        found=""
        while read -r rpath; do
            [ -n "$rpath" ] || continue
            case $rpath in
            @loader_path/*) expanded="$(dirname "$src")/${rpath#@loader_path/}" ;;
            @executable_path/*) expanded="$(dirname "$src")/${rpath#@executable_path/}" ;;
            *) expanded=$rpath ;;
            esac
            if [ -f "$expanded/$base" ]; then found="$expanded/$base"; break; fi
        done <<EOF
$(otool -l "$src" | awk '/cmd LC_RPATH/{getline; getline; print $2}')
EOF
        if [ -z "$found" ]; then
            libdir=$(pkg-config --variable=libdir sdl2 2>/dev/null || true)
            if [ -n "$libdir" ] && [ -f "$libdir/$base" ]; then found="$libdir/$base"; fi
        fi
        if [ -z "$found" ]; then
            echo "cannot resolve $dep from $src" >&2
            return 1
        fi
        echo "$found"
        ;;
    /*)
        [ -f "$dep" ] || { echo "missing $dep" >&2; return 1; }
        echo "$dep"
        ;;
    *)
        echo "unexpected dependency $dep" >&2
        return 1
        ;;
    esac
}

bundle_macho() {
    local src=$1 dest=$2 dep resolved base own
    local deps
    own=$(otool -D "$src" 2>/dev/null | awk 'NR==2 {print}')
    deps=$(otool -L "$src" | awk 'NR>1 {print $1}')
    for dep in $deps; do
        [ -n "$own" ] && [ "$dep" = "$own" ] && continue
        case $dep in
        /usr/lib/*|/System/*|@executable_path/*) continue ;;
        esac
        resolved=$(resolve_macho_dep "$src" "$dep")
        base=$(basename "$resolved")
        if [ ! -f "$stage/$base" ]; then
            cp -L "$resolved" "$stage/$base"
            chmod 755 "$stage/$base"
            install_name_tool -id "@executable_path/$base" "$stage/$base"
            bundle_macho "$resolved" "$stage/$base"
        fi
        install_name_tool -change "$dep" "@executable_path/$base" "$dest"
    done
}

drop_retail_programs() {
    local tree=$1 dir
    local dirs
    dirs=$(find "$tree" -type d \( -iname 'Ereg' -o -iname 'DX5' \) -print || true)
    if [ -n "$dirs" ]; then
        printf '%s\n' "$dirs" | while IFS= read -r dir; do
            rm -rf "$dir"
        done
    fi
    find "$tree" -type f \( \
        -iname '*.exe' -o -iname '*.dll' -o -iname '*.isu' -o -iname '*.drv' -o \
        -iname '*.386' -o -iname '*.pif' -o -iname '*.com' -o -iname '*.bat' -o \
        -name '.DS_Store' \
    \) -delete
    dirs=$(find "$tree" \( -iname '*.exe' -o -iname '*.dll' -o -iname '*.isu' -o -iname '*.drv' -o -iname '*.386' \) -print || true)
    if [ -n "$dirs" ]; then
        echo "retail programs left in package:" >&2
        printf '%s\n' "$dirs" >&2
        exit 1
    fi
}

write_launcher() {
    local path=$1
    cat > "$path" <<EOF
#!/bin/sh
cd "\$(dirname "\$0")" || exit 1
exec "./$bin" "\$@"
EOF
    chmod +x "$path"
}

package_game() {
    local id=$1 bin=$2 data_rel=$3 title=$4
    local stage=$dist/$id
    echo "== $id ($platform) ==" >&2
    rm -rf "$stage"
    mkdir -p "$stage/data/$(dirname "$data_rel")"
    echo "copy $bin and data/$data_rel" >&2
    cp "$root/build/bin/$bin" "$stage/$bin"
    chmod +x "$stage/$bin"
    cp -R "$root/data/$data_rel" "$stage/data/$data_rel"
    drop_retail_programs "$stage/data"
    [ -d "$stage/data/$data_rel" ] || { echo "missing data/$data_rel" >&2; exit 1; }
    cp "$root/LICENSE" "$stage/LICENSE"

    if [ "$platform" = macos-arm64 ]; then
        echo "bundle $bin" >&2
        bundle_macho "$root/build/bin/$bin" "$stage/$bin"
        local lib
        for lib in "$stage"/*.dylib; do
            [ -f "$lib" ] || continue
            echo "sign $(basename "$lib")" >&2
            run_within 60 codesign --force --sign - --timestamp=none "$lib"
        done
        echo "sign $bin" >&2
        run_within 60 codesign --force --sign - --timestamp=none "$stage/$bin"
        if otool -L "$stage/$bin" | awk 'NR>1 {print $1}' | grep -E '^/(opt|usr/local)/'; then
            echo "absolute library path remains in $bin" >&2
            exit 1
        fi
        xattr -cr "$stage" 2>/dev/null || true
        write_launcher "$stage/Play.command"
        cat > "$stage/README.txt" <<EOF
open-rts $title ($platform)

Double-click Play.command, or from Terminal:

  cd this-folder
  ./$bin

If macOS blocks the download, right-click Play.command and choose Open,
or run: xattr -dr com.quarantine .

Game data is in data/$data_rel. The engine is MIT licensed (LICENSE).
The original game data is not covered by that license.
EOF
        echo "zip $id" >&2
        rm -f "$dist/${id}-${platform}.zip"
        ditto -c -k --keepParent "$stage" "$dist/${id}-${platform}.zip"
    else
        patchelf --set-rpath '$ORIGIN' "$stage/$bin"
        bundle_linux "$stage/$bin"
        if ldd "$stage/$bin" | grep -q 'not found'; then
            ldd "$stage/$bin" >&2
            echo "unresolved library in $bin" >&2
            exit 1
        fi
        local path
        while read -r path; do
            [ -n "$path" ] || continue
            glibc_lib "$path" && continue
            case $path in
            "$stage"/*) ;;
            *) echo "unpackaged dependency $path" >&2; ldd "$stage/$bin" >&2; exit 1 ;;
            esac
        done <<EOF
$(ldd "$stage/$bin" | awk '/=> \// {print $3}')
EOF
        write_launcher "$stage/play.sh"
        cat > "$stage/README.txt" <<EOF
open-rts $title ($platform)

From this folder:

  ./play.sh

Game data is in data/$data_rel. SDL and the libraries it needs, other than
the system C library, are next to the executable. The engine is MIT licensed
(LICENSE). The original game data is not covered by that license.
EOF
        tar -C "$dist" -czf "$dist/${id}-${platform}.tar.gz" "$id"
    fi

    echo "-- check $bin from the archive directory --" >&2
    run_within 180 bash -c 'cd "$1" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "./$2" --check' _ "$stage" "$bin"
    echo "-- check $bin from another directory --" >&2
    run_within 180 bash -c 'cd /tmp && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$1/$2" --check' _ "$stage" "$bin"
    rm -rf "$stage"
}

package_game dark-reign dark-reign "REIGN/dark" "Dark Reign"
package_game dark-colony dark-colony "DCOLONY" "Dark Colony"
package_game 7legion 7legion "7LEGION" "7th Legion"
package_game kknd kknd "KKND" "KKnD"

echo "archives:"
ls -lh "$dist"
