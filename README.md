# drmpp
DRM Client Library

## Pre-Requisites

### 1. User must be added to the `input` group (if not root)

    sudo usermod -a -G input $USER

Power cycle machine for group to be applied

### 2. Keymap File

Locate your compiled keymap file at `$HOME/.xkb/keymap.xkb`

You can create on a desktop machine running X11 or Wayland:

    mkdir -p $HOME/.xkb
    xkbcomp $DISPLAY $HOME/.xkb/keymap.xkb

Ignore any warnings

### Frame buffer device access

Add user to the video group

    sudo adduser $USER video
    newgrp video

### TTY Terminal

The system compositor has control of the DRM driver when active.  You can suspend this access by switching to a TTY terminal.

On Fedora and Ubuntu you can switch to a TTY terminal using the keyboard combo:

    Ctl+Alt+F3


You can switch back to the System GUI using:

    Ctl+Alt+F2

### Troubleshooting
If you see permission denied errors the user has not been added to the input group:

    Failed to open /dev/input/event2 (Permission denied)
    Failed to open /dev/input/event17 (Permission denied)
