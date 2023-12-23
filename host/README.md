# Host Software for Raspberry Pi Zero 2 W

This directory contains the software and configuration files needed to run [Project Name] on a Raspberry Pi Zero 2 W. The setup includes a custom daemon, systemd configurations, and audio sink management via WirePlumber.

## Table of Contents

1. [Overview](#overview)
2. [Files](#files)
3. [Dependencies](#dependencies)
4. [Setup Instructions](#setup-instructions)
5. [Systemd Configuration](#systemd-configuration)
6. [Lua Script Installation](#lua-script-installation)

---

## Overview

The host software includes:
- A daemon for managing the project's main functionality.
- Systemd user service files for managing the daemon at startup.
- A Lua script for WirePlumber to configure mono audio sinks automatically.

---

## Files

- `Makefile`: Builds the daemon binary.
- `mono-sinks.lua`: Lua script for WirePlumber, located in `host/mono-sinks.lua`.
- `systemd/`: Contains systemd configuration files for running the daemon as a user service.

---

## Dependencies

The following dependencies must be installed on the Raspberry Pi Zero 2 W:

1. **PipeWire**: Version 1.1.0 built from source.
   - Commit: `8a1ed019231cdc6c8478384e70bfab84565edff3`
   - [PipeWire GitLab Repository](https://gitlab.freedesktop.org/pipewire/pipewire)

2. **WirePlumber**: Version 0.4.17 built from source.
   - Commit: `d3eb77b292655cef333a8f4cab4e861415bc37c2`
   - [WirePlumber GitLab Repository](https://gitlab.freedesktop.org/pipewire/wireplumber)

3. **Baresip**: Built from source.
   - Commit: `8c6f02a689ac8720692ad20d8ea7b0c9cc93677a`
   - [Baresip GitHub Repository](https://github.com/baresip/baresip)

---

## Setup Instructions

### Step 1: Build the Daemon
1. Run the `Makefile` to compile the daemon:
   ```bash
   make
   ```

2. The compiled binary will be placed in the current directory.

### Step 2: Systemd Configuration

The systemd configuration files are intended for user-level services and should be placed in `~/.config/systemd/user/`.

1. Copy the systemd service files:
   ```bash
   mkdir -p ~/.config/systemd/user
   cp host/systemd/*.service ~/.config/systemd/user/
   ```

2. Reload systemd to recognize the new services:
   ```bash
   systemctl --user daemon-reload
   ```

3. Enable and start the service:
   ```bash
   systemctl --user enable <service-name>
   systemctl --user start <service-name>
   ```

### Step 3: Lua Script Installation

1. Copy the Lua script to the WirePlumber scripts directory:
   ```bash
   sudo cp host/mono-sinks.lua /usr/local/share/wireplumber/scripts/mono-sinks.lua
   ```

2. Restart the WirePlumber service to apply the script:
   ```bash
   systemctl --user restart wireplumber
   ```