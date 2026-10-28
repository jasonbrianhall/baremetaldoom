# Doom for Linux

The same Doom as the bare-metal and DOS builds (free look, widescreen, OPL3 music), in an SDL2 window, with IPXSETUP and SERSETUP built in.

```
sudo apt install build-essential libsdl2-dev
make
./doom                      # finds doom2.wad, doom.wad, doom1.wad, freedoom*.wad
./doom -iwad path/to/doom.wad -warp 1 1
```

It looks for the IWAD in the current directory, `$DOOMWADDIR`, `$DOOMWADPATH`, `~/.local/share/games/doom` and `/usr/share/games/doom`. `default.cfg` and saved games go in the current directory, as on DOS.

**Controls** are the DOS build's: WASD, Ctrl or the left button fires, Space opens, Alt strafes, Shift runs, the mouse looks around. Alt+Enter toggles fullscreen (`-fullscreen` to start that way); Ctrl+Alt+G lets go of the mouse. `-width W -height H` sets the window size, `-novsync`, `-latency MS` for the sound.

## Net games

The drivers speak the original wire protocols, so a Linux machine can play against DOS machines running Doom 1.9's `IPXSETUP.EXE` / `SERSETUP.EXE` and the original `DOOM.EXE` (or `../msdos`'s doom.exe), real or in DOSBox. Everyone needs the same IWAD; game options (`-deathmatch`, `-altdeath`, `-warp`, `-skill`, `-nomonsters`...) go on the command line as usual. `-nodes N` is the number of players (default 2). Ctrl+C gives up while looking for players.

| | |
|---|---|
| `-ipx` | IPX over UDP broadcast on the LAN, no server. Linux machines only. `-udpport N` (default 10213), `-ipxbind ADDR` to pick the interface. |
| `-ipxserver [PORT]` | Hosts a DOSBox-compatible IPX tunnel server and plays. DOSBox machines join with `ipxnet connect <host> <port>`, then `ipxsetup -nodes N`. Default port 213, which needs root; use e.g. 21300. |
| `-ipxnet HOST[:PORT]` | Joins a DOSBox IPX server (`ipxnet startserver`, ipxbox, ipxgw). |
| `-ipxeth IFACE` | Real IPX frames on an Ethernet card, for DOS PCs on the LAN. `-ipxframe ii` (default), `802.3`, `802.2` or `snap` for what we send; all four are received. Needs `sudo setcap cap_net_raw+ep ./doom`. |
| `-serial /dev/ttyS0` | Null-modem cable, 8N1 at 14400 baud like SERSETUP; `-9600` … `-115200` to change it. `-dial NUMBER` / `-answer` drive a modem using `modem.cfg`. |
| `-serial HOST:PORT`, `-serial listen:PORT` | The serial protocol over TCP: Linux to Linux, or to DOSBox's COM1 with `serial1=nullmodem server:<host> port:<port> transparent:1` (or DOSBox listening with `serial1=nullmodem port:<port> transparent:1`). |

`-port N` sets the IPX socket (default 34459 = 869Bh, what Doom 1.9's IPXSETUP uses; the IPXSETUP source id released uses 34460). `-player1` / `-player2` decide who's who on a serial link.

Examples:

```
# three Linux machines on a LAN
./doom -ipx -nodes 3 -deathmatch -warp 1 1

# Linux hosts, a DOSBox player joins
./doom -ipxserver 21300 -nodes 2 -deathmatch
#   in DOSBox:  ipxnet connect 192.168.1.10 21300
#               ipxsetup -nodes 2 -deathmatch

# a DOS PC on the same Ethernet with a packet driver and IPX loaded
./doom -ipxeth eth0 -nodes 2

# null-modem cable to a DOS PC running SERSETUP
./doom -serial /dev/ttyUSB0
```

When every player runs this port, free look works in net games; with an original `DOOM.EXE` in the game, everyone plays vanilla (mouse-forward, autoaim).
