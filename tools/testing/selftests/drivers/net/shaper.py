#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_true, KsftSkipEx
from lib.py import ShaperFamily
from lib.py import NetDrvEnv
from lib.py import NlError
from lib.py import cmd
import glob
import sys

def get_shapers(cfg, nl_shaper) -> None:
    try:
        shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise

    # default configuration, no shapers configured
    ksft_eq(len(shapers), 0)

def get_caps(cfg, nl_shaper) -> None:
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex}, dump=True)
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise

    # each device implementing shaper support must support some
    # features in at least a scope
    ksft_true(len(caps)> 0)


def set_qshapers(cfg, nl_shaper) -> None:
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex, 'scope':'queue'})
    except NlError as e:
        if e.error == 95:
            cfg.queues = False;
            raise KsftSkipEx("shapers not supported by the device")
        raise
    if not 'support-bw-max' in caps or not 'support-metric-bps' in caps:
            raise KsftSkipEx("device does not support queue scope shapers with bw_max and metric bps")

    nl_shaper.set({'ifindex': cfg.ifindex,
                   'shaper': { 'handle': { 'scope': 'queue', 'id': 1 }, 'metric': 'bps', 'bw-max': 10000 }})
    nl_shaper.set({'ifindex': cfg.ifindex,
                   'shaper': { 'handle': { 'scope': 'queue', 'id': 2 }, 'metric': 'bps', 'bw-max': 20000 }})

    # querying a specific shaper not yet configured must fail
    raised = False
    try:
        shaper_q0 = nl_shaper.get({'ifindex': cfg.ifindex, 'handle': { 'scope': 'queue', 'id': 0}})
    except (NlError):
        raised = True
    ksft_eq(raised, True)

    shaper_q1 = nl_shaper.get({'ifindex': cfg.ifindex, 'handle': { 'scope': 'queue', 'id': 1 }})
    ksft_eq(shaper_q1, { 'parent': { 'scope': 'netdev', 'id': 0 },
                         'handle': { 'scope': 'queue', 'id': 1 },
                         'metric': 'bps',
                         'bw-min': 0,
                         'bw-max': 10000,
                         'burst': 0,
                         'priority': 0,
                         'weight': 0 })

    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'parent': { 'scope': 'netdev', 'id': 0 },
                       'handle': { 'scope': 'queue', 'id': 1 },
                       'metric': 'bps',
                       'bw-min': 0,
                       'bw-max': 10000,
                       'burst': 0,
                       'priority': 0,
                       'weight': 0 },
                      {'parent': { 'scope': 'netdev', 'id': 0 },
                       'handle': { 'scope': 'queue', 'id': 2 },
                       'metric': 'bps',
                       'bw-min': 0,
                       'bw-max': 20000,
                       'burst': 0,
                       'priority': 0,
                       'weight': 0 }])


def del_qshapers(cfg, nl_shaper) -> None:
    if not cfg.queues:
        raise KsftSkipEx("queue shapers not supported by device, skipping delete")

    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': { 'scope': 'queue', 'id': 2}})
    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': { 'scope': 'queue', 'id': 1}})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def set_nshapers(cfg, nl_shaper) -> None:
    # check required features
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex, 'scope':'netdev'})
    except NlError as e:
        if e.error == 95:
            cfg.netdev = False;
            raise KsftSkipEx("shapers not supported by the device")
        raise
    if not 'support-bw-max' in caps or not 'support-metric-bps' in caps:
            raise KsftSkipEx("device does not support nested netdev scope shapers with weight")

    nl_shaper.set({'ifindex': cfg.ifindex,
                   'shaper': { 'handle': { 'scope': 'netdev', 'id': 0 }, 'bw-max': 100000 }})

    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'parent': { 'scope': 'port', 'id': 0 },
                       'handle': { 'scope': 'netdev', 'id': 0 },
                       'metric': 'bps',
                       'bw-min': 0,
                       'bw-max': 100000,
                       'burst': 0,
                       'priority': 0,
                       'weight': 0 }])

