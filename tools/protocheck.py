#!/usr/bin/env python3
"""protocheck - declarations of the same function that disagree, across a
module's C files and its header.

A function declared in one file (a prototype written inline, or in the
header) with another calling convention or parameter list than its
definition in another file is not caught by the compiler: each translation
unit is right on its own. With Pascal conventions the callee pops what its
definition says, so a caller that pushed a different amount leaves its own
stack frame wrong - saved registers overwritten, a fault on return.

For every function name that is defined in one of FILES and declared
anywhere in FILES, the declaration's normalised signature (calling
convention keywords and parameter types, names dropped) is compared with
the definition's; differences are printed.

usage: protocheck.py FILE...    (e.g. user/*.c user/user.h)
"""
import re
import sys

CONV = ('__pascal', '__cdecl', 'WINAPI', 'CALLBACK', 'UAPI', 'UCB', 'PASCAL', 'FAR', '__far', '__near', '__loadds', 'NEAR')
EXPAND = {'WINAPI': '__far __pascal', 'CALLBACK': '__far __pascal', 'PASCAL': '__pascal', 'FAR': '__far',
          'UAPI': '__far __pascal __loadds', 'UCB': '__far __pascal', 'NEAR': '__near'}

def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', s)

def norm_conv(words):
    out = []
    for w in words:
        out.extend(EXPAND.get(w, w).split())
    # __loadds does not change the stack: compare the convention only
    return tuple(sorted(set(w for w in out if w in ('__pascal', '__cdecl', '__far', '__near'))))

def norm_params(p):
    p = p.strip()
    if p in ('', 'void'):
        return ()
    out = []
    for a in p.split(','):
        a = a.strip()
        if a == '...':
            out.append('...')
            continue
        a = re.sub(r'\bconst\b|\bvolatile\b|\bregister\b', ' ', a)
        a = re.sub(r'\s*\*\s*', ' * ', a)
        toks = a.split()
        # drop a trailing parameter name (an identifier after a type)
        if len(toks) > 1 and re.match(r'^[a-z_][A-Za-z0-9_]*$', toks[-1]) and toks[-1] not in ('int', 'char', 'long', 'short', 'unsigned', 'void'):
            toks = toks[:-1]
        out.append(' '.join(toks))
    return tuple(out)

PAT = re.compile(r'(^|[;}{\n])\s*((?:extern\s+|static\s+)?[A-Za-z_][\w\s\*]*?)\b([A-Za-z_]\w*)\s*\(([^()]*)\)\s*([;{])', re.S)

def scan(path, text):
    for m in PAT.finditer(text):
        head, name, params, end = m.group(2), m.group(3), m.group(4), m.group(5)
        words = head.replace('*', ' * ').split()
        if not words or words[0] in ('return', 'if', 'while', 'for', 'switch', 'sizeof', 'else', 'do', 'case', 'goto'):
            continue
        if name in ('if', 'while', 'for', 'switch', 'return', 'sizeof'):
            continue
        yield name, norm_conv(words), norm_params(params), end == '{', path, words

def main(files):
    defs, decls = {}, []
    for f in files:
        text = strip_comments(open(f, encoding='latin-1').read())
        for name, conv, params, isdef, path, words in scan(f, text):
            if 'static' in words:
                continue
            if isdef:
                defs[name] = (conv, params, path)
            else:
                decls.append((name, conv, params, path))
    bad = 0
    for name, conv, params, path in decls:
        if name not in defs:
            continue
        dconv, dparams, dpath = defs[name]
        if conv != dconv or params != dparams:
            print('%s: %s declared %s(%s) in %s, defined %s(%s) in %s' % (
                name, name, ' '.join(conv), ', '.join(params), path, ' '.join(dconv), ', '.join(dparams), dpath))
            bad += 1
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
