import os
import skimage
from skimage.filters import threshold_sauvola
import numpy as np
import time
from torus import Torus
from PIL import Image, ImageFilter
# from generate_dbt import generate_dbt
# from extract_frame import extract_frame


# Cameras resolution in pixel.
CAM_RESO = (36, 36)
# Cameras capture area in millimeter.
CAM_SIZE = (1, 1)
# Black in greyscale.
BLACK = 0
# White in greyscale.
WHITE = 255


def main():
    # Testing:
    # generate multiple test images
    # run pipeline
    # compare to truth

    # Testing 2:
    # test with real images

    # 1. Image Generation:
    dbt_w, dbt_h, win_w, win_h = 256, 256, 4, 4
    dpi = (300, 300)
    # "L": 8-bit greyscale. 0 means black, 255 means white.
    # "1": 1-bit bilevel, stored with the leftmost pixel in the most
    # significant bit. 0 means black, 1 means white.
    mode = "L"
    fname = generate_dbt(dbt_w, dbt_h, win_w, win_h, dpi, mode)
    img = Image.open(fname)

    # 1.1. Test frame simulation:
    cam_topleft = (0, 0)
    rot = 0
    subframe = get_test_frame(img, cam_topleft, CAM_RESO, CAM_SIZE, rot=rot)
    # subframe = get_test_frame(img, cam_topleft, CAM_RESO, CAM_SIZE, rot=rot,
    #                           dbt_dpi_overwrite=(300, 300))
    # subframe.show()  # DEBUG OUTPUT

    # 2. Image Analysis (extract array out of picture):
    # subframe = unrotate_image(subframe)  # TODO
    subframe = preprocess_image(subframe)
    subframe.show()  # DEBUG OUTPUT
    subarray = extract_bitarray(subframe, CAM_SIZE)
    print(subarray)  # DEBUG OUTPUT

    # 3. Decode array (and get position):
    # find_sequences_in_dbt(subarray, img, win_w, win_h)


# 1. Image Generation:
# generate_dbt.py
# import os
# from torus import Torus


def generate_dbt(r, s, m, n, dpi=(150, 150), mode="L"):
    if r == 256 and s == 256 and m == 4 and n == 4:
        return generate_256x256_4x4_dbt(dpi, mode)
    else:
        err_msg = ("Dimensions not supported yet. "
                   "Only 256x256/4x4 supported yet.")
        raise ValueError(err_msg)


def generate_256x256_4x4_dbt(dpi, mode):
    fname = f"output-256x256-4x4_{dpi[0]}x{dpi[1]}dpi_{mode}.png"
    values = [
        [0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 1, 0, 1, 1, 1],
        [0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 1],
        [0, 0, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0],
        [1, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1],
    ]
    m, n = 3, 2
    torus = Torus(values, m, n, "storage.txt")
    torus.transpose()
    torus.make()
    torus.make()
    torus.transpose()
    torus.make()
    torus.save(fname, dpi=dpi, mode=mode)
    return os.path.abspath(fname)


# 1.1. Test frame simulation:
# get_test_frame.py
# from PIL import Image


