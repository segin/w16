# w16 

`w16` is a clean-room reimplementation of Microsoft's Windows 3.1 operating system environment created with Claude. 

## Building

See [BUILDING.md](BUILDING.md) for details. 

Short info: Use Linux. Put OpenWatcom in `/opt/watcom`. Run `make`.

## Running

Once built, w16 will have the system tree in `build/w16`. Copy this `w16` to an appropriate DOS system's drive root (e.g. `C:\`), `CD` into the `\W16` directory, and run `WIN`. The `/S` and `/3` flags will give you Standard Mode and 386 Enhanced mode, respectively. 
