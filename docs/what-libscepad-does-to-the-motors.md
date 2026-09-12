# What libScePad does to the motors

Read out of `libScePad.dll` 4.00.00.15, image base `0x180000000`, because a
DualSense driven by a title through Sony's own library feels different from one
driven through XInput by ours, and the question was what else that library
does. The answer is: nothing else. This file exists so nobody has to disassemble
it again to find that out.

## The three functions

`scePadSetVibration(handle, ScePadVibrationParam *)` at RVA `0x14e80` reads two
bytes and no more:

    180014f21  movzbl (%rdi), %r8d      largeMotor, param byte 0
    180014f27  movzbl 0x1(%rdi), %edx   smallMotor, param byte 1
    180014f2b  callq  0x1800108a0       the builder

No scaling, no curve, no envelope, no state. Two bytes in, two bytes out.

`scePadSetVibrationMode(handle, mode)` at RVA `0x14fb0` validates the mode as
1..5 (`leal -0x1(%rdx), %eax; cmpl $0x4, %eax; ja fail`) and stores it one byte
into the device struct:

    180015091  movb %dil, 0x6(%r9)

It refuses to mean anything for a device that is not `054c:0ce6` or `054c:0df2`
-- the check is right there at `0x1800150b3` -- so the mode is a DualSense
concept and nothing else.

`scePadSetVolumeGain` at RVA `0xc510` is audio. mgvf-0007 took it apart already
and its four bits are the four the emulation drops.

## The builder, and the whole of the decision

`0x1800108a0` writes a report `0x02`, `0x30` bytes long, and the only bytes it
ever touches are these:

    1800109eb  movb $0x2, (%rcx)        report id
    1800109ee  movzbl 0x6(%rbx), %edx   the mode
    ...
    180010a2c  orb  $0x4, 0x27(%rcx)    flag2 bit 0x04  -- the HAPTIC path
    180010a32  orb  $0x1, 0x1(%rcx)     flag0 bit 0x01  -- the LEGACY path
    180010a36  movb %sil, 0x3(%rcx)     common[2], motor right = smallMotor
    180010a3d  movb %dil, 0x4(%rcx)     common[3], motor left  = largeMotor
    180010a41  movl $0x30, %r8d         and send 48 bytes

FIVE MODES, TWO BEHAVIOURS. The arithmetic at `0x1800109f2` is two range tests
written as masks:

    leal -0x2(%rdx), %eax ; testb $0xfd, %al ; je   ->  mode 2 or 4  ->  LEGACY
    subb $0x3, %dl        ; testb $0xfd, %dl ; je   ->  mode 3 or 5  ->  haptic
    (otherwise)                                     ->  mode 1, two globals

So the five modes an application can ask for collapse to the same two bits this
driver already has, and mode 1 -- the default -- lands on the haptic side unless
two globals in the library say otherwise.

AND A FIRMWARE GATE, which is the one thing here that was not already known:

    180010a21  movl $0x220, %eax
    180010a26  cmpw %ax, 0x50(%rbx)
    180010a2a  jb   0x180010a32          below 0x220 -> LEGACY, whatever was asked

A pad whose firmware is older than `0x220` is sent the legacy motors even by a
title that asked for the haptic path. The pad measured here is past it: in
hid-175158.log the library chose haptic on 8,568 of its packets and never once
set bit 0x01.

## And which games this rules out for XInput rumble

`HidD_GetHidGuid` and SETUPAPI are in the import table, and `HidD_GetAttributes`
beside them: the library finds its pad by walking the HID device interface and
reading vendor and product ids. The haptics device `mgvf-0010` adds carries the
pad's own ids, because it is the same physical device with a second top-level
collection and hidclass makes one device per top level.

So a title using this library sees two `054c:0df2` and one of them answers
nothing a DualSense would. **No game on this library starts while
`XInputRumble` is on**, and that is a rule with a mechanism rather than one
title misbehaving.

It is also not worth working around. A game that links this library is by
definition a game that drives the pad itself, and it rumbles over Bluetooth
without any of `mgvf-0010`'s work -- that is what `mgvf-0002` through
`mgvf-0004` are for. The XInput path exists for the games that never heard of a
DualSense, and those do not link this library. The two sets do not overlap.

## What this closes

There is no intensity field in that library, no second mechanism, and no
configuration step. It writes two motor bytes and one path bit, which is exactly
what `mgvf-0021` and `mgvf-0023` make this driver write.

The capture agrees with the disassembly, which is the part that matters. Of the
78 bytes of a Bluetooth `0x31`, the native title's motor packets are non-zero in
positions 0, 2, 3, 4, 5, 6 and 41 and in no others -- report id, tag, flag0,
flag1, the two motors, flag2. Those are the same seven this driver writes. A
stamped packet of ours at full motor is byte-for-byte a packet of theirs.

So a title that feels harder than ours is not reaching the pad differently. It
is asking for different numbers, or asking for mode 2 or 4, and both of those
are the title's choice rather than something to homologate.

