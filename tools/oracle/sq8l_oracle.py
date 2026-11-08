"""Black-box reference renders from the SQ8L 64-bit port (github.com/sq8l-revival/SQ8L).

The port's engine is not part of this repository (it carries no licence). This script only
*calls* its test API, built separately as libsq8l_testapi.{so,dylib} (target sq8l_testapi),
to render notes for measurement: envelope timings, pitch, levels. Nothing of the port is copied.

    SQ8L_TESTAPI=/path/to/libsq8l_testapi.so python3 sq8l_oracle.py
"""
import ctypes, os
import numpy as np

class RawMidiEvent(ctypes.Structure):
    _fields_ = [("deltaFrames", ctypes.c_int32), ("data", ctypes.c_uint8 * 3), ("noteOffVelocity", ctypes.c_uint8)]

class Oracle:
    CHUNK_HEADER = 0x1f
    def __init__(self, lib_path=None, sample_rate=44100.0):
        lib = ctypes.CDLL(lib_path or os.environ["SQ8L_TESTAPI"])
        lib.sq8l_synth_new.restype = ctypes.c_void_p
        lib.sq8l_synth_new.argtypes = [ctypes.c_float]
        for name, args in {
            "sq8l_synth_free": [ctypes.c_void_p],
            "sq8l_synth_events": [ctypes.c_void_p, ctypes.POINTER(RawMidiEvent), ctypes.c_int32],
            "sq8l_synth_process": [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float), ctypes.c_int32, ctypes.c_int32],
            "sq8l_synth_get_chunk": [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32],
            "sq8l_synth_set_chunk": [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32],
            "sq8l_sysex_from_nybbles": [ctypes.c_char_p, ctypes.c_char_p],
        }.items():
            getattr(lib, name).argtypes = args
        lib.sq8l_synth_get_chunk.restype = ctypes.c_int32
        lib.sq8l_synth_set_chunk.restype = ctypes.c_int32
        self.lib, self.sr = lib, sample_rate

    def render(self, program102, notes, seconds, controls=()):
        """program102: 102-byte SQ-80 program. notes: [(time_s, key, vel, dur_s)].
        controls: [(time_s, status, d1, d2)]. Returns (left, right) float arrays."""
        lib = self.lib
        s = lib.sq8l_synth_new(ctypes.c_float(self.sr))
        try:
            nyb = bytes(b for x in program102 for b in (x & 15, x >> 4))
            rec = ctypes.create_string_buffer(540)
            lib.sq8l_sysex_from_nybbles(nyb, rec)
            buf = ctypes.create_string_buffer(4096)
            n = lib.sq8l_synth_get_chunk(s, buf, 4096)
            chunk = bytearray(buf.raw[:n])
            chunk[self.CHUNK_HEADER:self.CHUNK_HEADER + 540] = rec.raw
            assert lib.sq8l_synth_set_chunk(s, bytes(chunk), len(chunk)) == 0  # 0 = loaded
            events = []
            for t, key, vel, dur in notes:
                events.append((t, (0x90, key, vel)))
                events.append((t + dur, (0x80, key, 64)))
            for t, st, d1, d2 in controls:
                events.append((t, (st, d1, d2)))
            events.sort(key=lambda e: e[0])
            total = int(seconds * self.sr)
            block = 512
            out_l = np.zeros(total, np.float32); out_r = np.zeros(total, np.float32)
            L = (ctypes.c_float * block)(); R = (ctypes.c_float * block)()
            ei = 0
            for pos in range(0, total, block):
                batch = []
                while ei < len(events) and int(events[ei][0] * self.sr) < pos + block:
                    t, d = events[ei]
                    batch.append(RawMidiEvent(max(0, int(t * self.sr) - pos), (ctypes.c_uint8 * 3)(*d), 64))
                    ei += 1
                if batch:
                    arr = (RawMidiEvent * len(batch))(*batch)
                    lib.sq8l_synth_events(s, arr, len(batch))
                lib.sq8l_synth_process(s, L, R, block, 1)
                m = min(block, total - pos)
                out_l[pos:pos + m] = np.frombuffer(L, np.float32)[:m]
                out_r[pos:pos + m] = np.frombuffer(R, np.float32)[:m]
            return out_l, out_r
        finally:
            lib.sq8l_synth_free(s)
