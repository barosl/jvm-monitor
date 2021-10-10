#!/usr/bin/env python3

import argparse
from pathlib import Path
import re

def proc(lines):
    exc_cls_sig = None
    exc_to_string_text = None
    frames = []
    cur_frame = None
    cur_locals = None
    for line in lines:
        key, val = line.split('=', 1)
        if key == 'exc_cls_sig':
            exc_cls_sig = val
        elif key == 'exc_to_string_text':
            exc_to_string_text = val
        elif key == 'frame_idx':
            assert len(frames) == int(val)
            cur_frame = {'locals': []}
            frames.append(cur_frame)
        elif key == 'method_name':
            cur_frame['method_name'] = val
        elif key == 'method_sig':
            cur_frame['method_sig'] = val
        elif key == 'line_num':
            cur_frame['line_num'] = int(val)
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
            cur_locals['cls_sig'] = val
        elif key == 'local_to_string_text':
            cur_locals['to_string_text'] = val

    return {
        'exc_cls_sig': exc_cls_sig,
        'exc_to_string_text': exc_to_string_text,
        'frames': frames,
    }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log_file')
    args = ap.parse_args()

    text = Path(args.log_file).read_text()
    lines = []
    for line in text.splitlines():
        if line == '---':
            if lines:
                res = proc(lines)
                print(res)
                lines.clear()
        else:
            lines.append(line)

if __name__ == '__main__':
    main()
