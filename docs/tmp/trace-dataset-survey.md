# NCCL / GPU Collective Trace Dataset Survey

Date: 2026-03-09

## Executive Summary

Publicly available NCCL collective trace corpora are scarce. The strongest public options I found are not standalone datasets, but trace bundles embedded in Meta / PyTorch / MLCommons repos:

1. **PARAM sample ET + Kineto traces**
   - Best immediate fit.
   - Public traces for **ResNet** and **DLRM**.
   - Includes real collective sequences such as `all_reduce` and `all_to_all`.
   - ET traces are directly replayable with PARAM; Kineto traces add event durations.

2. **Holistic Trace Analysis (HTA) distributed traces**
   - Public multi-rank PyTorch profiler traces.
   - The `vision_transformer` sample exposes `all_gather`, `reduce_scatter`, and `all_reduce`.
   - Good source for sequence mining plus timing.

3. **ASTRA-sim / Chakra ET traces**
   - Public and cleanly structured.
   - Best for controlled replay and format prototyping.
   - Less realistic than PARAM / HTA because the shipped workload traces are mostly microbenchmarks or small samples, not long end-to-end training runs.

If you need a larger corpus than the public samples above, the highest-leverage path is to use **PyTorch Profiler / ExecutionTraceObserver** or **PyTorch NCCL Flight Recorder** on public training workloads (MLPerf reference implementations, Megatron-DeepSpeed, TorchTitan, etc.) and normalize those outputs into an NCCLPol replay format.

## Recommended Top 3 For NCCLPol

### 1. PARAM public ET + Kineto traces

- URL: <https://github.com/facebookresearch/param/tree/main/et_replay/tests/inputs>
- Why it is the best fit:
  - Public model-derived traces, not just schema examples.
  - Includes both **ET** and **Kineto** flavors.
  - DLRM traces contain **`all_to_all`** and **`all_reduce`**, which are more representative of modern distributed training than plain DDP-only allreduce streams.
  - ET format already matches replay-style workflows.
- Direct usability:
  - **High**.
  - ET traces can be parsed directly or replayed via PARAM.
  - Kineto traces can be mined for durations and message sizes.

### 2. HTA `vision_transformer` traces

- URL: <https://github.com/facebookresearch/HolisticTraceAnalysis/tree/main/tests/data/vision_transformer>
- Why it is strong:
  - Public multi-rank distributed traces.
  - The rank-0 sample includes `distributedInfo.backend = "nccl"` and `world_size = 64`.
  - Contains `nccl:_all_gather_base`, `nccl:_reduce_scatter_base`, and `nccl:all_reduce`.
  - Preserves timing on each profiler event.
- Direct usability:
  - **Medium-high**.
  - Requires JSON parsing and byte derivation from `Input Dims` + dtype.

### 3. ASTRA-sim microbenchmark ET workloads

- URL: <https://github.com/astra-sim/astra-sim/tree/master/examples/workload/microbenchmarks>
- Why it is useful:
  - Public per-rank traces for `all_reduce`, `all_gather`, `all_to_all`, and `reduce_scatter`.
  - Very easy to feed into a replay harness once you parse Chakra ET protobuf.
  - Good for controlled policy evaluation across collective type / rank count / message size.
- Limitation:
  - Synthetic and short.
  - No measured runtime latencies in the shipped examples.

## Survey Table

