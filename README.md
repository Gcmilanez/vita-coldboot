# vita-coldboot (`coldboot_reset.skprx`)

A lightweight PS Vita kernel plugin (`*KERNEL`) for HENkaku Ensō that automatically performs a single **Cold Reset** (`kscePowerRequestColdReset`) on startup and then lets the system run normally.

## What Problem It Solves

On some PS Vita consoles, turning the device on after a full shutdown can leave the system prone to random crashes or minor graphical artifacts on screen, while rebooting via HENkaku Settings resolves the instability.

`coldboot_reset.skprx` automates that HENkaku-style reboot right when the console powers on, so it enters a stable state without requiring a manual restart every time.

## What It Does

1. **First boot (power-on):**
   * Waits for `SceSyscon` and `SceShell` (LiveArea) to initialize, plus a `2.0 s` settle delay.
   * Confirms via `SceKblParam` (`wakeup_factor & 0x7F == 0x14`) that this is a genuine power-on cold boot.
   * Writes and syncs a temporary flag file at `ur0:tai/.coldboot_done`.
   * Verifies the flag exists on disk and triggers `kscePowerRequestColdReset()`.
2. **Second boot (after the automatic reset):**
   * Detects the software-reset `wakeup_factor` (`0x04`) and deletes `ur0:tai/.coldboot_done`.
   * Logs both boot states to `ur0:tai/coldboot_reset.log`.
   * Exits its worker thread cleanly, leaving zero background overhead.

It only runs on genuine power-on cold boots (skips manual reboots and standby/resume).

## Boot States (`wakeup_factor`)

The PS Vita bootloader passes a `SceKblParam` block containing the Syscon `wakeup_factor`, which distinguishes a cold power-on from a software reset:

| Boot Type | `wakeup_factor` (`& 0x7F`) | Example Log (`ur0:/tai/coldboot_reset.log`) | Plugin Action |
| :--- | :--- | :--- | :--- |
| **Power-on cold boot** (from full shutdown) | `0xFF14` (`0x14`) | `boot1_coldreset wakeup=0x0000FF14 unk_C0=0x00000010 boot1=0x00000000 boot2=0x00000004` | Writes `ur0:tai/.coldboot_done` and calls `kscePowerRequestColdReset()` |
| **Software Cold Reset** (after plugin or HENkaku reboot) | `0xFF04` (`0x04`) | `boot2_ready wakeup=0x0000FF04 unk_C0=0x00000000 boot1=0x00000000 boot2=0x00000000` | Removes `ur0:tai/.coldboot_done`, logs result, and exits |

Because `kscePowerRequestColdReset()` transitions the console from the `0xFF14` power-on state into `0xFF04`, the plugin naturally ignores manual reboots and prevents boot loops even as a hardware-level safeguard alongside the flag file.

In the `0xFF04` state, the board's voltage regulators and capacitors are already charged, and `ScePower` has just performed an orderly teardown of drivers and LiveArea before resetting the SoC.

## Safety Features

* **Hardware + file dual anti-loop guard:** Only triggers on power-on cold boots (`wakeup_factor & 0x7F == 0x14`) when `ur0:tai/.coldboot_done` is absent, and never removes the flag before the reboot completes.
* **Safe Mode / Update / BSOD guard:** Automatically skips execution in Safe Mode, Update Mode, or post-BSOD reboots.
* **Disk verification:** Aborts the reset if `ur0:tai/.coldboot_done` cannot be written, synced, and verified on disk.
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
   * After reaching LiveArea for ~2 s, the console will reboot once automatically and stay ready for normal use on the second boot.
   * Check `ur0:/tai/coldboot_reset.log` in VitaShell to confirm `boot1_coldreset` followed by `boot2_ready`.

## Building from Source (VitaSDK)

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
mkdir -p build && cd build
cmake ..
make
```
