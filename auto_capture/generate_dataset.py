#!/usr/bin/env python3

import ebb_motion
import ebb_serial
import math
import time
import socket
import os
import numpy as np
from PIL import Image
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm

# connect to sensor.uni-regensburg.de wifi
IP = '10.61.22.3' # local IP, change this
TCP_PORT = 8090
BUFFER_SIZE = 1

STEPS_PER_MM = 81.05
COLUMNS = 100
ROWS = 100

PAGE_MARGIN = 5
M5_SIZE = 54
page_width = A4[0] / mm
page_height = A4[1] / mm
MOVE_AREA_WIDTH = page_width - 2*PAGE_MARGIN - M5_SIZE
MOVE_AREA_HEIGHT = page_height - 2*PAGE_MARGIN - M5_SIZE

STEPS_X = round(MOVE_AREA_WIDTH / (COLUMNS-1) * STEPS_PER_MM * -1)
STEPS_Y = round(MOVE_AREA_HEIGHT / (ROWS-1) * STEPS_PER_MM)

imgsize = [36, 36]
img_byte_len = imgsize[0] * imgsize[1]
connected = False


# TODO: replace local M5Stack functions with imports from ./livetracker/raw_grabber.py
def connect():
    sock_image = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_image.bind((IP, TCP_PORT))
    sock_image.listen(1)
    conn, addr = sock_image.accept()

    connected = True
    print("connected")
    return sock_image, conn

def get_image(conn):
    buf = b''

    while True:
        received = conn.recv(BUFFER_SIZE)
        if received == b'':
            print("connection broken")
            return None

        buf += received
        if received != b"\xFE":
            continue

        data = buf
        buf = b''

        # Remove terminator byte
        data = data[:-1]
        # Remove header byte (indicates frame analyse request)
        if len(data) == img_byte_len + 1 and data[0] == 0xFD:
            data = data[1:]
        # If the length is still not correct try again
        if len(data) != img_byte_len:
            #placeholder_print("Received bad data from image capture. Trying again...")
            continue
        break

    # Expand bytes to full range (0-255)
    data = bytes([min(b * 2, 255) for b in data])
    # Create image
    img = Image.frombytes("L", imgsize, data)

    return np.array(img)


def pos_in_grid(col, row):
    pos = (
        round(PAGE_MARGIN + M5_SIZE/2 + (col) * STEPS_X / STEPS_PER_MM * (-1), 2),
        round(PAGE_MARGIN + M5_SIZE/2 + (row) * STEPS_Y / STEPS_PER_MM, 2)
    )
    return pos

def save_img(pos, conn):
    filename = str(pos[0]) + "_" + str(pos[1]) + ".png"
    # save dataset in /DotTrack/img_dataset/
    target_dir = os.path.dirname(os.path.dirname(__file__)) + "img_dataset/"
    if not os.path.exists(target_dir):
        os.mkdir(target_dir)

    while True:
        if not connected:
            time.sleep(1)
            print("not connected")
        
        message = "failed\n"
        conn.send(message.encode()) # looks strange again. just believe dev #2 as well
        img = None
        img = get_image(conn)
        conn.send(message.encode()) # looks strange but just believe me

        if img.any():
            break

    image = Image.fromarray(get_image(conn))
    image.save(target_dir + filename)

def move_over_page(port, cols, rows, duration, conn):
    return_duration_x = 1000
    return_duration_y = round(1000 * 1.4142)
    duration_x = duration
    duration_y = round(duration * 1.4142)

    for r in range(rows):
        time.sleep(0.1) # pause for image capture to prevent shakey captures
        for c in range(cols):
            time.sleep(0.1) # pause for image capture to prevent shakey captures
            save_img(pos_in_grid(r, c), conn)
            if c != cols-1:
                ebb_motion.doABMove(port, 0, STEPS_X, duration_x)
                time.sleep(duration_x/1000)
        # move back to the first column
        ebb_motion.doABMove(port, 0, -1*STEPS_X*(cols-1), return_duration_x)
        time.sleep(return_duration_x/1000)
        if r != rows-1:
            ebb_motion.doABMove(port, STEPS_Y, 0, duration_y)
            time.sleep(duration_y/1000)

    ebb_motion.doABMove(port, -1*STEPS_Y*(rows-1), 0, return_duration_y)
    time.sleep(return_duration_y/1000)

if __name__ == "__main__":
    duration = 200

    sock_image, conn = connect()
    port = ebb_serial.openPort()
    ebb_motion.sendDisableMotors(port)

    move_over_page(port, COLUMNS, ROWS, duration, conn)
    ebb_motion.sendDisableMotors(port)

    sock_image.close()
    