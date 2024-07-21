# drmpp

DRM Client Library supporting KMS and GBM scenarios

### Creating a compiled keymap file

Running under CLI you will need a compiled xkb file located at `$HOME/.xkb/keymap.xkb`

You can create on a desktop machine running X11 or Wayland:

```
mkdir -p $HOME/.xkb/keymap.xkb
xkbcomp $DISPLAY $HOME/.xkb/keymap.xkb
```
Ignore any warnings

Copy `$HOME/.xkb/keymap.xkb` to `$HOME/.xkb/keymap.xkb`

### Troubleshooting

If you ever see

```
Failed to open /dev/input/event2 (Permission denied)
Failed to open /dev/input/event17 (Permission denied)
...
```

You user has not been added to the `input` group

```
sudo usermod -a -G input $USER
```
Power cycle machine for group to be applied