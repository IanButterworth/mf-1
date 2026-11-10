// C interface between the SwiftUI front end and the C++ engine.
// Every call is safe from the main thread; none blocks the audio thread.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts audio and MIDI. stateDir holds the saved "flash" contents. An
// empty outputUID follows the system default output device.
bool mf1_start(const char* stateDir, const char* midiPortName, const char* outputUID);
// Output devices: count (refreshes the list), then name and UID by index.
int mf1_output_count(void);
void mf1_output_info(int index, char name[256], char uid[256]);
// Moves audio to another device; an empty UID follows the system default.
void mf1_set_output(const char* uid);
void mf1_stop(void);
// True on a fresh state, before any factory banks were imported. Also true
// when a saved state existed but could not be read: that file is renamed
// to state.bin.unreadable-<time> and kept, the factory banks load, and a
// message says so. Saving is disabled if the file could not be moved.
bool mf1_needs_factory(void);
// Loads a 4104 byte DX7 bank into bank 0..3 and records it as the
// "last imported" bank used by Reset Patches.
bool mf1_import_factory_bank(int bank, const uint8_t* data, size_t len);

// ---- keys (on-board keyboard; note already includes octave/transpose)
void mf1_key_down(int note);
void mf1_key_up(int note);
void mf1_all_notes_off(void);
void mf1_pitch_bend(int value14);
void mf1_mod_wheel(int value);

// ---- presets and the edit buffer
int mf1_current_preset(void);
// 128 (banks A-D, as on the FM-1), or 160 in extended mode with bank E.
int mf1_preset_count(void);
void mf1_load_preset(int index);
void mf1_preset_name(int index, char out[11]);
void mf1_edit_name(char out[11]);
int mf1_edit_get(int vcedIndex);
void mf1_edit_set(int vcedIndex, int value);
bool mf1_edit_mono(void);
void mf1_edit_set_mono(bool mono);
// ENV overlay: index 0 = on/off, 1..4 = attack, decay, sustain, release.
int mf1_env_get(int index);
void mf1_env_set(int index, int value);
// FX parameters by CC number 0..23, at panel resolution (Q is 0..100).
int mf1_fx_get(int cc);
void mf1_fx_set(int cc, int value);
// Chain order: slot position 0..5 to effect 0..5 (Filter, Reverb, Delay,
// Distortion, Chorus, Phaser).
int mf1_fx_order(int position);
void mf1_fx_swap(int positionA, int positionB);
bool mf1_dirty(void);
// SAVE: writes the edit buffer to the current preset, then everything to disk.
void mf1_save(void);
void mf1_init_voice(void);
void mf1_rename(const char* name10);
void mf1_copy_voice(int fromIndex);
void mf1_reset_bank(int bank);
void mf1_set_op_mutes(int mask);  // bit i mutes OP(i+1) until the preset changes
int mf1_op_mutes(void);
// Algorithm routing for drawing: carrier mask and modulator links.
int mf1_carrier_mask(int algorithm);

// ---- global settings
enum {
  MF1_G_MIDI_CH, MF1_G_FX_CH, MF1_G_BEND_UP, MF1_G_BEND_DOWN, MF1_G_VELOCITY, MF1_G_GLIDE_MODE,
  MF1_G_GLIDE_TIME, MF1_G_VOLUME, MF1_G_TEMPO, MF1_G_QUIRKS, MF1_G_OCTAVE, MF1_G_TRANSPOSE,
  // Extended mode: MF-1 additions after the FM-1 signal path, ignored when off.
  MF1_G_EXTENDED, MF1_G_STEREO_WIDTH,
  MF1_G_COUNT
};
int mf1_global_get(int id);
void mf1_global_set(int id, int value);

// ---- arpeggiator
enum { MF1_A_MODE, MF1_A_OCTAVES, MF1_A_RATE, MF1_A_GATE, MF1_A_SWING, MF1_A_LATCH, MF1_A_SYNC, MF1_A_COUNT };
bool mf1_arp_on(void);
void mf1_arp_enable(bool on);
int mf1_arp_get(int id);
void mf1_arp_set(int id, int value);
const char* mf1_arp_mode_name(int mode);
const char* mf1_rate_name(int rate);
int mf1_rate_count(void);

