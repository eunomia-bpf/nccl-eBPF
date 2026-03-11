# plugin.cpp 修改方案：支持 kernel-user shared map

## 背景与目标

当前 `plugin.cpp` 的 `create_bpftime_maps()` 函数将 BPF object 文件中的所有 map 都以纯用户态方式（普通类型）创建到 bpftime 的共享内存中。内核 eBPF 程序（如挂载到 tracepoint/kprobe 上、持续采集 CPU 状态的程序）写入的 pinned map（位于 `/sys/fs/bpf/`）对用户态 policy 完全不可见。

目标：让 NCCL policy eBPF 程序通过标准的 `bpf_map_lookup_elem()` helper（eBPF 指令层面的 map 访问），透明地读取内核 eBPF 程序写入的 pinned map，从而将内核侧采集的系统状态（CPU throttle、NIC 队列深度、内存压力等）直接输入到 NCCL 调度决策。

---

## 当前代码结构分析

### 1. Map 创建：`create_bpftime_maps()`（第 445-497 行）

```cpp
bool create_bpftime_maps(SharedCommState *shared,
                         SharedCommState::LoadedPolicyState *policy_state,
                         struct bpf_object *obj) {
  bpf_object__for_each_map(map, obj) {
    attr.type = static_cast<int>(bpf_map__type(map));   // 普通类型（BPF_MAP_TYPE_ARRAY 等）
    attr.key_size = bpf_map__key_size(map);
    attr.value_size = bpf_map__value_size(map);
    attr.max_ents = bpf_map__max_entries(map);
    // kernel_bpf_map_id 未被设置，默认 0
    fd = bpftime_maps_create(-1, runtime_name.c_str(), attr);
    policy_state->map_fds.emplace(logical_name, fd);
    // 同步写入 verifier_maps
  }
}
```

问题：
- `attr.type` 始终是 BPF ELF 文件中声明的原始类型（`BPF_MAP_TYPE_ARRAY` 等），不是 kernel-user 类型。
- `attr.kernel_bpf_map_id` 永远为 0，bpftime 内部不会创建 `array_map_kernel_user_impl`。

### 2. bpftime 的 kernel-user map 机制

bpftime 定义了一组特殊 map 类型（`bpftime_shm.hpp` 第 66-120 行）：

```cpp
#define KERNEL_USER_MAP_OFFSET 1000
BPF_MAP_TYPE_KERNEL_USER_ARRAY = KERNEL_USER_MAP_OFFSET + BPF_MAP_TYPE_ARRAY  // = 1002
BPF_MAP_TYPE_KERNEL_USER_HASH  = KERNEL_USER_MAP_OFFSET + BPF_MAP_TYPE_HASH   // = 1001
```

当 `bpftime_maps_create()` 使用这些类型时，`map_handler.cpp` 会实例化 `array_map_kernel_user_impl` 或 `hash_map_kernel_user_impl`，内部通过 `bpf_map_get_fd_by_id(kernel_map_id)` 获取内核 map fd，再用 mmap 做零拷贝读取。

关键的 `bpf_map_attr` 字段：
```cpp
struct bpf_map_attr {
    int type;                // 设为 BPF_MAP_TYPE_KERNEL_USER_ARRAY 等
    uint32_t kernel_bpf_map_id;  // 内核 map 的 ID（bpf_obj_get_info_by_fd 返回的 id）
    uint32_t key_size;
    uint32_t value_size;
    uint32_t max_ents;
    // ...
};
```

`bpftime_driver::bpftime_maps_create_server()` 展示了完整的从 kernel id 到 bpftime map 的创建流程：
1. `bpf_map_get_fd_by_id(kernel_id)` 获取 fd
2. `bpf_obj_get_info_by_fd()` 查询 key_size / value_size / max_entries / name
3. 将 `attr.type = info.type + KERNEL_USER_MAP_OFFSET`
4. 将 `attr.kernel_bpf_map_id = kernel_id`
5. 调用 `bpftime_maps_create(kernel_id, info.name, attr)`

### 3. Map 生命周期管理

`LoadedPolicyState::~LoadedPolicyState()` 对 `map_fds` 中所有 fd 调用 `bpftime_close()`。kernel-user map 在 bpftime 侧注册的 fd 也需要被关闭，但底层的内核 map 不会因此消失，是安全的。

### 4. 程序加载流程（调用链）

```
pluginInitImpl()
  -> ensure_policy_loaded()
    -> load_policy_state()
      -> load_program_from_object()
          -> create_bpftime_maps()     // [插入点1：此处增加 kernel-user map]
          -> relocate_program_maps()   // map fd 重定位到 eBPF 指令
          -> verify_program()
          -> bpftime_prog_load()
```

