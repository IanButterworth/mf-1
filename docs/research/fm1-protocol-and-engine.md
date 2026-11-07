# M-VAVE FM-1: protocol, engine and firmware spec for a faithful macOS emulator

Compiled 2026-10-14 from the community reverse-engineering sources listed in section 7.
Every fact carries a source tag `[Sn]`. Confidence markers:

- **[confirmed]**: byte-exact capture, firmware disassembly with matching code, or official M-VAVE document.
- **[measured]**: hardware measurement by one community project (one unit, one firmware).
- **[inferred]**: reasoning from other facts; not tested.
- **[conflict]**: sources disagree; both sides given.
- **[new]**: my own decode of published captures in this research (section 1.5). Checked arithmetically against every published message, not on hardware.

Firmware context matters: the deepest disassembly (FM-1-RE) is of **V13/V14**; most measurements are on
**V15** (current stock, 2026-07-30). Where a V13 finding was not re-checked on V15, it is marked.

---

## 0. Executive summary (what an emulator must do)

- Engine: Google **msfa** (the Dexed DX7 core), ported to the JieLi pi32v2 CPU. **12 voices**, **44118 Hz**
  internal rate, **64-sample blocks**, 16-bit output. The msfa tables are byte-identical in the firmware. [S1 confirmed]
- But M-VAVE's V15 build deviates measurably from Dexed. Detune is about 3x too wide. Feedback is weaker.
  Algorithms 4/6 have a state-leak bug. The LFO behaves oddly (SysEx-sent LFO speed ignored; fast LFOs slow).
  Aftertouch gives strong vibrato. There is a 20 Hz high-pass and about 5.6 kHz roll-off on the output. [S6, S9, S11 measured]
- 128 presets = 4 banks (A-D) x 32 standard DX7 VMEM voices. Each preset also has a 59-byte settings record:
  FX chain, ENV, mono/poly and so on. [S1, S10, S11]
- MIDI in: notes, PC 0-127, pitch bend (range 1-24 up/down, default 12), channel aftertouch, CC1/2/4/64 on the
  note channel, CC0-23 effect controls on a separate FX channel (default 2), MIDI clock/start/stop, and DX7 SysEx
  (single voice, 32-voice bank, parameter change). [S12 confirmed official]
- MIDI out: **notes only**, channel 1, from keys/arp/seq. No CC, PC, clock or SysEx, except solicited replies
  to M-VAVE's private `00 59` command protocol. [S6, S12, S13]
- Post-synth: a reorderable 6-slot FX chain (Filter, Reverb, Delay, Distortion, Chorus, Phaser), stored per patch.
  Also a global per-patch ADSR "ENV" amp envelope, Glide (Full Time/Fingered, mono only), a 7-mode arpeggiator
  and a 16x16-step sequencer. [S13, S14, S10]

---

## 1. SysEx / MIDI protocol

### 1.1 Transports and USB identity

| Item | Value | Source |
|---|---|---|
| USB normal mode | VID:PID `4c4a:c755`, composite USB-MIDI + UAC audio, port names seen as "FM-1 Midi 0/1", "FM-1 Audio 0/1", "USB Composite Device" (name varies; match by substring) | [S2 11-ota-protocol.md; S6 FINDINGS.md] confirmed |
| USB OTA loader mode | `4d4a:4155` "ota-FM-1" | [S2] confirmed |
| Mask-ROM recovery | `4c4a:8057` "WL82 UBOOT1.00" mass storage | [S3 REVERSE_ENGINEERING.md] |
| BLE MIDI | Apple BLE-MIDI format, on by default; HOME LED shows state; long-press HOME turns BT off | [S2 05-midi.md; S13 ch.11] |
| 3.5 mm TRS MIDI | input only, no MIDI out socket | [S12; S13] |
| All three inputs active simultaneously | yes | [S13 ch.11] |
| MIDI thru | none ("doesn't retransmit received MIDI"). V13 code does have a route-merge pump to UART TX; treat as unused. | [S15 MIDI guide; S2 05-midi.md §1] conflict-minor |

### 1.2 Channel messages (official M-VAVE "FM-1 MIDI EN.docx", V15)

Source: https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1%20MIDI%20EN.docx [S12 confirmed]

Channels: **Note channel ("Midi Chn")** default **All/Omni** (settable ALL or 1-16). **FX channel ("Effect Chn")**
default **2** (1-16, no All). Real-time messages have no channel.

