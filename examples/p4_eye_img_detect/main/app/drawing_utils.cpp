/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "drawing_utils.h"

// Screen dimensions
#define WIDTH  1920
#define HEIGHT 1080

void draw_rectangle_rgb(uint16_t *buffer, int width, int height, int x1, int y1, int x2, int y2, int x_offset, int y_offset, uint8_t r, uint8_t g, uint8_t b, int thickness)
{
    // Apply offset to the coordinates
    x1 += x_offset;
    x2 += x_offset;
    y1 += y_offset;
    y2 += y_offset;

    // Clip coordinates to the buffer dimensions
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= width) x2 = width - 1;
    if (y2 >= height) y2 = height - 1;

    // Convert RGB888 to RGB565
    uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

    // Draw the top and bottom horizontal lines with thickness
    for (int t = 0; t < thickness; ++t) {
        for (int x = x1; x <= x2; ++x) {
            if (y1 + t >= 0 && y1 + t < height && x >= 0 && x < width) {
                buffer[(y1 + t) * width + x] = color;
            }
            if (y2 - t >= 0 && y2 - t < height && x >= 0 && x < width) {
                buffer[(y2 - t) * width + x] = color;
            }
        }
    }

    // Draw the left and right vertical lines with thickness
    for (int t = 0; t < thickness; ++t) {
        for (int y = y1; y <= y2; ++y) {
            if (x1 + t >= 0 && x1 + t < width && y >= 0 && y < height) {
                buffer[y * width + (x1 + t)] = color;
            }
            if (x2 - t >= 0 && x2 - t < width && y >= 0 && y < height) {
                buffer[y * width + (x2 - t)] = color;
            }
        }
    }
}

/**
 * @brief Draw a large green point at specified coordinates
 * 
 * @param buffer Pointer to RGB565 buffer
 * @param x X coordinate of the point
 * @param y Y coordinate of the point
 */
static void draw_large_green_point(uint16_t *buffer, int x, int y) {
    uint16_t green = 0x07E0;  // RGB565 value for green
    
    // Draw a circle-like point with radius 6
    for (int dx = -6; dx <= 6; ++dx) {
        for (int dy = -6; dy <= 6; ++dy) {
            int nx = x + dx;
            int ny = y + dy;

            if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
                buffer[ny * WIDTH + nx] = green;
            }
        }
    }
}

void draw_green_points(uint16_t *buffer, const std::vector<int> &landmarks) 
{
    // Draw 5 landmark points (usually representing facial landmarks)
    for (int i = 0; i < 5; i++) {
        int x = landmarks[2 * i];     
        int y = landmarks[2 * i + 1]; 

        draw_large_green_point(buffer, x, y);
    }
} 