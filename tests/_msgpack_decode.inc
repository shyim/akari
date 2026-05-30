<?php
/*
 * Minimal msgpack decoder for the test suite. Covers only the subset of types
 * the UDP exporter emits (see src/msgpack_write.h): positive/negative fixint,
 * fixmap/map16, fixarray/array16, fixstr/str8/str16, bin8/bin16, nil, and
 * uint8/16/32/64. Returns the decoded top-level value.
 */
function akari_msgpack_decode(string $buf)
{
    $pos = 0;
    $decode = function () use (&$decode, &$pos, $buf) {
        $b = ord($buf[$pos++]);
        if ($b < 0x80) return $b;                      // positive fixint
        if ($b >= 0xe0) return $b - 256;               // negative fixint
        if (($b & 0xf0) === 0x80) {                    // fixmap
            $map = [];
            for ($i = 0, $c = $b & 0x0f; $i < $c; $i++) { $k = $decode(); $map[$k] = $decode(); }
            return $map;
        }
        if (($b & 0xf0) === 0x90) {                     // fixarray
            $arr = [];
            for ($i = 0, $c = $b & 0x0f; $i < $c; $i++) $arr[] = $decode();
            return $arr;
        }
        if (($b & 0xe0) === 0xa0) {                     // fixstr
            $len = $b & 0x1f; $s = substr($buf, $pos, $len); $pos += $len; return $s;
        }
        switch ($b) {
            case 0xc0: return null;
            case 0xcc: return ord($buf[$pos++]);
            case 0xcd: $v = unpack('n', substr($buf, $pos, 2))[1]; $pos += 2; return $v;
            case 0xce: $v = unpack('N', substr($buf, $pos, 4))[1]; $pos += 4; return $v;
            case 0xcf: $v = unpack('J', substr($buf, $pos, 8))[1]; $pos += 8; return $v;
            case 0xd9: $len = ord($buf[$pos++]); $s = substr($buf, $pos, $len); $pos += $len; return $s;
            case 0xda: $len = unpack('n', substr($buf, $pos, 2))[1]; $pos += 2; $s = substr($buf, $pos, $len); $pos += $len; return $s;
            case 0xc4: $len = ord($buf[$pos++]); $s = substr($buf, $pos, $len); $pos += $len; return $s;
            case 0xc5: $len = unpack('n', substr($buf, $pos, 2))[1]; $pos += 2; $s = substr($buf, $pos, $len); $pos += $len; return $s;
            case 0xde: $c = unpack('n', substr($buf, $pos, 2))[1]; $pos += 2; $map = [];
                       for ($i = 0; $i < $c; $i++) { $k = $decode(); $map[$k] = $decode(); } return $map;
            case 0xdc: $c = unpack('n', substr($buf, $pos, 2))[1]; $pos += 2; $arr = [];
                       for ($i = 0; $i < $c; $i++) $arr[] = $decode(); return $arr;
        }
        throw new RuntimeException(sprintf('unhandled msgpack byte 0x%02x', $b));
    };
    return $decode();
}
