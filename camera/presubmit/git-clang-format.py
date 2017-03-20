#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A python wrapper to check style issues with git-clang-format"""

from __future__ import print_function

import argparse
import fnmatch
import os
import re
import subprocess
import sys


DIFF_FILEPATH_REGEXP = re.compile(r'^\+\+\+ b/(?P<file_path>.*)$',
                                  flags=re.MULTILINE)

# Use git-clang-format from upstream as it supports per-commit checks.
# TODO(jcliang): Remove this once sys-devel/llvm package is upgraded.
CLANG_FORMAT = os.path.join(os.path.abspath(os.path.dirname(__file__)),
                            'upstream-git-clang-format')

PRESUBMIT_IGNORE_FILE = os.path.join(
    os.path.abspath(os.path.dirname(os.path.dirname(__file__))),
    '.presubmitignore')

def main(argv):
  parser = argparse.ArgumentParser()
  parser.add_argument('--commit', type=str, help='The commit to check.',
                      default='HEAD')
  parser.add_argument('--fix', action='store_true',
                      help='Fix any formatting errors automatically.')
  parser.add_argument('--diff', action='store_true',
                      help='Show the diff output of clang-format.')
  args = parser.parse_args(argv)

  cmd = [CLANG_FORMAT, '--style', 'Chromium', '--diff', '%s^' % args.commit,
         args.commit]
  stdout = subprocess.check_output(cmd, universal_newlines=True)

  cmd = ['git', 'log', '%s^..%s' % (args.commit, args.commit),
         '--pretty=oneline']
  stdout = subprocess.check_output(cmd, universal_newlines=True)
  commit_hash, commit_message = stdout.split(' ', 1)
  print('[Commit %s] %s' % (commit_hash[:8], commit_message), file=sys.stderr)

  if stdout.strip() == 'no modified files to format':
    return 0

  diff_files = DIFF_FILEPATH_REGEXP.findall(stdout)
  ignored_files = []
  with open(PRESUBMIT_IGNORE_FILE) as f:
    for pattern in f.readlines():
      ignored_files += fnmatch.filter(diff_files, pattern.strip())
  diff_files = [f for f in diff_files if f not in ignored_files]

  if diff_files:
    if args.fix:
      proc = subprocess.Popen(['git', 'apply'], stdin=subprocess.PIPE,
                              universal_newlines=True)
      proc.communicate(input=stdout)
      proc.wait()
    else:
      print('The following files have formatting errors:')
      for f in diff_files:
        print('\t%s' % f)
      print('You can run `%s --fix %s` to fix this.' %
            (__file__, ' '.join(argv)))
      if args.diff:
        print('\nDiff output from clang-format:\n')
        print(stdout)
      return 1

  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
