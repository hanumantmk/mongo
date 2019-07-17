import { BSONDecoder } from './decoder';
import { BSONEncoder } from './encoder';

export function _mysq(n:i32): i32 {
    return n* n;
}

export function _mytransform(n:i32): ArrayBuffer {
    let encoder = new BSONEncoder();
    encoder.setString("x", "hello from assembly script");
    let bson: Uint8Array = encoder.serialize();
    return bson.buffer;
}

export function _myfilter(n:i32): i32 {
    return 1;
}