def del_nshapers(cfg, nl_shaper) -> None:
    if not cfg.netdev:
        raise KsftSkipEx("netdev shaper not supported by device, skipping delete")

    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': { 'scope': 'netdev'}})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def qgroups(cfg, nl_shaper) -> None:
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex, 'scope':'detached'})
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise
    if not 'support-bw-max' in caps or not 'support-metric-bps' in caps:
            raise KsftSkipEx("device does not support detached scope shapers with bw_max and metric bps")
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex, 'scope':'queue'})
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise
    if not 'support-nesting' in caps or not 'support-weight' in caps or not 'support-metric-bps' in caps:
            raise KsftSkipEx("device does not support nested queue scope shapers with weight")

    output_handle = nl_shaper.group({'ifindex': cfg.ifindex,
                   'inputs':[{ 'handle': { 'scope': 'queue', 'id': 1 }, 'metric': 'bps', 'weight': 3 },
                             { 'handle': { 'scope': 'queue', 'id': 2 }, 'metric': 'bps', 'weight': 2 }],
                   'output': { 'handle': {'scope':'detached'}, 'metric': 'bps', 'bw-max': 10000}})
    output_id = output_handle['handle']['id']

    shaper = nl_shaper.get({'ifindex': cfg.ifindex, 'handle': { 'scope': 'queue', 'id': 1 }})
    ksft_eq(shaper, {'parent': { 'scope': 'detached', 'id': output_id },
                     'handle': { 'scope': 'queue', 'id': 1 },
                     'metric': 'bps',
                     'bw-min': 0,
                     'bw-max': 0,
                     'burst': 0,
                     'priority': 0,
                     'weight': 3 })

    # grouping to a specified, not existing detached scope shaper must fail
    raised = False
    try:
        nl_shaper.group({'ifindex': cfg.ifindex,
                   'inputs':[ { 'handle': { 'scope': 'queue', 'id': 3 }, 'metric': 'bps', 'weight': 3 }],
                   'output': { 'handle': {'scope':'detached', 'id': output_id + 1 }, 'metric': 'bps', 'bw-max': 10000}})
    except (NlError):
        raised = True
    ksft_eq(raised, True)

    output_handle = nl_shaper.group({'ifindex': cfg.ifindex,
                   'inputs':[ { 'handle': { 'scope': 'queue', 'id': 3 }, 'metric': 'bps', 'weight': 4 }],
                   'output': { 'handle': {'scope':'detached', 'id': output_id} }})

    shaper = nl_shaper.get({'ifindex': cfg.ifindex, 'handle': { 'scope': 'queue', 'id': 3 }})
    ksft_eq(shaper, {'parent': { 'scope': 'detached', 'id': 0 },
                     'handle': { 'scope': 'queue', 'id': 3 },
                     'metric': 'bps',
                     'bw-min': 0,
                     'bw-max': 0,
                     'burst': 0,
                     'priority': 0,
                     'weight': 4 })

    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': { 'scope': 'queue', 'id': 2}})
    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': { 'scope': 'queue', 'id': 1}})

    # deleting a non empty group mast fail
    raised = False
    try:
        nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': { 'scope': 'detached', 'id': output_id }})
    except (NlError):
        raised = True
    ksft_eq(raised, True)
    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': { 'scope': 'queue', 'id': 3}})

    # the detached scope shaper deletion is implicit
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def main() -> None:
    with NetDrvEnv(__file__, queue_count=4) as cfg:
        cfg.queues = True
        cfg.netdev = True
        ksft_run([get_shapers,
                  get_caps,
                  set_qshapers,
                  del_qshapers,
                  qgroups,
                  set_nshapers,
                  del_nshapers], args=(cfg, ShaperFamily()))
    ksft_exit()


if __name__ == "__main__":
    main()
