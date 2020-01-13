# ihsie - NES Sprite Import/Export Tool
This command-line tool allows you to
* Export sprite data from a NES ROM to a PNG image file
* Import sprite data from a PNG image file to a NES ROM

Originally, this fell out of something specific I was trying to do with a specific game when I was bored. That game, incidentally, was Ice Hockey for NES. That's where the name of this program comes from- "Ice Hockey Sprite Import Export Tool". 

The program exports tiles of the 2BPP NES format with the ordering labeled "通常表示" ("Normal Ordering") in YY-CHR.

I was so close to simply using an already-existing program, e.g., YY-CHR, to do the things I wanted to do, but they fell short. Since I understood the image encoding format involved well enough, it wasn't too much work to write a tool to do it. From there, it also wasn't too much work to generalize the program and post it here.

## Demo

This shows the process of quickly changing the puck sprite to be crossed-out. The emulation speed is slowed-down to make stuff easier to see.

![Example image](https://raw.githubusercontent.com/clandrew/ihsie/master/Demo.gif "Example image")

## Workflow and Usage
```
ihsie export original.nes reference.png 0x8010 4096

!! Edit reference.png in an image editor of your choice.

copy /Y original.nes test.nes

ihsie import image.png test.nes 0x8010
```

This allows you to edit sprites; they will be patched into test.nes.

## Build
The tool is built and tested for a Windows-based operating environment on x86-64 computer architecture. The code is set up as a Visual Studio 2019 solution.
