# langsdorffkernel Touchfix Project for Samsung Galaxy A14 4G (A145F)
**Features**
- Fixed touchscreen compatibility on GSI builds.
- CPU overclocked to 2210 MHz and GPU overclocked to 1196 MHz.
- Updated GPU driver (bifrost r51p0)
- LZ4 compressed ZRAM.
- Official KernelSU with SUSFS.
- Dex touchpad support for OneUI ROMs.
- Built-in hook for the `${fps_position}` bug in flagship FOD ports.
- Updated `sdfat` driver to v2.8.1 (from a33x).
- AVB and some security checks disabled (varies by build type).
- Based on Linux 5.10.236.

**Known Issues**
- Nothing.

# Installation
**Installation — Odin3**
1. Download the latest release from the project's GitHub Releases.
3. Power off the device.
4. Boot into Download Mode (connect to PC while holding Volume UP + Volume DOWN).
5. Open Odin3 and place the `.tar` into the `AP` slot.
6. Flash and wait for the device to reboot.

**Installation — TWRP**
1. Download the latest release from the project's GitHub Releases.
3. Boot into Recovery Mode (TWRP).
4. Select `Install` → choose the `.zip` file.
5. Flash and reboot.

**Join / Follow**
- Telegram: https://t.me/a14stuffs

**Credits**
- Base kernel: [Gabriel2392](https://github.com/Gabriel2392)
- Dex touchpad support: [rsuntk](https://github.com/rsuntk)
- KernelSU: [tiann](https://github.com/tiann/)
- r51p0 driver: [xxmustafacooTR](https://github.com/xxmustafacooTR/)

# Download
- Download (version)**e** for Enforcing
- Download (version)**p** for Permissive
- Releases: [Here](https://github.com/clangsdorff/langsdorffkernel/releases)

**Device & Notes**
- Device: A145F (Exynos 850)
- Bootloader: U9
