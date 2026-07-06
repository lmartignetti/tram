#!/usr/bin/env python3

import os
import glob
import shutil
import pandas as pd
import re
from typing import Dict, Any
import numpy as np

from src.barchart import barchart, to_barchart_df
from src.cartesian import cartesian, to_cartesian_df
from src.cdf import cdf
from src.utils import store_df


CONFIG_DIR = "../build/config"
DATA_DIR = "../data"
PLOT_DIR = "../plots"
DATAFILE_PREFIX: str = "plotdata"
LABELS = "tpcbgdklv"
MEASURES = "rlt"
MEASURES_LABELS = {"r": "raw", "l": "lat", "t": "thr"}

LAT_FACTORS = {"ns": 1.0, "us": 0.001, "ms": 0.000001}
THR_FACTORS = {"": 1.0, "K": 0.001, "M": 0.000001, "G": 0.000000001}
SIZE_FACTORS = {"": 1.0, "K": 1024, "M": 1024 * 1024, "G": 1024 * 1024 * 1024}

# Maybe move this to data collection
RAMCAST_PATH = "resources/data_ramcast"
MSGPASS_PATH = "resources/data_msg_passing"

# Utility functions


def extract_df_entry(flag):
    match = re.search(rf"t([0-9]+)p([0-9]+)c([0-9]+)b([0-9]+)g([0-9]+)d([0-9]+)k([0-9]+)l([0-9]+)v([0-9]+[KMG]?B)", flag)
    df_dict = {}
    df_dict["t"] = int(match.group(1))
    df_dict["p"] = int(match.group(2))
    df_dict["c"] = int(match.group(3))
    df_dict["b"] = int(match.group(4))
    df_dict["g"] = int(match.group(5))
    df_dict["d"] = int(match.group(6))
    df_dict["k"] = int(match.group(7))
    df_dict["l"] = int(match.group(8))
    df_dict["v"] = str(match.group(9))

    for measure_label in "rlt":
        datafile = glob.glob(f"{DATA_DIR}/{measure_label}{flag}")[0]
        measure = pd.read_csv(datafile, sep=" ", header=None, skiprows=1)[0].tolist()
        df_dict[MEASURES_LABELS[measure_label]] = measure

    return pd.DataFrame([df_dict.values()], columns=df_dict.keys())


def payload_sort(payload_label: str):
    match = re.search(r"([0-9]+)([KMG]?)B", payload_label)
    msg_size_value = match.group(1)
    msg_size_measure_unit = match.group(2)

    return int(msg_size_value) * SIZE_FACTORS[msg_size_measure_unit]


