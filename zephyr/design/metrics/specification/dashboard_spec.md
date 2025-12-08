# Dashboards

# Overview

Filters: Fixed time ranges (last 1d, 7d, 14d, 30d, 90d)

| **Widget** | **Aggregation** | **Visualization** |
| --- | --- | --- |
| **Connected devices** | COUNT - Device is considered connected if MQTT connection was established at least once within last 7 days OR device is currently connected. | Line chart |
| **Devices by State** | COUNT by State (Healthy, Crashing) - Device is considered Healthy if no crash dump was send within last 7 days. | Stacked chart |
| **Devices by Firmware and Firmware version** | COUNT by Firmware | Firmware version;
Percentage from “Connected devices” | Table |
| **Logs ingested by Severity** | COUNT by Severity | Stacked chart |
| **Crash reports received** | COUNT | Line chart |

## Fleet stability

Filters: Firmware, Firmware version (default: ALL)

| **Widget** | **Aggregation** | **Visualization** |
| --- | --- | --- |
| **Number of crashes** | COUNT | Line chart |
| **Crash-free devices** | Percentage, compared to the previous period | Single number |
| **Crash-free hours** | N/A | Single number |
| **List of crashing devices with reason of crash** | N/A | Table |
| **Crashes by Firmware and Firmware version** | COUNT by Firmware | Firmware version;
Percentage from Active devices count | Table |
| **Reboots by Reason** | COUNT by Reason (not sure how to categorize it yet) | Stacked chart |

# Device detail

Filters: Fixed time ranges (last 1d, 7d, 14d, 30d, 90d)

| **Widget** | **Aggregation** | **Visualization** |
| --- | --- | --- |
| **Number of crashes** | COUNT | Line chart |
| **Crashes by Firmware and Firmware version** | COUNT by Firmware | Firmware version | Table |
| **Reboots by Reason** | COUNT by Reason (not sure how to categorize it yet) | Stacked chart |
| **Uptime** | N/A | Single number (e.g. 2d 4h 10m) |
| **Memory usage** | ? | Line chart or Stacked (e.g. 100MB free, 200MB total) |
| **Heap usage** | ? | Line chart or Stacked (e.g. 100MB free, 200MB total) |
| **CPU utilization** | ? | Line chart |
| **Device connects/disconnects** | ? | Line chart |
| **Data received/sent** | ? | Stacked chart |
| **Logs received/sent** | COUNT | Stacked chart |
| **Logs dropped** | COUNT | Line chart |
| **Metrics exported** | COUNT | Line chart |
| **Measurements dropped** | COUNT | Line chart |

# Considerations

- We should have an understanding when the firmware of a device changes to project firmware changes into charts. It provides crucial information. E.g., higher CPU load after firmware update. (This fully relies on users’ input for now, will be automatic once we support FOTA)
    
    ![image.png](image.png)
    

# Notes

![image.png](image%201.png)

![image.png](image%202.png)

https://insights.espressif.com/