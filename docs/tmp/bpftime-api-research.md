# bpftime API Research

## Scope

Reviewed these primary bpftime sources:

- `bpftime/runtime/include/bpftime_prog.hpp`
- `bpftime/runtime/include/bpftime_helper_group.hpp`
- `bpftime/runtime/include/bpftime_shm.hpp`
- `bpftime/runtime/src/bpftime_shm_internal.cpp`
- `bpftime/bpftime-verifier/include/bpftime-verifier.hpp`
- `bpftime/bpftime-verifier/test/map.cpp`
- `bpftime/example/attach_implementation/controller/controller.cpp`
- `bpftime/runtime/unit-test/attach_with_ebpf/test_helpers.cpp`
- `bpftime/vm/vm-core/include/ebpf-vm.h`

## Key Findings

### 1. Program object / VM surface

- The executable program wrapper is `bpftime::bpftime_prog`.
- Constructor:
  - `bpftime_prog(const ebpf_inst *insn, size_t insn_cnt, const char *name)`
  - `bpftime_prog(const ebpf_inst *insn, size_t insn_cnt, const char *name, agent_config config)`
- Load and run:
  - `int bpftime_prog_load(bool jit)`
  - `int bpftime_prog_exec(void *memory, size_t memory_size, uint64_t *return_val) const`
- Helper registration:
  - `int bpftime_prog_register_raw_helper(struct bpftime_helper_info info)`

### 2. Helper registration

- High-level helper registration is centered on `bpftime::bpftime_helper_group`.
- Relevant built-in groups:
  - `get_kernel_utils_helper_group()`
  - `get_shm_maps_helper_group()`
  - `get_ufunc_helper_group()`
- Typical usage:
  1. create / fetch helper groups
  2. optionally `append(...)`
  3. `add_helper_group_to_prog(prog)`
- For verifier setup, helper IDs can be collected with `get_helper_ids()`.

### 3. Map creation

- Userspace maps are created programmatically with:
  - `bpftime_maps_create(int fd, const char *name, bpftime::bpf_map_attr attr)`
- Map attributes live in `bpftime::bpf_map_attr` from `bpftime_shm.hpp`.
- Map CRUD is available through:
  - `bpftime_map_lookup_elem`
  - `bpftime_map_update_elem`
  - `bpftime_map_delete_elem`
- With `ENABLE_EBPF_VERIFIER` enabled, `bpftime_shm::add_bpf_map()` also updates verifier map descriptors automatically.

### 4. ELF loading and map relocations

- There is not a single public bpftime API that:
  - opens an ELF object,
  - creates maps,
  - applies ELF map relocations,
  - registers helpers,
  - verifies,
  - and loads the final executable program
  in one step.
- `bpftime_object_open(...)` is useful for object parsing in some examples, but it does not solve the full standalone embedding workflow here.
- Practical standalone embedding flow is:
  1. open ELF with libbpf or `bpftime_object_open`
  2. inspect maps and programs
  3. create maps via `bpftime_maps_create`
  4. patch `lddw` map references using ELF relocations (`R_BPF_64_64`) to `BPF_PSEUDO_MAP_FD`
  5. register helper groups
  6. verify
  7. construct `bpftime_prog` from relocated `ebpf_inst[]`
  8. `bpftime_prog_load(true/false)`

### 5. Verifier API

- Userspace verifier surface:
  - `bpftime::verifier::verify_ebpf_program(const uint64_t *raw_inst, size_t num_inst, const std::string &section_name)`
  - `set_available_helpers(...)`
  - `set_non_kernel_helpers(...)`
  - `set_map_descriptors(...)`
- Map verifier metadata uses `BpftimeMapDescriptor`.
- The verifier accepts Linux-style section names for supported program types.
- In this tree, bpftime’s verifier front-end only dispatches sections beginning with:
  - `uprobe`
  - `uretprobe`
  - `tracepoint`

### 6. VM-core API

- `bpftime/vm/vm-core/include/ebpf-vm.h` exposes the low-level VM ABI (`ebpf_vm`, `ebpf_inst`, JIT and execution hooks).
- It is the lower layer under `bpftime_prog`.
- For this project, `bpftime_prog` was the better entry point because it kept JIT / execution handling and helper registration integrated, while map support still had to be added manually through relocation + runtime map APIs.

## Answers To The User’s Key Questions

### How to create maps?

Use `bpftime_maps_create()` with a filled `bpftime::bpf_map_attr`.

### How to load programs with map relocations?

There is no one-call embed API for this in bpftime. The workable route is manual:

1. open ELF
2. create maps
3. parse relocation sections
4. rewrite `lddw` map instructions to `BPF_PSEUDO_MAP_FD` + bpftime map fd
5. construct and load `bpftime_prog`

### How to register helpers?

Use `bpftime_helper_group` and add the relevant groups to the program. For this plugin, the required groups were:

- kernel utils
- shared-memory map helpers

## Example Files That Were Most Useful

- `bpftime/example/attach_implementation/controller/controller.cpp`
  - good minimal example of opening a program, patching map references, creating a program fd, and attaching
- `bpftime/runtime/unit-test/attach_with_ebpf/test_helpers.cpp`
  - good example of helper-group registration and `bpftime_prog_load` / `bpftime_prog_exec`
- `bpftime/bpftime-verifier/test/map.cpp`
  - good reference for verifier-side helper ID and map descriptor setup

## Bottom Line

bpftime provides the pieces needed for this NCCL policy plugin:

- maps
- helper registration
- JIT/runtime execution
- verifier hooks

But the embedder still has to assemble them. The critical missing convenience layer is automatic ELF map relocation for standalone programmatic loading.
