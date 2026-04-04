#!/bin/bash -ex

# Copyright 2020 Google Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

: "${OUT_DIR:?Must set OUT_DIR}"
TOP=$(pwd)

UNAME="$(uname)"
case "$UNAME" in
Linux)
    OS='linux'
    ;;
Darwin)
    OS='darwin'
    ;;
*)
    echo "Unrecognized OS $UNAME"
    exit 1
    ;;
esac

UNAME_ARCH=$(uname -m)
case "$UNAME_ARCH" in
x86_64)
    ARCH='x86'
    ;;
aarch64)
    ARCH="arm64"
    ;;
*)
    echo "Unrecognized architecture $UNAME_ARCH"
    exit 1
    ;;
esac

build_soong=1
build_asan=1
build_native=1
build_java=1
[[ ! -d ${TOP}/toolchain/go ]] || build_go=1

use_musl=false
clean=true
while getopts ":-:" opt; do
    case "$opt" in
        -)
            case "${OPTARG}" in
                resume) clean= ;;
                musl) use_musl=true; build_arm64_cross_musl=1 ;;
                skip-go) unset build_go ;;
                skip-soong) unset build_soong ;;
                skip-soong-tests) skip_soong_tests=--skip-soong-tests ;;
                skip-asan) unset build_asan ;;
                *) echo "Unknown option --${OPTARG}"; exit 1 ;;
            esac;;
        *) echo "'${opt}' '${OPTARG}'"
    esac
done

primary_arch=""
if [[ ${ARCH} = x86 ]]; then
    primary_arch="\"HostArch\":\"x86_64\","
elif [[ ${ARCH} = arm64 ]]; then
    primary_arch="\"HostArch\":\"arm64\","
    use_musl=true
    unset build_native
    unset build_java
    unset build_asan
    unset build_arm64_cross_musl
    skip_soong_tests=--skip-soong-tests
fi

secondary_arch=""
if [[ ${OS} = linux && ${ARCH} = x86 ]]; then
    secondary_arch="\"HostSecondaryArch\":\"x86\","
fi

cross_compile=""
if [[ ${use_musl} = "true" && ${ARCH} = x86 ]]; then
    cross_compile=$(cat <<EOF
    "CrossHost": "linux_musl",
    "CrossHostArch": "arm64",
    "CrossHostSecondaryArch": "arm",
EOF
    )
elif [[ ${OS} = darwin ]]; then
    cross_compile=$(cat <<EOF
    "CrossHost": "darwin",
    "CrossHostArch": "arm64",
EOF
    )
fi

# Use toybox and other prebuilts even outside of the build (test running, go, etc)
export PATH=${TOP}/prebuilts/build-tools/path/${OS}-${ARCH}:$PATH

