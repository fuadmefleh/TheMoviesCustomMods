"""Convex hull block of The Movies .msh models (flagsA 0x80).

The block is a memory image with pointers relative to an arbitrary base (the address
the exporter had it at); the game relocates them on load. Layout, offsets from the
start of the block (after its u32 size):

  0   u32 0x10000 | face_count
  4   ptr  face pointer array (always offset 36)
  8   ptr  footprint outline record
  12  float3 centre, float3 half size (axis-aligned bounds)
  36  face_count pointers to face records
      face:    u32 n, float xmin, ymin, xmax, ymax, ptr -> n x float3 (follows directly)
      outline: same record shape; points are float3 with z = 0
Faces are the planar polygons of the 3D convex hull; the outline is the 2D convex hull
of the footprint, counter-clockwise.
"""
import struct

import numpy as np
from scipy.spatial import ConvexHull

BASE = 0x19D26E48   # any value works; this is the one the original exporter used


def _order(poly, normal):
    c = poly.mean(axis=0)
    a = poly[0] - c
    a /= np.linalg.norm(a)
    b = np.cross(normal, a)
    ang = np.arctan2((poly - c) @ b, (poly - c) @ a)
    return poly[np.argsort(ang)]


def faces3d(points, tol=1e-4):
    pts = np.asarray(points, dtype=np.float64)
    h = ConvexHull(pts)
    planes = []   # (normal, offset, vertex index set)
    for simplex, eq in zip(h.simplices, h.equations):
        n, d = eq[:3], eq[3]
        for p in planes:
            if np.dot(p[0], n) > 1 - tol and abs(p[1] - d) < 1e-3:
                p[2].update(simplex)
                break
        else:
            planes.append([n, d, set(simplex)])
    out = []
    for n, d, idx in planes:
        poly = pts[sorted(idx)]
        out.append(_order(poly, n))
    return out


def outline2d(points):
    pts = np.asarray([(p[0], p[1]) for p in points], dtype=np.float64)
    h = ConvexHull(pts)
    return pts[h.vertices]   # counter-clockwise


def build(points):
    pts = np.asarray(points, dtype=np.float64)
    lo, hi = pts.min(axis=0), pts.max(axis=0)
    faces = faces3d(pts)
    outline = outline2d(pts)
    n = len(faces)

    def record(poly3, at):
        xs, ys = poly3[:, 0], poly3[:, 1]
        head = struct.pack('<I4fI', len(poly3), xs.min(), ys.min(), xs.max(), ys.max(), BASE + at + 24 - 36)
        return head + b''.join(struct.pack('<3f', *p) for p in poly3)

    body = bytearray()
    offsets = []
    at = 36 + 4 * n
    recs = []
    for f in faces:
        r = record(f, at)
        offsets.append(at)
        recs.append(r)
        at += len(r)
    out3 = np.column_stack([outline, np.zeros(len(outline))])
    outline_at = at
    recs.append(record(out3, at))
    centre, half = (lo + hi) / 2, (hi - lo) / 2
    body += struct.pack('<III', 0x10000 | n, BASE, BASE + outline_at - 36)
    body += struct.pack('<6f', *centre, *half)
    body += b''.join(struct.pack('<I', BASE + o - 36) for o in offsets)
    for r in recs:
        body += r
    return bytes(body)


def parse(blob):
    """Returns (centre, half, faces[list of float3 lists], outline[list of float3])."""
    head, base = struct.unpack_from('<II', blob)
    n = head & 0xFFFF
    outline_ptr = struct.unpack_from('<I', blob, 8)[0]
    centre = struct.unpack_from('<3f', blob, 12)
    half = struct.unpack_from('<3f', blob, 24)

    def rec(off):
        cnt = struct.unpack_from('<I', blob, off)[0]
        vp = struct.unpack_from('<I', blob, off + 20)[0] - base + 36
        return [struct.unpack_from('<3f', blob, vp + 12 * i) for i in range(cnt)]
    faces = [rec(struct.unpack_from('<I', blob, 36 + 4 * i)[0] - base + 36) for i in range(n)]
    return centre, half, faces, rec(outline_ptr - base + 36)
