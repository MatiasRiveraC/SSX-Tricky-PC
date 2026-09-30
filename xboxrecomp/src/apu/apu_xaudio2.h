/**
 * XAudio2 Audio Output Backend
 */
#ifndef APU_XAUDIO2_H
#define APU_XAUDIO2_H

#include <stdint.h>

/* Initialize XAudio2. Returns 1 on success, 0 on failure. */
int xa2_init(void);

/* Shut down XAudio2 and release all resources. */
void xa2_shutdown(void);

/* Returns 1 if XAudio2 is active. */
int xa2_is_active(void);

/* Submit interleaved stereo 16-bit samples. Returns 1 if accepted. */
int xa2_submit_samples(const int16_t *samples, int num_samples);

/* Get the preferred buffer size in samples. */
int xa2_get_buffer_size(void);

/* Buffers queued on the device and not yet played. */
int xa2_queued(void);
/* 16 x 256 samples = ~85 ms of queued audio. The buffers used to be 1024
 * samples, and the frame thread renders one buffer in a single burst before
 * waiting on the device: a title that refills its streaming ring ~1000
 * samples ahead of the play cursor (SSX Tricky's EA mixer) had the last 32
 * samples of every burst read last lap's data -- an exact copy of the sound
 * 50 ms earlier, a click every 21 ms (part 183). Small buffers keep each
 * burst well inside the title's lead. */
#define XA2_TARGET_QUEUED 16

#endif /* APU_XAUDIO2_H */
