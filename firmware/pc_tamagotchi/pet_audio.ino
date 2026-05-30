// pet_audio.ino -- all sound output for PC-Pet.
//
// Arduino tab. Constants + melody data in pet_config.h; audio state + g_mute /
// g_hat in pet_state.h. playMelody is blocking (short one-shots only); the
// tick* engines are non-blocking (one note per loop via millis()) so the
// animation never freezes. M5.Speaker is routed to the built-in buzzer or the
// SPK2 I2S amp in setup() (PCP-012).

// Blocking one-shot melody (buttons, alerts, low-battery). Honors mute.
void playMelody(const MelNote* mel) {
  if (g_mute || !mel) return;
  for (int i = 0; mel[i].freq != 0; i++) {
    M5.Speaker.tone(mel[i].freq, mel[i].durMs);
    delay(mel[i].durMs + mel[i].pauseMs);
  }
}

// Event voice notification (PCP-015). Amp-only (SPK2). Plays a recorded clip
// (8-bit unsigned PCM from pet_assets.h) via M5.Speaker.playRaw when
// USE_WAV_VOICE is set and a clip is provided; otherwise falls back to the
// synthesized MelNote jingle. Honors mute. Non-blocking (playRaw is async).
void playVoice(const MelNote* mel, const uint8_t* pcm, size_t pcmLen) {
  if (g_hat != HAT_SPK2 || g_mute) return;
#if USE_WAV_VOICE
  if (pcm && pcmLen) { M5.Speaker.playRaw(pcm, pcmLen, VOICE_SR); return; }
#endif
  playMelody(mel);
}

// Non-blocking panic siren (PCP-013). Call every loop with the panic tier
// (0 = not panicking). Loops the per-tier pattern via millis(); instant silence
// when tier drops to 0 (matches #2 instant revert) or when muted.
void tickSiren(int tier) {
  if (tier < 1 || g_mute) {
    if (g_sirenTier != 0) { M5.Speaker.stop(); g_sirenTier = 0; g_sirenIdx = 0; }
    return;
  }
  if (tier != g_sirenTier) {      // tier changed -> restart pattern
    g_sirenTier = tier;
    g_sirenIdx  = 0;
    g_sirenNext = 0;
  }
  if (millis() < g_sirenNext) return;
  const MelNote* pat = (tier >= 3) ? SIREN_T3 : (tier == 2) ? SIREN_T2 : SIREN_T1;
  if (pat[g_sirenIdx].freq == 0) g_sirenIdx = 0;   // loop
  const MelNote& n = pat[g_sirenIdx];
  M5.Speaker.tone(n.freq, n.durMs);
  g_sirenNext = millis() + n.durMs + n.pauseMs;
  g_sirenIdx++;
}

// Non-blocking mood ambient loop (PCP-014). Quiet per-mood "voice" on a
// dedicated channel (AMB_CH) under the UI cues; fades in/out on mood change;
// off when not SPK2, muted, or panicking (the siren owns audio).
// NOTE: channel/volume API names vary by M5Unified version -- verify on-device.
void tickAmbient(Mood mood, int tier) {
  const MelNote* pat = nullptr;
  switch (mood) {
    case M_HAPPY:   pat = AMB_HAPPY;   break;
    case M_SLEEP:   pat = AMB_SLEEP;   break;
    case M_BUSY:    pat = AMB_BUSY;    break;
    case M_STUFFED: pat = AMB_STUFFED; break;
    case M_LOWPWR:  pat = AMB_LOWPWR;  break;
    default:        pat = nullptr;     break;   // HOT / PANIC: no ambient
  }
  bool active = (g_hat == HAT_SPK2) && !g_mute && tier == 0 && pat != nullptr;

  if (!active) {                          // fade out, then go silent
    if (g_ambVol > 0) {
      g_ambVol = (g_ambVol > 4) ? g_ambVol - 4 : 0;
      M5.Speaker.setChannelVolume(AMB_CH, g_ambVol);
      if (g_ambVol == 0) g_ambMood = -1;
    }
    return;
  }

  if ((int)mood != g_ambMood) {           // mood changed -> restart pattern
    g_ambMood = mood;
    g_ambIdx  = 0;
    g_ambNext = 0;
  }
  if (g_ambVol < AMB_VOL) g_ambVol += 2;  // fade in
  M5.Speaker.setChannelVolume(AMB_CH, g_ambVol);

  if (millis() < g_ambNext) return;
  if (pat[g_ambIdx].freq == 0) g_ambIdx = 0;   // loop
  const MelNote& n = pat[g_ambIdx];
  M5.Speaker.tone(n.freq, n.durMs, AMB_CH);
  g_ambNext = millis() + n.durMs + n.pauseMs;
  g_ambIdx++;
}

// Soft heartbeat whose rate follows CPU load (PCP-016). Off by default; SPK2
// only; non-blocking.
void tickHeartbeat(int cpu) {
  if (!g_heartbeatOn || g_hat != HAT_SPK2 || g_mute) return;
  uint32_t interval = 1200 - (uint32_t)(constrain(cpu, 0, 100) * 9);  // 1200->300ms
  if (interval < 300) interval = 300;
  if (millis() - g_lastBeat >= interval) {
    g_lastBeat = millis();
    M5.Speaker.tone(110, 35);   // soft low "thump"
  }
}

// Reactive chirp on a sharp metric change (PCP-016). Pitch maps to size +
// direction; rate-limited by CHIRP_COOLDOWN. SPK2 only; non-blocking.
void tickChirp(int cpu, int ram, int gpu, int temp) {
  if (g_hat != HAT_SPK2 || g_mute) return;
  if (millis() - g_lastChirp >= CHIRP_COOLDOWN) {
    int dCpu  = cpu - g_pchCpu;
    int dRam  = ram - g_pchRam;
    int dGpu  = (gpu  < 0 ? 0 : gpu)  - (g_pchGpu  < 0 ? 0 : g_pchGpu);
    int dTemp = (temp < 0 ? 0 : temp) - (g_pchTemp < 0 ? 0 : g_pchTemp);
    int best = dCpu;                                  // largest-magnitude change
    if (abs(dRam) > abs(best))      best = dRam;
    if (abs(dGpu) > abs(best))      best = dGpu;
    if (abs(dTemp) * 3 > abs(best)) best = dTemp * 3; // temp weighted
    if (abs(best) >= 20) {
      int freq = constrain(1500 + best * 12, 400, 3500);  // up=higher, down=lower
      M5.Speaker.tone(freq, 60);
      g_lastChirp = millis();
    }
  }
  g_pchCpu = cpu; g_pchRam = ram; g_pchGpu = gpu; g_pchTemp = temp;
}