# for testing the recognition pipeline
def get_test_frame(img, cam_topleft, cam_reso, cam_size=(1, 1), rot=0,
                   dbt_dpi_overwrite=None):
    # TODO Make cam_topleft an absolut position in mm or inch. Or allow an
    # offset to test patterns being cut off

    # Convert size from millimeters to inch
    cam_size_inch = (cam_size[0]*0.039370079, cam_size[1]*0.039370079)

    # Set up DPI value
    dpi = None
    cam_dpi = (int(cam_reso[0]//cam_size_inch[0]),
               int(cam_reso[1]//cam_size_inch[1]))
    # Read out DPI value from PNG metadata.
    for key in img.info:
        if(key == "dpi"):
            dpi = img.info[key]
    if dbt_dpi_overwrite is not None:
        # Set DPI to overwrite value
        dpi = dbt_dpi_overwrite
    if dpi is None:
        # Set DPI to default value (dpi of camera/sensor).
        # TODO: Nyquist as default?? 36 --> (36/2)-1 = 17 --> 17//size
        dpi = cam_dpi

    # Calculate scaling factors & scale accordingly.
    # The ratio_scale variable is required to scale the DBT image to the wanted
    # size and in correct relation to the camera size.
    ratio_scale = (cam_dpi[0]/dpi[0], cam_dpi[1]/dpi[1])
    # To prevent not intended anti aliasing (3 because of Nyquist).
    safety_scale = 3
    # Resize to set the image size in relation to the camera size and also
    # scale up to retain quality.
    img = img.resize((int(img.size[0]*safety_scale*ratio_scale[0]),
                      int(img.size[1]*safety_scale*ratio_scale[0])))

    # TODO Value-/Boundschecks?
    if rot != 0:
        img = img.rotate(-rot,
                         expand=False,
                         center=(cam_topleft[0]*safety_scale,
                                 cam_topleft[1]*safety_scale),
                         translate=(0, 0))
    left, top, right, bottom = (cam_topleft[0],
                                cam_topleft[1],
                                cam_topleft[0]+cam_reso[0],
                                cam_topleft[1]+cam_reso[1])
    img = img.crop(box=(left*safety_scale,
                        top*safety_scale,
                        right*safety_scale,
                        bottom*safety_scale))
    # The blur is supposed to emulate greyscale noise.
    img = img.filter(ImageFilter.GaussianBlur(radius=safety_scale+1))
    img = img.resize(cam_reso)  # , resample=Image.NEAREST) TODO:filter needed?
    return img


# 2. Image Analysis (extract array out of picture):
def preprocess_image(img):
    dpi = img.info["dpi"]
    # Edge enhance
    img = img.filter(ImageFilter.EDGE_ENHANCE_MORE)

    # Thresholding
    img = skimage.img_as_ubyte(img)

    # Tests
    # from skimage.filters import (try_all_threshold, threshold_sauvola,
    #                              threshold_local, threshold_niblack)
    # import matplotlib.pyplot as plt

    # tmp = skimage.img_as_ubyte(img > threshold_local(img, 15))
    # Image.fromarray(tmp).show()

    # tmp = skimage.img_as_ubyte(img > threshold_niblack(img))
    # Image.fromarray(tmp).show()

    # tmp = skimage.img_as_ubyte(img > threshold_sauvola(img))
    # Image.fromarray(tmp).show()

    # fig, ax = try_all_threshold(img, verbose=False)
    # plt.show()

    img = skimage.img_as_ubyte(img > threshold_sauvola(img))
    img = Image.fromarray(img)
    img.info["dpi"] = dpi
    return img


def unrotate_image(img):
    # preprocess image for easier stabilization
    # img = img.filter(ImageFilter.CONTOUR)
    # img = img.filter(ImageFilter.EMBOSS)
    pp_img = img.filter(ImageFilter.FIND_EDGES)
    # TODO: do stuff to get assumed rotation
    # APPROACH 1: rotate with 15° increments
    rot = 0
    img = img.rotate(rot,
                     expand=False,
                     center=(img.size[0]//2, img.size[1]//2),
                     translate=(0, 0))
    return img


def remove_helper_lines(img):
    pass


def extract_bitarray(img, cam_size):
    # TODO: Implement solution(s) to grid alignment (e.g. minimize errors)

    # Convert size from millimeters to inch
    cam_size_inch = (cam_size[0]*0.039370079, cam_size[1]*0.039370079)
    # Read out DPI value from PNG metadata.
    dpi = img.info["dpi"]
    # Number of pattern pixel the sensor should be able to see
    num_ppx = (int(dpi[0]*cam_size_inch[0]), int(dpi[1]*cam_size_inch[1]))
    # Ratio of camera/image pixel per pattern pixel
    px_ppx_ratio = (int(img.size[0]/num_ppx[0]), int(img.size[1]/num_ppx[1]))
    print(dpi[0]*cam_size_inch[0], img.size[0]/(dpi[0]*cam_size_inch[0]))
    print(dpi, num_ppx, px_ppx_ratio)

    bit_array = np.zeros((int(num_ppx[0]), int(num_ppx[1])), dtype="uint8")
    img_array = skimage.img_as_ubyte(img)
    # Grid based extraction
    # 0. Find grid anchor (for later)
    anchor = (0, 0)
    # 1. Implement grid as for-loop that walks every cell
    stop = (anchor[0]+px_ppx_ratio[0]*int(num_ppx[0]),
            anchor[1]+px_ppx_ratio[1]*int(num_ppx[1]))
    x_range = list(range(anchor[0], stop[0], px_ppx_ratio[0]))
    y_range = list(range(anchor[1], stop[1], px_ppx_ratio[1]))
    for i, x in enumerate(x_range):
        for j, y in enumerate(y_range):
            cell = img_array[y:y+px_ppx_ratio[1], x:x+px_ppx_ratio[0]]
            # print(x, y)
            # print(cell)
            bit_color = calc_cell_bit(cell)
            bit_array[j, i] = bit_color
    # 2. Calculate cell binary value (count black/white pixels; optional
    # margin)
    # 3. Write value to bit_array
    return bit_array

    # TODO: decide if this is still of use
    # # Testing code WITHOUT proper pixel accumulation/consolidation
    # data = skimage.img_as_ubyte(img)
    # # Alternative: data = np.array(img)
    # # Revert with: img = Image.fromarray(data)
    # # Alternative image viewer for numpy arrays
    # # from skimage.viewer import ImageViewer
    # # ImageViewer(img).show()
    # return data


# return: 0/black or 255/white and win_lose_delta (for error variable)
def calc_cell_bit(cell_array, error_count=False, margin=(0, 0),
                  cross_shape_crop=False):
    # TODO: Crop to margin
    # TODO: Maybe count in a cross shape when there are very few pixels (3x3)
    # TODO: Make it possible to return an error count

    col_counts = {}
    # https://stackoverflow.com/a/35549699
    col_counts[WHITE] = np.count_nonzero(cell_array == WHITE)
    col_counts[BLACK] = np.count_nonzero(cell_array == BLACK)
    # colors, counts = np.lib.arraysetops.unique(cell_array,
    #                                            return_counts=True)
    # col_counts = dict(zip(colors, counts))
    if col_counts[BLACK] > col_counts[WHITE]:
        return BLACK
    else:
        return WHITE


# 3. Decode array (and get position):
def find_sequences_in_dbt(frame_array, dbt_img, m, n):
    start_time = time.perf_counter()

    dbt_array = skimage.img_as_ubyte(dbt_img)

    # List comprehension for x, y and dbt_x, dbt_y produces more loops
    # Less looping ==> bit faster
    # Caching the range variables is also a tiny bit faster
    frame_x_range = frame_array.shape[0]-m+1
    frame_y_range = frame_array.shape[1]-n+1
    dbt_x_range = dbt_array.shape[0]-m+1
    dbt_y_range = dbt_array.shape[1]-n+1
    for x in range(frame_x_range):
        for y in range(frame_y_range):
            win = frame_array[x:x+m, y:y+n]
            for dbt_x in range(dbt_x_range):
                for dbt_y in range(dbt_y_range):
                    # np.array_equal() is slower (rather big difference)
                    if (win == dbt_array[dbt_x:dbt_x+m, dbt_y:dbt_y+n]).all():
                        print(dbt_x, dbt_y)

    total_time = time.perf_counter() - start_time
    print(f"Brute force lookup of",
          f"{frame_array.shape[0]}x{frame_array.shape[1]} subarray in",
          f"{dbt_array.shape[0]}x{dbt_array.shape[1]} DBT with {m}x{n} window",
          f"size took {total_time:.3f}s")


if __name__ == "__main__":
    main()
