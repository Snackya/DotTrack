from PIL import Image
import glob
import re
import time
import dt_sim as sim


def main():
    start_time = time.perf_counter()

    # Error margin in millimeters
    error_margin = 5  # Seems to NOT work anymore at 175, 150 and 125 dpi
    # error_margin = 6  # Seems to NOT work anymore at 175, 150 and 125 dpi
    # error_margin = 7  # Seems to work at 175, 150 and 125 dpi
    # error_margin = 8  # Seems to work at 200 dpi
    # error_margin = 9  # Seems to work at 200 dpi
    # error_margin = 10  # 7 and even more buffer
    # error_margin = 20  # get max

    # Margin of the movement area [page margin + (M5Stack size / 2) = 32]
    move_margin = (32, 32)

    # dt_sim.py values
    dbt_w, dbt_h, win_w, win_h = sim.DBT_W, sim.DBT_H, sim.WIN_W, sim.WIN_H
    dbt_log = sim.get_dbt_log(dbt_w, dbt_h, win_w, win_h)
    cam_size = sim.CAM_SIZE
    pipeline_id = "baseline"

    # RegEx patterns
    # Pattern to find dpi value in file name.
    dpi_pattern = r"(\d+)x(\d+)dpi"
    # Pattern to find pos value in file name.
    pos_pattern = r"(\d+\.\d+)x(\d+\.\d+)pos"

    glob_pattern = "*/**/*.png"
    # glob_pattern = "frames/BrotherHLL8360CDW/8192x4096_5x5_400x400dpi/*"
    # glob_pattern = "frames/LexmarkMS510dn/8192x4096_5x5_400x400dpi/*"
    # glob_pattern = "frames/BrotherHLL8360CDW/8192x4096_5x5_200x200dpi/*"
    # glob_pattern = "frames/LexmarkMS510dn/8192x4096_5x5_200x200dpi/*"
    # glob_pattern = "frames/BrotherHLL8360CDW/8192x4096_5x5_175x175dpi/*"
    # glob_pattern = "frames/LexmarkMS510dn/8192x4096_5x5_175x175dpi/*"
    # glob_pattern = "frames/BrotherHLL8360CDW/8192x4096_5x5_150x150dpi/*"
    # glob_pattern = "frames/LexmarkMS510dn/8192x4096_5x5_150x150dpi/*"
    # glob_pattern = "frames/BrotherHLL8360CDW/8192x4096_5x5_125x125dpi/*"
    # glob_pattern = "frames/LexmarkMS510dn/8192x4096_5x5_125x125dpi/*"
    files = glob.glob(glob_pattern, recursive=True)

    num_p = 0
    num_match = 0
    error_margins = []
    for file in sorted(files):
        print("=" * 80)
        print(file)
        dpi = re.findall(dpi_pattern, file)
        if len(dpi) == 0:
            raise Exception("No dpi value found in filename.")
        dpi = tuple([int(xy) for xy in dpi[-1]])
        # Skip 100x100dpi images because they can't be analysed with our
        # current implementation of the bit extraction.
        if dpi == (100, 100):
            continue

        pos = re.findall(pos_pattern, file)
        if len(pos) == 0:
            raise Exception("No position value found in filename.")
        pos = tuple([float(xy) for xy in pos[-1]])
        # Calculate estimated absolute position
        pos = pos[0] + move_margin[0], pos[1] + move_margin[1]
        print(pos)

        frame = Image.open(file)
        positions = sim.analyse_frame(frame,
                                      cam_size,
                                      dbt_log,
                                      dpi,
                                      win_w,
                                      win_h,
                                      pipeline_id)

        for p in positions:
            num_p += 1
            min_x, min_y = pos[0] - error_margin, pos[1] - error_margin
            max_x, max_y = pos[0] + error_margin, pos[1] + error_margin
            if min_x <= p[0] <= max_x and min_y <= p[1] <= max_y:
                print(f"{pos} & {p} was matched correctly with an error " +
                      f"margin of {error_margin} mm.")
                num_match += 1
                error_margins.append((pos[0] - p[0], pos[1] - p[1]))

    print(num_p, num_match, num_match / num_p)
    print(f"Total number of positions: {num_p}")
    print(f"Total number of matching positions: {num_match}")
    print(f"Accuracy percentage (num_match / num_p): {num_match / num_p:%}")
    print(f"Error margins:\n{error_margins}")

    total_time = time.perf_counter() - start_time
    print(f"Evaluation took {total_time:.3f}s")


if __name__ == "__main__":
    main()
