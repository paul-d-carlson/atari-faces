#!/usr/bin/env python3
"""
Build the eigenfaces demo data set and its numpy reference results.

The demo follows the method in "Face Recognition using Eigenfaces (Python)"
by Hassan Id Mansour: subtract the mean face, take the principal components
of the training faces, project every face onto them and rank the training
faces by Euclidean distance in that weight space. The Atari version shrinks
the Olivetti faces to 24x24 pixels with 64 gray levels (0..63) so that every
pixel product fits a 16-bit int.

Outputs
  data/FACES.DAT                 the only data file the Atari loads
  tests/fixtures/ef_reference.h  what a float PCA computes for the same
                                 quantized pixels; the C unit tests compare
                                 the integer implementation against it

FACES.DAT layout (all values unsigned bytes)
  0  'E' 'F'      magic
  2  1            format version
  3  n_train      training faces (<= 10)
  4  n_query      query faces (<= 5)
  5  24 24        width, height
  7  64           gray levels per pixel
  8  {label, photo} for every training face, then every query face
     (label = Olivetti subject 0..39, photo = photo number 0..9 of that subject)
  .. n_train * 576 pixel bytes, then n_query * 576, row-major, 0..63

Requirements: numpy, pillow, scikit-learn (see tools/requirements.txt).
"""

import argparse
import os
import sys
from typing import List, Sequence, Tuple

import numpy as np

W = 24
H = 24
PIXELS = W * H
LEVELS = 64
MAX_TRAIN = 10
MAX_QUERY = 5
MAGIC = b"EF"
VERSION = 1
HEADER_LEN = 8

# Five subjects, two training photos each, plus a third photo of each subject
# as a query. Subject 7 / photo 71 is the query used in the article; the other
# subjects were chosen by searching Olivetti for the set where every query is
# recognized with the widest margin at K=4 (see README, "Data").
DEFAULT_TRAIN = [70, 72, 106, 107, 159, 150, 214, 211, 260, 267]
DEFAULT_QUERY = [71, 104, 152, 215, 266]
DEFAULT_K = 4


def shrink(img64: np.ndarray) -> np.ndarray:
    """64x64 float [0,1] -> 24x24 ints 0..63 (bilinear, then drop 2 bits)."""
    from PIL import Image

    arr = np.clip(img64 * 255.0, 0.0, 255.0).astype(np.uint8)
    small = Image.fromarray(arr, mode="L").resize((W, H), resample=Image.BILINEAR)
    return (np.asarray(small, dtype=np.uint8) >> 2).astype(np.uint8)


def pack_dat(train: Sequence[Tuple[int, int, np.ndarray]],
             query: Sequence[Tuple[int, int, np.ndarray]]) -> bytes:
    """Serialize (label, photo, pixels) tuples into the FACES.DAT format."""
    if not 1 <= len(train) <= MAX_TRAIN:
        raise ValueError(f"need 1..{MAX_TRAIN} training faces")
    if len(query) > MAX_QUERY:
        raise ValueError(f"at most {MAX_QUERY} query faces")
    out = bytearray(MAGIC)
    out += bytes([VERSION, len(train), len(query), W, H, LEVELS])
    for label, photo, _ in list(train) + list(query):
        out += bytes([label, photo])
    for _, _, px in list(train) + list(query):
        flat = np.asarray(px, dtype=np.uint8).reshape(-1)
        if flat.size != PIXELS or int(flat.max()) >= LEVELS:
            raise ValueError("face must be 24x24 with values 0..63")
        out += flat.tobytes()
    return bytes(out)