`relocate_program_maps()` 依赖 `policy_state->map_fds`（按 map 名索引）将 eBPF 指令中的 map 引用替换为 bpftime fd。因此，只要 kernel-user map 也以正确的逻辑名注册进 `map_fds`，重定位阶段会自动生效。

---

## 设计方案

### 核心思路

在 `create_bpftime_maps()` 之后（或内部）插入一个新阶段：**发现并注册 kernel pinned map**。

对于 BPF object 文件中声明的某些 map（通过名字或 map flag 标记），从环境变量指定的 pinned path 获取对应的内核 map id，然后用 `KERNEL_USER_MAP_TYPE` 覆盖在 bpftime 中重新创建（或替换已创建的 fd）。

### 方案 A：名字匹配 + 环境变量路径表（推荐）

#### 配置接口

```
NCCL_POLICY_KERNEL_MAPS=cpu_state_map:/sys/fs/bpf/nccl_cpu_state,nic_queue_map:/sys/fs/bpf/nccl_nic_queue
```

格式为 `map名:pinned路径` 的逗号分隔列表。若某个名字的 map 同时在 BPF object 文件中存在，则将其替换为 kernel-user 版本；若不在 object 中，则作为额外 map 注入（供 policy 程序通过 map fd 访问，但需要 eBPF 程序配合）。

#### 新增函数

**1. 解析环境变量**

```cpp
// 返回 map_name -> pinned_path 的映射
std::unordered_map<std::string, std::string>
parse_kernel_map_env(const char *env_value);
```

- 解析 `NCCL_POLICY_KERNEL_MAPS` 的值
- 忽略格式错误的条目（只打印警告）

**2. 从 pinned path 获取内核 map 信息**

```cpp
struct KernelMapInfo {
    int kernel_map_id;   // 内核 map id（用于 bpf_map_get_fd_by_id）
    uint32_t type;       // 原始内核 map 类型
    uint32_t key_size;
    uint32_t value_size;
    uint32_t max_entries;
    char name[BPF_OBJ_NAME_LEN];
};

// 返回 false 表示 pinned path 不存在或无权限，优雅降级
bool probe_kernel_pinned_map(SharedCommState *shared,
                             const char *pinned_path,
                             KernelMapInfo *out_info);
```

实现：
```cpp
bool probe_kernel_pinned_map(SharedCommState *shared,
                             const char *pinned_path,
                             KernelMapInfo *out_info) {
    int fd = bpf_obj_get(pinned_path);   // libbpf API
    if (fd < 0) {
        log_plugin_message(shared->log_function, NCCL_TUNING, NCCL_LOG_WARN,
                           "kernel map not found at %s, skipping", pinned_path);
        return false;
    }
    bpf_map_info info = {};
    unsigned int info_len = sizeof(info);
    if (bpf_obj_get_info_by_fd(fd, &info, &info_len) < 0) {
        close(fd);
        return false;
    }
    out_info->kernel_map_id = info.id;
    out_info->type = info.type;
    out_info->key_size = info.key_size;
    out_info->value_size = info.value_size;
    out_info->max_entries = info.max_entries;
    memcpy(out_info->name, info.name, BPF_OBJ_NAME_LEN);
    close(fd);
    return true;
}
```

注意：`bpf_obj_get()` 返回的 fd 仅用于查询 id，随即关闭。bpftime 内部会在需要时再次调用 `bpf_map_get_fd_by_id(kernel_map_id)` 并持有。

**3. 注册 kernel-user map 到 bpftime**

```cpp
// 在 bpftime shm 中注册 kernel-user map，返回 bpftime fd
// 若该 fd 名已存在于 policy_state->map_fds，则先 bpftime_close 旧 fd 再替换
int register_kernel_user_map(SharedCommState *shared,
                             SharedCommState::LoadedPolicyState *policy_state,
                             const std::string &logical_name,
                             const std::string &runtime_name_prefix,
                             const KernelMapInfo &km_info);
```

实现：
```cpp
int register_kernel_user_map(...) {
    bpftime::bpf_map_attr attr = {};
    attr.type = km_info.type + KERNEL_USER_MAP_OFFSET;  // 关键转换
    attr.kernel_bpf_map_id = km_info.kernel_map_id;
    attr.key_size = km_info.key_size;
    attr.value_size = km_info.value_size;
    attr.max_ents = km_info.max_entries;

    const std::string runtime_name = runtime_name_prefix + logical_name;

    // 若该名字的 map 已存在（普通类型），先关闭旧 fd
    auto existing_it = policy_state->map_fds.find(logical_name);
    if (existing_it != policy_state->map_fds.end()) {
        bpftime_close(existing_it->second);
        policy_state->map_fds.erase(existing_it);
        policy_state->verifier_maps.erase(existing_it->second);
    }

    int fd = bpftime_maps_create(-1, runtime_name.c_str(), attr);
    if (fd < 0) {
        log_plugin_message(..., "failed to create kernel-user map %s", ...);
        return -1;
    }

    policy_state->map_fds.emplace(logical_name, fd);
    // 向 verifier 注册时，仍使用原始类型（verifier 不理解 kernel-user 偏移）
    policy_state->verifier_maps.emplace(
        fd, bpftime::verifier::BpftimeMapDescriptor{
            .original_fd = fd,
            .type = km_info.type,    // 原始类型，非 +1000
            .key_size = km_info.key_size,
            .value_size = km_info.value_size,
            .max_entries = km_info.max_entries,
            .inner_map_fd = static_cast<unsigned int>(-1),
        });
    return fd;
}
```

