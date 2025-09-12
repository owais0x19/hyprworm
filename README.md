# hyprworm

A fast and lightweight window switcher for Hyprland built in C. Quickly switch between open windows with a clean, configurable interface that works with any dmenu-compatible launcher.

## Why I Built This

I was tired of switching workspace to workspace to find mine so I looked into existing window switchers for Hyprland, but they were either too heavy, too slow, or didn't give me the control I wanted. I needed something that:

- **Just works** - no complex setup or dependencies
- **Fast** - instant window switching without lag
- **Configurable** - customize launcher, display format, and behavior
- **Lightweight** - minimal resource usage
- **Reliable** - robust error handling and logging

So I built hyprworm as a systems programming exercise that demonstrates Unix domain sockets, JSON parsing, process management, and the fork/exec/pipe model - all while solving a real problem.

## Features

- **Universal launcher support** - works with fuzzel, wofi, rofi, dmenu, bemenu
- **Configurable display** - show/hide window titles, customize workspace names
- **Workspace aliases** - replace workspace names with custom symbols (emojis, etc.)
- **Robust error handling** - comprehensive logging and graceful failure recovery
- **Memory efficient** - minimal resource usage with proper cleanup
- **Fast** - direct IPC communication with Hyprland

## Installation

### Dependencies

```bash
# Arch Linux
sudo pacman -S cjson

# Ubuntu/Debian
sudo apt install libcjson-dev

# Fedora
sudo dnf install cjson-devel
```

### Build from Source

```bash
git clone https://github.com/liammmcauliffe/hyprworm.git
cd hyprworm
make
sudo make install
```

## Quick Start

1. **Basic usage:**
   ```bash
   hyprworm
   ```

2. **Add to Hyprland config** (`~/.config/hypr/hyprland.conf`):
   ```ini
   bind = SUPER, S, exec, hyprworm
   ```

3. **Customize** by editing `~/.config/hyprworm/config`

## Configuration

Create `~/.config/hyprworm/config`:

```ini
# Launcher command - any dmenu-compatible launcher
launcher = fuzzel --dmenu

# Show window titles in display (true/false)
show_title = false

# Workspace aliases - replace names with custom symbols
workspace_alias_special = "⭐ "
workspace_alias_work = "💼 "
workspace_alias_gaming = "🎮 "

# Logging configuration
log_level = INFO          # ERROR, WARNING, INFO, DEBUG
log_file = /tmp/hyprworm.log  # Optional log file
debug_mode = false        # Enable debug output
```

### Launcher Examples

```ini
# Fuzzel (Wayland)
launcher = fuzzel --dmenu

# Wofi (Wayland)
launcher = wofi --dmenu

# Rofi (X11/Wayland)
launcher = rofi -dmenu

# Dmenu (X11)
launcher = dmenu

# Bemenu (Wayland)
launcher = bemenu
```

### Workspace Aliases

Replace workspace names with custom symbols:

```ini
# Simple replacement
workspace_alias_special = "⭐"

# With whitespace
workspace_alias_work = "💼 "

# Multiple aliases
workspace_alias_1 = "1️⃣"
workspace_alias_2 = "2️⃣"
workspace_alias_3 = "3️⃣"
```

## Usage

### Basic Commands

```bash
# Launch window switcher
hyprworm

# Check configuration
hyprworm --help

# Debug mode
hyprworm --debug
```

### Display Formats

**With titles (`show_title = true`):**
```
[⭐ :communication] firefox: GitHub - hyprworm
[💼 :work] code: README.md - hyprworm
[🎮 :gaming] steam: Steam
```

**Without titles (`show_title = false`):**
```
[⭐ :communication] firefox
[💼 :work] code  
[🎮 :gaming] steam
```

## Advanced Features

### Logging System

Configure logging levels and output:

```ini
# Log levels: ERROR, WARNING, INFO, DEBUG
log_level = INFO

# Optional log file (stderr if not specified)
log_file = /tmp/hyprworm.log

# Enable debug output
debug_mode = true
```

### Error Handling

Hyprworm includes comprehensive error handling:

- **Socket connection failures** - graceful fallback with clear error messages
- **JSON parsing errors** - detailed error reporting for malformed responses
- **Memory allocation failures** - proper cleanup and error recovery
- **Process management** - robust fork/exec error handling
- **Configuration validation** - clear error messages for invalid config

### Debug Mode

Enable debug mode for troubleshooting:

```ini
debug_mode = true
log_level = DEBUG
```

This will show:
- Socket connection details
- JSON parsing information
- Window count and processing
- Configuration loading details

## Architecture

Hyprworm follows a clean, modular architecture:

### Core Components

1. **IPC Module** - Unix domain socket communication with Hyprland
2. **Parser Module** - JSON parsing and window data extraction
3. **UI Bridge** - Process management and launcher communication
4. **Configuration** - Flexible config system with validation
5. **Logging** - Comprehensive logging and error reporting

### Data Flow

```
Hyprland → IPC Socket → JSON Parser → Window List → Launcher → User Selection → Focus Command
```

## Technical Details

### Dependencies

- **cJSON** - Lightweight JSON parsing
- **Standard C Library** - Socket programming, process management
- **Hyprland** - Window manager IPC

### Memory Management

- **Automatic cleanup** - All allocated memory is properly freed
- **Error recovery** - Graceful handling of allocation failures
- **Resource limits** - Bounded memory usage with dynamic resizing

### Performance

- **Direct IPC** - No intermediate processes or files
- **Efficient parsing** - Single-pass JSON processing
- **Minimal overhead** - Lightweight C implementation

## Troubleshooting

### Common Issues

**"Environment variables for Hyprland IPC not set"**
- Ensure you're running under a Hyprland session
- Check that `XDG_RUNTIME_DIR` and `HYPRLAND_INSTANCE_SIGNATURE` are set

**"Failed to connect to Hyprland socket"**
- Verify Hyprland is running
- Check socket path: `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock`

**"Failed to parse window data"**
- Check Hyprland is responding to `hyprctl clients -j`
- Enable debug mode to see detailed error information

**Launcher not working**
- Verify launcher is installed and in PATH
- Test launcher manually: `echo "test" | fuzzel --dmenu`
- Check launcher command in config file

### Debug Steps

1. **Enable debug mode:**
   ```ini
   debug_mode = true
   log_level = DEBUG
   ```

2. **Check logs:**
   ```bash
   tail -f /tmp/hyprworm.log
   ```

3. **Test components:**
   ```bash
   # Test Hyprland IPC
   hyprctl clients -j
   
   # Test launcher
   echo "test" | fuzzel --dmenu
   ```

## Contributing

Contributions welcome! Areas for improvement:

- **Additional launcher support** - New dmenu-compatible launchers
- **Enhanced configuration** - More customization options
- **Error handling** - More robust error recovery
- **Documentation** - Better examples and guides

## License

MIT License - see LICENSE file for details.

## Acknowledgments

- **Hyprland** - The amazing Wayland compositor that makes this possible
- **cJSON** - Lightweight JSON parsing library
- **Unix philosophy** - "Do one thing and do it well"

---
