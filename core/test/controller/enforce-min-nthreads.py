# Copyright 2018 Schibsted

import json
import sys

obj = json.load(sys.stdin)
assert len(obj["counts"]["thread"]) >= 5
