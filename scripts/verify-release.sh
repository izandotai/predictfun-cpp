#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
release_parent=${TMPDIR:-/tmp}
release_root=$(mktemp -d "$release_parent/predictfun-release.XXXXXX")

cleanup() {
  case "$release_root" in
    "$release_parent"/predictfun-release.*)
      rm -rf -- "$release_root"
      ;;
    *)
      printf 'refusing to clean unexpected release path: %s\n' "$release_root" >&2
      ;;
  esac
}
trap cleanup EXIT HUP INT TERM

generator=${CMAKE_GENERATOR:-Ninja}
parallel=${CMAKE_BUILD_PARALLEL_LEVEL:-4}
package_version=$(sed -n \
  's/^project(predictfun_cpp VERSION \([^ ]*\) LANGUAGES.*$/\1/p' \
  "$repo_root/CMakeLists.txt" | sed -n '1p')
if [ -z "$package_version" ]; then
  printf '%s\n' 'cannot determine predictfun package version' >&2
  exit 1
fi

cache_value() {
  cache_file=$1
  cache_name=$2
  if [ ! -f "$cache_file" ]; then
    return 0
  fi
  sed -n "s#^${cache_name}:[^=]*=##p" "$cache_file" | sed -n '1p'
}

existing_cache="$repo_root/build/release/CMakeCache.txt"
verify_glaze_source=${PREDICTFUN_VERIFY_GLAZE_SOURCE_DIR:-$(
  cache_value "$existing_cache" PREDICTFUN_GLAZE_SOURCE_DIR)}
verify_boost_source=${PREDICTFUN_VERIFY_BOOST_SOURCE_DIR:-$(
  cache_value "$existing_cache" PREDICTFUN_BOOST_SOURCE_DIR)}
verify_izan_source=${PREDICTFUN_VERIFY_IZAN_CRYPTO_SOURCE_DIR:-$(
  cache_value "$existing_cache" PREDICTFUN_IZAN_CRYPTO_SOURCE_DIR)}
verify_trezor_source=${PREDICTFUN_VERIFY_TREZOR_FIRMWARE_SOURCE_DIR:-$(
  cache_value "$existing_cache" FETCHCONTENT_SOURCE_DIR_TREZOR_FIRMWARE)}
verify_doctest_source=${PREDICTFUN_VERIFY_DOCTEST_SOURCE_DIR:-$(
  cache_value "$existing_cache" FETCHCONTENT_SOURCE_DIR_DOCTEST)}
verify_sodium_source=${PREDICTFUN_VERIFY_SODIUM_SOURCE_DIR:-$(
  cache_value "$existing_cache" FETCHCONTENT_SOURCE_DIR_SODIUM)}
verify_openssl_root=${PREDICTFUN_VERIFY_OPENSSL_ROOT_DIR:-$(
  cache_value "$existing_cache" OPENSSL_ROOT_DIR)}

for source_dir in "$verify_glaze_source" "$verify_boost_source" \
    "$verify_izan_source" "$verify_trezor_source" \
    "$verify_doctest_source" "$verify_sodium_source" \
    "$verify_openssl_root"; do
  if [ -n "$source_dir" ] && [ ! -d "$source_dir" ]; then
    printf 'configured dependency source is not a directory: %s\n' \
      "$source_dir" >&2
    exit 1
  fi
done

