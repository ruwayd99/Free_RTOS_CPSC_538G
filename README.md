# Build instructions (VS Code Extension)
### Require:
- VS Code "Raspberry Pi Pico" extension installed
- VS Code CMake extension installed
- Linux system (this was tested on Ubuntu 22.04)
- Pico SDK installed 

### Steps:
1. Open our project root directory (this directory) in VS Code
2. `ctrl+shift+P` and run the following one-by-one:
    - "CMake: select a kit" --> choose "Pico"
    - "CMake: delete cache and reconfigure"
    - "CMake: build"
3. Binary `.uf2` files should be in `build/FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard`









# In case of error...
In case of build errors, sorry about that. Please reach out to us at:
- ruwaydafeef99@gmail.com
- michaellin0902@gmail.com

if you'd like!