# TriggerScript

A Windows application that allows you to trigger scripts based on process events or time schedules.

## Features

- Monitor process start/end events
- Schedule scripts to run at specific times
- Run scripts on system startup
- Dark mode interface
- System tray integration
- Persistent configuration

## Requirements

- Windows OS
- Visual Studio (for building)
- C++ development tools

## Building

1. Open the solution in Visual Studio
2. Build the solution in Release mode
3. The executable will be created in the Release folder

## Usage

1. Run TriggerScript.exe
2. Click "Add" to create a new trigger
3. Select the script file and trigger type:
   - When process starts
   - When process ends
   - At specific time
   - On system startup
4. Configure the trigger details
5. Click OK to save

## Files

- `TriggerScript.cpp` - Main application source
- `resource.rc` - Resource file for dialog layout
- `resource.h` - Resource definitions
- `config.txt` - Configuration file (auto-generated)

## License

MIT License - Feel free to use and modify as needed. 
