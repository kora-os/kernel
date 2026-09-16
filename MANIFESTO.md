# The KoraOS Manifesto

Once upon a time, you switched on your home computer and, in an instant, you
were *there*. A blue screen with an `OK` prompt. A black screen with a blinking
`>`. Maybe, if you were lucky, a command line inside a window.

No boot wizard. No login. No updates. Just you and the machine.

You could start writing a program in BASIC. You could load another one. You
could drop in a game and just play. And when you were done, you switched it off.
That was the whole contract.

There was no Internet. To play a game you bought it, borrowed it from a friend,
or typed it in yourself — line by line, character by character — from a listing
printed in a magazine. Two pages of code that, after an evening of squinting,
finally gave you a clumsy version of Space Invaders. Nothing special. A rougher
take on a game that was already pretty simple.

But you had *coded* it. And chasing the bugs — almost always a typo, or two
lines swapped around — felt like something that was yours.

Then you started to experiment. Draw a circle. Write your own Hangman. You
learned by talking to friends, by reading magazines, and — the best part — by
typing commands with random values just to see what would happen.

`PEEK` and `POKE` were the magic words: read any byte in memory, write any byte
anywhere. Put the wrong value in the wrong place and you might discover how to
change the border color — or, far more likely, freeze the whole machine. No
matter. You
[turned it off and on again](https://media.tenor.com/rcOgStvkF7MAAAAC/it-crowd-chris-o-dowd.gif),
and started over.

## What KoraOS is

KoraOS is what a home computer would look like in 2026.

The hardware is already in your hands: a small, cheap (well — until recently)
Raspberry Pi, with a multi-core CPU, fast storage, and a real GPU. What's
missing is an operating system that treats it like a home computer instead of a
tiny server.

KoraOS is for the **user/developer** — Terry A. Davis' phrase for the person who
is both at once. Someone who doesn't want another Linux clone, but a machine to
*play* with. To experiment. To `PEEK` and `POKE` everywhere, watch what happens,
and — when it all falls over — turn it off and on again.

No memory protection. No sandbox. No permission dialogs standing between you and
the hardware. The whole machine is yours, the way it used to be.

## Where it's headed

*Not all of this exists yet — this is the direction, not a checklist of what's
done.*

- Runs on the Raspberry Pi 3/4, with QEMU for development
- **No memory protection** — `PEEK` and `POKE` wherever you like
- GPU support
- Multi-core support
- A FAT32 filesystem
- A BASIC *and* a Python shell
- Networking
- ...and much more

Switch it on. Experiment. And if something breaks — you already know what to do.