def extract_plotdata(df_data: pd.DataFrame, measure: str, col: str, row: str = None, filter: Dict[str, Any] = {}) -> pd.DataFrame:
    """Generate a dataframe with maximum 2 dimensions, ready to be stored on file. The input parameters are used to filter the data obtained from the tests. If measure is "r", row must be None. Every exception raised in an unrecoverable error.

    Args:
        measure (str): either "r", "l", "t"
        col (str): column label of the final matrix, it must be a valid column label which is not a measure
        row (str, optional): row label of the final matrix. Defaults to None.
        filter (Dict[str, Any], optional): filter used to reduce the final matrix to two dimensions. The dict key is the column label: for each one, only the rows with one of the specified values are kept. Defaults to {}.

    Returns:
        pd.DataFrame: final 2-dimensional matrix
    """

    # Check preconditions
    if row == None and col == None:
        raise Exception("row and col cannot be both None")
    if row != None and row not in LABELS:
        raise Exception("row " + row + " not permitted")
    if col != None and col not in LABELS:
        raise Exception("row " + row + " not permitted")
    if measure not in MEASURES_LABELS.values():
        raise Exception("measure " + measure + " not permitted")

    if measure == "raw" and row != None:
        raise Exception("If measure is raw, row must be None")

    for label in LABELS:
        if label not in filter.keys() and label != row and label != col:
            raise Exception(f"Error: value for label {label} not specified")
        if label in filter and (label == row or label == col):
            raise Exception(f"Error: duplicate label {label}")
        if label == row and label == col:
            raise Exception(f"Error: duplicate label {label}")

    # Apply filter
    df = df_data.copy(deep=True)
    for filter_key in filter.keys():
        df = df[df[filter_key].isin([filter[filter_key]])]

    # Extract col and row labels
    if col != None:
        cols_df = sorted(list(set(df[col].tolist())))
        if col == "v":
            cols_df = sorted(cols_df, key=payload_sort)

    if row != None:
        rows_df = sorted(list(set(df[row].tolist())))
        if row == "v":
            rows_df = sorted(rows_df, key=payload_sort)

    # Data processing to create the final matrix
    df_final: pd.DataFrame = pd.DataFrame()
    sample: pd.DataFrame = pd.DataFrame()
    if col != None and row != None:
        for col_df in cols_df:
            sample_col = pd.DataFrame(df[df[col] == col_df])
            df_col: pd.DataFrame = pd.DataFrame()
            for row_df in rows_df:
                sample: pd.DataFrame = sample_col[sample_col[row] == row_df]

                sample = sample[measure].tolist()[0] if len(sample[measure].tolist()) > 0 else [np.nan]
                sample_avg = np.average(sample)
                sample_std = np.std(sample)
                sample = pd.DataFrame([[sample_avg, sample_std]], columns=[f"{col_df}", f"{col_df}-std"], index=[f"{row_df}"])

                df_col = pd.concat([df_col, sample], axis=0)

            df_final = pd.concat([df_final, df_col], axis=1)

    elif col != None:
        for col_df in cols_df:
            sample = pd.DataFrame(df[df[col] == col_df])

            if measure == "raw":
                sample = pd.DataFrame(sample["raw"].tolist()[0], columns=[f"{col_df}"])
            else:
                sample = sample[measure].tolist()[0]
                sample_avg = np.average(sample)
                sample_std = np.std(sample)
                sample = pd.DataFrame([[sample_avg, sample_std]], columns=[f"{col_df}", f"{col_df}-std"])

            df_final = pd.concat([df_final, sample], axis=1)
    else:
        for row_df in rows_df:
            sample = pd.DataFrame(df[df[row] == row_df])

            sample = sample[measure].tolist()[0]
            sample_avg = np.average(sample)
            sample_std = np.std(sample)
            sample = pd.DataFrame([[sample_avg, sample_std]], columns=[f"-", f"std"], index=[row_df])

            df_final = pd.concat([df_final, pd.DataFrame(sample)], axis=0)

    if df_final.empty:
        raise Exception("Empty dataframe, check the filter")

    return df_final


def extract_msg_passing_plotdata(measure: str):
    libs = ["byzcast", "skeen"]
    num_destinations = [1, 2, 4, 8]
    df = pd.DataFrame()

    for lib_name in libs:
        df_lib = pd.read_csv(f"../../{MSGPASS_PATH}/{lib_name}_{measure}", sep="\t", header=None, skiprows=1)
        if measure == "lat":
            df_lib = df_lib * 1000  # from us to ns
        df_lib.columns = num_destinations
        df_lib = pd.DataFrame(df_lib.mean())
        df_lib.columns = [lib_name]
        df = pd.concat([df, df_lib], axis=1)

    return df, libs


# Plot functions


