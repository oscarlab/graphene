#!/usr/bin/env python2

import os, sys, mmap
from regression import Regression

loader = sys.argv[1]

# Running Bootstrap
regression = Regression(loader, "bootstrap")

regression.add_check(name="Basic Bootstrapping",
    check=lambda res: "User Program Started" in res[0].out)

regression.add_check(name="One Argument Given",
    check=lambda res: "# of Arguments: 1" in res[0].out and \
            "argv[0] = file:bootstrap" in res[0].out)

regression.add_check(name="Five Arguments Given",
    args = ['a', 'b', 'c', 'd'],
    check=lambda res: "# of Arguments: 5" in res[0].out and \
           "argv[0] = file:bootstrap" in res[0].out and \
           "argv[1] = a" in res[0].out and "argv[2] = b" in res[0].out and \
           "argv[3] = c" in res[0].out and "argv[4] = d" in res[0].out)

rv = regression.run_checks()
if rv: sys.exit(rv)

# Running Exec
regression = Regression(loader, "exec")

regression.add_check(name="2 page child binary",
    check=lambda res: "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 " in res[0].out)

rv = regression.run_checks()
if rv: sys.exit(rv)

# Running Exec with NULL path
regression = Regression(loader, "exec_null")

regression.add_check(name="Execv with NULL path",
    check=lambda res: "execv correctly returned error" in res[0].out)

rv = regression.run_checks()
if rv: sys.exit(rv)

# Shared Object Test
regression = Regression(loader, "shared_object")

regression.add_check(name="Shared Object",
    check=lambda res: "Hello world" in res[0].out)

rv = regression.run_checks()
if rv: sys.exit(rv)

regression = Regression(loader, "exit")

regression.add_check(name="Exit Code Propagation",
    check=lambda res: 113 == res[0].code)

rv = regression.run_checks()
if rv: sys.exit(rv)
