.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/loongarch/syscall-abi.rst
:Translator: WANG Xuerui <kernel@xen0n.name>

============================
Linux/LoongArch 系统调用 ABI
============================

本文档描述了 Linux/LoongArch 的系统调用 ABI。
由于当前内核仅有 64 位版本，以下的描述均视为遵循 LP64\* 的过程调用约定。

系统调用号与参数
================

正如其他新近的架构移植，绝大部分 Linux/LoongArch 系统调用号和参数都复用
asm-generic 的定义。
倒是有些值得一提的点。

* 没有 ``renameat`` ，请使用 ``renameat2`` 。
* 没有 ``getrlimit`` 或者 ``setrlimit`` ，请使用 ``prlimit64`` 。
* 没有 ``fstat`` 或者 ``newfstatat`` ，只有 ``statx`` 。
  自己进行系统调用的底层组件应当感知这一事实，如有必要，应自带兼容逻辑。

调用方式
========

目前都通过 ``syscall 0`` 指令进行系统调用。
尽管当下内核并不检查指令字中的立即数域，我们仍然强烈建议保持其为零，
这是为了防止未来它被赋予其他语义而造成您的程序产生非预期结果。

系统调用号应被存放于寄存器 ``a7`` 。
如系统调用有参数，这些参数应如函数调用一般，从 ``a0`` 到 ``a6`` 按顺序存放。
系统调用返回时， ``a0`` 存放返回值， ``t0-t8`` 则应被视作被破坏（clobbered）；
其他寄存器的值都保持不变。