def plot_plain():
    folder_name = "plain"

    test_names_lat = ["plain_lat"]
    test_names_thr = ["plain_base_thr", "plain_breadth_thr", "plain_depth_thr"]

    # Latency raw
    for overlay in ["breadth", "base", "depth"]:
        try:
            df, cols = extract_plotdata(test_names_lat, "r", row=None, col="num_destinations", filter={"overlay": [overlay]})
            datafile = store_df(folder_name, f"lat_raw_{overlay}", df, cols)
            params = {
                "datafile": datafile,
                "output": datafile,
                "title": "tram latency",
                "xlabel": f"Latency (us)",
                "key_labels": [str(col) + "d" for col in cols],
                "key_pos": "rb",
                "xfactor": LAT_FACTORS["us"],
                "xrange_min": 0,
                "xrange_max": 70,
            }
            cdf(params=params).plot()
        except Exception as e:
            print(f"Latency raw {overlay} skipped")

    ### Latency average
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/plain/lat_avg", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["dst", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["dst"])
    df_ramcast = df_ramcast.iloc[:, 1:]

    # Get tram data and concatenate
    df, cols = extract_plotdata(test_names_lat, "lw", col="overlay", row="num_destinations")
    df = pd.concat([df, df_ramcast], axis=1)

    df = to_barchart_df(df)
    datafile = store_df(folder_name, "lat_avg", df, df.columns)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Number of destinations",
        "ylabel": "Latency (us)",
        "yfactor": LAT_FACTORS["us"],
        "yrange_min": 0,
        "key_labels": list([df.columns[i] for i in range(len(df.columns)) if i % 2 == 1]),
        "key_pos": "lt",
        "whiskers": 1,
    }
    barchart(params=params).plot()

    ### Latency average normalized
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/plain/lat_avg", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["dst", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["dst"])
    df_ramcast = df_ramcast.iloc[:, 1:]

    # Get tram data and concatenate
    df, cols = extract_plotdata(test_names_lat, "l", col="overlay", row="num_destinations")
    df = df.div(df_ramcast["ramcast"], axis=0)

    df = to_cartesian_df(df)
    datafile = store_df(folder_name, "lat_avg_norm", df, df.columns)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Number of destinations",
        "ylabel": "Latency %",
        "yfactor": 100,
        "yrange_min": 0,
        "key_labels": [str(col) for col in cols],
        "key_pos": "lb",
    }
    cartesian(params=params).plot()

    ### Thorughput
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/plain/thr_max", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["dst", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["dst"])
    df_ramcast = df_ramcast.iloc[:, 1:]

    # Get tram data and concatenate
    df, cols = extract_plotdata(test_names_thr, "tw", col="overlay", row="num_destinations")
    df = pd.concat([df, df_ramcast], axis=1)

    df = to_barchart_df(df)
    datafile = store_df(folder_name, "thr_max", df, df.columns)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Number of destinations",
        "ylabel": "Throughput (Kmps)",
        "yfactor": THR_FACTORS["K"],
        "yrange_min": 0,
        "row_first": 1,
        "key_labels": list([df.columns[i] for i in range(len(df.columns)) if i % 2 == 1]),
        "whiskers": 1,
    }
    barchart(params=params).plot()

    ### Thorughput normalized
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/plain/thr_max", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["dst", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["dst"])
    df_ramcast = df_ramcast.iloc[:, 1:]

    # Get tram data and normalize
    df, cols = extract_plotdata(test_names_thr, "t", col="overlay", row="num_destinations")
    df = pd.concat([df, df_ramcast["ramcast"]], axis=1)
    df = df.div(df_ramcast["ramcast"], axis=0)

    df = to_cartesian_df(df)
    datafile = store_df(folder_name, "thr_max_norm", df, df.columns)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Number of destinations",
        "ylabel": "Normalized throughput",
        # "yfactor": 100,
        "yrange_min": 0,
        "row_first": 1,
        "key_labels": [str(col) for col in cols] + ["ramcast"],
        "key_pos": "lt",
    }
    cartesian(params=params).plot()

    # # TODO: this seems useless... remove it?
    # # Latency vs throughput by number of destinations
    # try:
    #     df1, cols1 = extract_plotdata(test_names_lat, "l", col="overlay", row="num_destinations")
    #     df2, cols2 = extract_plotdata(test_names_thr, "t", col="overlay", row="num_destinations")
    #     df = to_cartesian_df(df1, df2)
    #     datafile = store_df(folder_name, "lat_thr", df, df.columns)
    #     params = {
    #         "datafile": datafile,
    #         "output": datafile,
    #         "xlabel": "Latency (us)",
    #         "ylabel": "Throughput (Kmps)",
    #         "xfactor": LAT_FACTORS["us"],
    #         "yfactor": THR_FACTORS["K"],
    #         "xrange_min": 0,
    #         "yrange_min": 0,
    #         "key_labels": list(df.columns)[1:],
    #     }
    #     cartesian(params=params).plot()
    # except Exception as e:
    #     logging.error("Lat/thr by num dst skipped")

    # Max throughput by number of clients and number of destinations: check if saturation is reached
    for test_name_thr in test_names_thr:
        try:
            df2, cols = extract_plotdata([test_name_thr], "t", col="num_destinations", row="num_clients")
            df = to_cartesian_df(df2)
            datafile = store_df(folder_name, "thr_c_d_" + test_name_thr, df, df.columns)
            params = {
                "datafile": datafile,
                "output": datafile,
                "xlabel": "Num clients",
                "ylabel": "Throughput (Kmps)",
                "yfactor": THR_FACTORS["K"],
                "xrange_min": 0,
                "yrange_min": 0,
                "key_labels": [str(col) for col in cols],
            }
            cartesian(params=params).plot()
        except Exception as e:
            logging.error(f"Throughput saturation {test_name_thr} skipped")


