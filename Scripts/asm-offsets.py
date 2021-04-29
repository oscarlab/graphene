#!/usr/bin/env python3

import argparse
import re

regex = re.compile(r'''
    \s*
    \.ascii \s+ "GENERATED_INTEGER \s+
    (?P<name>\w+) \s+
    \$?(?P<offset>\d+)
    \s*"
    .*
''', re.VERBOSE)

argparser = argparse.ArgumentParser()
argparser.add_argument('infile', type=argparse.FileType('r'))
argparser.add_argument('outfile', type=argparse.FileType('w'))

def main(args=None):
    args = argparser.parse_args(args)

    args.outfile.write('''\
/* DO NOT MODIFY. THIS FILE WAS AUTO-GENERATED. */
#ifndef _ASM_OFFSETS_H_
#define _ASM_OFFSETS_H_

''')

    for line in args.infile:
        match = regex.fullmatch(line.rstrip('\n'))
        if not match:
            continue
        args.outfile.write(f'''\
#ifndef {match.group('name')}
#define {match.group('name')} {match.group('offset')}
#endif
''')

    args.outfile.write('''
#endif /* _ASM_OFFSETS_H_ */
''')

if __name__ == '__main__':
    main()
