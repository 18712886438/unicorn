#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-android-arm64"

if [[ -n "${ANDROID_NDK_HOME:-}" ]]; then
    ANDROID_NDK="${ANDROID_NDK_HOME}"
elif [[ -n "${ANDROID_NDK_ROOT:-}" ]]; then
    ANDROID_NDK="${ANDROID_NDK_ROOT}"
else
    echo "ANDROID_NDK_HOME or ANDROID_NDK_ROOT must be set" >&2
    exit 1
fi

TOOLCHAIN_FILE="${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
if [[ ! -f "${TOOLCHAIN_FILE}" ]]; then
    echo "Android toolchain file not found: ${TOOLCHAIN_FILE}" >&2
    exit 1
fi

configure_args=()
build_args=()

pkg_config_bin="${PKG_CONFIG:-}"
if [[ -z "${pkg_config_bin}" ]]; then
    if command -v pkg-config &>/dev/null; then
        pkg_config_bin="$(command -v pkg-config)"
    else
        pkg_config_bin="${BUILD_DIR}/pkg-config-stub.sh"
        mkdir -p "${BUILD_DIR}"
        cat > "${pkg_config_bin}" <<'EOF'
#!/usr/bin/env bash
# Minimal stub for environments without pkg-config; all probes succeed.
exit 0
EOF
        chmod +x "${pkg_config_bin}"
        echo "pkg-config not found; using stub at ${pkg_config_bin}" >&2
    fi
fi

while (($#)); do
    case "$1" in
        --)
            shift
            build_args+=("$@")
            break
            ;;
        *)
            configure_args+=("$1")
            shift
            ;;
    esac
done

mkdir -p "${BUILD_DIR}"

cmake_config_cmd=(
    env
    PKG_CONFIG="${pkg_config_bin}"
    cmake
    -S "${ROOT_DIR}"
    -B "${BUILD_DIR}"
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}"
    -DANDROID=ON
    -DANDROID_ABI=arm64-v8a
    -DANDROID_PLATFORM="${ANDROID_PLATFORM:-android-23}"
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
)
if (( ${#configure_args[@]} )); then
    cmake_config_cmd+=("${configure_args[@]}")
fi

"${cmake_config_cmd[@]}"

cmake_build_cmd=(
    cmake
    --build "${BUILD_DIR}"
    --parallel
)
if (( ${#build_args[@]} )); then
    cmake_build_cmd+=("${build_args[@]}")
fi

"${cmake_build_cmd[@]}"

strip_tool="${ANDROID_NDK}/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"
if [[ -x "${strip_tool}" ]]; then
    while IFS= read -r artifact; do
        [[ -f "${artifact}" ]] || continue
        "${strip_tool}" --strip-unneeded "${artifact}" || {
            echo "warning: failed to strip ${artifact}" >&2
        }
    done <<EOF
${BUILD_DIR}/libunicorn.so
${BUILD_DIR}/libunicorn.a
${BUILD_DIR}/libunicorn_static.a
${BUILD_DIR}/libaarch64-softmmu.a
EOF
else
    echo "warning: llvm-strip not found at ${strip_tool}, skipping strip step" >&2
fi
