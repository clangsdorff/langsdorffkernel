# langsdorffkernel Touchfix Project for Samsung Galaxy A14 4G (A145F & A145M)
**Features**
- Fixed touchscreen compatibility on GSI builds.
- CPU overclocked to 2210 MHz and GPU overclocked to 1196 MHz.
- Boots into the `energy_aware` governor instead of `schedutil`.
- Reworked GPU DVFS: fixed the dead highspeed jump, retuned thresholds, lowered the CPU floors.
- Every GPU clock step reachable and holdable. 1105 MHz opens at 65% utilization, 1196 MHz at 70%.
- Floor under the GPU clock ceiling, default 1001 MHz.
- Runtime tunables for the GPU ceiling floor and the governor redirect.
- Samsung input booster, short CPU and memory speed-up on taps, scrolling and key presses.
- Backported upstream menu idle governor, better sleep state choices and faster wake-up.
- Backported lazy RCU from Linux 6.2 with callback offloading on all CPUs, fewer wake-ups while idle.
- Backported batched TLB flushing from Linux 6.5, less stutter while switching apps and swapping.
- Backported PSI optimizations from Linux 6.1, faster memory pressure tracking for lmkd.
- Backported f2fs block age extent cache from Linux 6.2, less garbage collection on the eMMC.
- zram recompression with zstd 1.5.7 and 8-page zsmalloc chains, more apps stay in memory.
- Multi-gen LRU page cache fix, frequently read app files stay cached.
- Official KernelSU v3.3.1 with SUSFS v2.3.0.
- Dex touchpad support for OneUI ROMs.
- Removed firmware checks from check_connection in novatek touchscreen driver to fix 2 seconds delay for RestlessOS treble patchset based GSI's
- Built-in hook for the `${fps_position}` bug in flagship FOD ports.
- AVB and some security checks disabled.
- Merged with android13-5.10-lts, based on Linux 5.10.272.

**No Overclock & Root Version Features**
- Everything in main version, **expect**;
- CPU & GPU overclock.
- Floor under the GPU clock ceiling.
- KernelSU & SUSFS

**Recommendation**
- On One UI ROMs, disabling RAM Plus is recommended for better performance.

**Known Issues**
- Nothing.

# Installation
**Installation — Odin3**
1. Download the latest ".tar" release from the project's GitHub Releases.
3. Power off the device.
4. Boot into Download Mode (connect to PC while holding Volume UP + Volume DOWN).
5. Open Odin3 and place the `.tar` into the `AP` slot.
6. Flash and wait for the device to reboot.

**Installation — TWRP**
1. Download the latest ".zip" release from the project's GitHub Releases.
3. Boot into Recovery Mode (TWRP).
4. Select `Install` → choose the `.zip` file.
5. Flash and reboot.

**Join / Follow**
- Telegram: https://t.me/a14stuffs

**Credits**
- Base kernel: [Gabriel2392](https://github.com/Gabriel2392)
- CPU & GPU Overclock, `${fps_position}` hook, updated sdfat driver: [Gabriel2392](https://github.com/Gabriel2392)
- Dex touchpad support: [rsuntk](https://github.com/rsuntk)
- KernelSU: [tiann](https://github.com/tiann/)

# Download
- Download (version)**e** for Enforcing, **en** for no overclock & root variant
- Download (version)**p** for Permissive, **pn** for no overclock & root variant
- Releases: [Here](https://github.com/clangsdorff/langsdorffkernel/releases)

**Device & Notes**
- Device: A145F / A145M (Exynos 850)
- Bootloader: A145FXXU9DYG5