def plot_genuine():
    folder_name = "genuine"

    test_names_lat = ["genuine_lat"]
    test_names_thr = ["genuine_base_thr", "genuine_breadth_thr", "genuine_depth_thr"]

    # Latency raw
    for overlay in ["base", "breadth", "depth"]:
        try:
            df, cols = extract_plotdata(test_names_lat, "r", "num_destinations", row=None, filter={"overlay": [overlay]})
            datafile = store_df(folder_name, "lat_raw_" + str(overlay.split("-")[0]), df, cols)
            params = {
                "datafile": datafile,
                "output": datafile,
                "title": str(overlay.split("-")[0]).capitalize() + (" tree" if overlay != "ramcast" else ""),
                "xlabel": f"Latency (us)",
                "key_labels": [str(col) + "d" for col in cols],
                "key_pos": "rb",
                "xfactor": LAT_FACTORS["us"],
                "xrange_min": 0,
                "xrange_max": 110,
            }
            cdf(params=params).plot()
        except Exception as e:
            logging.error(f"Latency raw {overlay} skipped")

    ### Latency average
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/plain/lat_avg", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["dst", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["dst"])
    df_ramcast = df_ramcast.iloc[:, 1:]

    # Get tram data and concatenate
    df, cols = extract_plotdata(test_names_lat, "lw", col="overlay", row="num_destinations")
    df = pd.concat([df, df_ramcast], axis=1)

    df = to_barchart_df(df)
    datafile = store_df(folder_name, "lat_avg", df, cols)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Number of destinations",
        "ylabel": "Latency (us)",
        "yfactor": LAT_FACTORS["us"],
        "yrange_min": 0,
        "key_labels": list([df.columns[i] for i in range(len(df.columns)) if i % 2 == 1]),
        "key_pos": "lt",
        "whiskers": 1,
    }
    barchart(params=params).plot()

    ### Latency average normalized
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/plain/lat_avg", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["dst", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["dst"])
    df_ramcast = df_ramcast.iloc[:, 1:]

    # Get tram data and concatenate
    df, cols = extract_plotdata(test_names_lat, "l", col="overlay", row="num_destinations")
    df = df.div(df_ramcast["ramcast"], axis=0)

    df = to_cartesian_df(df)
    datafile = store_df(folder_name, "lat_avg_norm", df, df.columns)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Number of destinations",
        "ylabel": "Latency %",
        "yfactor": 100,
        "yrange_min": 0,
        "key_labels": [str(col) for col in cols],
        "key_pos": "lb",
    }
    cartesian(params=params).plot()

    ### Thorughput max
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/plain/thr_max", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["dst", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["dst"])
    df_ramcast = df_ramcast.iloc[:, 1:]

    # Get tram data and concatenate
    df, cols = extract_plotdata(test_names_thr, "tw", col="overlay", row="num_destinations")
    df = pd.concat([df, df_ramcast], axis=1)

    df = to_barchart_df(df)
    datafile = store_df(folder_name, "thr_max", df, cols)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Number of destinations",
        "ylabel": "Throughput (Kmps)",
        "yfactor": THR_FACTORS["K"],
        "yrange_min": 0,
        "row_first": 1,
        "key_labels": list([df.columns[i] for i in range(len(df.columns)) if i % 2 == 1]),
        "whiskers": 1,
    }
    barchart(params=params).plot()

    ### Thorughput normalized
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/plain/thr_max", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["dst", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["dst"])
    df_ramcast = df_ramcast.iloc[:, 1:]

    # Get tram data and normalize
    df, cols = extract_plotdata(test_names_thr, "t", col="overlay", row="num_destinations")
    df = pd.concat([df, df_ramcast["ramcast"]], axis=1)
    df = df.div(df_ramcast["ramcast"], axis=0)

    df = to_cartesian_df(df)
    datafile = store_df(folder_name, "thr_max_norm", df, df.columns)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Number of destinations",
        "ylabel": "Normalized throughput",
        # "yfactor": 100,
        "yrange_min": 0,
        "row_first": 1,
        "key_labels": [str(col) for col in cols] + ["ramcast"],
        "key_pos": "lt",
    }
    cartesian(params=params).plot()

    # Max throughput by number of clients and number of destinations: check if saturation is reached
    for test_name_thr in test_names_thr:
        try:
            df2, cols = extract_plotdata([test_name_thr], "t", col="num_destinations", row="num_clients")
            df = to_cartesian_df(df2)
            datafile = store_df(folder_name, "thr_c_d_" + test_name_thr, df, df.columns)
            params = {
                "datafile": datafile,
                "output": datafile,
                "xlabel": "Num clients",
                "ylabel": "Throughput (Kmps)",
                "yfactor": THR_FACTORS["K"],
                "xrange_min": 0,
                "yrange_min": 0,
                "key_labels": [str(col) for col in cols],
            }
            cartesian(params=params).plot()
        except Exception as e:
            logging.error(f"Throughput saturation {test_name_thr} skipped")


