#!/usr/bin/python

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

regression.run_checks()

regression = Regression(loader, "exit")

regression.add_check(name="Exit Code Propagation",
    check=lambda res: 1 == res[0].code)

rv = regression.run_checks()
if rv: sys.exit(rv)
