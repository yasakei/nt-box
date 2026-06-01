# Box Package Manager

Box is the official package manager for Neutron, allowing you to easily install, manage, and build native modules.

## Features

- 📦 **Easy Module Installation** - Install modules from NUR with one command
- 🔨 **Cross-Platform Building** - Supports Linux, macOS, Windows (MSVC & MINGW64)
- 🔍 **Module Discovery** - Search and browse available modules
- 📚 **Version Management** - Install specific module versions
- ⚡ **Auto-Detection** - Automatically detects compilers and platform

## Quick Start

```bash
# Install a module
./box install base64

# Use in your code
```
```neutron
use base64;
var encoded = base64.encode("Hello!");
```

## Installation

Box is built alongside Neutron:

```bash
cmake --build build
# Box binary: ./box
```

## Commands

- `box install <module>[@version]` - Install module from NUR
- `box info <module>` - Show module information
- `box search <query>` - Search for modules
- `box build native <source> <version>` - Build native module
- `box uninstall <module>` - Remove module
- `box help` - Show help

## Environment Variables

- `NT_BOX_SRC`: Override the path to `native_shim.cpp` during native module building. Ensure this directory contains `native_shim.cpp`.

## Documentation

See [docs/](./docs/) for full documentation.

## Links

- **NUR Registry**: https://github.com/neutron-modules/nur
- **Neutron**: https://github.com/yasakei/neutron
