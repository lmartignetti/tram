from itertools import product
import json
import argparse
import os
import shutil

TEST_DATA_OPTIONAL_PARAMS = {"duration_ms": 3000, "num_msgs": 0, "collect_every_ms": 10, "max_latency_samples": 1000}
TEST_CLUSTER_OPTIONAL_PARAMS = {
    "tree": 0,
    "group_size": 2,
    "num_clients": 1,
    "cbuf_len": 1,
    "payload_size": 64,
    "pending_len": 10,
    "log_len": 50,
    "genuine": False,
    "num_destinations": 1,
}

KB = 1024
MB = 1024 * 1024
GB = 1024 * 1024 * 1024

# A tree is defined by the parent of each node (-1 is the root)
TRAM_TREES = {}
TRAM_TREES[0] = [-1, 0, 1, 1, 2, 2, 3, 3]  # base
TRAM_TREES[1] = [-1, 0, 0, 0, 0, 0, 0, 0]  # breadth
TRAM_TREES[2] = [-1, 0, 1, 2, 3, 4, 5, 6]  # depth
TRAM_TREES[3] = [-1, 0, 0]  # simple

MAIN_CONFIG = "./config.json"
CONFIG_DIR = "./build/config"

BASE_TCP_PORT = 20000


def main():
    # Command line parsing
    parser = argparse.ArgumentParser(description="Creating tests configuration files")
    parser.add_argument("-l", "--local", action="store_true", help="Local build only")
    parser.add_argument("--uselocal", action="store_true", help="Run on the local machine too")

    args = parser.parse_args()

    local_build = args.local
    use_local = args.uselocal

    # Tests definition
    conf = {}
    # Plain
    add_test_bulk(conf, {"tree": [0, 1, 2], "cbuf_len": [1, 2, 3, 4, 5], "num_destinations": [1, 2, 3, 4, 5, 6, 7, 8]})
    # Genuine
    add_test_bulk(conf, {"tree": [0, 1, 2], "cbuf_len": [1, 2, 3, 4, 5], "genuine": [True], "num_destinations": [1, 2, 3, 4, 5, 6, 7, 8]})
    # Msg size
    add_test_bulk(
        conf,
        {"tree": [0, 1, 2], "cbuf_len": [1, 2, 3, 4, 5], "payload_size": [32, 64, 128, 256, 512, 1 * KB], "num_destinations": [1, 2, 3, 4, 5, 6, 7, 8]},
    )
    # Replication
    add_test_bulk(conf, {"tree": [3], "group_size": [1, 2, 3, 4, 5], "cbuf_len": [1, 2, 3, 4, 5]})
    # Leader election
    # TODO

    # Parsing the main configuration file
    num_machines = 0
    ips = []
    with open(f"{MAIN_CONFIG}", "r") as file:
        main_config = json.load(file)
        num_machines = len(main_config) - 1

        if use_local:
            num_machines = num_machines + 1
            ips.append(main_config[0]["ip"])  # use current node as well
        for remote in main_config[1:]:
            ips.append(remote["ip"])

    # Storing the configuration files
    max_num_processes = store_tests_configs(conf, num_machines)

    # Generate the general network configuration file
    cpu_core_count = count_cpus_in_numa_node(0)
    network: list = []
    for i in range(max_num_processes):
        network.append(
            {
                "ip": ips[i % num_machines],
                "tcp_port": BASE_TCP_PORT + 2 * (i // num_machines),
                "rdma_port": BASE_TCP_PORT + 2 * (i // num_machines) + 1,
                "cpu_core": 0 if local_build else (i // num_machines) % cpu_core_count,
            }
        )

    with open(f"{CONFIG_DIR}/network.json", "w") as file:
        json.dump(network, file, indent=2)

    # Generate the general data collection configuration file
    data: dict = TEST_DATA_OPTIONAL_PARAMS
    with open(f"{CONFIG_DIR}/data.json", "w") as file:
        json.dump(data, file, indent=2)

    with open(f"{CONFIG_DIR}/tests.conf", "w") as file:
        file.write(f"./benchmarks ../../../{CONFIG_DIR}\n")


def add_test_bulk(tests: dict, cluster_bulk_conf):
    for cluster_conf in [dict(zip(cluster_bulk_conf, v)) for v in product(*cluster_bulk_conf.values())]:
        add_test(tests, cluster_conf)


def add_test(tests: dict, cluster_conf: dict = {}):
    init_conf = {}
    for key in TEST_CLUSTER_OPTIONAL_PARAMS:
        init_conf[key] = cluster_conf[key] if key in cluster_conf else TEST_CLUSTER_OPTIONAL_PARAMS[key]

    flag = ""
    flag = flag + f"t{init_conf['tree']}"
    flag = flag + f"p{init_conf['group_size']}"
    flag = flag + f"c{init_conf['num_clients']}"
    flag = flag + f"b{init_conf['cbuf_len']}"
    flag = flag + f"g{1 if init_conf['genuine'] else 0}"
    flag = flag + f"d{init_conf['num_destinations']}"
    flag = flag + f"k{init_conf['pending_len']}"
    flag = flag + f"l{init_conf['log_len']}"
    flag = flag + f"v{payload_str(init_conf['payload_size'])}"
    tests[f"{flag}"] = init_conf


def store_tests_configs(tests: dict, num_machines: int):
    if num_machines <= 0:
        print("No machines are available: you can use the local one with --uselocal")
        exit(1)

    max_num_processes = 0
    for test_name, test in dict(tests).items():
        test["tree"] = TRAM_TREES[test["tree"]]
        if test["num_destinations"] <= 0 or test["num_destinations"] > len(test["tree"]):
            print(f"Wrong number of destinations ({test['num_destinations']}) with {len(test['tree'])} groups")
            exit(1)

        num_servers = len(test["tree"]) * test["group_size"]
        num_clients = test["num_clients"]
        num_processes = num_servers + num_clients

        if num_servers >= num_machines:
            print(f"WARN: test {test_name} specifies {num_processes} processes, but you only have {num_machines} machines!")

        max_num_processes = max(max_num_processes, num_processes)

        with open(f"{CONFIG_DIR}/config_{test_name}.json", "w") as file:
            json.dump(test, file, indent=2)

    return max_num_processes


def count_cpus_in_numa_node(node=0):
    path = f"/sys/devices/system/node/node{node}/cpulist"
    with open(path) as f:
        cpulist = f.read().strip()

    count = 0
    for part in cpulist.split(","):
        if "-" in part:
            start, end = map(int, part.split("-"))
            count += end - start + 1
        else:
            count += 1
    return count


def payload_str(size: int):
    if size % GB == 0:
        return f"{int(size / GB)}GB"
    elif size % MB == 0:
        return f"{int(size / MB)}MB"
    elif size % KB == 0:
        return f"{int(size / KB)}KB"
    else:
        return f"{int(size)}B"


if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)) + "/../..")  # cd to project root folder

    # Create the directory for the configuration files of the benchmarks
    try:
        shutil.rmtree(CONFIG_DIR)
    except FileNotFoundError:
        pass
    except PermissionError:
        print(f"Permission denied: Unable to remove {CONFIG_DIR}.")
    except Exception as e:
        print(f"An error occurred: {e}")
    os.makedirs(f"{CONFIG_DIR}", exist_ok=True)

    main()
