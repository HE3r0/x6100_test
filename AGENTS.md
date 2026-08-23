# AGENTS.md — x6100_test / test/x6100 (MAC6100 baseline GUI)

> Prepared by **Cursor AI (Auto / Composer)** for the MAC6100 project, with the project owner (SO0BAD / HE3r0).

This tree is the **active MAC6100 GUI baseline** (gdyuldin v0.34.2, no BT APP button).

- Buildroot tag: `mac6100-baseline-1` on `AetherX6100Buildroot` / `bootlogo`
- Hub docs: `C:\Projects\Mac6100\docs\ai\baseline.md`
- Reference image: `C:\Projects\sdcard25.img`

## Critical

- Edit **this** WSL clone (`~/Projects/test/x6100`), not `~/Projects/x6100_gui` (obsolete)
- After changes, rebuild from Aether: `x6100-gui-dirclean` → `x6100-gui-rebuild` → `make`
- Start new features from Buildroot tag `mac6100-baseline-1` + this tree

## Active feature branch

- Branch: `feature/bluetooth-ui` — Bluetooth GUI (start from MAC6100 baseline)
- Soft-button shell first; BlueZ actions come later