def plot_msg_size():
    folder_name = "msg_size"

    # Latency average
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/msg_size/lat_avg", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["msg_size", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["msg_size"])
    df_ramcast = df_ramcast.iloc[:, 1:]

    # Get tram data and concatenate
    test_names_lat = ["plain_lat", "msg_size_1KB_lat", "msg_size_16KB_lat", "msg_size_128KB_lat"]
    df, cols = extract_plotdata(test_names_lat, "lw", "overlay", "payload_size", filter={"num_destinations": [8], "overlay": ["breadth"]})
    df = pd.concat([df, df_ramcast], axis=1)

    df = to_barchart_df(df)
    datafile = store_df(folder_name, "lat_avg", df, df.columns)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Payload size",
        "ylabel": "Latency (us)",
        "row_count": 3,
        "yfactor": LAT_FACTORS["us"],
        "yrange_min": 1,
        "key_labels": ["tram", "ramcast"],
        "key_pos": "lt",
        "ylogscale": 10,
        "whiskers": 1,
    }
    barchart(params=params).plot()

    # Thorughput
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/msg_size/thr_max", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["msg_size", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["msg_size"])
    df_ramcast = df_ramcast.iloc[:, 1:]

    # Get tram data and concatenate
    test_names_thr = [
        "plain_depth_thr",
        "msg_size_1KB_thr",
        "msg_size_16KB_thr",
        "msg_size_128KB_thr",
    ]
    df, cols = extract_plotdata(test_names_thr, "tw", "overlay", "payload_size", filter={"num_destinations": [8]})
    df = pd.concat([df, df_ramcast], axis=1)

    df = to_barchart_df(df)
    datafile = store_df(folder_name, "thr_max", df, cols)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Payload size",
        "ylabel": "Throughput (Kmps)",
        "row_count": 3,
        "yfactor": THR_FACTORS["K"],
        "key_labels": ["tram", "ramcast"],
        "ylogscale": 10,
        "yrange_min": 1,
        "whiskers": 1,
    }
    barchart(params=params).plot()

    # Max throughput by number of clients and number of destinations: check if saturation is reached
    # for test_name_thr in test_names_thr:
    #     df2, cols = extract_plotdata([test_name_thr], "t", col="num_destinations", row="num_clients")
    #     df = to_cartesian_df(df2)
    #     datafile = store_df(folder_name, "thr_c_d_" + test_name_thr, df, df.columns)
    #     params = {
    #         "datafile": datafile,
    #         "output": datafile,
    #         "xlabel": "Num clients",
    #         "ylabel": "Throughput (Kmps)",
    #         "yfactor": THR_FACTORS["K"],
    #         "xrange_min": 0,
    #         "yrange_min": 0,
    #         "key_labels": [str(col) for col in cols],
    #     }
    #     cartesian(params=params).plot()