def unpack_dat(blob: bytes):
    """Inverse of pack_dat; returns (train, query) lists of (label, photo, px)."""
    if blob[:2] != MAGIC or blob[2] != VERSION:
        raise ValueError("not a FACES.DAT v1 file")
    n_train, n_query, w, h, levels = blob[3:8]
    if (w, h, levels) != (W, H, LEVELS):
        raise ValueError("unexpected geometry")
    n = n_train + n_query
    meta = blob[HEADER_LEN:HEADER_LEN + 2 * n]
    pix = blob[HEADER_LEN + 2 * n:]
    if len(pix) != n * PIXELS:
        raise ValueError("truncated pixel data")
    faces = []
    for i in range(n):
        px = np.frombuffer(pix[i * PIXELS:(i + 1) * PIXELS], dtype=np.uint8)
        faces.append((meta[2 * i], meta[2 * i + 1], px.reshape(H, W)))
    return faces[:n_train], faces[n_train:]


def reference(train_px: np.ndarray, query_px: np.ndarray, k: int) -> dict:
    """Float PCA on the same integer pixels the Atari sees (article method).

    This is the textbook version of what src/ef_model.c does in 16-bit
    integers on the 6502 (see src/ef_model.h for an introduction). The C
    unit tests check the Atari's results against these.
    """
    n_faces = train_px.shape[0]
    # One row per face: 576 pixels each.
    faces = train_px.reshape(n_faces, -1).astype(np.int64)
    # The mean face, with the same rounding as ef_compute_mean():
    # (sum + n/2) / n.
    mean = (faces.sum(axis=0) + n_faces // 2) // n_faces
    # Centered faces: what makes each face different from the average.
    centered = faces - mean
    # The small N x N matrix of dot products between centered faces
    # (Turk and Pentland's "snapshot" trick) instead of the 576 x 576
    # pixel covariance. Both have the same nonzero eigenvalues.
    cov = centered @ centered.T
    # eigh() is for symmetric matrices: it returns every eigenvalue and
    # eigenvector at once (the Atari finds them one at a time by power
    # iteration). They come smallest first, so reverse them.
    eigenvalues, small_eigvecs = np.linalg.eigh(cov.astype(np.float64))
    order = np.argsort(eigenvalues)[::-1]
    eigenvalues = eigenvalues[order]
    small_eigvecs = small_eigvecs[:, order]
    # Each eigenface is a blend of the centered faces, in the amounts its
    # small eigenvector gives; then scaled to length 1. One per column.
    eigenfaces = centered.T.astype(np.float64) @ small_eigvecs[:, :k]
    eigenfaces /= np.linalg.norm(eigenfaces, axis=0)
    # Weights: how far each face lies along each eigenface (dot product
    # with the unit eigenface).
    weights = centered @ eigenfaces
    rankings = []
    dists = []
    for query in query_px.reshape(query_px.shape[0], -1).astype(np.int64):
        query_weights = (query - mean) @ eigenfaces
        # Euclidean distance in weight space to every training face.
        distances = np.linalg.norm(weights - query_weights, axis=1)
        rankings.append([int(i) for i in np.argsort(distances, kind="stable")])
        dists.append(distances)
    # The trace (sum of the diagonal) is the sum of all the eigenvalues:
    # the total variation, so eigenvalue / trace is one axis's share.
    trace = float(np.trace(cov))
    return {
        "mean": mean.astype(np.int64),
        "cov": cov,
        "eigenvalues": eigenvalues,
        "variance_permille": [int(round(1000.0 * v / trace)) for v in eigenvalues[:k]],
        "eigenfaces": eigenfaces.T,
        "weights": weights,
        "rankings": rankings,
        "distances": dists,
    }


def load_olivetti(indices: Sequence[int]):
    from sklearn.datasets import fetch_olivetti_faces

    data = fetch_olivetti_faces(shuffle=False)
    out = []
    for idx in indices:
        out.append((int(data.target[idx]), int(idx % 10), shrink(data.images[idx])))
    return out


def c_array(name: str, ctype: str, values, per_line: int = 16) -> str:
    vals = [str(int(v)) for v in values]
    lines = []
    for i in range(0, len(vals), per_line):
        lines.append("    " + ", ".join(vals[i:i + per_line]) + ",")
    return f"static const {ctype} {name}[{len(vals)}] = {{\n" + "\n".join(lines) + "\n};\n"


def write_reference_header(path: str, train, query, k: int, ref: dict) -> None:
    n = len(train)
    eig_s8 = []
    for e in ref["eigenfaces"]:
        eig_s8.extend(np.round(e / np.abs(e).max() * 127.0).astype(int))
    dist_x4 = [int(round(v * 4.0)) for d in ref["distances"] for v in d]
    text = [
        "/* Generated by tools/export_faces.py -- do not edit. */",
        "#ifndef EF_REFERENCE_H",
        "#define EF_REFERENCE_H",
        "",
        f"#define REF_N_TRAIN {n}",
        f"#define REF_N_QUERY {len(query)}",
        f"#define REF_K {k}",
        "",
        c_array("ref_train_labels", "unsigned char", [f[0] for f in train]),
        c_array("ref_query_labels", "unsigned char", [f[0] for f in query]),
        "/* Mean face, 0..63, rounded like ef_compute_mean(). */",
        c_array("ref_mean", "unsigned char", ref["mean"]),
        "/* Share of total variance per component, in 1/1000. */",
        c_array("ref_variance_permille", "short", ref["variance_permille"]),
        "/* Unit eigenfaces rescaled so the largest |value| is 127; the sign of",
        " * an eigenvector is arbitrary, so compare with |cosine|. */",
        c_array("ref_eigenfaces", "signed char", eig_s8),
        "/* Training indices sorted by distance, one row per query. */",
        c_array("ref_rankings", "unsigned char", [i for r in ref["rankings"] for i in r]),
        "/* Distance from each query to each training face, in 1/4 gray levels",
        " * (the unit of ef_distance()). */",
        c_array("ref_dist_x4", "long", dist_x4),
        "#endif",
        "",
    ]
    with open(path, "w", encoding="ascii") as fh:
        fh.write("\n".join(text))


def parse_list(text: str) -> List[int]:
    return [int(v) for v in text.split(",") if v.strip()]


def main(argv=None) -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    p = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    p.add_argument("--train", default=",".join(map(str, DEFAULT_TRAIN)),
                   help="comma-separated Olivetti image indices for training")
    p.add_argument("--query", default=",".join(map(str, DEFAULT_QUERY)),
                   help="comma-separated Olivetti image indices to recognize")
    p.add_argument("-k", type=int, default=DEFAULT_K, help="eigenfaces kept")
    p.add_argument("--dat", default=os.path.join(root, "data", "FACES.DAT"))
    p.add_argument("--header", default=os.path.join(root, "tests", "fixtures", "ef_reference.h"))
    args = p.parse_args(argv)

    train_idx = parse_list(args.train)
    query_idx = parse_list(args.query)
    if not 1 <= args.k < len(train_idx):
        raise SystemExit("-k must be at least 1 and less than the training count")
    if set(train_idx) & set(query_idx):
        raise SystemExit("query images must not also be training images")

    train = load_olivetti(train_idx)
    query = load_olivetti(query_idx)
    blob = pack_dat(train, query)
    ref = reference(np.array([f[2] for f in train]), np.array([f[2] for f in query]), args.k)

    os.makedirs(os.path.dirname(args.dat), exist_ok=True)
    with open(args.dat, "wb") as fh:
        fh.write(blob)
    os.makedirs(os.path.dirname(args.header), exist_ok=True)
    write_reference_header(args.header, train, query, args.k, ref)

    print(f"{args.dat}: {len(blob)} bytes, {len(train)} training + {len(query)} query faces")
    correct = 0
    for f, r in zip(query, ref["rankings"]):
        got = train[r[0]][0]
        correct += got == f[0]
        print(f"  query subject {f[0]:2d} -> closest subject {got:2d}"
              f"{'' if got == f[0] else '  (WRONG)'}")
    print(f"top-1 correct: {correct}/{len(query)}; variance kept "
          f"{sum(ref['variance_permille']) / 10:.1f}% with K={args.k}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
