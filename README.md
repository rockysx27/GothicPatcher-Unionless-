# GothicPatcher-Unionless

## Overview

This autopatcher automates the download, extraction, and setup of essential patches and enhancements for **Gothic I** and **Gothic II**, streamlining the patching process.

> **Note:** Manual interaction is still required to complete the installers downloaded from World of Gothic.

---

## What the autopatcher does

### Gothic I

- Downloads the required installers (`gothic_patch_108k.exe`, `gothic1_playerkit-1.08k.exe`, `G1Classic-SystemPack-1.8.exe`).
- Launches each installer for manual completion.
- Installs the `SystemPack-1.9` patch automatically.
- Downloads and extracts `GD3D11-v17.8-dev26.zip` to `Gothic/System`.
- Detects AMD GPUs and installs the `dvzk 32bit` Direct3D 11 driver if needed.
- Applies the 4GB RAM patch.
- Downloads and moves `FCH_Models_G1.VDF` to `Gothic/Data` for improved head meshes.
- Updates the `Gothic.ini` configuration file.

### Gothic II

- Downloads the required installers (`gothic2_fix-2.6.0.0-rev2.exe`, `gothic2_playerkit-2.6f.exe`, `G2NoTR-SystemPack-1.8.exe`).
- Launches each installer for manual completion.
- Installs the `SystemPack-1.9` patch automatically.
- Downloads and extracts `GD3D11-v17.8-dev26.zip` to `Gothic/System`.
- Detects AMD GPUs and installs the `dvzk 32bit` Direct3D 11 driver if needed.
- Applies the 4GB RAM patch.
- Downloads and extracts `Normalmaps_Original.zip` to `Gothic II/System/GD3D11/textures/replacements`.
- Downloads and moves `FCH_Models_G2.VDF` to `Gothic II/Data` for improved head meshes.
- Updates the `Gothic.ini` configuration file.

---

Run the patcher and follow the prompts to complete the necessary installers. The rest is handled automatically.