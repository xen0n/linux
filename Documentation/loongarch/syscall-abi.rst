.. SPDX-License-Identifier: GPL-2.0

===============================
Linux/LoongArch system call ABI
===============================

This document describes the system call ABI of Linux/LoongArch.
As the kernel is 64-bit only for now, the description below assumes an LP64\*
calling convention.

Syscall numbers and parameters
==============================

Like with other recent architecture ports, for the most part Linux/LoongArch
reuses the asm-generic syscall numbers and parameters.
There are a few points worth mentioning though.

* There is no ``renameat``. Use ``renameat2`` instead.
* There is no ``getrlimit`` or ``setrlimit``. Use ``prlimit64`` instead.
* There is no ``fstat`` or ``newfstatat``. Only ``statx`` is provided, and
  low-level components making their own syscalls are expected to be aware of
  this (and provide their own shims if necessary).

Invocation
==========

System calls are currently made with the ``syscall 0`` instruction.
Although the immediate field in the instruction is not checked at present,
it is strongly advised to keep it zeroed in case it is to be made meaningful
in the future.

The system call number is placed in the register ``a7``.
Parameters, if present, are placed from ``a0`` through ``a6`` as needed,
as if calling a function with the respective arguments.
Upon return, ``a0`` contains the return value, and ``t0-t8`` should be
considered clobbered; all other registers are preserved.
