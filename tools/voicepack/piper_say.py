#!/usr/bin/env python
"""
piper_say.py - Piper synthesis WITHOUT piper-tts's broken phonemizer.

WHY THIS EXISTS. piper-tts 1.7.0's macOS arm64 wheel ships an espeakbridge.so
whose espeak data path is hardcoded to the CI machine that built it
(/Users/runner/work/...). espeakbridge.initialize() IGNORES its argument, so
neither ESPEAK_DATA_PATH nor the correct bundled directory has any effect, and
the hardcoded path cannot be created without root. The model loads fine; only
phonemization is broken.

So: espeak-ng (Homebrew) does the phonemes, piper's OWN phoneme_ids logic maps
them to ids, and onnxruntime runs the model. Only the broken step is replaced,
which keeps the id conventions identical to upstream rather than reinvented.
"""
import json, subprocess, sys, unicodedata
import numpy as np
import onnxruntime

# The venv piper is installed into; see README.
import os
_VENV = os.environ.get("PIPER_VENV_SITE")
if _VENV:
    sys.path.insert(0, _VENV)
from piper.phoneme_ids import phonemes_to_ids           # upstream, unmodified


class PiperVoice:
    def __init__(self, onnx_path, espeak_voice=None):
        with open(onnx_path + ".json", encoding="utf-8") as f:
            self.cfg = json.load(f)
        self.id_map = {k: v for k, v in self.cfg["phoneme_id_map"].items()}
        self.rate = self.cfg["audio"]["sample_rate"]
        self.voice = espeak_voice or self.cfg["espeak"]["voice"]
        inf = self.cfg.get("inference", {})
        self.noise_scale = inf.get("noise_scale", 0.667)
        self.length_scale = inf.get("length_scale", 1.0)
        self.noise_w = inf.get("noise_w", 0.8)
        so = onnxruntime.SessionOptions()
        so.log_severity_level = 3
        self.sess = onnxruntime.InferenceSession(
            onnx_path, sess_options=so, providers=["CPUExecutionProvider"])

    def phonemize(self, text):
        """espeak-ng IPA, WITH the clause terminators kept.

        THE BUG THIS FIXES: piper's own phonemizer appends the punctuation
        that ended each clause to the phoneme string - "." , "?" , "," and "!"
        are real entries in the id map (10, 13, 8, 4) and the model was
        trained with them. They are how it knows where a sentence ends, which
        is where falling intonation and the natural breath go.

        Dropping them, as the first version did, leaves the model guessing at
        phrase structure. It does not go flat; it invents pauses in the wrong
        places - reported as "weird pauses between some of the words", which
        is exactly the symptom.

        espeak's CLI does not report terminators separately the way the C API
        does, so the text is split into clauses here and each one phonemized
        on its own with its punctuation put back."""
        import re
        phonemes = []
        clauses = [c for c in re.findall(r"[^.!?,;:]+[.!?,;:]?", text) if c.strip()]
        for clause in clauses:
            clause = clause.strip()
            m = re.search(r"([.!?,;:])$", clause)
            term = m.group(1) if m else ""
            body = clause[:-1].strip() if term else clause
            if not body:
                continue
            out = subprocess.run(
                ["espeak-ng", "-q", "--ipa", "-v", self.voice, body],
                capture_output=True, text=True, check=True).stdout
            # espeak breaks long input over lines; they are one clause to us.
            s = " ".join(line.strip() for line in out.splitlines() if line.strip())
            s = re.sub(r"\([^)]+\)", "", s)      # drop (lang) switch markers
            s += term
            phonemes.extend(list(unicodedata.normalize("NFD", s)))
            phonemes.append(" ")
        return [p for p in phonemes if p]

    def synth(self, text, length_scale=None, noise_scale=None):
        ph = self.phonemize(text)
        ids = phonemes_to_ids(ph, self.id_map)
        x = np.array([ids], dtype=np.int64)
        x_len = np.array([x.shape[1]], dtype=np.int64)
        scales = np.array([
            noise_scale if noise_scale is not None else self.noise_scale,
            length_scale if length_scale is not None else self.length_scale,
            self.noise_w], dtype=np.float32)
        audio = self.sess.run(None, {"input": x, "input_lengths": x_len,
                                     "scales": scales})[0]
        audio = audio.squeeze()
        peak = np.max(np.abs(audio)) or 1.0
        return (audio / peak * 0.95 * 32767).astype(np.int16)


if __name__ == "__main__":
    import wave, time
    v = PiperVoice(sys.argv[1])
    t0 = time.time()
    pcm = v.synth(sys.argv[2])
    print(f"synth {len(pcm)/v.rate:.2f}s of audio in {time.time()-t0:.2f}s "
          f"@ {v.rate} Hz")
    with wave.open(sys.argv[3], "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(v.rate)
        w.writeframes(pcm.tobytes())
    print("wrote", sys.argv[3])
