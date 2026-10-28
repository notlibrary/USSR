# Stalin API

`$include "src/stalin/stalin.su"` gives a script the whole editor.

## Entry point

`stalin_main(code): args` runs one command line, a vector of strings without
the program name, exactly as `ussr src/stalin/stalin.su ARGS...` would, and
returns its exit status: 0 done, 1 nothing to do, 2 a usage or device error
(the message went to stderr).

## Native builtins it stands on

```
bread(v): dev lba [count [sector]]   # count sectors (1) of sector bytes (512) from lba; null on error
bwrite(n): dev lba bytes [sector]    # whole sectors at lba; bytes written or -1
bsize(n): dev                        # size in bytes, -1 when unknown
bdevices(v):                         # [path bytes kind bus model sector] per storage device
crc32(n): data [from [count]]        # CRC-32 (zlib, GPT)
```

## After `st_open(code): dev` and `st_load(code):`

| Global | Holds |
|--------|-------|
| `st_dev`, `st_bytes`, `st_lbas`, `st_sector` | the device, its size in bytes and sectors, the sector size |
| `st_kind` | `"gpt"`, `"mbr"` or `"none"` |
| `st_parts` | `StPart` structs: `num start end type name uuid attrs boot logical` (`end` inclusive; `type` a GUID on GPT, two hex digits on MBR) |
| `st_first`, `st_last`, `st_disk_guid`, `st_entry_count`, `st_entry_size` | GPT header fields |
| `st_disk_sig` | MBR disk signature |
| `st_notes` | warnings about the table (a damaged header, ...) |

`st_commit(code):` writes `st_parts` back as a table of `st_kind`;
`st_show(code):` prints them; `st_probe(v): start sectors type` gives `[fs
label bytes]` for the file system in a sector range; `st_type_code(c): name
kind` and `st_type_name(n): code kind` translate type names.

`STALIN_SEED=N` in the environment makes new GUIDs and disk signatures
repeatable (the tests use it).