def plot_replication():
    folder_name = "replication"

    test_names_lat = ["replication_1p_lat", "replication_3p_lat", "replication_5p_lat", "replication_7p_lat"]

    ### Latency average
    df, cols = extract_plotdata(test_names_lat, "l", "num_destinations", "processes_per_group")
    df = to_cartesian_df(df)
    datafile = store_df(folder_name, "lat_avg", df, cols)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Processes per group",
        "ylabel": "Latency (us)",
        "yfactor": LAT_FACTORS["us"],
        "yrange_min": 0,
        "key_labels": [str(col) + "d" for col in cols] + ["r1", "r3", "r5", "r7"],
        "key_pos": "lt",
    }
    cartesian(params=params).plot()

    ### Latency average with RamCast
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/replication/lat_avg", sep=" ", header=None, skiprows=1)
    df_ramcast = df_ramcast.iloc[:, 4:]

    # Get tram data and concatenate
    df, cols = extract_plotdata(test_names_lat, "l", "num_destinations", "processes_per_group")

    df = to_cartesian_df(df)
    df = df.iloc[:, 4:]
    df = pd.concat([df, df_ramcast], axis=1)

    datafile = store_df(folder_name, "lat_avg_comp", df, cols)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Processes per group",
        "ylabel": "Latency (us)",
        "yfactor": LAT_FACTORS["us"],
        "yrange_min": 0,
        "key_labels": ["tram", "ramcast"],
        "key_pos": "lt",
    }
    cartesian(params=params).plot()

    # tram Thorughput
    # test_names = ["tram_replication_base_1p_thr","tram_replication_base_3p_thr", "tram_replication_base_5p_thr", "tram_replication_base_7p_thr", "tram_replication_base_9p_thr","tram_replication_breadth_1p_thr","tram_replication_breadth_3p_thr", "tram_replication_breadth_5p_thr", "tram_replication_breadth_7p_thr", "tram_replication_breadth_9p_thr","tram_replication_depth_1p_thr","tram_replication_depth_3p_thr", "tram_replication_depth_5p_thr", "tram_replication_depth_7p_thr", "tram_replication_depth_9p_thr",]
    # df, cols = extract_plotdata(test_names, "t", "processes_per_group", "num_destinations", {"overlay": ["base"], "num_destinations": [2, 3, 4, 5, 6, 7, 8]})
    # df = to_cartesian_df(df)
    # datafile = store_df(folder_name, "thr_max", df, cols)
    # params = {
    #     "datafile": datafile,
    #     "output": datafile,
    #     "title": "Maximum troughput",
    #     "xlabel": "Number of destinations",
    #     "ylabel": "Throughput (Kmps)",
    #     "yfactor": THR_FACTORS["K"],
    #     "yrange_min": 0,
    #     "key_labels": [str(col) + "p" for col in cols],
    # }
    # cartesian(params=params).plot()