| Message | Bytes | Response |
|---|---|---|
| Note On | `9n kk vv`, vel 1-127 | sound; **vel 0 = Note Off** (added in V15 changelog) |
| Note Off | `8n kk vv` | release |
| Program Change | `Cn pp` 0-127 | voice 001-128 (PC 0 = preset 001). Unit busy about 45 ms; notes queue, not lost [S6 measured] |
| Pitch Bend | `En ll mm` 14-bit | bend; range set by Pitch Up / Pitch Dn, 1-24 semitones each, **default 12** [S15] |
| Channel aftertouch | `Dn vv` | "Channel aftertouch" (routes to msfa controller matrix; produces strong vibrato on stock V15, removed by czietz's patch) [S12; S8] |
| CC1 mod wheel, CC2 breath, CC4 foot | `Bn 01/02/04 vv` | msfa controller matrix (`dx7_mod_update`) [S2 05-midi.md §4 confirmed V13] |
| CC64 sustain | `Bn 40 vv` | sustain; release on 0 [S12] |

Ignored on V15 (measured [S6]): CC7, CC11 (on note channel), CC120, CC121 (does not clear sustain), CC123,
Universal Master Volume, Universal Identity Request, DX7 dump requests. Portamento and mono DX7 function
parameters (CC5/65/126/127) are ignored. All-notes-off must be done with explicit note-offs. [S6; S16 fm1-research.md §2.2]

V13 disassembly: CC is accepted only if `(status & 0x0F) == CC-channel` and `cc <= 23`. Note and CC channel
filters are separate bytes (`ENG+4781`, `ENG+4782`). [S2 05-midi.md §5.2, §7]

**Quirks to emulate** [S6 measured]:
- A note-off is matched using the **voice transpose in effect at note-off time**. Changing TRNP while a note is
  held makes that note hang forever. Internally `sounding = note + transpose - 24` [S2 §4].
- Velocity: incoming MIDI velocity is scaled by **100/127** before the engine [S10 HARDWARE-NOTES.md, FM-1_093,
  measured]. [S6] instead measured about 0.87 dB lower per KVS step, the same curve shape as DX7. Likely the same effect.
- Onboard keys are not velocity-sensitive. They send a fixed "Global Velocity" (GLO page 2, 1-127; figure shows 90).
  MIDI-out notes carry velocity 90 by default [S6; S13 ch.8].
- Note-on vel 0: V15 changelog says it is now treated as note-off [S17 release notes]. [S6] V15 table says
  "releases". FM-1+VA's manual lists "a note ended with Note On velocity 0 keeps sounding" as a stock bug
  [S16 §1.3 quoting the FM-1+VA manual]. **[conflict]** Probably fixed in V15, present before.

### 1.3 Effect CCs (FX channel, default ch 2), official map [S12 confirmed; S15; S13 App. A]

Switch CCs: `0` = off, `>=1` = on. Values above max are clamped to max [S15; FM-1+VA manual via S16 §7.8].
CCs above 23 on the FX channel are discarded. The CC map does **not** follow on-screen chain reordering [S15]
(the tutorial [S13 App. A] implies chain order; treat the fixed map as authoritative). **CC1/CC7/CC11 on the FX
channel hit Filter Type / Reverb Mix / Delay Mix**, not mod wheel, volume or expression.

| CC | Effect | Param | Range | Notes |
|---|---|---|---|---|
| 0 | Filter | Switch | 0/≥1 | |
| 1 | Filter | Type | 0 LPF, 1 BPF, 2 HPF | |
| 2 | Filter | Cutoff | 0-107 | 100 Hz-20 kHz, curve unknown [S15] |
| 3 | Filter | Q | 0-10 | shown 0-100 in steps of 10 [S13] |
| 4 | Reverb | Switch | | |
| 5 | Reverb | Type | 0 Room, 1 Hall, 2 Plate | |
| 6 | Reverb | Decay | 0-100 | measured RT40: Hall d5 0.40 s, d25 1.5 s, d50 2.3 s, d75 3.7 s, d100 ≈5 s; Room d100 3.7 s; Plate d100 4.3 s (mix 60) [S18 reverb.md measured] |
| 7 | Reverb | Mix | 0-100 | |
| 8 | Delay | Switch | | |
| 9 | Delay | Decay | 0-100 | acts as feedback [S16 §7.8] |
| 10 | Delay | Rate | 0-100 | higher = shorter echo, 0.8 s down to 0.1 s (FM-1+VA manual, "Likely" for stock) [S16 §7.8] |
| 11 | Delay | Mix | 0-100 | |
| 12 | Distortion | Switch | | soft clip ("M-VAVE's original distortion") [S16 §7.8] |
| 13 | Distortion | Gain | 0-100 | |
| 14 | Distortion | Tone | 0-100 | |
| 15 | Distortion | Level | 0-100 | level 20 ≈ -14 dB; usable as output trim [S6] |
| 16 | Chorus | Switch | | |
| 17 | Chorus | Freq | 0-100 | 0.1-1 Hz (FM-1+VA manual) [S16 §7.8] |
| 18 | Chorus | Depth | 0-100 | |
| 19 | Chorus | Mix | 0-100 | |
| 20 | Phaser | Switch | | |
| 21 | Phaser | Freq | 0-100 | 0.5-6 Hz (FM-1+VA manual) [S16 §7.8] |
| 22 | Phaser | Depth | 0-100 | |
| 23 | Phaser | Mix | 0-100 | |

On-screen knob order differs from CC order for Chorus/Phaser (screen: Depth K1, Freq K2, Mix K3) and Reverb
(screen: Type K1, Decay K2, Mix K3; switch on K4) [S13 ch.6; S18 says Reverb K2 = mix, K3 = decay **[conflict, minor]**].
V15 changed FX and ENV values from 0-10 to 0-100 without rescaling stored values [S17; S18].

### 1.4 Yamaha DX7 SysEx (stock handler)

The handler is `midi_message_handler 0x02023EA0` (V13) [S2 05-midi.md §5.1 confirmed]. Official doc: "SysEx:
F0 43 10 pp qq vv F7 → single-parameter write (param = pp×128+qq, 0–155; value vv)" [S12].

| Frame | Exact bytes | Stock behaviour |
|---|---|---|
| 32-voice bulk (VMEM) | `F0 43 0n 09 20 00 <4096 data> <ck> F7` = **4104 bytes** | `n` must be ≤ 0x0F (any channel). Checksum verified: `ck = (128 - (sum(data) & 0x7F)) & 0x7F`. On success the **device shows a bank picker: turn knob 1/2/3/4 for bank A/B/C/D** (A = presets 1-32, B 33-64, C 65-96, D 97-128). The bank is then saved to flash automatically "after a short delay". Names arrive intact. [S2; S13 ch.11; S16 README step 9] |
| Single voice (VCED) | `F0 43 0n 00 01 1B <155 data> <ck> F7` = **163 bytes** | V15: loads the **edit buffer** (not saved until SAVE). It **stalls the unit 20-450 ms at random**, and notes after it are delayed or dropped [S6 measured]. V13 disassembly shows it going through `dx7voice_pack_store` (pack to VMEM and flash write at the current slot) [S2 §5.1]. **[conflict V13 vs V15]**. FM-1+VA overwrites the stored preset. |
| Parameter change | `F0 43 10 gg pp vv F7` | `addr = (gg << 7) + pp; editBuffer[addr] = vv`. **Byte 2 must be exactly 0x10** (no channel nibble). No range validation. Sets dirty flags [S2 §5.1; S16 §1.1 confirmed]. **0 ms latency; applies to the next note, not to sounding notes** [S6 measured]. A whole voice as 156 back-to-back param changes is safe [S6]. Address 155 (DX7 op on/off) is not in the 155-byte buffer; do not rely on it [S16]. |

Notes on behaviour:
- **No readback**: no DX7 dump request is answered [S6]. The only readback is the M-VAVE vendor read (1.5.4).
- Sounding notes keep their voice when the edit buffer changes, so tails from different voices overlap [S6].
- Program change drops unsaved edits [S10].
- **LFO speed via SysEx is ignored on V15**: param 137 by dump or param change has no effect, and the LFO stays at
  ≈5.8-6 Hz [S6 measured]. See 2.3 for the conflict.

#### DX7 VCED (155 bytes, edit buffer / param-change addresses)
Standard Yamaha layout, which the firmware uses unchanged: 21 bytes per operator, **OP6 first** (addr 0-20 =
OP6 ... 105-125 = OP1) [S16 §1.1; S2 §5.2; layout per Yamaha DX7 MIDI spec / Dexed].

| Op offset | Param | Range |
|---|---|---|
| 0-3 | EG R1-R4 | 0-99 |
| 4-7 | EG L1-L4 | 0-99 |
| 8 | Kbd level scaling break point | 0-99 (39 = C3) |
| 9 / 10 | Left / right depth | 0-99 |
| 11 / 12 | Left / right curve | 0-3 (-LIN, -EXP, +EXP, +LIN) |
| 13 | Kbd rate scaling | 0-7 |
| 14 | Amp mod sensitivity | 0-3 |
| 15 | Key velocity sensitivity | 0-7 |
| 16 | Output level | 0-99 |
| 17 | Osc mode | 0 ratio, 1 fixed |
| 18 | Freq coarse | 0-31 |
| 19 | Freq fine | 0-99 |
| 20 | Detune | 0-14 (7 = centre) |

Globals: 126-129 PEG R1-R4, 130-133 PEG L1-L4 (50 = no shift), 134 algorithm 0-31, 135 feedback 0-7, 136 osc key
sync 0-1, 137 LFO speed 0-99, 138 LFO delay 0-99, 139 LFO PMD 0-99, 140 LFO AMD 0-99, 141 LFO key sync 0-1,
142 LFO wave 0-5 (tri, saw down, saw up, square, sine, S&H), 143 pitch mod sens 0-7, 144 transpose 0-48
(24 = C3), 145-154 name (10 ASCII).
FM-1 working buffer address (V13): `0x01C0E670 + 5608`; ops at `+5624 + i*21`, globals at `+5734` [S1 §5.2].
Note: S1 places op i at +5624, i.e. 16 bytes after the buffer start. This looks inconsistent with a plain 155-byte VCED;
treat the RAM offsets as **[uncertain]**. The MIDI address map above is what matters.

#### DX7 VMEM (128 bytes per voice, used in bulk dumps and flash)
Per op, 17 bytes, OP6 first: 0-3 R1-R4; 4-7 L1-L4; 8 BP; 9 LD; 10 RD; 11 `RC<<2 | LC`; 12 `DET<<3 | RS`;
13 `KVS<<2 | AMS`; 14 OL; 15 `FC<<1 | MODE`; 16 FF.
Globals: 102-105 PR1-4; 106-109 PL1-4; 110 ALG; 111 `OKS<<3 | FB`; 112 LFS; 113 LFD; 114 LPMD; 115 LAMD;
116 `LPMS<<4 | LFW<<1 | LKS`; 117 TRNP; 118-127 name.
The firmware's `dx7voice_pack_store 0x0201D532` / unpack `0x0201D69C` are "exactly unpackProgram's inverse" (msfa)
[S1 §5.2 confirmed]. (S1's prose rendering of the bit packing, "b0 = R1 | (R2&3)<<2", is wrong as written.
Use the standard layout above, which S16's tests and S4's bank files exercise.)

### 1.5 M-VAVE private protocol (manufacturer bytes `00 32 ...` on the wire)

#### 1.5.1 Framing [S2 11-ota-protocol.md "syscmd core"; S19 fm1_read_voice.py; confirmed]
The `00 32` "manufacturer ID" is **not** a manufacturer ID. Every M-VAVE vendor message is a plain binary packet,
**packed 8→7 as one continuous LSB-first bitstream** between `F0` and `F7`:

```
packet = 00 59 | cmd:u8 | len:u24le | payload[len] | ck:u8
ck     = (~sum(payload)) & 0xFF                      (sum over payload only)
wire   = F0 pack7_lsb(packet) F7
pack7_lsb: shift bytes into an accumulator LSB-first, emit 7 bits at a time; pad the final partial septet with 0.
```
The device requires `len + 7 == decoded packet length` [S2]. The leading `00 59` packs to `00 32 ...`, and the
next wire byte mixes in the low 5 bits of `cmd`. That is why KingParamount saw a different "third byte"
(45/05/09/01) per message type.

Reference implementation (MIT, czietz) [S19]:
```python
def pack7(data):          # 8-bit -> 7-bit, LSB-first bitstream
    out=bytearray(); buf=0; n=0
    for v in data:
        buf |= v << n; n += 8
        while n >= 7: out.append(buf & 0x7F); buf >>= 7; n -= 7
    if n: out.append(buf & 0x7F)
    return bytes(out)
def mkcmd(cmd, payload):
    lp=len(payload); p=bytes([0x00,0x59,cmd,lp&0xff,(lp>>8)&0xff,(lp>>16)&0xff])+bytes(payload)
    return pack7(p + bytes([(sum(payload)&0xff)^0xff]))
```

#### 1.5.2 Command map (V13/V14 dispatcher `update_cmd_dispatch 0x02026BC4`) [S2 confirmed static]
| cmd | dec | Behaviour |
|---|---|---|
| 0x11 | 17 | return fixed 27-byte record (= identity: `"FM-1_0NN"` + padding) |
| 0x12 | 18 | return fixed 13-byte descriptor |
| 0x13-0x14, 0x16-0x20, 0x25-0x2F | | no-op |
| 0x15 | 21 | drain ring buffer at `ENG+960` |
| 0x21 | 33 | callback object method +0 `(u32 arg)` |
| 0x22 | 34 | callback object method +8 `(data, u32 arg, u24 len)`: **write** |
| 0x23 | 35 | callback object method +4 `(resp, u32 arg, u24 len)`: **read** |
| 0x24 | 36 | callback object method +12 `(u32, u32)` → u16 |
| 0x30 | 48 | complete an armed transfer (token/length match) |
| 0x30 (OTA loader) | | OTA read-request/data-response channel (1.5.6) |
Commands 33-36 take **the first payload byte as the callback-object index** (8 objects) [S2; my decode below].
FM-1-RE warns: never send guessed 33-36/48 to real hardware [S2].

#### 1.5.3 Decoded messages from the published preset-restore capture [new: my decode of S4's bytes, consistent with S2/S19 framing]
The capture is from M-VAVE's Chinese-language updater restoring the factory presets: 44 messages in about 200 ms.
Each message is acknowledged before the next is sent [S4 protocol-and-provenance.md].

| Role | Wire bytes (S4) | Decoded packet (`00 59 cmd len payload ck`) |
|---|---|---|
| Identify (host) | `F0 00 32 45 00 00 00 40 7F F7` | `00 59 11 000000 ff`: cmd 0x11, no payload |
| Identity reply (device, 41 B wire / 34 B decoded) | `F0 00 32 45 58 01 00 00 23 4D 5A 44 79 05 26 4C 1A 00 ... 20 06 F7` | `00 59 11 1b0000 "FM-1_015" 00x19 ck`: cmd 0x11, len 27. (`"#MZD"` is packing noise.) FM-1-RE's capture `...79 05 06 4C 1C...` decodes to `"FM-1_009"`; FM-1+VA's `...06 4E 1C` to `"FM-1_089"` |
| Setup x4 (host) | `F0 00 32 05 29 00 00 40 02 00 SS 00 00 20 CK F7`, SS = 00/20/40/60 | `00 59 21 050000 \| 05 \| off:u32le \| ck`: cmd **0x21** (method +0), object **5**, `off = 0x0000/0x1000/0x2000/0x3000` = bank × 4096 = bank × 32 voices × 128 B. ck = `~0x05` = `fa`, `~0x15` = `ea`, ... matches. Probably **erase/prepare a 4 KiB sector** [inferred] |
| Ack (device) | `F0 00 32 01 08 00 00 00 00 7F 01 F7` | `00 59 00 010000 \| 00 \| ff`: cmd 0x00, status 0x00 = OK [inferred] |
| Voice block x16 (host, 1190 B each) | `F0 00 32 09 41 40 00 40 02 00 SS 00 00 00 00 01 00 ...payload... CK F7` | `00 59 22 080400 \| 05 \| off:u32le \| 000400 (len 1024) \| 1024 bytes = 8 packed VMEM voices \| ck`: cmd **0x22** (write), object 5, `off = first_voice × 128` (0x0000, 0x0400, ... 0x3C00), length 1024. Total packet 1039 B → 1188 septets + F0/F7 = **1190 B** (matches S4). `ck = ~sum(05, off, len, data) & 0xFF` (S4 could not find CK because it is the tail of the bitstream) |

So "how 128 presets are organised" on the wire: callback object 5 is a flat 16 KiB voice store
(`voice i` at byte offset `i*128`, standard 128-byte VMEM). It is sent as 16 blocks of 8 voices after
4 per-bank setup messages. This restore path writes all 4 banks without the on-device A-D prompt.
The V13 flash layout matches: patches at `slot*128`, plus a **59-byte settings record per slot at flash
`+0xC000 + slot*59`** [S1 §5.2 med].
**Timing**: the whole restore is about 200 ms. The host waits for each ack, so there are no fixed delays [S4].

#### 1.5.4 Reading the current voice (stock V14/V15) [S19 confirmed by author on V14/V15]
```
request : mkcmd(0x23, [0x05, 0,0,0,0, 155, 0, 0])   # object 5, offset 0, length 155
reply   : unpack7(msg) ; data = reply[14:-1]        # strip 14-byte echoed header (00 59 cmd len3 obj off4 len3) and ck
          -> 155-byte VCED of the current sound
```
**[uncertain]** Reads of object 5 at offset 0 return the 155-byte *edit buffer*. Writes to object 5 address
packed 128-byte voices. Either object 5 is a RAM region whose read path differs, or the two sides differ.
Not resolved by any source. Emulator choice: implement both behaviours as observed.

#### 1.5.5 Other vendor entry points
- `F0 35 59 xx xx xx F7` (7 bytes): "arms the M-Vave vendor channel" (`b[ENG+6555] |= 0x80`), swallowed. It is
  also suppressed on USB loopback [S2 05-midi.md §2.1 high]. Purpose unclear.
- Enter firmware upgrade: `F0 22 24 35 7F F7` (not packed). The device then pulls the file (1.5.6) [S2; S3].
- A 6-byte magic + `0x7D/0x7F` + `F7` enters mask-ROM loader (`0xFFC02532`) [S2 05-midi §2.2 med].
- BLE carries the same syscmd family via event `0x72`, header `{00 59 x len3}`, ≤1046 bytes [S2 05-midi §5.4].
- The old "scanner finds only Param ID 0x40" result [S3, `fm1_sysex_scanner.py`] is a misreading: the scanner
  sends `F0 00 32 45 00 00 00 <id> 7F F7`, and only id 0x40 forms the valid packed checksum of an empty cmd 0x11.

#### 1.5.6 Firmware update (OTA) protocol [S2 11-ota-protocol.md confirmed by hardware reflash; S3]
1. Identify (cmd 0x11), then `F0 22 24 35 7F F7`.
2. The device *pulls* the file with read requests, all on cmd **0x30**:
   `00 59 30 | body_len:u24le | flash_type:u8 | address:u32le | req_len:u24le | data (responses only) | ck`;
   `body_len` = 8 (request) or `len(data)+8` (response); `ck = ~sum(flash_type..end of data) & 0xFF`.
   (The older "fixed `00 32 41 41` prefix" framing in S3 is only correct for aligned block sizes [S2].)
3. Step 1 verifies the entry reads, writes a JieLi `UPDATA_PARM` boot record, and resets into the OTA loader
   (`4d4a:4155`). Step 2: identify + upgrade command again, then a full pull.
4. Terminal requests: address `0xE0000000` len 8 (verification done) and `0xF0000000` len 8 (finish). The host
   answers with the 8 bytes `"success\0"`. Host timing in M-UPGRADE: sleep 2000 ms after each upgrade command,
   8000 ms receive timeout, keep MIDI open 3000 ms after verify.
5. The device refuses a same-version "no-op" update (that is why czietz's patch needs a V14 downgrade first) [S2; S8].

---

## 2. Sound engine

### 2.1 Core identity and fixed numbers [S1 docs/io/04-synth-engine.md, V13; confirmed]
- msfa (Google music-synthesizer-for-android, Apache-2.0), the core of Dexed/MicroDexed. Byte-exact tables
  in `app.bin`: `pitchmodsenstab`, `ampmodsenstab`, `velocity_data`, `exp_scale_data`, `levellut`,
  `pitchenv_tab`, `pitchenv_rate`, `coarsemul`. Also formula constants 50857777 (logfreq base), 25190424 (LFO),
  ScaleCurve `*329>>12/15`, ScaleVelocity `-239`, fixed-freq `(4458616*((coarse&3)*100+fine))>>3`,
  detune `13457*(detune-7)` (V13 code).
- **Sample rate 44118 Hz** (DAC clock-trim-corrected), block **64 samples** (≈1.45 ms), freqlut computed at boot
  for 44118. USB audio is presented as 44.1 kHz [S6; S18].
- **Polyphony 12** (`5616 = 12 × 468`-byte voice array); round-robin allocation from a cursor; **steals the
  voice at the cursor** (not the quietest); optional "reuse released voice first" flag; sustain defers release
  [S1 §5.1; S2 §4].
- Voice output: `mix += sat(voice_out >> 4)`, then a 64-frame s32 mix, converted to **16-bit stereo** ping-pong
  buffers [S1 §4]. USB audio is **mono (L = R), 16-bit grid, exact silence between notes** [S10 measured on
  FM-1_093]. The UAC capture format is S24_3LE stereo 44.1 kHz (container only) [S18].
- Per-voice 5-stage float gain ramp (anti-click fade in/out) [S1 med].

### 2.2 Differences from upstream msfa/Dexed (static, V13) [S1 §6]
- u16 sin/exp2 LUTs (msfa uses s32), **no sine interpolation in operator kernels** (LFO sine is interpolated): slightly rougher operators [high].
- Gain smoothing threshold ≈16284 vs msfa `kLevelThresh = 1120` (different units) [med].
- `Env::advance` shift lacks `+LG_N` (compensated elsewhere) [med].
- **Algorithm table copied to writable RAM**. `fm_core_render` patches entries for algorithms 3/5 (0-based =
  **algorithms 4 and 6**) at runtime [med]. This matches the FM-1+VA manual's stock bug: "**Algorithms 4 and 6
  stay altered after any patch that uses feedback**" [S16 §1.3]. **[inferred link]** Emulate: after a voice uses
  feedback on alg 4/6 (the Dexed MkI-style 2/3-op feedback loop), the patched routing persists into later
  voices that use those algorithms until something resets it.

### 2.3 Measured deviations on stock V15 (to model) [S6 FINDINGS.md unless noted; measured, one unit]
| Area | Finding |
|---|---|
| **Detune** | Beat at A4, DT0/DT14 vs DT7: 4.8/5.2 Hz ≈ **2.8 cents/step**, slightly asymmetric. msfa/Dexed ≈ 0.96 c/step (13457/2^24 octave). So about 3x too wide. Fixed by czietz's patch and FM-1+VA (1.6 Hz, 0.9 c/step) [S6; S8; S16]. Note: the V13 code shows the exact msfa constant [S1], so the V15 widening is elsewhere **[uncertain where]** |
| **Feedback** | About **1/4 of DX7-emulation strength**: H2 at FB1 is -38.9 dB vs -26 dB from the msfa formula. Scales with OL/EG like DX7. BRASS 1's feedback modulator sounds about 4.5 dB *stronger* on the unit, unexplained |
| **Modulation index** | DX7-exact (4π at full level); first sideband within 0.4 dB from OL 40-99 |
| **Velocity** | Never boosts above the stored level; DX7 curve shape; about 0.87 dB lower per KVS step (consistent with 100/127 input scaling [S10]) |
| **LFO** | "Fixed ~5.8 Hz, near-sine, free-running; patch LFO speed, waveform and delay ignored" (speeds 0-99 by dump or param change, all 6 waves, delay 0-99). Depths follow the patch: vibrato = DX7 sens table × 0.88 (±41/137/383/1069 cents at PMS 1/3/5/7, PMD 99); tremolo 2.1/5.3 dB p-p at AMS 1/2; **AMS 3 silences the operator at every trough**, even at AMD 50 |
| LFO (conflict) | **[conflict]** The V15 tutorial describes a working on-device Speed knob: "about 2 Hz at 10, 4 Hz at 30 and 8 Hz at 50", six waveforms, delay, LFO sync [S13 ch.7]. The FM-1+VA manual says stock "fast LFOs run slow" [S16]. Virtual-FM-1 found "the firmware's LFO table is Dexed's, entry for entry" on FM-1_093 [S10]. V13 code computes rate/delay/wave from the patch [S1 §4.5]. Best reading **[inferred]**: on stock V15 the LFO params set over SysEx do not take effect until a panel edit or patch-select recompute. The panel LFO works with a Dexed-like table but a capped/slow top end. Needs a hardware check |
| Pitch EG | Moves ratio-mode operators but **not fixed-frequency** ones (msfa behaviour). Slow PEG sweeps are **1.37x slower** than the DX7 figure (PR1 40: 437 ms for 90 % of a 20-step glide; 60: 202 ms; 80: 98 ms). PEG levels linear around 50 (1/32 octave per step in 27-85). RateScale does not affect the PEG [S13] |
| Op EG | Attack timing follows DX7 rates (rise to full: R1 80 ≈5 ms, 75 ≈9, 70 ≈18, 50 ≈150, 35 ≈700 ms; measured on FM-1+VA, which keeps the stock engine); release rate 72 ≈ -38 dB at +50 ms |
| Kbd scaling | Algorithm 22 routing, attack rates (R1 20-80) and keyboard rate scaling (RS 0/4/7, notes 48-84) match DX7 |
| Fixed freq | Exact: 302/1514/2512 Hz within 0.3 Hz |
| **Output filter** | **First-order high-pass at 20 Hz** (fits `-10log10(1+(20/f)^2)` within 0.1 dB). Treble roll-off: one-pole about **5.6 kHz low-pass** (-1 dB @3.5k, -2.8 @5.3k, -4.7 @7-10.5k, -8 @13.6k) [S6]. [S10] measured only 1/2/3.5 dB down at 6-10/10-14/14-20 kHz on FM-1_093 **[conflict in magnitude]** |
| Output stage | Odd harmonics 3-7 at about -55 to -61 dB near full level only; USB drops/repeats 512-sample blocks a few times a second; faint sidebands ±68 Hz around a held sine [S10] |
| Level | Raw USB level low (full sine ≈ -58..-61 dBFS peak in one setup) [S6]; follows MASTER knob [S10]. No MIDI master volume on stock [S6] |
| **Aftertouch** | Channel aftertouch causes "unexpected strong vibrato" (czietz removed it) [S8] |
| OP mute bug | "switching off OP1 silenced OP6, OP2 silenced OP5, and so on, in M-VAVE's firmware" (operator-switch bitmask reversed) [S20 FM-1+VA manual v93 notes] |
| Live edits | Parameter changes never reach a sounding note (applied at next note-on) [S6] |
Whole-patch result: Workbench's fitted software model matches 12 ROM1 voices within 3.2 dB loudness contour and
4.8 dB 1/3-octave spectrum [S6]. Virtual-FM-1 (msfa + FM-1 velocity/transpose/detune/LFO fixes) matches 8
factory presets within about 0.6 dB per octave band below 6 kHz [S10].

### 2.4 Per-patch extras beyond DX7 (stored in the 59-byte settings record) [S13; S10; S11]
- **FX chain**: 6 effects, each on/off + params, **reorderable** (SEL grabs, SELECT swaps). Order, on/off and
  values are stored **per patch**. A switched-off effect is skipped. Program change loads the stored FX
  [S13 ch.6; S16 §7.7]. Default on Reset Patches: all effects off [S16 §7.8]. Effect algorithms: no published
  DSP details. V13 has a 6-slot dispatch table (init/process, 12 B entries) at `0x02045FC4` and an 8192-byte
  FX arena [S1]. Emulate with generic designs fitted to the ranges in 1.3.
- **ENV**: a 4-stage **ADSR amp envelope** applied to the whole voice output and multiplied with the op EGs.
  Attack/Decay/Sustain/Release 0-100 (durations, not rates). Switchable on/off; off by default; saved with the
  patch [S13 ch.7; added V09, rescaled 0-100 in V15 S17].
- **MONO/POLY** per patch (saved) [S13 ch.7].
- **Osc Sync** (= DX7 OKS) appears on both LFO p2 and EDIT GLO p1 [S13].
- Record layout: inferred by Virtual-FM-1 from FM-1+VA in `firmwares/fm1_common/Fm1Record.h`. Stock bytes
  are often `0x03` "unset". Bytes 45-53 read `06 00 00 07 00 00 08 00 00` on FM presets [S10; S11 FIRMWARE-GAPS.md].
  **[uncertain for stock]**

### 2.5 Global (instrument) settings [S13 ch.2/7/11; S10]
GLO page 1: Midi Chn (ALL/1-16), Effect Chn (1-16, default 2), Pitch Up, Pitch Dn (1-24 each).
GLO page 2: Reset Patches (per bank A-D, **restores the last imported bank**, not the factory set),
Velocity (1-127, onboard keys), Glide (Full Time / Fingered), Glide Val (0-100 = 0-1 s).
FM-1_093 RAM: glide time `+0x82`, mode `+0x83` (0 Full Time, 1 Fingered) from base `0x01C0FEFD`; a second copy
of bend/glide sits near `0x01C61D57` [S10]. That is the FM-1+VA build, but the settings are stock-derived.

### 2.6 Glide [S13 ch.7; S17 V15]
- Added in V15. Glide Val 0-100 maps to **0-1 s**. Modes: **Full Time** (every note) and **Fingered**
  (legato only). Greyed out unless the patch is MONO.
- Curve: unknown. Virtual-FM-1 notes msfa's `porta.cpp` as the likely basis, unmodelled [S11]. V13 voice struct
  already has portamento/glissando state at voice+420..437 [S1 §4.4].

### 2.7 Arpeggiator [S13 ch.9 (V15); S2 05-midi §6 (V13); S20]
- Modes (7): **Up, Down, Inclusive** (up-down repeating ends), **Exclusive** (no repeated ends), **Random**,
  **Order** (as played), **Repeat** (each note twice). V13 code: 0 up, 1 down, 2 up-down (mirrored),
  3 down-up, 4 random (Fisher-Yates using HW RNG), 5 played order, 6 off. **[conflict V13 vs V15 list]**.
  Use the V15 list.
- Octave 1-4. Rate: 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T (1/1, 1/2 added in V13).
  Tempo 30-300 BPM. Gate 20-100 %. Swing 50-75 %. Latch on/off (finishes the cycle on unlatch). Sync to MIDI clock.
- Held-note window: up to 27 notes; `idx = note - 53 - semi - 12*oct` [S2].
- Arp settings are **global, not per patch, and not saved across power-off** [S13].
- Arp and sequencer are mutually exclusive ("Disable SEQ to enter" / "Disable ARP to enter") [S13].
- Shared tick engine; minimum step 30 ms; step time scaled by gate `x*param/100` [S2].
- **[conflict]** Tempo: the tutorial says arp and seq tempos are separate [S13]; the V13 changelog says "BPM
  synchronization for ARP and Seq", and [S18] says they share one BPM.

### 2.8 Sequencer [S13 ch.10; S18 sequencer.md; S17]
- **16 patterns × 16 steps**. Step recording (REC, each key fills the next step, wraps and overwrites).
  Long notes span steps (hold key + turn SELECT; V15). REC + SELECT moves through steps. Empty step = rest.
  The lower 16 keys show the steps.
- Page 1: Pattern 1-16, Clear (step / pattern), **Chain 1-16** (plays N consecutive patterns from the
  current one, wrapping 16→1), **Step 1-16** (loop length, non-destructive).
- Page 2: **Voice 1-128 bound per pattern** (the PRESETS knob is dead while SEQ is on), Rate (**stored per step**,
  so pattern length = sum of step lengths), Tempo 30-300, Gate 20-100 %.
- Page 3: Swing 50-75 %, Sync (MIDI clock), Transpose ±24.
- Patterns auto-save to flash; there is no save step. V13 pattern record is 32 B: up to 10 note slots (<0 = rest),
  velocities, count, flags; banks of 2048 B in flash at `+0x4000` [S2 §6.5; V13, pre-V15 features].
- MIDI clock (official [S12]): `F8` follows external BPM (24 ppqn [S15]), `FA` start from step 0,
  `FB` **same as Start (no resume)**, `FC` stop. SPP and MTC are discarded [S15].
  **[conflict]** [S18] lists "receiving MIDI Clock" as missing from stock V15, contradicting the official doc.
- Not in stock: more than 16 steps, ratchet/chance, multiple notes per step (as a chord), overdub [S18].
  A one-step gap between chained patterns is reported, unconfirmed [S21 KVR].

### 2.9 Init voice and other UI behaviour
- **Init Voice** (EDIT → GLO key → page 2 → K1): resets the edit buffer to "a single operator producing a pure
  sine wave, with the other five turned all the way down". No confirmation; affects only the edit buffer [S13 ch.2].
  This presumably matches the Yamaha DX7 INIT VOICE (alg 1, OP1 OL 99, others 0, all EG R 99/L 99 99 99 0,
  PEG 50s, transpose 24, name "INIT VOICE") **[inferred, exact bytes unverified]**.
- Rename and Copy Voice on the same page (V15). Long-press an OP key mutes that op; changing preset clears mutes [S13; S17].
- Keyboard: 27 keys. OCT± up to ±3 octaves. Hold OCT + turn ALGORITHM for ±12 semitones. Both OCT resets [S13].
- Transpose has three scopes: SEQ transpose (pattern only), OCT/semitone (live only), patch TRNP (both) [S13].

---

## 3. Hardware and firmware

| Fact | Detail | Source |
|---|---|---|
| SoC | JieLi **AC791N** (WL82 series), dual **pi32v2** cores, XIP flash at `0x02000000`. The `JL-BR22` string is inherited library naming, not the SoC | [S1 README; S3] |
| Flash | **1 MiB** SPI, single bank, no recovery button, no debug header. Boot head `0x0000-0x3FFF` (never erase); app at `0x4000` (to ≈`0x92FFF`); presets/settings at `0x97000` (0x8000) and `0xFC000` (0x2000) per the Felucca brick fix | [S22 dreyandersson; S3] |
| RTOS | JieLi SDK RTOS with task queues ("c04"/"04") | [S2] |
| Audio | On-chip DAC, DMA ping-pong pumps the renderer, HW SRC block present; headphone TRS out + small speaker; class-compliant USB audio in/out | [S1; S13; S23 taoofmac] |
| Display / UI | 240×240 colour TFT; 27 silicone keys; 4 knobs + MASTER, SELECT, PRESETS, ALGORITHM encoders; buttons HOME SAVE ARP SEQ PLAY/STOP REC FX SEL ENV LFO EDIT GLO, OCT-/OCT+ | [S13; S3] |
| Battery | 2000 mAh, ≈12 h; 161.5 × 96.5 × 28.6 mm, ≈250 g | [S23] |
| Sister device | SMK-37 Pro uses the same SoC, bootloader and chip key | [S3] |

**Firmware versions** (official release notes https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1.txt [S17]; fwradar [S24]):
- **V09** (2026-06-24): ENV on/off switch; RATE change during SEQ record; SEQ rests and note replacement.
- **V13** (2026-07-03): ARP/SEQ BPM sync; OP page memory; tone change in sequencer; some factory tones
  re-levelled; global velocity 1-127; MIDI channel select (default ALL); Program Change 0-127; rates 1/1 and 1/2.
- **V14** (2026-07-06): no notes published. Its embedded image is 704,052 B and its app is 1888 B larger than V13 [S2].
- **V15** (2026-07-30): brighter LEDs; per-bank Reset (to last import); **Glide** 0-1 s (mono only);
  SEQ long notes, voice binding, Step, Chain; Init/Rename/Copy voice; long-press OP mute; **FX and ENV 0-10 →
  0-100**; **vel-0 note-on = note-off**; CC control of FX.
  File: https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/firmware/FM-1.fwsc , **699,956 bytes**,
  SHA-256 `DB1642B2B6FA5C2CCCB11FFD13878068BB28601678D3644049F99DC40E7EDB8A` [S8; S6; HTTP HEAD checked 2026-10-14].
  Copyright M-VAVE; do not redistribute [S8]. Since October 2026 M-VAVE also ships this V15 image in FM1 Open
  (`sdk/firmware/FM-1_015.fwsc`) under Apache-2.0, as a recovery image [S25 THIRD_PARTY.md, confirmed].
- **V16** (2026-10-21, current; 718,196 bytes at the same URL) [S17 confirmed]: **eight Voices** (timbral parts),
  each with its own preset, MIDI channel, level, pan, bend ranges, glide and fine tune; **20-voice polyphony**
  shared across them (was 12). Patterns up to 64 steps with per-step ratchets, and an 8 × 8 arrangement replaces
  chaining. **Effects move to a shared library of 128 FX groups**, stored apart from voice presets; all Voices go
  through one shared stereo chain. **FX CCs move from 0-23 to 70-93** (Filter 70-73, Reverb 74-77, Delay 78-81,
  Distortion 82-85, Chorus 86-89, Phaser 90-93; the old map is gone). Adds CC7 master volume, CC11 expression,
  ARP from external MIDI, Bank Export as DX7 SysEx, a Panic hold on PLAY/STOP, key LEDs from MIDI notes 0-26 on the
  effect channel, RGB colour editing of the screen. MF-1 emulates V15; see section 8.

**.fwsc container** [S2 11-ota-protocol.md; S3]: JieLi UFW (`@JMUA`, `JLUFW`, target `AC791N`). The first
960 bytes are 20 × 0x30 blocks of 0x2F data + 1 marker byte; strip the markers to get a 940-byte logical header,
then the rest is raw (704084 → 704064 logical for V13). The header decrypts with HDRKEY 0xFFFF and has 8 entries:
`flash.bin, info.log, USR, isd_config.ini, ota.bin, script.ver, blimit.bin, tail.bin`.
`script.ver` (SFC-encrypted, chipkey 0x980F) = `AC791N-v0.01-cfg_tool-v0.10`. `ota.bin` is a nested image
holding `usb_hid_ota.bin` as 6 LZ4 block chunks. CRCs are CRC-16/CCITT-FALSE. **No cryptographic signature**.
Unpacked V13/V14 `app.bin` is about 583-585 KB. Images in the repo: `firmware-images/v13`, `v14` [S2].

---

## 4. Factory presets: where to get them, and licensing

- **Recovered stock factory set** (KingParamount), 4 standard DX7 VMEM banks, verified by me (4104 B,
  header `F0 43 00 09 20 00`, valid checksum, F7):
  - https://raw.githubusercontent.com/KingParamount/fm1-factory-presets/main/banks/FM-1_factory_bank1.syx (A: PIANO/ORGAN/SYN LEAD/SYN PAD, interleaved) sha256 `6a46c1ee...28f9`
  - .../banks/FM-1_factory_bank2.syx (B: GUITAR/DS GUITAR/BASS/SYN BASS) `0f076624...0264`
  - .../banks/FM-1_factory_bank3.syx (C: BRASS/WOODWIND/STRING/VOICE) `5eab4699...b99d`
  - .../banks/FM-1_factory_bank4.syx (D: percussion/FX, original names) `8142fb69...0308`
  - .../banks/FM-1_factory_128voices_packed.bin (16384 B, all 128 packed voices)
  - Captured from the Chinese updater that **downgrades to V14**, so this is the V13/V14-era set. V13 "updated
    some factory tones to make the volume more even" [S17]. Whether V15 ships identical voices is **[uncertain]**.
  - Quirks preserved: `Organ 2` lower case, `PIANO3`, `BI   BEN` (G lost), slots 115/119/123 identical "SAW EM UP" [S4].
  - Provenance: 126/128 byte-identical to BlackWinny's Dexed_cart 1.0 compilation; 40 trace to Yamaha
    ROM/VRC cartridges [S4]. (FM-1-RE's claim "factory banks are the literal DX7 ROM1A" [S1 docs/03] is
    contradicted by this decode.)
  - **Licence** [S4 LICENCE.md]: `banks/` CC0 1.0, covering *only* the capture/decode/reassembly. "I claim no
    rights in the voice parameters themselves", which come from Yamaha ROM/VRC material and community banks as
    selected by M-VAVE. `tools/` MIT. `docs/` and `FM-1-tutorial.pdf` CC BY-SA 4.0. **Practical status**: the
    file packaging is free; the underlying voice data has mixed, unclear rights (Yamaha factory voices and
    community patches). Fine for a personal emulator; for a distributed product, ship a downloader or require
    user import **[legal judgement, not advice]**.
- Benny Sparra's fm1-editor bundles these four banks unchanged under CC0, plus 26 mene311 themed banks
  (with permission) and Yamaha Black Boxes banks [S16 README].
- Yamaha DX7 ROM1A-4B and VRC101-112: https://yamahablackboxes.com/patches/dx7/factory/rom1a.syx etc.
  (`get_dx7_factory.sh` [S4]). Yamaha copyright; widely circulated **[rights unclear]**.
- mene311/fm1-banks: 26 × 32-voice themed banks, https://github.com/mene311/fm1-banks (licence not checked).
- Commercial: NatLife "Ambient & Dreams".

---

## 5. Licences of the community sources (for reuse in an emulator)

| Repo | Licence | Reuse notes |
|---|---|---|
| aroum/fm1-custom-fw | **none declared** (GitHub: no licence) | Read only; facts are fine, do not copy code (`fm1_flasher.py`, `fm1_sysex_scanner.py`) |
| cicloid/awesome-fm-1 | CC0-1.0 | free |
| keremimo/melodee | GPL-3.0-only (with Apache-2.0 msfa, MIT, BSD parts; CZ-1/Prophet data with no licence granted) | Copying code makes the emulator GPL-3.0. Its `fm6_core.c` is an msfa/Dexed restatement, not the stock engine |
| KingParamount/fm1-factory-presets | Mixed: banks CC0, tools MIT, docs CC BY-SA 4.0 | see §4 |
| AL-255/FM-1-RE | WTFPL | docs and tools free; also contains M-VAVE firmware images (M-VAVE copyright; do not redistribute) |
| czietz/fm1-read-voice | MIT (in README/source) | `pack7`/`mkcmd` reusable with notice |
| czietz/fm1-firmware-patcher | Unlicense | free; the patched firmware itself may not be distributed |
| thegiantsnail/fm1-workbench-public | GPL-3.0 | `app/fm1-synth.js` is a hardware-fitted "Software FM-1" (fits for detune, feedback, LFO, velocity, filters); GPL if copied |
| jbschooley/Virtual-FM-1 | GPL-3.0-or-later (msfa Apache-2.0 inside) | the closest existing "software FM-1" (JUCE, VST3/AU, macOS). GPL if copied |
| benny-sparra/fm1-dx7-patch-importer (fm1-editor.com) | MIT | `docs/fm1-research.md` is a good evidence ledger |
| fuleo/fm1-guide | none declared | facts only |
| msfa (google/music-synthesizer-for-android) | **Apache-2.0** | **the right base for a closed or permissive emulator**: same core as the firmware |
| Dexed | GPL-3.0 | avoid if you want non-GPL |
Recommendation: build on **msfa (Apache-2.0)** directly and implement the measured deviations (§2.3) from the
facts. Facts are not copyrightable. Avoid copying GPL code unless the emulator will be GPL.

---

## 6. Emulator checklist (derived)

1. msfa at 44118 Hz (or 44100, a 0.04 % pitch error), 64-sample blocks, 12 voices, round-robin steal at the cursor.
2. Deviations: detune ×≈2.9 (match 2.8 c/step, slight asymmetry); feedback strength ≈1/4 (-12 dB on H2);
   velocity × 100/127; alg 4/6 state-leak (optional "bug-compatible" switch); OP-mute reversal (optional);
   aftertouch → pitch mod; AMS 3 full-depth trough; PEG 1.37x slower on slow rates; output 20 Hz 1-pole HPF +
   ≈5.6 kHz 1-pole LPF; 16-bit mono out.
3. LFO: decide via the hardware question in §2.3. Default: Dexed table from panel and patch-select, and ignore
   param 137 via SysEx (bug-compatible mode).
4. Post chain: per-patch ADSR ENV (0-100 durations), then 6 reorderable FX with the 1.3 CC map and clamping.
5. MIDI: note channel/FX channel filters; CC ≤ 23 on FX channel only; ignore CC7/11/120/121/123 on the note channel;
   transpose-at-note-off hang quirk; PC +45 ms busy; VCED dump stall (optional).
6. SysEx: DX7 bank → A-D picker (UI), single voice → edit buffer, param change `F0 43 10` only.
   Vendor `00 59` packets: answer cmd 0x11 identity (`"FM-1_015"`), ack 0x00, implement 0x21/0x22/0x23 on
   object 5, so the M-VAVE restore tool, czietz's reader and fm1-editor's identify work against a virtual MIDI port.
7. Arp (7 modes, global), sequencer (16×16, per-step rate, voice per pattern, chain/step), MIDI clock
   (FB = restart). MIDI out: notes only, ch 1, velocity = Global Velocity for onboard keys.

---

## 7. Sources

- [S1] AL-255/FM-1-RE docs: https://github.com/AL-255/FM-1-RE/blob/main/docs/io/04-synth-engine.md , docs/03-dx7-core-identification.md , docs/reversing/03-audio-and-synth.md , docs/io/03-audio-dac.md
- [S2] AL-255/FM-1-RE: https://github.com/AL-255/FM-1-RE/blob/main/docs/io/05-midi.md , https://github.com/AL-255/FM-1-RE/blob/main/docs/io/11-ota-protocol.md
- [S3] aroum/fm1-custom-fw: https://github.com/aroum/fm1-custom-fw/blob/main/REVERSE_ENGINEERING.md , README.md , fm1_sysex_scanner.py
- [S4] KingParamount/fm1-factory-presets: https://github.com/KingParamount/fm1-factory-presets/blob/main/docs/protocol-and-provenance.md , README.md, LICENCE.md, tools/fm1_decode.py
- [S6] thegiantsnail/fm1-workbench-public FINDINGS.md: https://github.com/thegiantsnail/fm1-workbench-public/blob/main/FINDINGS.md
- [S8] czietz/fm1-firmware-patcher: https://github.com/czietz/fm1-firmware-patcher
- [S10] jbschooley/Virtual-FM-1 docs/HARDWARE-NOTES.md: https://github.com/jbschooley/Virtual-FM-1/blob/main/docs/HARDWARE-NOTES.md
- [S11] jbschooley/Virtual-FM-1 docs/FIRMWARE-GAPS.md, README.md
- [S12] M-VAVE official "FM-1 MIDI EN.docx": https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1%20MIDI%20EN.docx
- [S13] KingParamount "FM-1 tutorial" PDF (V15, Release Version 2): https://github.com/KingParamount/fm1-factory-presets/blob/main/FM-1-tutorial.pdf
- [S15] Baud Girl FM-1 MIDI guide: https://m-vave-fm1-midi-guide.up.railway.app/
- [S16] benny-sparra/fm1-dx7-patch-importer docs/fm1-research.md and README: https://github.com/benny-sparra/fm1-dx7-patch-importer
- [S17] M-VAVE release notes: https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1.txt
- [S18] fuleo/fm1-guide: https://fuleo.github.io/fm1-guide/reverb.html , /sequencer.html
- [S19] czietz/fm1-read-voice: https://github.com/czietz/fm1-read-voice/blob/main/fm1_read_voice.py
- [S20] Baud Girl FM-1+VA manual: https://baudgirl.com/work/FM-1+VA/manual
- [S21] KVR thread: https://www.kvraudio.com/forum/viewtopic.php?t=632127
- [S22] Drey Andersson: https://dreyandersson.com/blog/m-vave-fm-1-custom-firmware/
- [S23] Tao of Mac review: https://taoofmac.com/space/reviews/2026/10/11/2100
- [S24] fwradar: https://fwradar.com/p/m-vave-fm-1
- [S25] M-VAVE FM1 Open: https://github.com/m-vave/fm1_open (README, LICENSE.md, THIRD_PARTY.md, sdk/include)
- Also read: cicloid/awesome-fm-1 README (https://github.com/cicloid/awesome-fm-1), keremimo/melodee LICENSING.md,
  Synth Anatomy librarian article (https://synthanatomy.com/2026/07/m-vave-fm-1-patch-librarian.html: Benny Sparra's
  browser librarian; 4 banks A-D × 32; no protocol detail).

## 8. Open questions worth a hardware test before relying on them
1. Stock V15 LFO: does panel Speed work while SysEx param 137 does not? What is the actual Hz table?
2. Where V15's detune widening comes from (asymmetric 4.8/5.2 Hz).
3. The exact alg 4/6 state-leak trigger and reset condition.
4. Whether the V15 factory set equals the recovered V14-era set.
5. The 59-byte settings record layout on stock V15.
6. Effect DSP (reverb/chorus/phaser algorithms, cutoff curve 0-107).
7. Glide curve (msfa porta or custom); one-step chain gap; arp/seq shared vs separate tempo; stock MIDI clock receive.
8. Object 5 read vs write semantics in the `00 59` protocol (§1.5.4).
9. Whether MF-1 should follow V16 (eight Voices, 20-voice polyphony, the FX library and CCs 70-93), offer V15
   and V16 as a choice, or stay on V15, which custom firmwares still expect as their base. Also whether V16's
   engine sounds the same as V15's for one Voice.
