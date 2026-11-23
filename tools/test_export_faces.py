"""Tests for tools/export_faces.py (no network or scikit-learn needed).

Run with:  make test-tools   (or python -m unittest discover -s tools)
"""

import os
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_faces as ef  # noqa: E402


def face(value: int) -> np.ndarray:
    return np.full((ef.H, ef.W), value, dtype=np.uint8)


class PackTests(unittest.TestCase):
    def test_round_trip(self):
        train = [(7, 0, face(10)), (7, 2, face(20))]
        query = [(7, 1, face(63))]
        blob = ef.pack_dat(train, query)
        self.assertEqual(len(blob), ef.HEADER_LEN + 2 * 3 + 3 * ef.PIXELS)
        self.assertEqual(blob[:8], b"EF\x01\x02\x01\x18\x18\x40")
        self.assertEqual(blob[8:14], bytes([7, 0, 7, 2, 7, 1]))
        t, q = ef.unpack_dat(blob)
        self.assertEqual([(l, s) for l, s, _ in t], [(7, 0), (7, 2)])
        self.assertTrue((q[0][2] == 63).all())

    def test_rejects_bad_faces(self):
        with self.assertRaises(ValueError):
            ef.pack_dat([(0, 0, face(64))], [])
        with self.assertRaises(ValueError):
            ef.pack_dat([(0, 0, np.zeros((24, 23), dtype=np.uint8))], [])
        with self.assertRaises(ValueError):
            ef.pack_dat([], [])
        with self.assertRaises(ValueError):
            ef.pack_dat([(0, 0, face(1))] * 11, [])
        with self.assertRaises(ValueError):
            ef.pack_dat([(0, 0, face(1))], [(0, 0, face(1))] * (ef.MAX_QUERY + 1))

    def test_unpack_rejects_bad_files(self):
        good = ef.pack_dat([(1, 0, face(5))], [])
        with self.assertRaises(ValueError):
            ef.unpack_dat(b"XX" + good[2:])
        with self.assertRaises(ValueError):
            ef.unpack_dat(good[:-1])
        with self.assertRaises(ValueError):
            ef.unpack_dat(good[:5] + b"\x20" + good[6:])


class ShrinkTests(unittest.TestCase):
    def test_size_and_levels(self):
        img = np.linspace(0.0, 1.0, 64 * 64).reshape(64, 64)
        small = ef.shrink(img)
        self.assertEqual(small.shape, (24, 24))
        self.assertLess(int(small.max()), 64)
        self.assertLessEqual(int(small.min()), 1)
        self.assertGreaterEqual(int(small.max()), 62)


class ReferenceTests(unittest.TestCase):
    def setUp(self):
        # Two orthogonal patterns, as in tests/unit/test_model.c.
        a = np.where(np.arange(ef.PIXELS) < ef.PIXELS // 2, 1, -1)
        b = np.where((np.arange(ef.PIXELS) % ef.W) % 2 == 0, 1, -1)
        self.train = np.array([32 + 20 * a, 32 - 20 * a, 32 + 8 * b, 32 - 8 * b])
        self.query = np.array([32 + 20 * a, 32 + 8 * b])

    def test_known_answer(self):
        ref = ef.reference(self.train, self.query, 2)
        self.assertTrue((ref["mean"] == 32).all())
        self.assertEqual(ref["variance_permille"], [862, 138])
        self.assertEqual(ref["rankings"][0][0], 0)
        self.assertEqual(ref["rankings"][1][0], 2)
        self.assertAlmostEqual(ref["distances"][0][1], 2 * 20 * 24, places=6)
        norms = np.linalg.norm(ref["eigenfaces"], axis=1)
        self.assertTrue(np.allclose(norms, 1.0))

    def test_mean_rounds_like_the_atari(self):
        train = np.array([np.zeros(ef.PIXELS), np.ones(ef.PIXELS), np.ones(ef.PIXELS)])
        ref = ef.reference(train.astype(np.uint8), train[:1].astype(np.uint8), 1)
        self.assertEqual(int(ref["mean"][0]), 1)    # 2/3 rounds up

    def test_header_is_valid_c(self):
        ref = ef.reference(self.train, self.query, 2)
        train = [(1, 0, f.reshape(24, 24)) for f in self.train]
        query = [(1, 1, f.reshape(24, 24)) for f in self.query]
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "ref.h")
            ef.write_reference_header(path, train, query, 2, ref)
            with open(path, encoding="ascii") as fh:
                text = fh.read()
        self.assertIn("#define REF_K 2", text)
        self.assertIn("static const short ref_variance_permille[2]", text)
        self.assertIn("ref_eigenfaces[1152]", text)
        self.assertEqual(text.count("{"), text.count("}"))


class CommandLineTests(unittest.TestCase):
    def test_rejects_overlap_and_bad_k(self):
        with self.assertRaises(SystemExit):
            ef.main(["--train", "1,2,3", "--query", "3"])
        with self.assertRaises(SystemExit):
            ef.main(["--train", "1,2", "--query", "3", "-k", "2"])

    def test_parse_list(self):
        self.assertEqual(ef.parse_list("1, 2,,3"), [1, 2, 3])


if __name__ == "__main__":
    unittest.main()
