# Test skeleton

This project is only for testing the component that's why it's main is inside the `test/main/example_unit_test.c`. For testing we are using the ESP-IDF Unity framework.
## Configurations

## Instructions for running the example.
- In your terminal from inside this folder run
```bash
cd test/
idf.py build
idf.py flash
```
It will start running all the tests. Right now the sdkconfig.defaults is configured for qemu testing and hardwired ethernet based testing. Which can be configured or changed using the below.

```bash
idf.py menuconfig
```
## Instructions for adding spotflow testing to custom project.
- In the project Cmake file add 
``` text
set(TEST_COMPONENTS "spotflow/device_sdk" CACHE STRING "Spotflow component testing")
```

And it will add all the supported tests automatically from the spotflow component.