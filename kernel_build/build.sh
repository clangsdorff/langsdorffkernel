#!/bin/bash

XY_VERSION="@3.0"

set -e

if [ -z "$1" ]; then
    echo "Please exec from root directory"
    exit 1
fi
cd "$1"

if [ "$(uname -m)" != "x86_64" ]; then
  echo "This script requires an x86_64 (64-bit) machine."
  exit 1
fi

export PATH="$(pwd)/kernel_build/bin:$PATH"

# Configs
OUTDIR="$(pwd)/out"
MODULES_OUTDIR="$(pwd)/modules_out"
TMPDIR="$(pwd)/kernel_build/tmp"

IN_PLATFORM="$(pwd)/kernel_build/vboot_platform"
IN_DLKM="$(pwd)/kernel_build/vboot_dlkm"
IN_DTB="$OUTDIR/arch/arm64/boot/dts/exynos/s5e3830.dtb"

PLATFORM_RAMDISK_DIR="$TMPDIR/ramdisk_platform"
DLKM_RAMDISK_DIR="$TMPDIR/ramdisk_dlkm"
MODULES_DIR="$DLKM_RAMDISK_DIR/lib/modules"

MKBOOTIMG="$(pwd)/kernel_build/mkbootimg/mkbootimg.py"
MKDTBOIMG="$(pwd)/kernel_build/dtb/mkdtboimg.py"
AVBTOOL="$(pwd)/kernel_build/avbtool"

OUT_KERNELZIP="$(pwd)/kernel_build/langsdorff${XY_VERSION}_a14.zip"
OUT_KERNELTAR="$(pwd)/kernel_build/langsdorff${XY_VERSION}_a14.tar"
OUT_KERNEL="$OUTDIR/arch/arm64/boot/Image"
OUT_BOOTIMG="$(pwd)/kernel_build/zip/boot.img"
OUT_VENDORBOOTIMG="$(pwd)/kernel_build/zip/vendor_boot.img"
OUT_DTBIMAGE="$TMPDIR/dtb.img"
BUILD_TOOLS_DIR="$(pwd)/kernel_build/build-tools"
BUILD_TOOLS_REPO="https://android.googlesource.com/platform/prebuilts/build-tools"

# Kernel-side
BUILD_ARGS="LOCALVERSION=-langsdorff${XY_VERSION} KBUILD_BUILD_USER=CommandLangsdorff KBUILD_BUILD_HOST=langsdorff"

kfinish() {
    rm -rf "$TMPDIR"
#    rm -rf "$OUTDIR"
    rm -rf "$MODULES_OUTDIR"
}

kfinish

DIR="$(readlink -f .)"
PARENT_DIR="$(readlink -f ${DIR}/..)"

export CROSS_COMPILE="$PARENT_DIR/clang-r450784d/bin/aarch64-linux-gnu-"
export CC="$PARENT_DIR/clang-r450784d/bin/clang"
export PATH="$PARENT_DIR/build-tools/path/linux-x86:$PARENT_DIR/clang-r450784d/bin:$PATH"
export LLVM_LDFLAGS="-fuse-ld=mold"
export DTC_FLAGS="-@"
export PLATFORM_VERSION=14
export LLVM=1
export DEPMOD=depmod
export ARCH=arm64
export TARGET_SOC=s5e3830


make -j$(nproc --all) -C $(pwd) O=out $BUILD_ARGS a14_defconfig 
make -j$(nproc --all) -C $(pwd) O=out $BUILD_ARGS dtbs
make -j$(nproc --all) -C $(pwd) O=out $BUILD_ARGS
make -j$(nproc --all) -C $(pwd) O=out INSTALL_MOD_STRIP="--strip-debug --keep-section=.ARM.attributes" INSTALL_MOD_PATH="$MODULES_OUTDIR" modules_install

rm -rf "$TMPDIR"
rm -f "$OUT_BOOTIMG"
rm -f "$OUT_VENDORBOOTIMG"
mkdir "$TMPDIR"
mkdir -p "$MODULES_DIR/0.0"
mkdir "$PLATFORM_RAMDISK_DIR"

