# Research tools

These are the scripts used to reverse-engineer the game and test the plugin. They are not needed
to build or use the plugin. See `docs/RE_NOTES.md` for how they were used.

They read **your own** game dumps from `dump/` (or the folder in the `OWE_DUMP` environment
variable). Nothing from the game is included in this repository.

```
dump/1.0/code.bin, dump/1.0/FieldRo.cro, dump/1.0/static_demangled.txt   (base game)
dump/1.2/code.bin, dump/1.2/FieldRo.cro, dump/1.2/static.crs             (v1.2 update)
```

| Script | What it does |
|---|---|
| `ctr.py`, `romfs_extract.py`, `split_rom.py` | CIA / NCCH / ExeFS / RomFS extraction (code.bin is BLZ-decompressed) |
| `garc_unpack.py`, `lz11.py` | GARC archives and LZ11 decompression (Pokémon models are in GARC `a/0/9/4`) |
| `crsexp.py` | Exported symbol names from `static.crs` (4916 mangled C++ names for code.bin) |
| `croload.py`, `staticimp.py` | Loads and relocates a CRO (e.g. FieldRo.cro) and resolves its imports |
| `fr12.py` / `fdis.py` | Disassembly of v1.2 / v1.0 FieldRo and code.bin with symbol names (capstone) |
| `pmatch.py`, `mini.py` | Masked byte-pattern matching to find the same function in another version |
| `run.sh`, `emu.sh`, `battleloop.sh` | Headless Azahar test rig (Xvfb + xdotool): restart with the new build, press keys, screenshots, automatic battle stress test |

Python packages: `capstone`, `cxxfilt`. `static_demangled.txt` = the `crsexp.py` exports,
demangled, one `0 <address> <name>` per line.
