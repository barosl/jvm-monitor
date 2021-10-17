#!/usr/bin/env python3

import argparse
from pathlib import Path
import re
import json

PRIM_TYPES = {'Z': 'boolean', 'B': 'byte', 'C': 'char', 'S': 'short', 'I': 'int', 'J': 'long', 'F': 'float', 'D': 'double', 'V': 'void'}

def parse_java_vm_sig_inner(sig):
    tag = sig[0]
    if prim_type := PRIM_TYPES.get(tag):
        return prim_type, sig[1:]
    elif tag == 'L':
        pos = sig.index(';')
        cls = sig[1:pos].replace('/', '.')
        return cls, sig[pos+1:]
    elif tag == '[':
        ty, remaining = parse_java_vm_sig_inner(sig[1:])
        return ty + '[]', remaining
    elif tag == '(':
        pos = sig.index(')')
        arg_sig = sig[1:pos]
        arg_tys = []
        while arg_sig:
            ty, arg_sig = parse_java_vm_sig_inner(arg_sig)
            arg_tys.append(ty)
        ret_ty, remaining = parse_java_vm_sig_inner(sig[pos+1:])
        return f"{ret_ty}({', '.join(arg_tys)})", remaining
    else:
        raise RuntimeError(f"Invalid Java VM type signature: {sig}")

def parse_java_vm_sig(sig):
    parsed, remaining = parse_java_vm_sig_inner(sig)
    assert not remaining
    return parsed

def proc(lines):
    res = {
        'exc_cls_sig': None,
        'exc_to_string_text': None,
        'ts_usec': None,
        'frames': [],
    }

    cur_frame = None
    cur_locals = None
    for line in lines:
        key, val = line.split('=', 1)
        if key == 'exc_cls_sig':
            res['exc_cls_sig'] = parse_java_vm_sig(val)
        elif key == 'exc_to_string_text':
            res['exc_to_string_text'] = val
        elif key == 'ts_usec':
            res['ts_usec'] = int(val)
        elif key == 'frame_idx':
            assert len(res['frames']) == int(val)
            cur_frame = {'locals': []}
            res['frames'].append(cur_frame)
        elif key == 'method_name':
            cur_frame['method_name'] = val
        elif key == 'method_sig':
            cur_frame['method_sig'] = parse_java_vm_sig(val)
        elif key == 'line_num':
            cur_frame['line_num'] = int(val)
        elif key == 'method_cls_sig':
            cur_frame['method_cls_sig'] = parse_java_vm_sig(val)
        elif key == 'method_cls_gen':
            cur_frame['method_cls_gen'] = val
        elif key == 'local_idx':
            assert len(cur_frame['locals']) == int(val)
            cur_locals = {}
            cur_frame['locals'].append(cur_locals)
        elif key == 'local_name':
            cur_locals['name'] = val
        elif key == 'local_type':
            cur_locals['type'] = val
        elif key == 'local_val':
            cur_locals['val'] = val
        elif key == 'local_cls_sig':
            cur_locals['cls_sig'] = parse_java_vm_sig(val)

    return res

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log_file')
    ap.add_argument('out_file')
    args = ap.parse_args()

    text = Path(args.log_file).read_text()
    lines = []
    res = []
    for line in text.splitlines():
        if line == '---':
            if lines:
                data = proc(lines)
                res.append(data)
                lines.clear()
        else:
            lines.append(line)
    Path(args.out_file).write_text(json.dumps(res))

if __name__ == '__main__':
    main()
