Plexus P/20 Emulator
====================

This is the source code for a an emulator which more-or-less faithfully
simulates a Plexus P/20 UNIX server.

What's a Plexus P/20?
---------------------

Plexus was a company manufacturing Unix servers, initially based on Z8000 CPUs but
later switching to Motorola 680x0 CPUs. The P20 was a 'lower-cost' machine supporting
up to 16 users. It was sold with up to 2MiB of RAM (although the hardware supports up
to 8MiB and two 68010 CPUs running at 10 or 12.5MHz. The one this emulator is based 
on has an 87MiB MFM hard disk, a 5.25" floppy drive and a tape drive as well.

This emulator
-------------

This emulator emulates most aspects of a Plexus P/20 system: you can boot from a hard
disk image, log into UNIX and play around. The things currently unsupported are the
tape drive, floppy drive, and any Multibus cards.

The easiest way to get this working is by using the version compiled to WebAssembly. To
use this, point your (recently-ish) browser 
[the web version](https://spritetm.github.io/plexus_20_emu/) and enjoy.

If you want to do more specialized things, like changing the emulator code, or you want 
to run the emulator faster, you might want to clone the repo and compile it from
scratch. The emulator has no dependencies aside from a C compiler and make: to 
compile simply run 'make'. (If that fails, try 'gmake'.) This will produce the 'emu'
binary.

To actually run the emulator, you need the system roms (``U15-MERGED.BIN`` and 
``U17-MERGED.BIN``) as well as a hard disk image. Both can be found at 
[Adrian Blacks P20 repo](https://github.com/misterblack1/plexus-p20/).

Credits
-------

The bulk of the code is written by Sprite_tm. However, this emulator would not have
been possible without:

* Adrian Black, for un-earthing a Plexus P/20 and documenting the process of 
  getting it working,

* Ewen McNeill, Peter Kooiman, rakslice and Paul Brook for sending in pull 
  requests to add/fix various issues.

* The people at the Plexus-P20 channel on Usagi Electrics discord for pulling
  apart the diag ROMs, schematics, and UNIX kernel to figure out how exactly the
  issue works. Also for providing mental support and general interest.

