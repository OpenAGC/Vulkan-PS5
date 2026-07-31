#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

"$script_dir/run_fw550_scalar_block_layout.sh"
"$script_dir/run_fw550_alpha_to_one.sh"
