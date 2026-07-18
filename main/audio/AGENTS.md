# AUDIO: Voice Pipeline (Wake → ASR → Encoder → Network)

Real-time audio capture, processing, Opus codec, and wake-word engine. Three concurrent FreeRTOS tasks handle the full duplex pipeline.

## PIPELINE ARCHITECTURE

```
Input (ADC/I2S)
  ├── AudioInputTask (core 1, pri 5)
  │   ├── WakeWordDetector ──→ "wake detected" callback
  │   └── AudioProcessor (AEC/VAD) ──→ audio_encode_queue_
  ├── OpusCodecTask
  │   ├── encode: audio_encode_queue_ → OPUS → audio_send_queue_
  │   └── decode: audio_decode_queue_ → PCM → audio_playback_queue_
  └── AudioOutputTask → I2S DAC/PDM → Speaker
```

## KEY FILES

| File | Purpose |
|------|---------|
| `audio_service.{h,cc}` | Central orchestrator, 3× task creation, queue management, power timer |
| `audio_codec.{h,cc}` | HAL for I2S audio codec chips (ES8311/ES8374/ES8388/ES8389/Box/Dummy/NoAudio) |
| `audio_processor.h` | Interface + `AfeAudioProcessor` (ESP-SR AFE for AEC/NS/VAD) |
| `wake_word.h` | Interface — wake word detection trigger |
| `codecs/` | 7 audio codec driver implementations |
| `codecs/no_audio_codec.*` | Stub for display-only or debug boards |
| `processors/` | `AfeAudioProcessor` + `NoAudioProcessor` + debug hooks |
| `demuxer/ogg_demuxer.*` | Ogg container demuxing (for incoming Opus packets) |

## CONVENTIONS

- **3-task model**: Input, Output, OpusCodec — pinned to separate cores
- **Power save**: Codec ADC/DAC auto-disabled after `AUDIO_POWER_TIMEOUT_MS` inactivity
- **Queue chain**: PCM → encode_queue → OPUS → send_queue (uplink); decode_queue → playback_queue (downlink)
- **Sampling**: 16kHz mono 16-bit internally; codec may run 48kHz then resampled

## ANTI-PATTERNS

- **DO NOT** call codec I2C operations from interrupt context
- **NEVER** block AudioOutputTask — it must drain playback_queue_ within 100ms
- **WakeWord** runs before AFE processing in the input task; ensure no reentrancy