def plot_mu():
    folder_name = "mu"

    # Get tram data and concatenate
    test_names_thr = ["plain_depth_thr_mu", "mu_small", "mu"]
    df, cols = extract_plotdata(test_names_thr, "tw", col="overlay", row="payload_size", filter={"num_destinations": [1]})

    df = to_barchart_df(df)
    datafile = store_df(folder_name, "thr_max", df, cols)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Payload size",
        "ylabel": "Throughput (Kmps)",
        "yfactor": THR_FACTORS["K"],
        "key_labels": ["depth", "mu", "mu-small"],
        "whiskers": 1,
    }
    barchart(params=params).plot()

    # Max throughput by number of clients and number of destinations: check if saturation is reached
    for test_name_thr in test_names_thr:
        df2, cols = extract_plotdata([test_name_thr], "t", col="num_destinations", row="num_clients")
        df = to_cartesian_df(df2)
        datafile = store_df(folder_name, "thr_c_d_" + test_name_thr, df, df.columns)
        params = {
            "datafile": datafile,
            "output": datafile,
            "xlabel": "Num clients",
            "ylabel": "Throughput (Kmps)",
            "yfactor": THR_FACTORS["K"],
            "xrange_min": 0,
            "yrange_min": 0,
            "key_labels": [str(col) for col in cols],
        }
        cartesian(params=params).plot()


def plot_msg_passing():
    folder_name = "msg_passing"

    ### Latency average
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/plain/lat_avg", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["dst", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["dst"])
    df_ramcast = df_ramcast.iloc[:, 1:]
    df_ramcast = df_ramcast[df_ramcast.index.isin([1, 2, 4, 8])]

    # Get tram data and concatenate
    df0, cols0 = extract_plotdata(["plain_lat"], "l", "overlay", "num_destinations", {"overlay": ["base"], "num_destinations": [1, 2, 4, 8]})
    df0 = pd.concat([df0, df_ramcast["ramcast"]], axis=1)

    df1, cols1 = extract_msg_passing_plotdata("lat")
    df = pd.concat([df0, df1], axis=1)
    # print(df.div(df["base"], axis=0))
    cols = ["tram-base", "ramcast", "byzcast-base", "skeen"]
    df = to_cartesian_df(df)
    datafile = store_df(folder_name, "lat_avg", df, cols)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Number of destinations",
        "ylabel": "Latency (us)",
        "key_labels": [str(col) for col in cols],
        "key_pos": "rb",
        "yrange_min": 0,
        "yfactor": LAT_FACTORS["us"],
        # "ylogscale": 10,
    }
    cartesian(params=params).plot()

    ### Thorughput max
    # Get ramcast data
    df_ramcast = pd.read_csv(f"../../{RAMCAST_PATH}/plain/thr_max", sep=" ", header=None, skiprows=1)
    df_ramcast.columns = ["dst", "ramcast", "ramcast-stdev"]
    df_ramcast.index = list(df_ramcast["dst"])
    df_ramcast = df_ramcast.iloc[:, 1:]
    df_ramcast = df_ramcast[df_ramcast.index.isin([1, 2, 4, 8])]

    # Get tram data and concatenate
    df0, cols0 = extract_plotdata(["plain_base_thr"], "t", "overlay", "num_destinations", {"overlay": ["base"], "num_destinations": [1, 2, 4, 8]})
    df0 = pd.concat([df0, df_ramcast["ramcast"]], axis=1)

    df1, cols1 = extract_msg_passing_plotdata("thr")
    df = pd.concat([df0, df1], axis=1)
    # print(df.div(df["base"], axis=0))
    cols = ["tram-base", "ramcast", "byzcast-base", "skeen"]
    df = to_cartesian_df(df)
    datafile = store_df(folder_name, "thr_max", df, cols)
    params = {
        "datafile": datafile,
        "output": datafile,
        "xlabel": "Number of destinations",
        "ylabel": "Throughput (Kmps)",
        "key_labels": [str(col) for col in cols],
        "yfactor": THR_FACTORS["K"],
        "ylogscale": 10,
    }
    cartesian(params=params).plot()