| Source | URL | Format | What It Contains | Direct Use For NCCLPol | Notes |
| --- | --- | --- | --- | --- | --- |
| PARAM sample traces | <https://github.com/facebookresearch/param/tree/main/et_replay/tests/inputs> | PyTorch ET (`.json.gz` / `.json`) and Kineto profiler traces (`.json.gz` / `.json`) | `resnet_et.json.gz`, `resnet_kineto.json.gz`, `dlrm_pytorch_et.tar.gz`, `dlrm_kineto.tar.gz` | **Yes** | Strongest public source I found. ResNet ET includes `c10d::allreduce_` / `nccl:all_reduce`; DLRM includes `c10d::alltoall_base_` / `nccl:all_to_all` plus `all_reduce`. |
| PARAM ET replay docs | <https://github.com/facebookresearch/param/blob/main/docs/using_ET.md> | Documentation for collecting and replaying ET | Shows how to collect ET with `ExecutionTraceObserver` and replay with `commsTraceReplay.par --trace-type et` | **Yes** | Important operational path if you want to extend the public sample set. |
| HTA distributed traces | <https://github.com/facebookresearch/HolisticTraceAnalysis/tree/main/tests/data/vision_transformer> | Gzipped PyTorch profiler / Kineto JSON traces | 8 public rank files from a ViT workload; rank-0 trace advertises `backend=nccl`, `world_size=64` | **Yes, after JSON parsing** | Best public multi-rank profiler trace I found. Rank-0 contains many `all_gather`, `reduce_scatter`, and `all_reduce` events with durations. |
| HTA NCCL parser sample | <https://github.com/facebookresearch/HolisticTraceAnalysis/tree/main/tests/data/nccl_parser_config> | Gzipped PyTorch profiler trace | Smaller parser-oriented sample with `nccl:all_reduce` and `nccl:broadcast` | **Yes, but limited** | Good for parser development; not as realistic as the ViT sample. |
| ASTRA-sim workload traces | <https://github.com/astra-sim/astra-sim/tree/master/examples/workload/microbenchmarks> | Chakra protobuf ET (`.et`) | Public per-rank traces for `all_reduce`, `all_gather`, `all_to_all`, `reduce_scatter` at 4/8/16 NPUs and 1 MB | **Yes** | Excellent for deterministic replay; synthetic workload only. |
| ASTRA-sim workload generators | <https://github.com/astra-sim/astra-sim/tree/master/examples/workload/microbenchmarks/generator_scripts> | Python generators for Chakra ET | Generator scripts set `comm_type` and `comm_size` explicitly in ET nodes | **Yes** | Useful if you want more synthetic sizes / rank counts beyond the shipped examples. |
| Chakra sample traces | <https://github.com/mlcommons/chakra/tree/main/tests/data> | Chakra protobuf ET and JSONized workload graphs | `feeder_tests_trace.tar.gz`, `json_trace.tar.gz` | **Yes, but limited** | Public sample traces, not a large corpus. The sample JSON includes `c10d::allreduce_`, `c10d::broadcast_`, and NCCL kernels. |
| Chakra schema / converters | <https://github.com/mlcommons/chakra> | Schema + converters + feeder | `COMM_COLL_NODE`, `CollectiveCommType`, timing fields, `chakra_converter`, `chakra_jsonizer` | **Yes, as a normalization target** | Best interchange format if you want one replay schema across profilers and simulators. |
| PyTorch Profiler + ExecutionTraceObserver | <https://docs.pytorch.org/docs/stable/profiler.html> | Kineto JSON traces + ET observer output | Profiler can export Chrome/Perfetto traces and ET during the same profiling window | **Yes, but you must collect it yourself** | Best general collection pipeline for new workloads. |
| PyTorch NCCL Flight Recorder | <https://docs.pytorch.org/tutorials/unstable/flight_recorder_tutorial.html> | Pickled NCCL trace dumps | Per-collective entries with `profiling_name`, `input_sizes`, `input_dtypes`, process-group info, and optional timing | **Yes, but you must collect it yourself** | Lower-overhead NCCL-focused capture path than full profiler traces. Very attractive for future data collection. |
| DeepSpeed communication logger | <https://github.com/microsoft/DeepSpeed/blob/master/docs/_tutorials/comms-logging.md> | Text logs / summaries | Per-op name, message size, latency, throughput, bus bandwidth; examples include `reduce_scatter_tensor` and `all_gather_into_tensor` | **Yes, but you must collect it yourself** | Good if your workloads already use DeepSpeed. Output is easier to parse than full profiler JSON. |
| Kineto sample traces | <https://github.com/pytorch/kineto/tree/main/tb_plugin/samples> | Gzipped profiler traces | Public sample traces for parser / UI development | **Low** | The shipped ResNet samples I inspected did **not** contain NCCL events, so they are not a strong replay source for NCCLPol. |

## Detailed Findings

### 1. PARAM sample ET + Kineto traces

- URL:
  - Repo: <https://github.com/facebookresearch/param>
  - Inputs: <https://github.com/facebookresearch/param/tree/main/et_replay/tests/inputs>
  - ET docs: <https://github.com/facebookresearch/param/blob/main/docs/using_ET.md>
- Format:
  - PyTorch ET JSON (`resnet_et.json.gz`, `dlrm_pytorch_et.tar.gz`)
  - Kineto / PyTorch profiler JSON (`resnet_kineto.json.gz`, `dlrm_kineto.tar.gz`)
