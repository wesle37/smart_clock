# Smart clock project

Code for a smart alarm clock using ESP-IDF and LVGL.

Hardware:
ESP32-C3
Hosyond ST7796 capacitive tft

## Requirements
- [ESP-IDF v6.2](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32c3/get-started/index.html)
- CMake
- Ninja

## Build
If using ESP-IDF venv,
```bash
idf.py set-target esp32
idf.py build
idf.py flash
```

If using VSCode, install the ESP-IDF extension. Edit the .vscode/c_cpp_properties.json and add
` "<user>/.espressif/v6.0.2/esp-idf/components/**" `
to `includePath`. Then build.