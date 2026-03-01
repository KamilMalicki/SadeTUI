# Sade

Sade is a lightweight, framebuffer-based terminal multiplexer for Linux. It runs completely independent of X11 or Wayland display servers, drawing its interface directly to `/dev/fb0` using the `libvterm` and `FreeType` libraries.

## Key Features

* **No X11/Wayland Required:** Runs directly in a raw TTY console.
* **Multi-Window Support:** Manage up to 4 independent terminal windows simultaneously.
* **Dynamic Layouts:** Switch layouts on the fly:
* Master-Stack
* Grid
* Equal Columns
* Equal Rows


* **TrueType Rendering:** Smooth and readable text rendering powered by FreeType.

## Requirements and Dependencies

To compile the project, ensure you have standard build tools (`gcc`, `make`, `pkg-config`) installed, along with the following libraries:

* **freetype2** 
* **libvterm** 

On Debian/Ubuntu-based systems, you can install them using:

```bash
sudo apt install build-essential pkg-config libfreetype6-dev libvterm-dev

```

## Important Setup Note

The application requires a TrueType font file named `mono.ttf` to run.
**You must place a `mono.ttf` file in the same directory from which you execute the program.** Otherwise, the application will fail with a missing font error.

## Compilation and Installation

1. Clone the repository and navigate to the project directory.
2. Build the project using `make`:
```bash
make

```


3. Optionally, install it system-wide (requires root privileges):
```bash
sudo make install

```



## Usage and Keybindings

Because Sade manipulates your terminal settings and initiates a graphics mode on `/dev/tty`, it is highly recommended to run it from a clean virtual console (e.g., by pressing `Ctrl+Alt+F3`).

```bash
./Sade

```

Navigation is driven by key sequences (preceded by the `Escape` key):

* **`Esc` + `n`** – Open a new terminal window.
* **`Esc` + `c`** – Close the currently focused terminal.
* **`Esc` + `l`** – Cycle through available layouts (Master -> Grid -> Columns -> Rows).
* **`Esc` + `Tab`** – Change the focused window.
* **`Esc` + `q`** – Quit the application.

## License

This project is licensed under the **Apache License, Version 2.0**. See the source file headers for more details. Copyright 2026 KamilMalicki.