- What it contains:
  - `resnet_et.json.gz` is a PyTorch ET graph with `nodes`.
  - In the public ResNet ET sample I inspected, the communication nodes included:
    - `c10d::allreduce_` (15)
    - `nccl:all_reduce` (15)
    - `nccl:broadcast` (6)
  - `dlrm_pytorch_et.tar.gz` contains 8 ET files (`dlrm_eg_0.json` ... `dlrm_eg_7.json`).
  - In the public DLRM ET / Kineto samples I inspected, the communication events included:
    - `c10d::alltoall_base_`
    - `nccl:all_to_all`
    - `c10d::allreduce_`
    - `nccl:all_reduce`
- Why it is valuable:
  - Gives you both a graph-style replay trace and a timing-rich profiler trace for the same family of workloads.
  - DLRM adds `all_to_all`, which is especially useful for policy evaluation beyond plain DDP.
- Direct use or conversion:
  - **Directly usable** if you build a small ET parser.
  - Kineto traces need conversion to your replay schema.
  - PARAM already documents ET replay, so this is the easiest path to a functional replay prototype.

Example extraction from PARAM ET:

```python
import gzip
import json

with gzip.open("resnet_et.json.gz", "rt") as fh:
    et = json.load(fh)

for node in et["nodes"]:
    if node["name"] == "c10d::allreduce_":
        tensor = node["inputs"]["values"][0][0]
        num_elem = tensor[3]
        elem_bytes = tensor[4]
        msg_bytes = num_elem * elem_bytes
        print({
            "collective": "all_reduce",
            "bytes": msg_bytes,
            "device": tensor[5],
        })
```

Example extraction from PARAM Kineto:

```python
import json

with open("dlrm_kineto/worker0_step_12.1694128201545.pt.trace.json") as fh:
    trace = json.load(fh)

for ev in trace["traceEvents"]:
    if ev.get("name", "").startswith("nccl:"):
        dims = ev.get("args", {}).get("Input Dims", [])
        dtypes = ev.get("args", {}).get("Input type", [])
        print({
            "collective": ev["name"].replace("nccl:", ""),
            "input_dims": dims,
            "input_types": dtypes,
            "duration_us": ev.get("dur"),
        })
```

### 2. HTA distributed profiler traces

- URL:
  - Repo: <https://github.com/facebookresearch/HolisticTraceAnalysis>
  - Trace collection docs: <https://github.com/facebookresearch/HolisticTraceAnalysis/blob/main/docs/source/intro/trace_collection.rst>
  - ViT traces: <https://github.com/facebookresearch/HolisticTraceAnalysis/tree/main/tests/data/vision_transformer>
  - Smaller NCCL parser sample: <https://github.com/facebookresearch/HolisticTraceAnalysis/tree/main/tests/data/nccl_parser_config>
- Format:
  - Gzipped PyTorch profiler / Kineto JSON traces.
- What it contains:
  - `vision_transformer/rank-0.json.gz` includes:
    - `distributedInfo.backend = "nccl"`
    - `distributedInfo.world_size = 64`
    - `nccl:_all_gather_base`
    - `nccl:_reduce_scatter_base`
    - `nccl:all_reduce`
  - In the rank-0 trace I inspected, event counts were:
    - `nccl:_all_gather_base` (465)
    - `nccl:_reduce_scatter_base` (20)
    - `nccl:all_reduce` (5)
  - Event args include `Input Dims` and `Input type`; trace events include `dur`.
- Why it is valuable:
  - Closer to a real distributed training run than most public samples.
  - Preserves timing directly.
  - Includes collectives common in FSDP / sharded training.
- Direct use or conversion:
  - **Needs conversion** from profiler JSON to your replay schema.
  - Straightforward to parse.

Example extraction:

```python
import gzip
import json

dtype_bytes = {
    "c10::Half": 2,
    "Half": 2,
    "float": 4,
    "c10::Float": 4,
}

with gzip.open("rank-0.json.gz", "rt") as fh:
    trace = json.load(fh)

world_size = trace["distributedInfo"]["world_size"]

for ev in trace["traceEvents"]:
    name = ev.get("name", "")
    if name.startswith("nccl:"):
        dims = ev.get("args", {}).get("Input Dims", [])
        dtypes = ev.get("args", {}).get("Input type", [])
        num_elem = dims[0][0] if dims and dims[0] else None
        elem_bytes = dtype_bytes.get(dtypes[0], None) if dtypes else None
        print({
            "collective": name.replace("nccl:", ""),
            "bytes": num_elem * elem_bytes if num_elem and elem_bytes else None,
            "world_size": world_size,
            "duration_us": ev.get("dur"),
        })
```

