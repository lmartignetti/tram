# tram

This is the source code of the paper "TRAM: Tree-based atomic multicast on RDMA" published at the ACM Symposium on Cloud Computing 2026 (SoCC 2026).

## Project description

`tram` implements atomic multicast in shared memory using RDMA.

## How to build and run the experiments

```
scripts/build.sh -r -t
scripts/run.sh -r
```

## How to collect the generated data and plot it

After the tests have been run, the generated data can be collected into the `data` directory, and the plots can be generated into the `plots` directory:
```
python3 ./plotting/collect.py
python3 ./plotting/plot.py
```

## Contacts

For any question or comment, you can contact: `martilo@usi.ch`