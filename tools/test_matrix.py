#!/usr/bin/env python3
"""Simple LED matrix test program."""

import sys
import time
from rgbmatrix import graphics, RGBMatrix, RGBMatrixOptions

def test_solid_colors():
    """Test solid colors."""
    options = RGBMatrixOptions()
    options.cols = 128
    options.rows = 64
    options.chain_length = 2
    options.parallel = 3
    options.row_address_type = 3
    options.gpio_slowdown = 5
    options.brightness = 100
    options.disable_hardware_pulsing = True

    matrix = RGBMatrix(options=options)

    try:
        canvas = matrix.CreateFrameCanvas()

        # Clear display
        canvas.Clear()
        matrix.SwapOnVSync(canvas)
        print("Display cleared")
        time.sleep(1)

        # Red
        canvas.Clear()
        for x in range(canvas.width):
            for y in range(canvas.height):
                canvas.SetPixel(x, y, 255, 0, 0)
        matrix.SwapOnVSync(canvas)
        print("Red screen")
        time.sleep(1)

        # Green
        canvas.Clear()
        for x in range(canvas.width):
            for y in range(canvas.height):
                canvas.SetPixel(x, y, 0, 255, 0)
        matrix.SwapOnVSync(canvas)
        print("Green screen")
        time.sleep(1)

        # Blue
        canvas.Clear()
        for x in range(canvas.width):
            for y in range(canvas.height):
                canvas.SetPixel(x, y, 0, 0, 255)
        matrix.SwapOnVSync(canvas)
        print("Blue screen")
        time.sleep(1)

        # White
        canvas.Clear()
        for x in range(canvas.width):
            for y in range(canvas.height):
                canvas.SetPixel(x, y, 255, 255, 255)
        matrix.SwapOnVSync(canvas)
        print("White screen")
        time.sleep(1)

        # Yellow
        canvas.Clear()
        for x in range(canvas.width):
            for y in range(canvas.height):
                canvas.SetPixel(x, y, 255, 255, 0)
        matrix.SwapOnVSync(canvas)
        print("Yellow screen")
        time.sleep(1)

        # Clear
        canvas.Clear()
        matrix.SwapOnVSync(canvas)
        print("Display cleared")

    finally:
        matrix.Clear()

if __name__ == "__main__":
    test_solid_colors()