### 3. ASTRA-sim workload traces

- URL:
  - Repo: <https://github.com/astra-sim/astra-sim>
  - Microbenchmarks: <https://github.com/astra-sim/astra-sim/tree/master/examples/workload/microbenchmarks>
  - Generator scripts: <https://github.com/astra-sim/astra-sim/tree/master/examples/workload/microbenchmarks/generator_scripts>
- Format:
  - Chakra protobuf ET files (`.et`), one file per rank.
- What it contains:
  - Shipped public traces for:
    - `all_reduce`
    - `all_gather`
    - `all_to_all`
    - `reduce_scatter`
  - Rank counts provided in the examples:
    - 4 NPUs
    - 8 NPUs
    - 16 NPUs
  - Size in shipped examples:
    - 1 MB
  - Generator scripts set `comm_type` and `comm_size` explicitly in each ET node.
- Why it is valuable:
  - Very clean replay input.
  - Perfect for controlled experiments and unit testing the replay layer.
- Limitation:
  - Synthetic.
  - No real model phase structure.
  - No measured timing in the shipped sample traces.
- Direct use or conversion:
  - **Directly usable** once you parse Chakra protobuf, or after `chakra_jsonizer`.

Example extraction path:

```bash
chakra_jsonizer \
  --input_filename all_reduce.0.et \
  --output_filename all_reduce.0.json
```

Then extract `COMM_COLL_NODE` attributes:

- `comm_type`
- `comm_size`

If you want larger synthetic coverage, the generator scripts can produce new ET files by varying rank count and size.

### 4. Chakra sample traces and schema

- URL:
  - Repo: <https://github.com/mlcommons/chakra>
  - Tests/data: <https://github.com/mlcommons/chakra/tree/main/tests/data>
  - Schema: <https://github.com/mlcommons/chakra/blob/main/schema/protobuf/et_def.proto>
- Format:
  - Chakra protobuf ET.
  - JSONized workload graph.
- What it contains:
  - Sample trace bundles such as:
    - `feeder_tests_trace.tar.gz`
    - `json_trace.tar.gz`
  - The JSON sample I inspected included:
    - `c10d::broadcast_`
    - `c10d::allreduce_`
    - NCCL kernel nodes
  - The Chakra schema explicitly supports:
    - `COMM_COLL_NODE`
    - `CollectiveCommType`
    - `start_time_micros`
    - `duration_micros`
    - arbitrary attributes such as `comm_size`
- Why it is valuable:
  - Strong normalization target.
  - Already accepted by ASTRA-sim.
- Limitation:
  - Public sample traces are small and more suitable for conversion / parser validation than for realistic workload replay.
- Direct use or conversion:
  - **Usable**, but primarily as a format layer or sample trace, not as a large benchmark corpus.

Example extraction:

```bash
chakra_jsonizer \
  --input_filename chakra.0.et \
  --output_filename chakra.0.json
```

Then filter JSON nodes for communication nodes:

```python
import json

with open("chakra.0.json") as fh:
    obj = json.load(fh)

for node in obj["workload_graph"]:
    if node.get("NodeType") == 7:
        print(node["Name"], node.get("runtime"))
```

### 5. PyTorch Profiler + ExecutionTraceObserver

- URL:
  - Profiler docs: <https://docs.pytorch.org/docs/stable/profiler.html>
  - PARAM ET collection doc: <https://github.com/facebookresearch/param/blob/main/docs/using_ET.md>
- Format:
  - Kineto JSON traces.
  - ET JSON traces via `ExecutionTraceObserver`.
- What it contains:
  - Profiler traces preserve per-event timing.
  - With `record_shapes=True`, collective events expose tensor shapes and dtypes.
  - ET preserves graph structure plus tensor metadata needed to derive bytes.
- Why it is valuable:
  - Best general-purpose trace collection pipeline if you want to build your own corpus from public training code.
- Direct use or conversion:
  - **Collection tool**, not a public dataset by itself.

Example collection pattern:

```python
from torch.profiler import profile, ProfilerActivity
from torch.profiler import ExecutionTraceObserver

et = ExecutionTraceObserver().register_callback("execution_trace.json")

with profile(
    activities=[ProfilerActivity.CPU, ProfilerActivity.CUDA],
    record_shapes=True,
    execution_trace_observer=et,
) as prof:
    for step, batch in enumerate(loader):
        train_step(batch)
        prof.step()
```

### 6. PyTorch NCCL Flight Recorder

- URL: <https://docs.pytorch.org/tutorials/unstable/flight_recorder_tutorial.html>
- Format:
  - Pickled dump files, one per rank.
- What it contains:
  - Per-collective entries with:
    - `profiling_name`
    - `input_sizes`
    - `input_dtypes`
    - process-group metadata
    - optional timing if `TORCH_NCCL_ENABLE_TIMING=true`
- Why it is valuable:
  - More NCCL-focused and lower overhead than full profiler traces.
  - The dump format is already very close to the replay schema you want.
- Direct use or conversion:
  - **Collection tool**, not a public dataset by itself.

Example extraction:

```python
import pickle

with open("trace_rank_0.pkl", "rb") as fh:
    dump = pickle.load(fh)

for entry in dump["entries"]:
    print({
        "collective": entry["profiling_name"].replace("nccl:", ""),
        "input_sizes": entry["input_sizes"],
        "input_dtypes": entry["input_dtypes"],
        "state": entry["state"],
    })
```

### 7. DeepSpeed communication logger

- URL: <https://github.com/microsoft/DeepSpeed/blob/master/docs/_tutorials/comms-logging.md>
- Format:
  - Text logs or aggregated summaries.
- What it contains:
  - Collective name.
  - Message size.
  - Latency.
  - Throughput / bus bandwidth.
  - Optional caller function with `debug=true`.
- Why it is valuable:
  - Extremely easy to convert into an NCCLPol replay CSV.
  - DeepSpeed docs include sample output for `reduce_scatter_tensor` and `all_gather_into_tensor`.
- Limitation:
  - No public corpus shipped in the repo.
  - Only useful if you run DeepSpeed workloads.
- Direct use or conversion:
  - **Collection tool**, not a public dataset.

Example extraction idea:

```python
import re

pat = re.compile(
    r"comm op: (?P<op>[^|]+) \| time \(ms\): (?P<ms>[0-9.]+) \| msg size: (?P<size>[0-9.]+) MB"
)

for line in open("deepspeed-comms.log"):
    m = pat.search(line)
    if m:
        print(m.groupdict())
```

### 8. Kineto sample traces

- URL: <https://github.com/pytorch/kineto/tree/main/tb_plugin/samples>
- Format:
  - Gzipped profiler traces.
- What it contains:
  - Good public profiler samples for parser / UI testing.
- Limitation:
  - The ResNet samples I inspected did **not** contain NCCL collectives.
- Direct use or conversion:
  - **Low value** for NCCLPol replay unless you collect your own distributed traces.

## What I Did Not Find

I searched for the following and did **not** find a strong, maintained public corpus:

- A reusable public dataset of long `NCCL_DEBUG=INFO` training logs with per-collective sequences and sizes.
- Public `.nsys-rep` / `.qdrep` corpora for modern distributed training jobs.
- MLPerf result repositories that publish raw NCCL / profiler traces as reusable artifacts.

There are scattered public logs and issue attachments on GitHub, but I did not find a high-quality public corpus that is better than the PARAM / HTA / Chakra-family sources above.

## Practical Recommendation For NCCLPol

If the goal is to get replay coverage quickly, I would use this sequence:

1. Start with **PARAM DLRM + ResNet** traces as the first public replay corpus.
2. Add **HTA `vision_transformer`** traces to cover `all_gather` and `reduce_scatter`.
3. Use **ASTRA-sim microbench ETs** for controlled sweeps over collective type, rank count, and size.
4. If you need broader realism, collect your own traces from public training code with **PyTorch Profiler / ET** or **PyTorch NCCL Flight Recorder**.

For a normalized NCCLPol replay format, I would store these fields:

- `trace_id`
- `rank`
- `world_size`
- `collective`
- `bytes`
- `timestamp_us` or sequence index
- `duration_us` if available
- `source_format`
- `source_path`

That schema is sufficient to ingest PARAM ET, Kineto profiler traces, HTA traces, Chakra ET, Flight Recorder dumps, and DeepSpeed comm logs with only small source-specific adapters.