if [ -n "${build_soong}" ]; then
    SOONG_OUT=${OUT_DIR}/soong
    SOONG_HOST_OUT=${OUT_DIR}/host/${OS}-${ARCH}
    [[ -z "${clean}" ]] || rm -rf ${SOONG_OUT}
    mkdir -p ${SOONG_OUT}
    rm -rf ${SOONG_OUT}/dist ${SOONG_OUT}/dist-common ${SOONG_OUT}/dist-arm64
    cat > ${SOONG_OUT}/soong.variables.tmp << EOF
{
    "Allow_missing_dependencies": true,
    ${primary_arch}
    ${secondary_arch}
    ${cross_compile}
    "HostMusl": $use_musl,
    "StripByDefault": true,
    "VendorVars": {
        "cpython3": {
            "force_build_host": "true"
        },
        "art_module": {
            "source_build": "true"
        }
    },
    "VendorVarTypes": {
        "cpython3": {
            "force_build_host": "bool"
        }
    }
}
EOF
    if cmp -s ${SOONG_OUT}/soong.variables.tmp ${SOONG_OUT}/soong.variables; then
        rm ${SOONG_OUT}/soong.variables.tmp
    else
        mv -f ${SOONG_OUT}/soong.variables.tmp ${SOONG_OUT}/soong.variables
    fi
    SOONG_GO_BINARIES=(
        bpfmt
        go_extractor
        kotlinc_extractor
        merge_zips
        soong_zip
        runextractor
        rust_extractor
        zip2zip
    )
    SOONG_BINARIES=(
        aconfig
        acp
        aidl
        bison
        bloaty
        brotli
        bzip2
        ckati
        edit_monitor
        flex
        gavinhoward-bc
        hidl-gen
        hidl-lint
        m4
        make
        n2
        ninja
        one-true-awk
        openssl
        py3-cmd
        py3-launcher64
        py3-launcher-autorun64
        tool_event_logger
        toybox
        xz
        zipalign
        ziptime
        ziptool
    )
    SOONG_MUSL_BINARIES=(
        py3-launcher-static64
        py3-launcher-autorun-static64
    )
    SOONG_ASAN_BINARIES=(
        acp
        aidl
        ckati
        gavinhoward-bc
        ninja
        toybox
        zipalign
        ziptime
        ziptool
    )
    SOONG_JAVA_LIBRARIES=(
        desugar.jar
        dx.jar
        javac_extractor.jar
        ktfmt.jar
        turbine.jar
    )
    SOONG_JAVA_WRAPPERS=(
        dx
    )
    if [[ $OS == "linux" ]]; then
        SOONG_BINARIES+=(
            create_minidebuginfo
            nsjail
        )
    fi

    go_binaries="${SOONG_GO_BINARIES[@]/#/${SOONG_HOST_OUT}/bin/}"
    if [ -n "${build_native}" ]; then
        binaries="${SOONG_BINARIES[@]/#/${SOONG_HOST_OUT}/bin/}"
        asan_binaries="${SOONG_ASAN_BINARIES[@]/#/${SOONG_HOST_OUT}/bin/}"
        tests="${SOONG_HOST_OUT}/nativetest64/n2_e2e_tests/n2_e2e_tests \
             ${SOONG_HOST_OUT}/nativetest64/n2_unit_tests/n2_unit_tests \
             ${SOONG_HOST_OUT}/nativetest64/ninja_test/ninja_test \
             ${SOONG_HOST_OUT}/nativetest64/ckati_find_test/ckati_find_test \
             ${SOONG_HOST_OUT}/nativetest64/par_test/par_test"

        # TODO: When we have a better method of extracting zips from Soong, use that.
        py3_stdlib_zip="${SOONG_OUT}/.intermediates/external/python/cpython3/Lib/py3-stdlib-zip/gen/py3-stdlib.zip"
    fi
    if [ -n "${build_java}" ]; then
        jars="${SOONG_JAVA_LIBRARIES[@]/#/${SOONG_HOST_OUT}/framework/}"
        wrappers="${SOONG_JAVA_WRAPPERS[@]/#/${SOONG_HOST_OUT}/bin/}"
    fi

    musl_x86_sysroot=""
    musl_x86_64_sysroot=""
    musl_arm_sysroot=""
    musl_arm64_sysroot=""
    cross_binaries=""
    if [[ ${use_musl} = "true" && -n "${build_arm64_cross_musl}" ]]; then
        binaries="${binaries} ${SOONG_MUSL_BINARIES[@]/#/${SOONG_HOST_OUT}/bin/}"
        musl_x86_sysroot="${SOONG_OUT}/.intermediates/external/musl/libc_musl_sysroot/linux_musl_x86/gen/libc_musl_sysroot.zip"
        musl_x86_64_sysroot="${SOONG_OUT}/.intermediates/external/musl/libc_musl_sysroot/linux_musl_x86_64/gen/libc_musl_sysroot.zip"

        # Build cross-compiled musl arm64 binaries, except for go binaries
        # since Blueprint doesn't have cross compilation support for them.
        SOONG_HOST_ARM_OUT=${OUT_DIR}/host/linux-arm64
        cross_binaries="${SOONG_BINARIES[@]/#/${SOONG_HOST_ARM_OUT}/bin/}"
        cross_binaries="${cross_binaries} ${SOONG_MUSL_BINARIES[@]/#/${SOONG_HOST_ARM_OUT}/bin/}"

        musl_arm_sysroot="${SOONG_OUT}/.intermediates/external/musl/libc_musl_sysroot/linux_musl_arm/gen/libc_musl_sysroot.zip"
        musl_arm64_sysroot="${SOONG_OUT}/.intermediates/external/musl/libc_musl_sysroot/linux_musl_arm64/gen/libc_musl_sysroot.zip"
    fi

    # Build everything
    build/soong/soong_ui.bash --make-mode --soong-only --skip-config ${skip_soong_tests} \
        ${go_binaries} \
        ${binaries} \
        ${cross_binaries} \
        ${wrappers} \
        ${jars} \
        ${py3_stdlib_zip} \
        ${musl_x86_sysroot} \
        ${musl_x86_64_sysroot} \
        ${musl_arm_sysroot} \
        ${musl_arm64_sysroot} \
        ${tests} \
        soong_docs

    if [ -n "${build_native}" ]; then
        # Run ninja tests
        ${SOONG_HOST_OUT}/nativetest64/ninja_test/ninja_test

        # Run n2 tests
        timeout -v -k 10 300 ${SOONG_HOST_OUT}/nativetest64/n2_unit_tests/n2_unit_tests
        N2_PATH=${SOONG_HOST_OUT}/bin/n2 timeout -v -k 10 300 ${SOONG_HOST_OUT}/nativetest64/n2_e2e_tests/n2_e2e_tests

        # Run ckati tests
        ${SOONG_HOST_OUT}/nativetest64/ckati_find_test/ckati_find_test

        # Run python par/py*-cmd tests
        ANDROID_HOST_OUT=${PWD}/${SOONG_HOST_OUT} build/soong/python/tests/runtest.sh
    fi

    # Copy arch-specific binaries
    mkdir -p ${SOONG_OUT}/dist/bin
    cp ${go_binaries} ${binaries} ${SOONG_OUT}/dist/bin/
    for i in $(ls -d ${SOONG_HOST_OUT}/lib* 2> /dev/null); do
        cp -R ${i} ${SOONG_OUT}/dist/
    done

    # Copy cross-compiled binaries
    if [[ "${use_musl}" = "true" && -n "${build_arm64_cross_musl}" ]]; then
        mkdir -p ${SOONG_OUT}/dist-arm64/bin
        cp ${cross_binaries} ${SOONG_OUT}/dist-arm64/bin/
        cp -R ${SOONG_HOST_ARM_OUT}/lib* ${SOONG_OUT}/dist-arm64/
    fi

    if [ -n "${build_java}" ]; then
        # Copy jars and wrappers
        mkdir -p ${SOONG_OUT}/dist-common/{bin,flex,framework,py3-stdlib,py3-headers}
        cp ${wrappers} ${SOONG_OUT}/dist-common/bin
        cp ${jars} ${SOONG_OUT}/dist-common/framework
    fi

    if [ -n "${build_native}" ]; then
        cp -r external/bison/data ${SOONG_OUT}/dist-common/bison
        cp external/bison/NOTICE ${SOONG_OUT}/dist-common/bison/
        cp -r external/flex/src/FlexLexer.h ${SOONG_OUT}/dist-common/flex/
        cp external/flex/NOTICE ${SOONG_OUT}/dist-common/flex/

        ${SOONG_OUT}/dist/bin/zip2zip -i ${py3_stdlib_zip} -o ${OUT_DIR}/py3-stdlib.zip "**/*:py3-stdlib/"
        cp external/python/cpython3/LICENSE ${SOONG_OUT}/dist-common/py3-stdlib/
        py3_headers=(
            ${TOP}/external/python/cpython3/Include
            ${TOP}/external/python/cpython3/android/linux_x86_64/pyconfig
            ${TOP}/external/python/cpython3/android/linux_arm64/pyconfig
            ${TOP}/external/python/cpython3/android/bionic/pyconfig
            ${TOP}/external/python/cpython3/android/darwin/pyconfig
        )
        for py3_header in ${py3_headers[@]}; do
            REL_SUBDIR=${py3_header#*/cpython3/}
            REL_SUBDIR=$(dirname ${REL_SUBDIR})
            mkdir -p ${SOONG_OUT}/dist-common/py3-headers/${REL_SUBDIR} || true
            cp -r ${py3_header} ${SOONG_OUT}/dist-common/py3-headers/${REL_SUBDIR}/
        done
        cp external/python/cpython3/LICENSE ${SOONG_OUT}/dist-common/py3-headers/
    fi

    if [[ "${use_musl}" = "true" && -n "${build_arm64_cross_musl}" ]]; then
        cp ${musl_x86_64_sysroot} ${SOONG_OUT}/musl-sysroot-x86_64-unknown-linux-musl.zip
        cp ${musl_x86_sysroot} ${SOONG_OUT}/musl-sysroot-i686-unknown-linux-musl.zip
        cp ${musl_arm_sysroot} ${SOONG_OUT}/musl-sysroot-arm-unknown-linux-musleabihf.zip
        cp ${musl_arm64_sysroot} ${SOONG_OUT}/musl-sysroot-aarch64-unknown-linux-musl.zip
    fi

    if [[ $OS == "linux" && -n "${build_asan}" ]]; then
        # Build ASAN versions
        export ASAN_OPTIONS=detect_leaks=0
        cat > ${SOONG_OUT}/soong.variables << EOF
{
    "Allow_missing_dependencies": true,
    "HostArch":"x86_64",
    ${secondary_arch}
    "SanitizeHost": ["address"],
    "VendorVars": {
        "art_module": {
            "source_build": "true"
        }
    }
}
EOF

        export ASAN_SYMBOLIZER_PATH=${PWD}/prebuilts/clang/host/linux-x86/llvm-binutils-stable/llvm-symbolizer

        # Clean up non-ASAN installed versions
        rm -rf ${SOONG_HOST_OUT}

        # Build everything with ASAN
        build/soong/soong_ui.bash --make-mode --soong-only --skip-config ${skip_soong_tests} \
            ${asan_binaries} \
            ${SOONG_HOST_OUT}/nativetest64/ninja_test/ninja_test \
            ${SOONG_HOST_OUT}/nativetest64/ckati_find_test/ckati_find_test

        # Run ninja tests
        ${SOONG_HOST_OUT}/nativetest64/ninja_test/ninja_test

        # Run ckati tests
        ${SOONG_HOST_OUT}/nativetest64/ckati_find_test/ckati_find_test

        # Copy arch-specific binaries
        mkdir -p ${SOONG_OUT}/dist/asan/bin
        cp ${asan_binaries} ${SOONG_OUT}/dist/asan/bin/
        cp -R ${SOONG_HOST_OUT}/lib* ${SOONG_OUT}/dist/asan/
    fi

    # Package arch-specific prebuilts
    ${SOONG_OUT}/dist/bin/soong_zip -o ${OUT_DIR}/build-prebuilts.zip -C ${SOONG_OUT}/dist -D ${SOONG_OUT}/dist

    if [[ ${use_musl} = "true" && -n "${build_arm64_cross_musl}" ]]; then
        # Package cross-compiled prebuilts
        ${SOONG_OUT}/dist/bin/soong_zip -o ${OUT_DIR}/build-arm64-prebuilts.zip -C ${SOONG_OUT}/dist-arm64 -D ${SOONG_OUT}/dist-arm64
    fi

    if [[ -n "${build_java}" || -n "${build_native}" ]]; then
        # Package common prebuilts
        ${SOONG_OUT}/dist/bin/soong_zip -o ${OUT_DIR}/build-common-prebuilts.tmp.zip -C ${SOONG_OUT}/dist-common -D ${SOONG_OUT}/dist-common
        ${SOONG_OUT}/dist/bin/merge_zips ${OUT_DIR}/build-common-prebuilts.zip ${OUT_DIR}/build-common-prebuilts.tmp.zip ${OUT_DIR}/py3-stdlib.zip
    fi
fi

if [ -z "${skip_soong_tests}" ]; then
    build/soong/scripts/run-soong-tests-with-go-tools.sh
fi

# Go
if [ -n "${build_go}" ]; then
    GO_OUT=${OUT_DIR}/obj/go
    rm -rf ${GO_OUT}
    mkdir -p ${GO_OUT}
    cp -a ${TOP}/toolchain/go/* ${GO_OUT}/
    rm -f ${GO_OUT}/update_prebuilts.sh
    (
        cd ${GO_OUT}/src
        export GOROOT_BOOTSTRAP=${TOP}/prebuilts/go/${OS}-${ARCH}
        export GOROOT_FINAL=./prebuilts/go/${OS}-${ARCH}
        export GO_TEST_TIMEOUT_SCALE=100
        export GODEBUG=installgoroot=all
        ./make.bash
        rm -rf ../pkg/bootstrap
        rm -rf ../pkg/obj
        GOROOT=$(pwd)/.. ../bin/go install -race std
    )
    ${SOONG_OUT}/dist/bin/soong_zip -o ${OUT_DIR}/go.zip -C ${GO_OUT} -D ${GO_OUT}
fi

if [ -n "${DIST_DIR}" ]; then
    mkdir -p ${DIST_DIR} || true

    if [ -n "${build_soong}" ]; then
        cp ${OUT_DIR}/build-prebuilts.zip ${DIST_DIR}/
        if [ -n "${build_java}" ]; then
            cp ${OUT_DIR}/build-common-prebuilts.zip ${DIST_DIR}/
        fi
        cp ${SOONG_OUT}/docs/*.html ${DIST_DIR}/
        if [[ ${use_musl} = "true" && -n "${build_arm64_cross_musl}" ]]; then
            cp ${OUT_DIR}/build-arm64-prebuilts.zip ${DIST_DIR}/

            cp ${SOONG_OUT}/musl-sysroot-x86_64-unknown-linux-musl.zip ${DIST_DIR}/
            cp ${SOONG_OUT}/musl-sysroot-i686-unknown-linux-musl.zip ${DIST_DIR}/
            cp ${SOONG_OUT}/musl-sysroot-arm-unknown-linux-musleabihf.zip ${DIST_DIR}/
            cp ${SOONG_OUT}/musl-sysroot-aarch64-unknown-linux-musl.zip ${DIST_DIR}/
        fi
    fi
    if [ -n "${build_go}" ]; then
        cp ${OUT_DIR}/go.zip ${DIST_DIR}/
    fi
fi
