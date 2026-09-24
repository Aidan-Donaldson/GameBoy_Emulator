# GameBoy Emulator

An emulator for the original GameBoy that can play games that run on MBC1-based cartridges or directly mapped games. Passes Blargg's CPU instruction tests and Mooneye's MBC1 tests.
There are a couple visual bugs that still need fixed.

# Load ROM
Change the file path located in the BootSequence procedure to that of your .gb ROM. Ensure that the number of bits needed to address the number of banks in the cartridge is correctly set.

# How to compile

Compile with:
```gcc PPU.c -o exe```
