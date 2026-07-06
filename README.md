# Logger

Logger is a terminal-based amateur radio logging application for entering QSOs, looking up DXCC information, and monitoring DXCluster spots.

## What it does

- Records QSOs from the terminal UI
- Displays DXCC, CQ zone, and ITU zone information while typing a callsign
- Shows a dedicated callsign suggestions panel in the top-right corner with all matching history entries
- Connects to a DXCluster server and shows received spots in the cluster window
- Tracks simple statistics
- Exports log data to CSV and ADIF files

## Features

- QSO entry with frequency, RST, and mode detection
- Callsign history suggestions with multi-match list view (top-right panel)
- Local DXCC lookup from a CTY database
- DXCluster status and spot display
- Invalid QSO marking for export exclusion
- CSV/ADIF export support, including custom ADIF filename
- One-key CTY database update from the internet

## Requirements

- C compiler (GCC or Clang)
- CMake
- make
- ncurses development libraries
- pthread support
- curl or wget (for CTY database download)

On Debian/Ubuntu systems, install the required packages with:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libncurses-dev
```

## Build

From the project root:

```bash
cmake -S . -B build
cmake --build build
```

The executable will be created in the build directory.

## Regression Tests

The project includes a regression suite in `tests/regression` that validates
core non-UI behavior:

- configuration parsing and defaults
- CTY database loading and callsign lookup
- QSO parsing, band/mode detection, and invalid toggle behavior
- statistics aggregation
- CSV and ADIF export content
- Maidenhead locator conversion

Run the tests with:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Unit Tests

The project also includes unit tests in `tests/unit` to verify exported
non-UI functions from core modules:

- `config`: `config_load`
- `cty`: `cty_load`, `cty_lookup`
- `qso`: `qso_init`, `qso_add`, `qso_mark_invalid`, `detect_band`, `detect_mode`
- `stats`: `stats_update`
- `export`: `export_csv`, `export_adif`
- `maidenhead`: `locator_to_latlon`
- `dxcluster`: `dxcluster_set_status`

Run all tests (regression + unit):

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

```bash
cd build
./logger
```

## Configuration

The application reads a configuration file named logger.conf from the working directory.

Example configuration:

```ini
LAT=21.104127
LON=37.300154
LOCATOR=AA00AA

DXC_HOST=dx.da0bcc.de
DXC_PORT=7300
DXC_CALL=AAXAAA
```

### Configuration fields

- LAT: latitude of your station
- LON: longitude of your station
- LOCATOR: Maidenhead locator of your station
- DXC_HOST: DXCluster hostname
- DXC_PORT: DXCluster TCP port
- DXC_CALL: your callsign used for cluster login

## Commands

While running the application, you can use these commands in the input line:

- export: write log.csv and log.adi
- export mylog.adi: write log.csv and custom ADIF file mylog.adi
- invalid: mark the most recent QSO as invalid so it is skipped by exports
- quit: exit the program

## Function keys

- F1: help/status hint
- F2: prompt for ADIF filename, then export CSV and ADIF using the entered ADIF name
- F3: recalculate statistics
- F4: toggle DXCluster fullscreen view
- F5: download the latest wl_cty.dat and reload CTY entries
- F10: quit

## Callsign suggestions

When you start typing the first token (callsign), Logger checks `call_history.txt`
and shows all matching callsigns in a separate window in the top-right corner.

- Suggestions are ordered by recency (newest first)
- Use Up/Down arrows to select a different suggested callsign
- Press Space to apply the currently selected suggestion and continue with the next field
- Press Tab to apply the currently selected suggestion
- Suggestions are shown only while editing the first token

## Data files

The program expects the DXCC database file named wl_cty.dat in the working directory or in the build directory.

When F5 is used, wl_cty.dat is downloaded and replaced in the current working directory.

## Notes

- The application uses ncurses, so it is intended for terminal environments.
- DXCluster connectivity depends on the configured host, port, and network access.
- If you want to use a different DXCluster server, update DXC_HOST and DXC_PORT in logger.conf.
