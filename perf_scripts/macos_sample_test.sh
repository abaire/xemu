#!/usr/bin/env bash
# Runs one PfifoContention test without stall tracing and takes a macOS `sample` profile of xemu during its measured
# frames. Summarize the profile with macos_sample_tree.py.
#
# usage: macos_sample_test.sh PYTHON OUT_DIR TEST [run_stall_trace.py options...]
#
#   PYTHON   the Python interpreter of an xemu-perf-tester install
#   options  passed to run_stall_trace.py; --tester-dir, --iso and --xemu are required
#
# The run uses 400 measured frames unless --measure-frames is given, so that the profile falls inside the test. Set
# SAMPLE_DELAY_SECONDS (default 4) to how long xemu takes to boot and finish warmup, and SAMPLE_SECONDS (default 4) to
# the profile length.
set -eu

if [[ $# -lt 3 ]]; then
  sed -n '2,13p' "$0"
  exit 1
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
python="$1"
out_dir="$2"
test="$3"
shift 3

mkdir -p "${out_dir}"
out_dir="$(cd "${out_dir}" && pwd)"
delay="${SAMPLE_DELAY_SECONDS:-4}"
seconds="${SAMPLE_SECONDS:-4}"

"${python}" "${script_dir}/run_stall_trace.py" "${out_dir}" "${test}" --no-trace --measure-frames 400 "$@" &
runner=$!

# xemu-perf-tester runs its copy of xemu from the cache in OUT_DIR. It launches xemu once briefly to read its version,
# so wait for a launch that stays up past the boot delay.
pattern="${out_dir}/cache/dev_xemu/"
pid=""
for _ in $(seq 1 240); do
  candidate="$(pgrep -f -n "${pattern}" || true)"
  if [[ -n "${candidate}" ]]; then
    sleep "${delay}"
    if kill -0 "${candidate}" 2>/dev/null; then
      pid="${candidate}"
      break
    fi
  fi
  sleep 0.5
done

if [[ -n "${pid}" ]]; then
  sample "${pid}" "${seconds}" -mayDie -file "${out_dir}/${test}-sample.txt" >/dev/null 2>&1 || true
  echo "Wrote ${out_dir}/${test}-sample.txt"
else
  echo "xemu never stayed up long enough to sample" >&2
fi
wait "${runner}"
