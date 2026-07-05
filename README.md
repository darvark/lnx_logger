# Logger

Logger is a terminal-based amateur radio logging application for entering QSOs, looking up DXCC information, and monitoring DXCluster spots.

<img width="1408" height="683" alt="obraz" src="https://github.com/user-attachments/assets/73f0c233-01ec-4088-8eb7-ca527bcd89e2" />

## What it does

- Records QSOs from the terminal UI
- Displays DXCC, CQ zone, and ITU zone information while typing a callsign
- Connects to a DXCluster server and shows received spots in the cluster window
- Tracks simple statistics
- Exports log data to CSV and ADIF files

## Features

- QSO entry with frequency, RST, and mode detection
- Local DXCC lookup from a CTY database
- DXCluster status and spot display
- Invalid QSO marking for export exclusion
- CSV/ADIF export support

## Requirements

- C compiler (GCC or Clang)
- CMake
- make
- ncurses development libraries
- pthread support

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
- invalid: mark the most recent QSO as invalid so it is skipped by exports
- quit: exit the program

## Data files

The program expects the DXCC database file named wl_cty.dat in the working directory or in the build directory.

## Notes

- The application uses ncurses, so it is intended for terminal environments.
- DXCluster connectivity depends on the configured host, port, and network access.
- If you want to use a different DXCluster server, update DXC_HOST and DXC_PORT in logger.conf.
