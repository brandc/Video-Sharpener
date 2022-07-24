![Programming Language](https://img.shields.io/badge/C-Programming%20Language-brightgreen)
![Zero Clause BSD License](https://img.shields.io/badge/License-BSD%20Zero%20Clause-green)

# Video Image Sharpener
Sharpens video images to hide some compression artifacts.
The human eye likes to see complexity and compression often blurs images.
This does not restore images to their original state, only enhances edges for the viewing experience.

# Usage
For videos stored in their uncompressed format
```
./sharpener -i input.y4m -o output.y4m
```

For piped input
```
video_decoder | ./sharpener -i - -o - | video_encoder
```

NOTE: Only accepts YUV4FFMPEG sampled at 888.

# Building
## Requirements:
- GNU C Compiler

## Dependencies
- POSIX pthreads

## Process
Build process
```
make all
```

# License
All code and files in this repository are licensed under the 0-BSD License
