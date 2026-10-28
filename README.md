# Doodle Pet

A drawing toy and a virtual pet for kids, running on a cheap ESP32 touchscreen board.

It has two apps, picked from a start screen at power-up:

- **Drawing**: a simple paint app. Drawings are saved, and the gallery replays each one stroke by stroke, as if it's being drawn again.
- **Pet**: a little blob that lives in a room you draw. Floors and stairs you draw are solid, food you draw drops for it to eat, toys get kicked around, and drawing a door brings a friend round to play. Its needs carry on while the board is off (with WiFi), so it has to be looked after.

Once a child picks an app there's no way back to the start screen without restarting, so they stay where they chose. Everything is icon-based, so non-readers can use it. The pet app has an **(i)** button with an explanation and settings for grown-ups.

## The hardware

The **Elegoo / LCDwiki E32R28T**, a 2.8" ESP32 display module. It's sold as "2.8 inch ESP32-32E display module" and similar names.

- ESP32-WROOM-32E (4MB flash, no PSRAM)
- 2.8" 240x320 TFT: ILI9341, or ST7789 on newer revisions
- Resistive touch (XPT2046)
- RGB LED on the back, which this project uses
- Also on board, but not used yet: speaker output, SD card slot, battery connector

It looks a lot like the "Cheap Yellow Display" (ESP32-2432S028), but the pins are different, so this firmware won't run on one without changing `platformio.ini`.

## Getting started

