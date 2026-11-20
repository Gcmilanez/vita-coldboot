# vita-coldboot (`coldboot_reset.skprx`)

A lightweight PS Vita kernel plugin (`*KERNEL`) for HENkaku Ensō that automatically performs a single **Cold Reset** (`kscePowerRequestColdReset`) on startup and then lets the system run normally.

## What Problem It Solves

On some PS Vita consoles, turning the device on after a full shutdown can leave the system prone to random crashes or minor graphical artifacts on screen, while rebooting via HENkaku Settings resolves the instability.

`coldboot_reset.skprx` automates that HENkaku-style reboot right when the console powers on, so it enters a stable state without requiring a manual restart every time.

## What It Does

1. **First boot (power-on):**
   * Waits for `SceSyscon` and `SceShell` (LiveArea) to initialize, plus a `1.5 s` settle delay.
   * Writes and syncs a temporary flag file at `ur0:tai/.coldboot_done`.
   * Verifies the flag exists on disk and triggers `kscePowerRequestColdReset()`.
2. **Second boot (after the automatic reset):**
   * Detects and deletes `ur0:tai/.coldboot_done`.
   * Logs both boot states to `ur0:tai/coldboot_reset.log`.
   * Exits its worker thread cleanly, leaving zero background overhead.

It only runs during real boots (power-on or reboot) and does not trigger when waking from standby.

## Safety Features

* **Safe Mode / Update / BSOD guard:** Automatically skips execution in Safe Mode, Update Mode, or post-BSOD reboots.
* **Anti-bootloop check:** Aborts the reset if `ur0:tai/.coldboot_done` cannot be written, synced, and verified on disk.
* **Button bypass:**
  * Hold **`SELECT`** or **`R`** during startup to skip the automatic reset for that boot.
  * Hold **`L`** on boot to bypass all `taiHEN` plugins as usual.

## Installation

1. Download `coldboot_reset.skprx` from the [Releases](https://github.com/Gcmilanez/vita-coldboot/releases) page.
2. Copy `coldboot_reset.skprx` to `ur0:/tai/coldboot_reset.skprx` (via VitaShell / FTP).
3. Add it under `*KERNEL` in `ur0:/tai/config.txt`:

```text
*KERNEL
ur0:tai/0syscall6.skprx
ur0:tai/nonpdrm.skprx
ur0:tai/coldboot_reset.skprx
```

4. Power off the PS Vita completely and turn it back on.
   * After reaching LiveArea for ~1.5 s, the console will reboot once automatically and stay ready for normal use on the second boot.
   * Check `ur0:/tai/coldboot_reset.log` in VitaShell to confirm `boot1_coldreset` followed by `boot2_ready`.

## Building from Source (VitaSDK)

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
mkdir -p build && cd build
cmake ..
make
```
