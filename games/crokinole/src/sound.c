/* crokinole's sound, made in code at load (no audio files):
 *   clack     disc on disc: a hard wooden knock, two body modes over a short click, louder and brighter the harder
 *   thock     disc on a peg: the rubber sleeve's dull thud
 *   ditch     a disc dropping into the felt: a soft low bump
 *   plonk     a disc dropping into the hole: a hollow knock and a settle
 *   flick     the finger's snap
 *   slide     discs gliding on lacquer: a quiet hiss, its level following the fastest disc
 *   chimes    a round counted, a match won: soft marimba notes
 * Everything plays on the shell's effects bus. */
#include "sound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/shell.h"

#define RATE 44100
enum { S_CLACK, S_CLACK_SOFT, S_THOCK, S_DITCH, S_PLONK, S_FLICK, S_SLIDE, S_ROUND, S_WIN, S_FOUL, S_TWENTY, S_MENU, S_SELECT, S_BACK, S_COUNT };

static cw_sound snd[S_COUNT];
static cw_voice slide_voice;
static float *pcm;
static uint32_t rng = 7;
static float nz(void) { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 8388608.0f - 1.0f; }

/* a struck piece of wood: modes (Hz, decay /s, level) over a click of filtered noise */
static cw_sound knock(float secs, const float (*modes)[3], int nm, float click, float click_decay, float gain) {
  int n = (int)(secs * RATE);
  float lp = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / RATE, v = 0;
    for (int k = 0; k < nm; k++) v += sinf(6.2831853f * modes[k][0] * t) * expf(-t * modes[k][1]) * modes[k][2];
    lp += (nz() - lp) * 0.6f;
    v += lp * click * expf(-t * click_decay);
    float att = i < 20 ? (float)i / 20.0f : 1;
    pcm[i] = v * gain * att;
  }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}

static cw_sound marimba(const float *hz, int count, float each, float gain) {
  int per = (int)(each * RATE), n = per * count + RATE;
  memset(pcm, 0, sizeof(float) * (size_t)n);
  for (int k = 0; k < count; k++)
    for (int i = 0; i < n - per * k; i++) {
      float t = (float)i / RATE;
      float v = (sinf(6.2831853f * hz[k] * t) * expf(-t * 5) + 0.35f * sinf(6.2831853f * hz[k] * 4 * t) * expf(-t * 22)) * gain;
      if (i < 30) v *= (float)i / 30.0f;
      pcm[per * k + i] += v;
    }
  return cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
}

void sound_init(void) {
  pcm = malloc(sizeof(float) * RATE * 4);
  { const float m[][3] = { { 2350, 55, 0.55f }, { 3720, 80, 0.35f }, { 1180, 40, 0.25f }, { 5900, 140, 0.15f } }; snd[S_CLACK] = knock(0.22f, m, 4, 0.9f, 260, 0.75f); }
  { const float m[][3] = { { 2100, 70, 0.5f }, { 3300, 95, 0.25f }, { 1050, 50, 0.2f } }; snd[S_CLACK_SOFT] = knock(0.14f, m, 3, 0.35f, 300, 0.5f); }
  { const float m[][3] = { { 190, 45, 0.7f }, { 410, 70, 0.25f }, { 900, 120, 0.1f } }; snd[S_THOCK] = knock(0.2f, m, 3, 0.25f, 120, 0.8f); }
  { const float m[][3] = { { 120, 30, 0.6f }, { 260, 55, 0.2f } }; snd[S_DITCH] = knock(0.25f, m, 2, 0.4f, 60, 0.7f); }
  { const float m[][3] = { { 330, 18, 0.6f }, { 520, 30, 0.3f }, { 1460, 60, 0.15f } }; snd[S_PLONK] = knock(0.5f, m, 3, 0.3f, 90, 0.8f); }
  { const float m[][3] = { { 1800, 200, 0.2f } }; snd[S_FLICK] = knock(0.06f, m, 1, 1.0f, 400, 0.45f); }
  { /* the glide: soft filtered noise, looped */
    int n = RATE;
    float lp = 0, lp2 = 0, hp = 0, last = 0;
    for (int i = 0; i < n; i++) {
      lp += (nz() - lp) * 0.18f, lp2 += (lp - lp2) * 0.3f;
      hp = 0.97f * (hp + lp2 - last), last = lp2;
      pcm[i] = hp * 0.9f;
    }
    for (int i = 0; i < 600; i++) { float k = (float)i / 600; pcm[i] *= k; pcm[n - 1 - i] *= k; }
    snd[S_SLIDE] = cw_sound_from_pcm(pcm, n, 1, RATE, NULL);
    cw_sound_set_loop(snd[S_SLIDE], true, 0, n);
  }
  { static const float a[] = { 523.25f, 659.25f, 783.99f }; snd[S_ROUND] = marimba(a, 3, 0.14f, 0.3f); }
  { static const float a[] = { 523.25f, 659.25f, 783.99f, 1046.5f, 783.99f, 1046.5f }; snd[S_WIN] = marimba(a, 6, 0.16f, 0.3f); }
  { static const float a[] = { 392.0f, 329.63f }; snd[S_FOUL] = marimba(a, 2, 0.16f, 0.25f); }
  { static const float a[] = { 783.99f, 1046.5f, 1318.5f }; snd[S_TWENTY] = marimba(a, 3, 0.09f, 0.3f); }
  { const float m[][3] = { { 1400, 60, 0.3f } }; snd[S_MENU] = knock(0.06f, m, 1, 0.3f, 300, 0.35f); }
  { const float m[][3] = { { 2100, 50, 0.4f }, { 3300, 80, 0.2f } }; snd[S_SELECT] = knock(0.12f, m, 2, 0.5f, 260, 0.5f); }
  { const float m[][3] = { { 900, 50, 0.4f } }; snd[S_BACK] = knock(0.1f, m, 1, 0.3f, 260, 0.4f); }
  free(pcm);
  pcm = NULL;
}

