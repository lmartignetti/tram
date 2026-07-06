#!/usr/bin/env python3

import os
import glob
import shutil
import re


CONFIG_DIR = "../build/config"
DATA_DIR = "../data"
BUILD_TYPE: str = "Release"
LOG_DIR = f"../build/{BUILD_TYPE}/benchmarks/logs"
DATAFILE_PREFIX: str = "plotdata"

# Utility functions


def collect_test_data(config_file):
    flag = str(re.search(rf"config_([^_]+).json", os.path.basename(config_file)).group(1))
    for measure_label in "rlt":
        datafile = glob.glob(f"{LOG_DIR}/{flag}/{flag}_{DATAFILE_PREFIX}_*_{measure_label}")[0]
        target = f"{DATA_DIR}/{measure_label}{flag}"
        shutil.copyfile(datafile, target)


# Main function


def main():
    script_dir: str = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)

    try:
        shutil.rmtree(DATA_DIR)
    except FileNotFoundError:
        pass
    except PermissionError:
        print(f"Permission denied: Unable to remove plots.")
    except Exception as e:
        print(f"An error occurred: {e}")
    os.makedirs(DATA_DIR, exist_ok=True)

    for config_file in glob.glob(f"{CONFIG_DIR}/config_*"):
        collect_test_data(config_file)


if __name__ == "__main__":
    main()
