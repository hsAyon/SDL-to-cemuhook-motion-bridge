# SDL to Cemuhook Motion Bridge

> [!NOTE]  
> This application was built specifically for myself as a personal tool to resolve a problem in my own setup. It is shared for anyone facing a similar problem.

This program was created as a workaround for an issue regarding macOS emulators not properly accepting motion inputs from Switch controllers. 

The bridge captures that native motion input using **SDL3** and exposes it via a local **DSU (DualShock UDP Server) protocol stream**, allowing it to be used with any Cemuhook-compatible emulator.

---

## Build Instructions

### Prerequisites
1. **SDL3** (installable via Homebrew: `brew install sdl3`)
2. **Clang** compiler toolchain

### Build Command
Open your terminal in the same directory as the source file and execute the following command:

```bash
clang sdl_to_cemuhook_motion_bridge.c -o sdl_to_cemuhook_motion_bridge $(pkg-config --cflags --libs sdl3)
```

*Built using Gemini 3.5 Flash*