// ---- sequencer
enum {
  MF1_S_PATTERN, MF1_S_CHAIN, MF1_S_STEPS, MF1_S_VOICE, MF1_S_RATE, MF1_S_GATE, MF1_S_SWING,
  MF1_S_SYNC, MF1_S_TRANSPOSE, MF1_S_COUNT
};
bool mf1_seq_on(void);
void mf1_seq_enable(bool on);
int mf1_seq_get(int id);
void mf1_seq_set(int id, int value);
void mf1_seq_clear_pattern(void);
void mf1_seq_play(void);
void mf1_seq_stop(void);
bool mf1_seq_playing(void);
void mf1_seq_record(bool on);
bool mf1_seq_recording(void);
int mf1_seq_rec_step(void);
// Turning SELECT while recording: moves the record step, or stretches the
// held notes into a long note when keys are down.
void mf1_seq_select(int delta);
int mf1_seq_play_step(void);
// Bit i set when step i of the current pattern has notes.
int mf1_seq_step_mask(void);

// ---- pre-programmed songs
int mf1_song_count(void);
const char* mf1_song_name(int index);
// Loads the song's voice and effects into the edit buffer (unsaved, so it
// can be kept with SAVE), turns ARP and SEQ off, and plays the song.
void mf1_song_play(int index);
// Plays the notes of a Standard MIDI File (drums skipped). With voiceSong
// at a built-in song's index, that song's voice and effects are loaded;
// with -1 the current voice plays it. False if it is not a MIDI file.
bool mf1_song_play_midi(const uint8_t* data, size_t len, int voiceSong);
void mf1_song_stop(void);
bool mf1_song_playing(void);

// ---- SysEx import / export
// Returns 1 for a 32 voice bank (waits for a bank choice), 2 for a single
// voice (loaded into the edit buffer), 0 if not understood.
int mf1_import_syx(const uint8_t* data, size_t len);
// A bank arrived (from a file or over MIDI) and waits for A..D.
bool mf1_bank_pending(void);
void mf1_bank_pending_name(int voice, char out[11]);
void mf1_bank_commit(int bank);
void mf1_bank_cancel(void);
// Writes a 4104 byte DX7 bank dump of bank 0..3.
void mf1_export_bank(int bank, uint8_t out[4104]);
// Writes a 163 byte DX7 single voice dump of the edit buffer.
void mf1_export_voice(uint8_t out[163]);

// ---- status for the panel
// Notes sounding, one bit per MIDI note, for the key LEDs.
void mf1_sounding(uint64_t* lo, uint64_t* hi);
// Copies the newest n scope samples.
void mf1_scope(float* out, int n);
float mf1_cpu_load(void);
int mf1_active_voices(void);
float mf1_peak(void);
// Where this window's sound sits, -1 left .. 1 right, among the app's other windows. Not saved; 0 is unchanged.
void mf1_set_pan(float pan);
// Increments when something other than the UI changed the model (MIDI,
// SysEx, the sequencer switching voice), so the UI knows to refresh.
uint32_t mf1_generation(void);
// Short message for a popup from an external event, empty if none.
void mf1_take_message(char out[64]);
double mf1_sample_rate(void);
// Latency report: IO buffer, device, safety offset and stream latency in
// frames, the sample rate, and the last note-on's queue wait in ms.
void mf1_latency(double out[6]);
const char* mf1_midi_port_name(void);

// ---- offline rendering, for tests and benchmarks; no audio device.
void mf1_offline_init(double sampleRate);
void mf1_offline_render(float* out, int frames);
void mf1_offline_render_stereo(float* left, float* right, int frames);
// Feeds one complete MIDI message as if it arrived on the MIDI port.
void mf1_inject_midi(const uint8_t* data, size_t len);
// In offline mode, MIDI the engine would send is kept; this takes the
// oldest message, returning its length (0 when none).
size_t mf1_take_midi_out(uint8_t* out, size_t max);
// Offline: loads stateDir as mf1_start does, and saves there from now on;
// an empty path stops saving. True when a saved state was loaded.
bool mf1_offline_open_state(const char* stateDir);
// Encodes a message as the virtual MIDI source sends it, in event lists
// of at most listBytes (0 for the size used when sending), and decodes it
// again as the input ports do. Writes the decoded messages back to back,
// returns their total length and stores the number of event lists.
size_t mf1_midi_loopback(const uint8_t* data, size_t len, size_t listBytes, uint8_t* out, size_t max,
                         int* lists);

#ifdef __cplusplus
}
#endif
