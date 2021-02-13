#!/usr/bin/env python3

import subprocess
import sys

# Note: if you're not me, don't expect this to work.

def main():
  subprocess.check_call(['rm', '-rf', 'release'])
  subprocess.check_call(['mkdir', 'release'])

  executables = None
  search_prefix = 'EXECUTABLES='
  with open('Makefile', 'rt') as f:
    for line in f:
      if line.startswith(search_prefix):
        executables = line[len(search_prefix):].split()

  assert executables is not None, 'cannot parse Makefile'

  for executable in executables:
    subprocess.check_call(['cp', executable, 'release/' + executable])
    subprocess.check_call(['codesign', '--force', '--verify', '--verbose',
        '--sign', 'Developer ID Application: Martin Michelsen',
        'release/' + executable])

  subprocess.check_call(['cp', 'README.md', 'release/README.md'])

  subprocess.check_call(['zip', '-r', 'release.zip', 'release'])


if __name__ == '__main__':
  sys.exit(main())
