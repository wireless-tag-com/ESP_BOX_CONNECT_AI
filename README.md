# BOX_AI DEMO

| Board             | Support Status |
| ----------------- | -------------- |
| ESP32-S3-BOX      | YES            |
| ESP32-S3-BOX-Lite | YES            |
| ESP32-S3-BOX-3    | YES            |


在这个示例中，我们结合使用大模型和ESP-BOX来创建一个基于语音的聊天机器人。
ESP-BOX是一个包含ESP32-S3微控制器的设备或系统。
此实现的目的是使用户能够使用口语与聊天机器人进行交流。

## 使用的ESP_IDF版本
* ESP-IDF version [release/v5.3](https://github.com/espressif/esp-idf/tree/release/v5.3)

### 克隆对应版本的 ESP_IDF 

```
git clone -b release/v5.3 --recursive https://github.com/espressif/esp-idf.git
```

## **Hardware Required**

* A一个 ESP32-S3-BOX，ESP32-S3-BOX-Lite 或 ESP32-S3-BOX-3
* 一根用于供电和编程的 USB-C 数据线


## 硬件选择

使用时要在menuconfig选择合适的硬件 ([ESP32-S3-BOX](https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box/hardware_overview_for_box.md), [ESP32-S3-BOX-Lite](https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_lite/hardware_overview_for_lite.md) or [ESP32-S3-BOX-3](https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_3/hardware_overview_for_box_3.md)) 

## 配网方式
uart 配网，在 main/app/app_uart.c 中找到对应tx rx 管脚，使用串口板和box上的对应管脚对接，并通过串口调试助手发送如下指令
{
"cmd":0,
"ssid":"wifi账号",
"password":"wifi密码"
}


## Note
使用demo 需要联系商务获取测试用的 productID 和 deviceID
