# Controllers

A PlayStation pad under CrossOver, what it does by default, and which of the
per-title switches are worth touching. Everything on this page was measured on
one machine with a DualSense and a DualSense Edge, over Bluetooth and over a
cable, against stable CrossOver 26.3 carrying this project's controller set.

The set ships two ways: standalone as
[MacGamePadFix](https://github.com/MathiasKowoll/MacGamePadFix), or from
RaccoonBot's Options, which also gives every switch below a per-title home.

## What you get without touching anything

**The pad is a PlayStation pad, everywhere.** Glyphs, touchpad, PS button and
adaptive triggers, on Bluetooth as well as on a cable. It is never replaced,
wrapped or hidden: it keeps its own report descriptor and every client that
opened it before opens the same bytes now.

That is three patches telling the truth about which bus a device is on, and it
is the settled part of this. A DualShock 4 gets the same truth told about it,
because they key on the bus rather than on which pad it is.

**Vibration, if the game drives the pad itself.** Most PlayStation-aware titles
do. Nothing needs turning on.

## The four kinds of game, which is what decides the switches

The switch that matters is **Rumble through XInput**, and which way it should go
is decided by how the game reaches a controller at all. Read the game, not the
symptom.

| the game | how to tell | Rumble through XInput |
| --- | --- | --- |
| drives the pad itself | it already rumbles | **off** — it does not need it |
| uses Sony's own library | it ships `libScePad.dll` | **off** — same reason |
| only speaks XInput | it imports `xinput1_*.dll` and has no rumble here | **on** — this is what it is for |
| only speaks Steam Input | no controller at all until Steam Input is on | either — both were measured together |

A game's imports are the honest answer and they take a moment to read. From the
game's folder:

    llvm-objdump -p <game>.exe | grep -i "DLL Name" | grep -iE "scepad|xinput|dinput|steam"

**What "only speaks XInput" means for a PlayStation pad.** A DualSense is not an
XInput device — not here and not on Windows. A game that reaches its controller
only through XInput cannot see one, which is why such titles need Steam Input,
which presents a virtual pad that *is* an XInput device. What this project adds
is the pad's **motors** under that interface, so the rumble arrives even though
the sticks never came from there.

## Rumble through XInput, and what it costs

It offers the pad's motors to XInput as a small device of its own, beside the
pad. Off by default, per title.

**It costs nothing in frames.** A Bluetooth link to a pad carries about
sixty-five reports a second and no API moves it, so a second program writing
every frame offers more than the radio drains and everybody pays — the game
included. The motors are stamped into a packet the game is already sending
instead, so nothing of ours reaches the link. Measured: 92–95% of every motor
change carried that way.

**Two faults it used to have are gone**, and they are worth knowing because
older builds still describe them:

- Titles on Sony's library would not start with it on. The motors device carried
  the pad's vendor and product ids and that library enumerates by them, so it saw
  two pads and one of them answered nothing. Fixed in 0.2.2.
- It declared a button that is never pressed, and a game reading it could stop
  holding a button down. Fixed in 0.2.2.

## Vibration: Modern, Legacy, and the strength bar

**Modern** is the pad's own haptic path, the one Sony's library asks for on every
packet it sends. It is the finer of the two and it is the default, on Bluetooth
and on a cable alike.

**Legacy** asks the pad to imitate a pair of rotating-mass motors. It is clearly
harder at the same command — measured by running one Sony title twice with
nothing changed but that, so the difference is the path and not the game.

Neither is better. Modern has more detail, Legacy has more force, and each
remembers its own strength so switching lands where you left it.

**The strength bar carries no number on purpose.** A multiplier invites
arithmetic that does not survive contact with the pad: the two paths saturate at
different points, and a title already asking for everything cannot be multiplied
higher. Past about six times what the game asks, every request saturates and the
identical byte reaches the motors however far the bar goes. It stops at four
times for that reason.

**And more strength is not more force past the middle.** On the title traced
here, whose loudest request is 143 of 255, a little under half the bar is where
that reaches the ceiling with nothing clipped. Above it the loud and the very
loud arrive the same.

## What was measured on which title

Only titles worked on deliberately. This is not an inventory of anyone's
library.

| title | how it reaches the pad | what was found |
| --- | --- | --- |
| [Beast of Reincarnation](Beast-of-Reincarnation.md) | XInput | Rumble where there was none, and where the frame cost and the deadband were measured. Asks 1 to 143 of 255 across a session |
| [Mortal Shell 2](Mortal-Shell-2.md) | Sony's library | Would not start with the XInput switch on until 0.2.2. Does not need it: it rumbles on its own |
| Ghost of Tsushima | its own | Would not keep L3 held with the XInput switch on, until the motors device stopped declaring a button. Recognised as a Sony pad either way |
| God of War Ragnarök | XInput only | Imports `XINPUT1_4` and nothing else, so it needs Steam Input to see a controller at all. Rumbles with both on |

## When something is wrong

**A pad that disappears from a game.** The narrowest thing in the set is the
test that hides the motors device from everything but XInput: it requires a
multi-axis usage, a haptics collection and no axis at all. That was read from the
descriptor this project builds and never tried against a real force-feedback
wheel, because there is not one here. Start there.

**A button that will not stay held.** One title had this and the cause was ours.
If another does, say so — the reading behind that fix is a reading, not a proof.

**Frames.** Turn the trace on for the title and send the log; it measures the
cost of every write and whether the pad's input stream is disturbed.

RaccoonBot has a **Keep a HID trace** switch per title for exactly this, and
`diagnostics/read-hid-trace.sh` in the tooling repository reads what comes out.
