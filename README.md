# CS122A-Final-Project | Embedded Security System
Noah Wisner
Josh
Alex

## Tools and Repos Used
- Professor Knight's FPGA Framebuffer
- Raspberry PI FreeRTOS

## How to Run
1. Launch VSCode
2. "Import Project" using Raspberry Pi Pico Extension
3. Open Command Prompt and upload the following:

```bash
mkdir build
cmake -B build -S .
```
4. Open OSS-CAD-Suite via console (NOTE: ADDING TO YOUR PATH DOES NOT ACTIVATE Yosys CORRECTLY ON WINDOWS)
5. build

```bash
cmake --build build
```