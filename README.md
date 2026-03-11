# Shivishell

Shivishell is a compact, cross-platform C shell with a fast line editor, simple pipelines, and a restricted command sandbox backed by a local `commands` directory.

**Features**
- Cross-platform input handling (Win32 console or POSIX termios).
- Line editing with left/right arrows, history navigation, and backspace.
- Command history persisted to `~/.shivishell_history`.
- Simple quoting for arguments and pipeline splitting using `|`.
- Built-in `cd`.
- Commands are resolved only from a `commands` directory located next to the executable.

**Build**
All platforms (Node.js required):
```bash
node build.js
```
Or:
```bash
npm run build
```

Makefile (Linux/macOS):
```bash
make
```

**Tests**
All platforms (Node.js required):
```bash
node test.js
```
Or:
```bash
npm test
```

Makefile:
```bash
make test
```

**Run**
1. Ensure a `commands` directory exists next to the executable.
2. Launch the binary from `build/` (or wherever you placed it).

**Commands Directory**
Shivishell only executes commands that exist inside a `commands` folder placed alongside the executable. For example:
- Windows: `build/commands/hello.exe` or `build/commands/hello.cmd`
- Linux/macOS: `build/commands/hello`

If a command is missing, the shell reports `command not available`.

**Prompt**
The prompt is rendered as:
```
[username] current/working/directory
>
```
Example:
```
[Shiva] ~/OneDrive/Desktop
>
```

**Notes**
- Pipelines are joined and executed using the system shell (`cmd.exe /C` on Windows, `/bin/sh -c` on POSIX).
- Ctrl+C cancels the current line without exiting the shell.
- Type `exit` to quit.
- macOS uses `_NSGetExecutablePath` to locate the `commands` directory next to the executable.
- Set `NO_COLOR` or `SHIVI_NO_COLOR` to disable prompt colors.
