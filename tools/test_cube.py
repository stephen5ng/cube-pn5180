#!/usr/bin/env python3
"""Test cube display on LED matrix."""

import time
import math
from rgbmatrix import graphics, RGBMatrix, RGBMatrixOptions

def draw_cube():
    """Draw a bouncing rotating cube."""
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
    canvas = matrix.CreateFrameCanvas()
    
    width = canvas.width
    height = canvas.height
    
    # Bouncing position
    pos_x = width // 2
    pos_y = height // 2
    vel_x = 3
    vel_y = 2
    
    # Cube vertices (3D coordinates)
    vertices = [
        [-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],  # back face
        [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]       # front face
    ]
    
    # Cube edges (vertex pairs)
    edges = [
        (0, 1), (1, 2), (2, 3), (3, 0),  # back face
        (4, 5), (5, 6), (6, 7), (7, 4),  # front face
        (0, 4), (1, 5), (2, 6), (3, 7)   # connecting edges
    ]
    
    try:
        angle = 0
        cube_size = 40  # Bigger cube
        
        while True:
            canvas.Clear()
            
            # Update bouncing position
            pos_x += vel_x
            pos_y += vel_y
            
            # Bounce off edges (with cube size margin)
            if pos_x - cube_size < 0 or pos_x + cube_size >= width:
                vel_x = -vel_x
                pos_x = max(cube_size, min(width - cube_size - 1, pos_x))
            if pos_y - cube_size < 0 or pos_y + cube_size >= height:
                vel_y = -vel_y
                pos_y = max(cube_size, min(height - cube_size - 1, pos_y))
            
            # Rotate cube
            cos_a = math.cos(angle)
            sin_a = math.sin(angle)
            cos_b = math.cos(angle * 0.7)
            sin_b = math.sin(angle * 0.7)
            
            # Project vertices to 2D
            projected = []
            for v in vertices:
                # Rotate around Y axis
                x = v[0] * cos_a - v[2] * sin_a
                z = v[0] * sin_a + v[2] * cos_a
                y = v[1]
                
                # Rotate around X axis
                y_rot = y * cos_b - z * sin_b
                z_rot = y * sin_b + z * cos_b
                
                # Simple perspective projection
                scale = cube_size / (4 + z_rot)
                px = int(pos_x + x * scale)
                py = int(pos_y + y_rot * scale)
                projected.append((px, py))
            
            # Draw edges
            for edge in edges:
                x1, y1 = projected[edge[0]]
                x2, y2 = projected[edge[1]]
                
                # Simple line drawing (Bresenham's algorithm)
                dx = abs(x2 - x1)
                dy = abs(y2 - y1)
                sx = 1 if x1 < x2 else -1
                sy = 1 if y1 < y2 else -1
                err = dx - dy
                
                while True:
                    if 0 <= x1 < width and 0 <= y1 < height:
                        canvas.SetPixel(x1, y1, 0, 255, 255)  # Cyan
                    
                    if x1 == x2 and y1 == y2:
                        break
                    
                    e2 = 2 * err
                    if e2 > -dy:
                        err -= dy
                        x1 += sx
                    if e2 < dx:
                        err += dx
                        y1 += sy
            
            canvas = matrix.SwapOnVSync(canvas)
            angle += 0.05
            time.sleep(0.03)
            
    except KeyboardInterrupt:
        matrix.Clear()

if __name__ == "__main__":
    draw_cube()