1. **Install PlatformIO.** Either the [PlatformIO IDE extension for VS Code](https://platformio.org/install/ide?install=vscode), or just the CLI (`brew install platformio` on macOS, or `pip install platformio`).
2. **Clone this repo** and `cd` into it.
3. **Optional, recommended for the pet: WiFi.** Copy `include/wifi_config.example.h` to `include/wifi_config.h` and fill in your network and timezone. The pet app uses WiFi once at start-up to get the time, then turns it off. Without it, the pet's time only passes while the board is switched on. `wifi_config.h` is git-ignored.
4. **Plug the board in over USB-C and flash it:**
   ```sh
   pio run -t upload
   ```
   The first build downloads the ESP32 toolchain, which takes a few minutes.
5. **Calibrate touch.** On first boot, tap the centre of each of the three crosses. The calibration is saved. To redo it later, hold the board's **BOOT** button for 2 seconds on the start screen.

If the screen stays white after flashing, your board has the newer ST7789 panel: flash with `pio run -e e32r28t_st7789 -t upload` instead. See [Troubleshooting](#troubleshooting) for other problems.

## Commands

```sh
pio run                      # build
pio run -t upload            # build + flash
pio device monitor           # serial monitor (Ctrl+C to quit)
pio run -t upload && pio device monitor

pio run -e e32r28t_st7789 -t upload   # flash the ST7789 variant instead
pio device list              # show serial ports
pio run -t compiledb         # regenerate compile_commands.json for Zed/clangd
pio run -t erase             # wipe the flash, including drawings and the pet
```

The same commands are available in Zed as tasks (`task: spawn`, filter "PIO").

## The apps

### Drawing

Tap a colour on the left, draw on the canvas, and use the icon buttons on the right:

- **Plus** clears the canvas for a new drawing
- **Floppy disk** saves the drawing (saving again overwrites the same drawing until you start a new page). A green tick means saved, a red cross means empty or storage full.
- **Picture** opens the gallery, where each drawing replays stroke by stroke: **arrows** to browse, **green play** to watch it draw again, **bin** (tap twice, it turns red with the lid open) to delete, **pencil** to go back to drawing

The RGB LED on the back shows the colour you're drawing with. Touches that slip onto the palette or buttons mid-stroke are ignored.

### Pet

The pet starts as an egg: tap a colour to paint it, then tap the egg three times to hatch it.

Each colour in the palette is a tool, with a picture on its swatch:

| Swatch | Tool | What you draw becomes |
|---|---|---|
| Black (stairs) | Floor | Floors, ramps, walls and stairs. The pet walks on them, steps over small lines, jumps up steps and slides down slopes. |
| White (eraser) | Eraser | Rubs out floors, food, toys and the door |
| Red (apple) | Fruit | Food that drops. Tasty and filling. |
| Green (leaf) | Veg | Food: very filling, a bit boring |
| Blue (drop) | Water | A drink: gives it energy |
| Magenta (sweet) | Sweets | Treats: great fun, not filling |
| Orange (ball) | Toy | Circles become balls; anything else is a toy that falls and gets pushed around. Drag a ball and let go to throw it. |
| Yellow (door) | Door | A door. A friend comes through it to play for a minute or so, then goes home. Tap the door for another visit. |

Strokes on top of a piece of food (or a toy) join it, so an apple can get a leaf. Lifting your finger briefly and carrying on nearby continues the same stroke.

Tap the pet to boop it, or rub it. The meters on the right are fullness, fun and energy. Each pet has a secret favourite food that makes it extra happy.

- **(i)** opens two pages explaining all this, then **Settings**: change the pet's colour, see its favourite food and whether the WiFi clock is set, and start again with a new egg (tap twice; the pet walks off and an egg appears).
- **Picture** hangs a saved drawing on the wall. The pet goes to admire it, more often if it has the pet's favourite colour in it.
- **Bin** (tap twice) clears the floors, food, toys and door. The picture stays.

It sleeps from 19:00 to 07:00 (poking it wakes it, grumpily, for a minute) and when it runs out of energy. If its fullness or fun sits at zero for 24 hours it runs away, and a new egg appears. It grows a little over its first three days.

The RGB LED shows the pet's colour, breathes slowly while it sleeps, and pulses red when it needs looking after. Picking a tool shows that tool's colour for a moment.

Tuning lives at the top of `src/pet_stats.cpp` (needs per hour) and `src/pet_stats.h` (bedtime, run-away time).

## How it works

### Storage

Everything is in LittleFS on the ESP32's flash, so it survives power-off and reflashing. `pio run -t erase` wipes everything.

- `/d/0001.bin`, ...: drawings, as stroke lists (so the gallery can replay them)
- `/pet/state.bin`: the pet's needs, age, colour and hung picture
- `/pet/room.bin`: the room's floors and picture, as a 4-bit image
- `/cal.bin`: touch calibration

Food, toys and the door aren't saved; they're gone after a restart.

### Code layout

| File | What |
|---|---|
| `src/main.cpp` | Start screen |
| `src/draw_app.*` | Drawing app |
| `src/pet_app.*` | Pet app: behaviour, movement, visitors, what each tool makes, input, help pages |
| `src/pet_room.*` | The room image (also what the pet collides with) and the hung picture |
| `src/pet_stats.*` | Needs, saving, WiFi clock |
| `src/pet_draw.*` | Drawing the pet, egg, balls and effects |
| `src/ui.*`, `src/touch.*`, `src/strokes.*`, `src/led.*` | Shared: palette and icons, touch, strokes and saved drawings, the RGB LED |

The pet's room is a 240x240 image at 4 bits per pixel (29KB). It's both what's drawn behind the pet and what it collides with. Each frame, only the areas that changed are redrawn: the room is composited into an 80x80 sprite, the pet and its things are drawn on top, and the result is pushed to the screen.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Upload hangs at `Connecting....` | Hold **BOOT**, start the upload, then release BOOT once it starts writing. If you see "Invalid head of packet" or "serial noise", lower `upload_speed` (already 460800 because 921600 corrupts data on this board; try 230400). |
| No `/dev/cu.usbserial-*` port in `pio device list` | Try a different USB-C cable (many are charge-only). The board uses a CH340 USB-serial chip. If the port still doesn't appear, install the WCH CH34x macOS driver and allow it in System Settings → Privacy & Security. |
| Backlight on but screen stays **white** | Newer revision with an ST7789 panel. Use `pio run -e e32r28t_st7789 -t upload`, or set `default_envs = e32r28t_st7789`. |
| Colours inverted (black shows as white) | Toggle `-DTFT_INVERSION_ON=1` / `-DTFT_INVERSION_OFF=1` in the env's `build_flags`. |
| Red and blue swapped | Toggle `-DTFT_RGB_ORDER=TFT_BGR` / `TFT_RGB`. |
| Garbled image | Lower `-DSPI_FREQUENCY` to `27000000`. |
| Touch is off, or taps land in the wrong place | Hold **BOOT** for 2 seconds on the start screen to recalibrate. |
| RGB LED stays off or shows the wrong colours | Check the `LED_*` pins in `platformio.ini` against your board revision. |
| Pet app says the clock isn't set | Check `include/wifi_config.h`, and watch `pio device monitor` for the `clock:` line at start-up. The ESP32 only supports 2.4GHz WiFi. |

## Hardware notes

- Display SPI: MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, RST tied to EN, backlight 21 (active HIGH)
- Touch (XPT2046): its own pins, CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 (bit-banged in `src/touch.cpp`)
- RGB LED: red 22, green 16, blue 17, common anode (LOW lights it)
- BOOT button: GPIO 0, used on the start screen to recalibrate touch

## Adding more ESP32 boards

All boards share the `[env]` section in `platformio.ini`. To run this on another board, add an `[env:<name>]` with its `board` and `build_flags`, then build it with `pio run -e <name>`.
