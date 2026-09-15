/*
 * pop_stubs.c - Minimal stubs for SDLPoP symbols that belong to subsystems we
 * intentionally do NOT vendor on the ESP32 port (audio/MIDI), plus a POSIX
 * access() that ESP-IDF's newlib does not provide.
 *
 * Audio is out of scope for now (deferred/optional), so the MIDI entry points
 * are no-ops. access() is only used by the engine to probe for optional files
 * on a real filesystem; on the ESP32 all game data is served from flash via
 * pop_open_embedded_dat(), so reporting "not found" is the correct behaviour.
 */
#include "sdlpop/common.h"
#include <errno.h>

/* --- MIDI (audio deferred) --------------------------------------------- */
void stop_midi(void) {}

void play_midi_sound(sound_buffer_type* buffer) {
	(void)buffer;
}

void midi_callback(void* userdata, Uint8* stream, int len) {
	(void)userdata;
	if (stream != NULL && len > 0) {
		memset(stream, 0, (size_t)len);
	}
}

/* --- POSIX access() not provided by ESP-IDF newlib --------------------- */
int access(const char* path, int mode) {
	(void)path;
	(void)mode;
	errno = ENOENT;
	return -1;
}
