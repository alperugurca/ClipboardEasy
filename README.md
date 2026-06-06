# Clipboard Easy

Clipboard history manager with one-click copy or paste. Windows app.

Clipboard Easy is a small Windows clipboard manager. It watches copied text, keeps the latest 100 entries, and lets you reuse them quickly.

## Download

Download `ClipboardEasy.exe` from the latest release:

https://github.com/alperugurca/ClipboardEasy/releases/latest

## Features

- Saves copied text automatically.
- Keeps up to 100 clipboard entries.
- Click `Copy` to make saved text the current clipboard text.
- Click `Paste + Enter` to paste saved text at the cursor and press Enter.
- Delete one entry or clear the full history.
- Runs as a standalone Windows `.exe`.
- Stores history locally in `clipboard_history.json`.

## How To Use

1. Run `ClipboardEasy.exe`.
2. Copy text anywhere.
3. Choose `Copy` or `Paste + Enter`.
4. Click saved text from the list.

## Build

Run:

```bat
build.bat
```

The build creates `ClipboardEasy.exe`.

## Files

- `ClipboardEasy.exe` - Windows app.
- `clipboard_easy.cpp` - Main Windows app source code.
- `app.ico` - App icon.
- `app.rc` - Windows resource file for the icon.
- `build.bat` - Builds and signs the app when a signing certificate exists.
- `Start Clipboard Easy.bat` - Starts the built app.
