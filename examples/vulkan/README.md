# Vulkan Examples (WIP)

## vk-khr-inp

This example uses Vulkan KHR (Display KHR)

## vk-kms-inp

This example uses Vulkan KMS (DRM+GBM+Vulkan)

### Notes

The KHR enumeration latency can be over 2 seconds for some calls. This has been seen on Ubuntu 20 Thinkpad 2 laptop with
Intel+NVidia GPU laptop. No customizations.

In the case of Fedora 41 on a Ryzen 9 (integrated GPU) no delay is experienced.