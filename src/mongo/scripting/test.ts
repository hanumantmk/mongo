import { BSONDecoder } from './decoder';
import { BSONEncoder } from './encoder';

export function getNext(n:i32): ArrayBuffer {
    let len = load<i32>(n);
    let encoder = new BSONEncoder();
    encoder.setBoolean("is_eof", len == 5);
    if (len != 5) {
        encoder.pushObject("next_doc");
        encoder.setString("msg", "hello from assembly script");
        encoder.popObject();
    }
    let bson: Uint8Array = encoder.serialize();
    return bson.buffer;
}
