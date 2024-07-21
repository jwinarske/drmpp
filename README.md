# drmpp

DRM Client Library supporting KMS and GBM scenarios

### Troubleshooting

If you ever see
```
Failed to open /dev/input/event2 (Permission denied)
Failed to open /dev/input/event17 (Permission denied)
...
```

Add user to the input group
```
sudo usermod -a -G input $USER
```
