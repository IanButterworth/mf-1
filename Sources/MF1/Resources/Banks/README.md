# Factory banks

The four DX7 32-voice banks of the FM-1 V15 factory set (presets 1–128).

They are M-VAVE's V15 factory bank, byte for byte. M-VAVE distributes that bank under the Apache License 2.0
in [FM1 Open](https://github.com/m-vave/fm1_open) (`app/fm_factory.inc`, extracted from the V15 recovery image
`FM-1_015.fwsc`; see its `THIRD_PARTY.md`). The 16,384 voice bytes, all four banks in order, have SHA-256
`ecad0b8620f9d784d46c8da1e3c5de02c40214649ed8dec218026431046aad25`, the hash FM1 Open pins. Only the DX7
bulk-dump header, checksum and end byte around each 4096-byte bank are MF-1's. The Apache-2.0 text is in
`LICENSES/Apache-2.0.txt`.

MF-1's MIT licence does not cover these voices, and MF-1 claims no rights in them. Many of them are older than
the FM-1: [KingParamount/fm1-factory-presets](https://github.com/KingParamount/fm1-factory-presets) traced 40
to Yamaha DX7 ROM and VRC cartridges and most of the rest to the community Dexed_cart 1.0 compilation.

An earlier MF-1 shipped KingParamount's capture of the V13/V14-era set. Two voices differ in V15: 35 BASS 1 and
45 GUITAR 4. A real V15 unit holds the V15 versions (measured 2026-11-07 from a full flash read).