**4. 新的驱动函数**

```cpp
void attach_kernel_maps(SharedCommState *shared,
                        SharedCommState::LoadedPolicyState *policy_state,
                        const std::string &map_namespace);
```

在 `create_bpftime_maps()` 调用之后、`relocate_program_maps()` 之前调用。

实现：
```cpp
void attach_kernel_maps(SharedCommState *shared,
                        SharedCommState::LoadedPolicyState *policy_state,
                        const std::string &map_namespace) {
    const char *env = getenv("NCCL_POLICY_KERNEL_MAPS");
    if (!env || env[0] == '\0')
        return;

    auto kernel_map_specs = parse_kernel_map_env(env);
    for (const auto &[map_name, pinned_path] : kernel_map_specs) {
        KernelMapInfo km_info = {};
        if (!probe_kernel_pinned_map(shared, pinned_path.c_str(), &km_info)) {
            // 内核 map 不存在时优雅降级：保留原有用户态 map（若存在）
            log_plugin_message(shared->log_function, NCCL_TUNING, NCCL_LOG_WARN,
                               "kernel map %s unavailable, falling back to "
                               "userspace map", map_name.c_str());
            continue;
        }
        if (register_kernel_user_map(shared, policy_state, map_name,
                                     map_namespace, km_info) < 0) {
            log_plugin_message(shared->log_function, NCCL_TUNING, NCCL_LOG_WARN,
                               "failed to register kernel map %s",
                               map_name.c_str());
        } else {
            log_plugin_message(shared->log_function, NCCL_TUNING, NCCL_LOG_INFO,
                               "attached kernel map %s (id=%d, path=%s)",
                               map_name.c_str(), km_info.kernel_map_id,
                               pinned_path.c_str());
        }
    }
}
```

#### 修改 `load_program_from_object()`

在 `create_bpftime_maps()` 和 `relocate_program_maps()` 之间插入一行：

```cpp
bool load_program_from_object(...) {
    // ...
    if (!create_bpftime_maps(shared, policy_state, obj.get()))   // 已有
        return false;
    attach_kernel_maps(shared, policy_state, map_namespace);     // [新增]
    if (!relocate_program_maps(shared, path, policy_state, &spec))  // 已有
        return false;
    // ...
}
```

注意：`map_namespace` 目前在 `create_bpftime_maps()` 内部构造，需要将其提取到 `load_program_from_object()` 层面共享，或在 `attach_kernel_maps()` 中独立生成（可用 `policy_state` 指针地址作区分）。

---

### 方案 B：单一 map 路径（简化版）

若场景固定为"只有一个 CPU 状态 map"，可以更简单：

```
NCCL_POLICY_KERNEL_MAP_PATH=/sys/fs/bpf/nccl_cpu_state
NCCL_POLICY_KERNEL_MAP_NAME=cpu_state_map   # 对应 BPF object 中的 map 名
```

相比方案 A，实现更简单但扩展性差。建议跳过此方案，直接实现方案 A（方案 A 的多 map 解析本质上只是循环）。

---

## 数据流图

```
内核侧（kprobe/tracepoint eBPF 程序）
  |
  | bpf_map_update_elem()
  v
内核 BPF map（pinned at /sys/fs/bpf/nccl_cpu_state）
  |
  | bpf_obj_get() + bpf_obj_get_info_by_fd()    <- probe_kernel_pinned_map()
  | 获取 kernel_map_id
  v
bpftime shm 中的 array_map_kernel_user_impl
  |   内部持有 map_fd = bpf_map_get_fd_by_id(kernel_map_id)
  |   elem_lookup 通过 mmap 零拷贝读取内核 map 内容
  v
bpftime prog helper: bpf_map_lookup_elem(fd, key)
  |   fd 是 bpftime 侧的虚拟 fd
  |   runtime 通过 fd -> map_handler -> array_map_kernel_user_impl
  v
NCCL policy eBPF 程序（运行在 bpftime LLVM JIT 中）
  |
  | 读取内核写入的 CPU 状态
  v
nccl_policy_action（channels / algo / proto 决策）
```

---

## 修改文件和位置汇总

