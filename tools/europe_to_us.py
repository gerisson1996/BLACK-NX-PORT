"""Turn the Europe default.xbe into the US image the recompile is made from.

Same algorithm as Black-Android's native/game_files.cpp (ConvertEurope):
src/common/europe.patch is a list of blocks -- copy N bytes from the Europe
xbe with a few of them changed (byte += delta), append new bytes, then move
the read position (even: forward by half, odd: back by half plus one).
Numbers are 7 bits a byte, low first. Only a Europe xbe with the expected
SHA-1 is accepted.

usage: python tools/europe_to_us.py <europe default.xbe> <europe.patch> <out us.xbe>
"""
import hashlib
import sys

EUROPE_SHA1 = '796ad8369354e48aa15644d4707dd70bd3bc3b4c'


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def done(self):
        return self.at == len(self.data)

    def byte(self):
        if self.at >= len(self.data):
            raise ValueError('patch ends early')
        b = self.data[self.at]
        self.at += 1
        return b

    def number(self):
        n = shift = 0
        while shift < 64:
            b = self.byte()
            n |= (b & 0x7F) << shift
            if b < 0x80:
                return n
            shift += 7
        raise ValueError('number too long')


def convert(src, patch):
    p = Reader(patch)
    out = bytearray()
    read = 0
    while not p.done():
        copy = p.number()
        if read > len(src) or copy > len(src) - read:
            raise ValueError('copy past the end of the Europe xbe')
        start = len(out)
        out += src[read:read + copy]
        at = 0
        for _ in range(p.number()):
            at += p.number()
            if at >= copy:
                raise ValueError('change outside its block')
            out[start + at] = (out[start + at] + p.byte()) & 0xFF
            at += 1
        for _ in range(p.number()):
            out.append(p.byte())
        end = read + copy
        move = p.number()
        dist = move >> 1
        if dist > len(src):
            raise ValueError('move too far')
        if move & 1:
            if dist >= end:
                raise ValueError('move before the start')
            read = end - dist - 1
        else:
            read = end + dist
    return bytes(out)


def main():
    src = open(sys.argv[1], 'rb').read()
    sha = hashlib.sha1(src).hexdigest()
    if sha != EUROPE_SHA1:
        raise SystemExit(f'not the expected Europe xbe (sha1 {sha})')
    us = convert(src, open(sys.argv[2], 'rb').read())
    open(sys.argv[3], 'wb').write(us)
    print(f'US xbe: {len(us)} bytes, sha1 {hashlib.sha1(us).hexdigest()}')


if __name__ == '__main__':
    main()