def plot_lead():
    folder_name = "lead"
    test_names = ["lead"]

    # Lead delay
    # df, cols = extract_plotdata(test_names, "f", col="overlay", row="num_destinations")
    # df = to_barchart_df(df)
    # datafile = store_df(folder_name, "lead_lat", df, cols)
    # params = {
    #     "datafile": datafile,
    #     "output": datafile,
    #     "title": "Average leader election latency",
    #     "xlabel": "Num destinations",
    #     "ylabel": "Latency (us)",
    #     "yrange_min": 0,
    #     "key_labels": [str(col).split("-")[0] for col in cols],
    #     "key_pos": "lt",
    # }
    # barchart(params=params).plot()

    # tram latency raw
    # df, cols = extract_plotdata(test_names, "r", row=None, col="num_destinations")
    # datafile = store_df(folder_name, "lat_cdf", df, cols)
    # params = {
    #     "datafile": datafile,
    #     "output": datafile,
    #     "title": "tram latency",
    #     "xlabel": f"Latency (us)",
    #     "key_labels": [str(col) + "d" for col in cols],
    #     "key_pos": "rb",
    #     "xfactor": LAT_FACTORS["us"],
    #     "xrange_min": 0,
    #     "xrange_max": 70,
    # }
    # cdf(params=params).plot()

    df, cols = extract_plotdata(test_names, "r", row=None, col="num_destinations")
    df = df.iloc[: 10000 + 1]

    for d in range(1, 2):
        df_d = pd.DataFrame(df.iloc[:, 10 * d : 10 * (d + 1)]) / 1000
        # df_d = df_d.where(df_d > 1000).dropna(how="all").reset_index().iloc[:, 1:]
        values_over_1000 = [v for v in df_d.values[df_d.values > 2000.0].tolist()]
        # print(int(np.mean(values_over_1000)), int(np.std(values_over_1000)), int(np.min(values_over_1000)), int(np.max(values_over_1000)))
        df_d = to_cartesian_df(df_d)
        datafile = store_df(folder_name, "lat_raw_lead_d" + str(d + 1), df_d, [cols[d]])
        params = {
            "datafile": datafile,
            "output": datafile,
            "title": "tram latency",
            "xlabel": "Number of messages",
            "ylabel": f"Latency (ms)",
            # "key_labels": [f"{x}c" for x in range(1, 11)],
            # "key_pos": "rb",
            "xrange_max": 10000,
            "yrange_min": 0,
            "yfactor": LAT_FACTORS["us"],
        }
        cartesian(params=params).plot()

    # Latency avg
    # df, cols = extract_plotdata(test_names, "ll", row=None, col="num_destinations")
    # for d in range(len(df.columns)):
    #     df_d = to_cartesian_df(pd.DataFrame(df[d + 1]))
    #     datafile = store_df(folder_name, "lat_avg_lead_d" + str(d + 1), df_d, [cols[d]])
    #     params = {
    #         "datafile": datafile,
    #         "output": datafile,
    #         "title": "tram latency",
    #         "xlabel": f"Latency (us)",
    #         "key_labels": [str(cols[d]) + "d"],
    #         "key_pos": "rb",
    #         "xfactor": 1 / 1000,
    #         "yfactor": LAT_FACTORS["us"],
    #     }
    #     cartesian(params=params).plot()


# Main function


def main():
    script_dir: str = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)

    # Create the directories for the plots
    try:
        shutil.rmtree(PLOT_DIR)
    except FileNotFoundError:
        pass
    except PermissionError:
        print(f"Permission denied: Unable to remove plots.")
    except Exception as e:
        print(f"An error occurred: {e}")
    os.makedirs(PLOT_DIR, exist_ok=True)

    # Load all data in memory
    df_data = pd.DataFrame()
    for config_file in glob.glob(f"{CONFIG_DIR}/config_*"):
        flag = str(re.search(rf"config_([^_]+).json", os.path.basename(config_file)).group(1))
        df_entry = extract_df_entry(flag)
        df_data = pd.concat([df_data, df_entry])

    # Plots
    # logging.info(f"Plotting plain...")
    # plot_plain()
    # logging.info("Done")

    # logging.info(f"Plotting genuine...")
    # plot_genuine()
    # logging.info("Done")

    # logging.info(f"Plotting msg_size...")
    # plot_msg_size()
    # logging.info("Done")

    # logging.info(f"Plotting replication...")
    # plot_replication()
    # logging.info("Done")

    # print(f"> Plotting msg_passing - ", end="", flush=True)
    # plot_msg_passing()
    # print("Done")

    plot_mu()

    # logging.info(f"Plotting lead...")
    # plot_lead()
    # logging.info("Done")


if __name__ == "__main__":
    main()
