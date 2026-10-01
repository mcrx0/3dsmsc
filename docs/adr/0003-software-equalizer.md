# ADR 0003: Run the equalizer in software on the audio thread

## Status

Accepted

## Context

The player needs a multi-band equalizer. The 3DS DSP, driven through NDSP, offers only one
biquad filter per channel, which cannot make a ten-band graphic equalizer. The audio already passes
through the CPU on the way to NDSP, because the decoders produce PCM there.

## Decision

Filter the PCM in software: ten peaking biquad filters in series, applied in place to each block on
the audio thread after decoding and before the block reaches NDSP. The filter code is portable C++
(`src/audio/equalizer.cpp`) with no libctru dependency, so the host tests measure its gain on test
tones. Only bands with a non-zero gain are processed, and a flat equalizer skips processing entirely.
Half of the loudest boost is removed from the signal as headroom, and the output saturates instead
of wrapping.

## Consequences

The equalizer costs CPU, estimated at roughly 5 to 15 percent of an Old 3DS with all ten bands
active, on top of decoding; flat bands are free. Its behaviour is identical on every platform and
fully testable on the host. Bands at or above 45 percent of the sample rate are skipped, so the
16 kHz band is inactive for low sample-rate files. Moving to the DSP later would mean replacing the
filter chain, since NDSP cannot express ten bands.