static void play(int s, float vol, float pitch, float pan) {
  cw_audio_play(snd[s], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = vol, .pitch = pitch > 0 ? pitch : 1, .pan = pan });
}

void sound_menu(int kind) { play(kind == 0 ? S_MENU : kind == 2 ? S_BACK : S_SELECT, 0.8f, 1, 0); }

void sound_events(const game_events *ev, float cam_a) {
  int played = 0;
  for (int i = 0; i < ev->n && played < 6; i++) {
    const crk_event *e = &ev->e[i];
    /* left / right as the camera sees it: the camera looks in toward the centre from angle cam_a */
    float rx = -sinf(cam_a), ry = cosf(cam_a);
    float pan = -(e->x * rx + e->y * ry) / 0.35f;
    pan = pan < -0.8f ? -0.8f : pan > 0.8f ? 0.8f : pan;
    float sp = e->speed, vol = sp / 1.2f;
    vol = vol < 0.08f ? 0.08f : vol > 1 ? 1 : vol;
    float pitch = 0.94f + 0.12f * (float)((e->a * 37 + e->b * 11 + i) % 10) / 10.0f;
    switch (e->kind) {
    case CRK_EV_DISC: play(sp > 0.35f ? S_CLACK : S_CLACK_SOFT, vol, pitch, pan); break;
    case CRK_EV_PEG: play(S_THOCK, vol, pitch, pan); break;
    case CRK_EV_DITCH: play(S_DITCH, 0.6f, pitch, pan); break;
    case CRK_EV_HOLE: play(S_PLONK, 0.9f, 1, pan); break;
    default: break;
    }
    played++;
  }
  if (ev->flick) play(S_FLICK, 0.5f + ev->flick_power * 0.5f, 1, 0);
  if (ev->twenty) play(S_TWENTY, 0.7f, 1, 0);
  if (ev->foul) play(S_FOUL, 0.6f, 1, 0);
  if (ev->game_over) play(S_WIN, 0.8f, 1, 0);
  else if (ev->round_end) play(S_ROUND, 0.7f, 1, 0);
}

void sound_slide(float speed) {
  float vol = speed / 1.6f;
  vol = vol > 0.6f ? 0.6f : vol;
  if (vol < 0.01f) {
    if (cw_voice_is_playing(slide_voice)) cw_voice_stop(slide_voice, 0.08f), slide_voice = (cw_voice){ 0 };
    return;
  }
  if (!cw_voice_is_playing(slide_voice)) slide_voice = cw_audio_play(snd[S_SLIDE], &(cw_play_desc){ .bus = cw_shell_sfx_bus(), .volume = vol, .loop = true });
  else cw_voice_set_volume(slide_voice, vol), cw_voice_set_pitch(slide_voice, 0.8f + speed * 0.25f);
}

void sound_silence(void) { sound_slide(0); }