configure_sdk() {
  build_dir=$1
  prefix_dir=$2
  signer=$3
  tests=$4

  set -- cmake -S "$repo_root" -B "$build_dir" -G "$generator" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix_dir" \
    -DPREDICTFUN_BUILD_TESTS="$tests" \
    -DPREDICTFUN_BUILD_TOOLS=OFF \
    -DPREDICTFUN_BUILD_LOCAL_SIGNER="$signer" \
    -DPREDICTFUN_WARNINGS_AS_ERRORS=ON
  if [ -n "$verify_glaze_source" ]; then
    set -- "$@" -DPREDICTFUN_GLAZE_SOURCE_DIR="$verify_glaze_source"
  fi
  if [ -n "$verify_boost_source" ]; then
    set -- "$@" -DPREDICTFUN_BOOST_SOURCE_DIR="$verify_boost_source"
  fi
  if [ -n "$verify_izan_source" ]; then
    set -- "$@" -DPREDICTFUN_IZAN_CRYPTO_SOURCE_DIR="$verify_izan_source"
  fi
  if [ -n "$verify_trezor_source" ]; then
    set -- "$@" -DFETCHCONTENT_SOURCE_DIR_TREZOR_FIRMWARE="$verify_trezor_source"
  fi
  if [ -n "$verify_doctest_source" ]; then
    set -- "$@" -DFETCHCONTENT_SOURCE_DIR_DOCTEST="$verify_doctest_source"
  fi
  if [ -n "$verify_sodium_source" ]; then
    set -- "$@" -DFETCHCONTENT_SOURCE_DIR_SODIUM="$verify_sodium_source"
  fi
  if [ -n "$verify_openssl_root" ]; then
    set -- "$@" -DOPENSSL_ROOT_DIR="$verify_openssl_root"
  fi
  "$@"
  cmake --build "$build_dir" --parallel "$parallel"
  if [ "$tests" = ON ]; then
    ctest --test-dir "$build_dir" --output-on-failure
  fi
  cmake --install "$build_dir"
}

configure_consumer() {
  sdk_build=$1
  prefix_dir=$2
  consumer_build=$3
  signer=$4

  consumer_boost_source=${verify_boost_source:-$sdk_build/_deps/boost-src}
  consumer_izan_source=${verify_izan_source:-$sdk_build/_deps/izan_crypto_dependency-src}
  set -- cmake -S "$repo_root/tests/package_consumer" -B "$consumer_build" \
    -G "$generator" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$prefix_dir" \
    -DPREDICTFUN_BOOST_SOURCE_DIR="$consumer_boost_source" \
    -DPREDICTFUN_IZAN_CRYPTO_SOURCE_DIR="$consumer_izan_source" \
    -DPREDICTFUN_PACKAGE_VERSION="$package_version" \
    -DPREDICTFUN_PACKAGE_CONSUMER_WITH_SIGNER="$signer"
  if [ -n "$verify_trezor_source" ]; then
    set -- "$@" -DFETCHCONTENT_SOURCE_DIR_TREZOR_FIRMWARE="$verify_trezor_source"
  fi
  if [ -n "$verify_doctest_source" ]; then
    set -- "$@" -DFETCHCONTENT_SOURCE_DIR_DOCTEST="$verify_doctest_source"
  fi
  if [ -n "$verify_sodium_source" ]; then
    set -- "$@" -DFETCHCONTENT_SOURCE_DIR_SODIUM="$verify_sodium_source"
  fi
  if [ -n "$verify_openssl_root" ]; then
    set -- "$@" -DOPENSSL_ROOT_DIR="$verify_openssl_root"
  fi
  "$@"
  cmake --build "$consumer_build" --parallel "$parallel"
  ctest --test-dir "$consumer_build" --output-on-failure
}

printf '%s\n' '[1/4] isolated Release build, test and full install'
full_build="$release_root/full-build"
full_prefix="$release_root/full-prefix"
configure_sdk "$full_build" "$full_prefix" ON ON

printf '%s\n' '[2/4] full installed-package consumers, including explicit signer'
configure_consumer "$full_build" "$full_prefix" \
  "$release_root/full-consumer" ON

printf '%s\n' '[3/4] isolated read-only install without local signer'
readonly_build="$release_root/readonly-build"
readonly_prefix="$release_root/readonly-prefix"
configure_sdk "$readonly_build" "$readonly_prefix" OFF OFF

if find "$readonly_prefix" -type f \
    \( -name '*local_signer*' -o -name '*local_transaction_signer*' \) \
    -print | grep -q .; then
  printf '%s\n' 'read-only install unexpectedly contains signer artifacts' >&2
  exit 1
fi
if grep -E 'add_library\(predictfun::local_(transaction_)?signer' \
    "$readonly_prefix"/lib/cmake/predictfun/predictfunTargets*.cmake \
    >/dev/null 2>&1; then
  printf '%s\n' 'read-only install unexpectedly exports signer targets' >&2
  exit 1
fi

printf '%s\n' '[4/4] public/read-only installed-package consumer'
configure_consumer "$readonly_build" "$readonly_prefix" \
  "$release_root/readonly-consumer" OFF

printf '%s\n' 'predictfun-cpp isolated release verification passed'
