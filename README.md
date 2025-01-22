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



## Note
Please note that,
1. To proceed with the demo, you need an **OpenAI API key**, and you must possess valid tokens to access the OpenAI server.
2. To provide the WIFI credentials and the OpenAI secret key, please follow the on display prompts to proceed.
3. Additionally, as a result of **OpenAI's restrictions**, this particular example cannot be supported within Mainland China.
4. ChatGPT is a large language model that is unable to distinguish between Chinese Jian ti (简体) and Fan ti (繁體) characters. Due to the constraints of LVGL (Light and Versatile Graphics Library), this project currently only supports Jian Ti (simplified Chinese characters).