| 文件 | 修改位置 | 类型 |
|------|----------|------|
| `plugin.cpp` | 第 445-497 行 `create_bpftime_maps()` 之后 | 新增调用 `attach_kernel_maps()` |
| `plugin.cpp` | 第 795-850 行 `load_program_from_object()` | 在 `create_bpftime_maps` 和 `relocate_program_maps` 之间插入 |
| `plugin.cpp` | 全局命名空间内新增函数 | 新增 4 个函数（约 120 行）|

不需要修改 bpftime 库本身（`bpftime_maps_create` API 已完全支持）。不需要修改 eBPF policy 程序（map 访问通过名字重定位，透明替换）。

---

## 关键 API 依赖

```cpp
// libbpf（已在 plugin.cpp 中 include <bpf/libbpf.h>）
int bpf_obj_get(const char *pathname);         // 从 pinned path 获取 fd
int bpf_obj_get_info_by_fd(int fd, void *info, unsigned int *info_len);

// bpftime（已在 plugin.cpp 中 include "bpftime_shm.hpp"）
int bpftime_maps_create(int fd, const char *name, bpftime::bpf_map_attr attr);
// attr.type = BPF_MAP_TYPE_KERNEL_USER_ARRAY 或 BPF_MAP_TYPE_KERNEL_USER_HASH
// attr.kernel_bpf_map_id = 内核 map id
```

`bpf_map_attr::type` 的转换规则（来自 `bpftime_shm.hpp`）：
- `BPF_MAP_TYPE_ARRAY` (2) -> `2 + 1000 = 1002 = BPF_MAP_TYPE_KERNEL_USER_ARRAY`
- `BPF_MAP_TYPE_HASH` (1)  -> `1 + 1000 = 1001 = BPF_MAP_TYPE_KERNEL_USER_HASH`

当前 bpftime 只支持 `KERNEL_USER_ARRAY`, `KERNEL_USER_HASH`, `KERNEL_USER_PERCPU_ARRAY`, `KERNEL_USER_PERF_EVENT_ARRAY`。若内核 map 类型不在此列，`bpftime_maps_create()` 会返回错误，`probe_kernel_pinned_map` 需要检查并给出清晰日志。

---

## verifier 处理注意事项

`verify_program()` 使用 `policy_state->verifier_maps` 中的类型信息对 eBPF 字节码做安全验证。注册 kernel-user map 时，向 verifier 传递的应是**原始内核类型**（不加 1000 偏移），否则 PREVAIL verifier 不认识这个类型，会拒绝验证。

```cpp
// 正确：verifier 使用原始类型
.type = km_info.type,   // BPF_MAP_TYPE_ARRAY = 2

// 错误：verifier 不认识 1002
.type = km_info.type + KERNEL_USER_MAP_OFFSET,
```

---

## 错误处理和降级策略

| 情形 | 行为 |
|------|------|
| `NCCL_POLICY_KERNEL_MAPS` 未设置 | `attach_kernel_maps()` 直接返回，无副作用 |
| pinned path 不存在（`bpf_obj_get` 返回 -ENOENT） | 打印 WARN，跳过该 map，保留 object 文件中原有的用户态 map |
| 内核 map 类型不支持 kernel-user | 打印 WARN，跳过该 map，保留原有用户态 map |
| `bpftime_maps_create` 失败 | 打印 WARN，跳过，保留原有用户态 map（不触发整体 policy 加载失败） |
| 内核 map key_size/value_size 与 object 文件声明不匹配 | 打印 WARN（policy 程序可能读到错误数据，但不崩溃）；可选增加断言 |

---

## 热重载支持

`pluginReloadPolicyImpl()` 调用 `load_policy_state()` 重新走完整个加载链。`attach_kernel_maps()` 在每次 `load_program_from_object()` 时都会重新探测内核 map，因此：
- 若内核 map 在热重载时已经存在，新的 policy 会绑定新的 fd（内核 map id 不变，但 bpftime fd 是新的）
- 旧的 `LoadedPolicyState` 析构时会 `bpftime_close()` 旧 fd，不泄漏

---

## 预估工作量

| 任务 | 估计行数 | 工作量 |
|------|----------|--------|
| `parse_kernel_map_env()` | ~30 行 | 0.5h |
| `probe_kernel_pinned_map()` | ~30 行 | 0.5h |
| `register_kernel_user_map()` | ~40 行 | 1h（需处理 verifier_maps 中类型） |
| `attach_kernel_maps()` | ~25 行 | 0.5h |
| `load_program_from_object()` 修改 | ~5 行 | 0.25h |
| 单元/集成测试 | ~100 行 | 2h |
| **合计** | **~230 行** | **~4.75h** |

额外工作（可选）：
- 编写配套内核侧测试程序（将 `/proc/stat` 解析结果写入 pinned map），约 1-2h
- 修改 eBPF policy 示例使用 cpu_state_map 的数据，约 0.5h