cp -rf "$IN_PLATFORM"/* "$PLATFORM_RAMDISK_DIR/"
mkdir -p "$PLATFORM_RAMDISK_DIR/first_stage_ramdisk"
cp -f "$PLATFORM_RAMDISK_DIR/fstab.s5e3830" "$PLATFORM_RAMDISK_DIR/first_stage_ramdisk/fstab.s5e3830"

if ! find "$MODULES_OUTDIR/lib/modules" -mindepth 1 -type d | read; then
    echo "Unknown error!"
    exit 1
fi

missing_modules=""

for module in $(cat "$IN_DLKM/modules.load"); do
    i=$(find "$MODULES_OUTDIR/lib/modules" -name $module);
    if [ -f "$i" ]; then
        cp -f "$i" "$MODULES_DIR/0.0/$module"
    else
	missing_modules="$missing_modules $module"
    fi
done

if [ "$missing_modules" != "" ]; then
        echo "ERROR: the following modules were not found: $missing_modules"
	exit 1
fi

depmod 0.0 -b "$DLKM_RAMDISK_DIR"
sed -i 's/\([^ ]\+\)/\/lib\/modules\/\1/g' "$MODULES_DIR/0.0/modules.dep"
cd "$MODULES_DIR/0.0"
for i in $(find . -name "modules.*" -type f); do
    if [ $(basename "$i") != "modules.dep" ] && [ $(basename "$i") != "modules.softdep" ] && [ $(basename "$i") != "modules.alias" ]; then
        rm -f "$i"
    fi
done
cd "$DIR"

cp -f "$IN_DLKM/modules.load" "$MODULES_DIR/0.0/modules.load"
mv "$MODULES_DIR/0.0"/* "$MODULES_DIR/"
rm -rf "$MODULES_DIR/0.0"

echo "Building dtb image..."
python2 "$MKDTBOIMG" create "$OUT_DTBIMAGE" --custom0=0x00000000 --custom1=0xff000000 --version=0 --page_size=2048 "$IN_DTB" || exit 1

echo "Building boot image..."

$MKBOOTIMG --header_version 4 \
    --kernel "$OUT_KERNEL" \
    --output "$OUT_BOOTIMG" \
    --os_version 14.0.0 \
    --os_patch_level 2025-04 || exit 1

echo "Done!"
echo "Building vendor_boot image..."

cd "$DLKM_RAMDISK_DIR"
find . | cpio --quiet -o -H newc -R root:root | lz4 -9cl > ../ramdisk_dlkm.lz4
cd ../ramdisk_platform
find . | cpio --quiet -o -H newc -R root:root | lz4 -9cl > ../ramdisk_platform.lz4
cd ..
echo "buildtime_bootconfig=enable" > bootconfig

$MKBOOTIMG --header_version 4 \
    --vendor_boot "$OUT_VENDORBOOTIMG" \
    --vendor_bootconfig "$(pwd)/bootconfig" \
    --dtb "$OUT_DTBIMAGE" \
    --vendor_ramdisk "$(pwd)/ramdisk_platform.lz4" \
    --ramdisk_type dlkm \
    --ramdisk_name dlkm \
    --vendor_ramdisk_fragment "$(pwd)/ramdisk_dlkm.lz4" \
    --os_version 14.0.0 \
    --os_patch_level 2025-04 || exit 1

cd "$DIR"

# AVB Key Generation and Footer Addition
if [ ! -f private_key.pem ]; then
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:4096 -out private_key.pem
    openssl rsa -pubout -in private_key.pem -out public_key.pem
fi

# boot partition calculate
BOOT_SIZE=$(wc -c < "$OUT_BOOTIMG")
BOOT_PARTITION_SIZE=$(( (BOOT_SIZE + 2097152 + 4095) / 4096 * 4096 ))
$AVBTOOL add_hash_footer --image "$OUT_BOOTIMG" --partition_name boot --partition_size "$BOOT_PARTITION_SIZE" --key private_key.pem --flags 0x2 --algorithm SHA256_RSA4096

#vendor boot partition calculate
VENDOR_BOOT_SIZE=$(wc -c < "$OUT_VENDORBOOTIMG")
VENDOR_BOOT_PARTITION_SIZE=$(( (VENDOR_BOOT_SIZE + 2097152 + 4095) / 4096 * 4096 ))
$AVBTOOL add_hash_footer --image "$OUT_VENDORBOOTIMG" --partition_name vendor_boot --partition_size "$VENDOR_BOOT_PARTITION_SIZE" --key private_key.pem --algorithm SHA256_RSA4096

#create vbmeta
$AVBTOOL make_vbmeta_image --output "$(pwd)/kernel_build/zip/vbmeta.img" --key private_key.pem --algorithm SHA256_RSA4096 --include_descriptors_from_image "$OUT_BOOTIMG" --include_descriptors_from_image "$OUT_VENDORBOOTIMG"

echo "Done!"

echo "Building zip..."
cd "$(pwd)/kernel_build/zip"
rm -f "$OUT_KERNELZIP"
brotli --quality=6 -c boot.img > boot.br
brotli --quality=6 -c vendor_boot.img > vendor_boot.br
brotli --quality=6 -c vbmeta.img > vbmeta.br
zip -r9 -q "$OUT_KERNELZIP" META-INF boot.br vendor_boot.br vbmeta.br
rm -f boot.br vendor_boot.br vbmeta.br
cd "$DIR"
echo "Done! Output: $OUT_KERNELZIP"

echo "Building tar..."
cd "$(pwd)/kernel_build"
rm -f "$OUT_KERNELTAR"
lz4 -c -12 -B6 --content-size "$OUT_BOOTIMG" > boot.img.lz4
lz4 -c -12 -B6 --content-size "$OUT_VENDORBOOTIMG" > vendor_boot.img.lz4
lz4 -c -12 -B6 --content-size "$(pwd)/zip/vbmeta.img" > vbmeta.img.lz4
tar -cf "$OUT_KERNELTAR" boot.img.lz4 vendor_boot.img.lz4 vbmeta.img.lz4
rm -f boot.img.lz4 vendor_boot.img.lz4 vbmeta.img.lz4
cd "$DIR"
echo "Done! Output: $OUT_KERNELTAR"

echo "Cleaning..."
rm -f "${OUT_VENDORBOOTIMG}" "${OUT_BOOTIMG}" "$(pwd)/kernel_build/zip/vbmeta.img"
kfinish
