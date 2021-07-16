# SQLite example

This directory contains an example for running SQLite in Graphene, including the
Makefile and a template for generating the manifest. The application is tested
on Ubuntu 20.04, with both normal Linux and SGX platforms.

# Generating the manifest

## Installing prerequisite:

Please run the following command to install SQLite (Ubuntu-specific):

    sudo apt-get install sqlite3

## Building for Linux

Run `make` (non-debug) or `make DEBUG=1` (debug) in the directory.

## Building for SGX

Run `make SGX=1` (non-debug) or `make SGX=1 DEBUG=1` (debug) in the directory.

# Running SQLite with Graphene

Here's an example of running SQLite under Graphene:

Without SGX:
```
graphene-direct sqlite scripts/testdir/test.db < scripts/create.sql
graphene-direct sqlite scripts/testdir/test.db < scripts/update.sql
graphene-direct sqlite scripts/testdir/test.db < scripts/select.sql
```

With SGX:
```
graphene-sgx sqlite scripts/testdir/test.db < scripts/create.sql
graphene-sgx sqlite scripts/testdir/test.db < scripts/update.sql
graphene-sgx sqlite scripts/testdir/test.db < scripts/select.sql
```

# Note about concurrency

SQLite uses POSIX record locks (`fcntl`) to guard concurrent accesses to the
database file. These locks are emulated within Graphene, and not translated to
host-level locks, even if you are mounting a file from the host.

That means it is safe to access the same database file from multiple processes,
but only within a **single Graphene instance**. In other words, a multi-process
Graphene application is OK, but multiple Graphene instances should not access
the same database file concurrently.
