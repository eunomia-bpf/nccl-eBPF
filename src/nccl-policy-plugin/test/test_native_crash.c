#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <functional>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "nccl_tuner.h"
#include "policy_test_paths.h"

static int failf(const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputc('\n', stderr);
  return -1;
}

static void no_op_logger(ncclDebugLogLevel level, unsigned long flags,
                         const char *file, int line, const char *fmt, ...) {
  (void)level;
  (void)flags;
  (void)file;
  (void)line;
  (void)fmt;
}

static int read_fd_to_string(int fd, std::string *output) {
  char buffer[4096];
  ssize_t nread;

  output->clear();
  while ((nread = read(fd, buffer, sizeof(buffer))) > 0)
    output->append(buffer, (size_t)nread);
  if (nread < 0)
    return failf("read failed while capturing stderr: %s", strerror(errno));
  return 0;
}

static int capture_stderr(const std::function<void(void)> &fn,
                          std::string *captured) {
  int pipe_fds[2] = {-1, -1};
  int saved_stderr = -1;
  int rc = 0;

  if (pipe(pipe_fds) != 0)
    return failf("pipe failed while capturing stderr: %s", strerror(errno));

  saved_stderr = dup(STDERR_FILENO);
  if (saved_stderr < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return failf("dup failed while capturing stderr: %s", strerror(errno));
  }

  fflush(stderr);
  if (dup2(pipe_fds[1], STDERR_FILENO) < 0) {
    rc = failf("dup2 failed while capturing stderr: %s", strerror(errno));
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    close(saved_stderr);
    return rc;
  }
  close(pipe_fds[1]);
  pipe_fds[1] = -1;

  fn();

  fflush(stderr);
  if (dup2(saved_stderr, STDERR_FILENO) < 0)
    rc = failf("failed to restore stderr: %s", strerror(errno));
  close(saved_stderr);

  if (read_fd_to_string(pipe_fds[0], captured) != 0)
    rc = -1;
  close(pipe_fds[0]);
  return rc;
}

static std::string collapse_whitespace(const std::string &text) {
  std::string collapsed;
  int last_was_space = 1;

  for (char ch : text) {
    if (isspace((unsigned char)ch)) {
      if (!last_was_space) {
        collapsed.push_back(' ');
        last_was_space = 1;
      }
      continue;
    }
    collapsed.push_back(ch);
    last_was_space = 0;
  }

  if (!collapsed.empty() && collapsed.back() == ' ')
    collapsed.pop_back();
  return collapsed;
}

static std::string summarize_verifier_detail(const std::string &captured) {
  const std::string needle = "verifier rejected";
  size_t pos = captured.find(needle);
  std::string summary;

  if (pos == std::string::npos)
    return "-";

  summary = collapse_whitespace(captured.substr(pos));
  if (summary.size() > 200) {
    summary.resize(197);
    summary += "...";
  }
  return summary;
}

static int run_native_plugin_child(const char *native_plugin_path) {
  void *handle = NULL;
  const ncclTuner_v5_t *plugin = NULL;
  void *plugin_context = NULL;
  int n_channels = 1;

  handle = dlopen(native_plugin_path, RTLD_NOW | RTLD_LOCAL);
  if (!handle)
    return failf("dlopen failed for %s: %s", native_plugin_path, dlerror());

  plugin = (const ncclTuner_v5_t *)dlsym(handle, NCCL_TUNER_PLUGIN_SYMBOL);
  if (!plugin) {
    dlclose(handle);
    return failf("dlsym failed for %s: %s", NCCL_TUNER_PLUGIN_SYMBOL,
                 dlerror());
  }

  if (plugin->init(&plugin_context, 0, 8, 1, no_op_logger, NULL, NULL) !=
      ncclSuccess) {
    dlclose(handle);
    return failf("native crash plugin init failed");
  }

  (void)plugin->getCollInfo(plugin_context, ncclFuncAllReduce, 1024, 1, NULL, 0,
                            0, 0, &n_channels);
  plugin->finalize(plugin_context);
  dlclose(handle);
  return 0;
}

static int observe_native_crash(const char *native_plugin_path, int *signal_no) {
  pid_t pid = fork();
  int status = 0;

  if (pid < 0)
    return failf("fork failed: %s", strerror(errno));
  if (pid == 0) {
    const int child_rc = run_native_plugin_child(native_plugin_path);
    _exit(child_rc == 0 ? 0 : 2);
  }

  if (waitpid(pid, &status, 0) < 0)
    return failf("waitpid failed: %s", strerror(errno));

  if (WIFSIGNALED(status)) {
    *signal_no = WTERMSIG(status);
    return 0;
  }

  *signal_no = 0;
  return failf("native plugin exited without a signal (status=%d)", status);
}

static int probe_ebpf_rejection(const char *plugin_path,
                                const char *bad_policy_path,
                                int *accepted,
                                std::string *verifier_detail) {
  void *handle = NULL;
  const ncclTuner_v5_t *plugin = NULL;
  void *plugin_context = NULL;
  std::string captured;
  std::string dlopen_error;
  std::string dlsym_error;
  int init_rc = ncclInternalError;
  int dlopen_ok = 0;
  int dlsym_ok = 0;
  int init_called = 0;

  if (setenv("NCCL_POLICY_BPF_PATH", bad_policy_path, 1) != 0)
    return failf("setenv failed for NCCL_POLICY_BPF_PATH: %s", strerror(errno));
  if (setenv("NCCL_POLICY_VERIFY_MODE", "strict", 1) != 0)
    return failf("setenv failed for NCCL_POLICY_VERIFY_MODE: %s",
                 strerror(errno));

  if (capture_stderr(
          [&]() {
            handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
              dlopen_error = dlerror() ? dlerror() : "unknown";
              return;
            }
            dlopen_ok = 1;

            plugin = (const ncclTuner_v5_t *)dlsym(handle,
                                                   NCCL_TUNER_PLUGIN_SYMBOL);
            if (!plugin) {
              dlsym_error = dlerror() ? dlerror() : "unknown";
              return;
            }
            dlsym_ok = 1;

            init_rc = plugin->init(&plugin_context, 0, 8, 1, no_op_logger,
                                   NULL, NULL);
            init_called = 1;
            if (init_rc == ncclSuccess && plugin_context) {
              plugin->finalize(plugin_context);
              plugin_context = NULL;
            }

            dlclose(handle);
            handle = NULL;
          },
          &captured) != 0) {
    if (handle)
      dlclose(handle);
    return -1;
  }

  if (handle)
    dlclose(handle);
  if (!dlopen_ok)
    return failf("dlopen failed for %s: %s", plugin_path, dlopen_error.c_str());
  if (!dlsym_ok)
    return failf("dlsym failed for %s: %s", NCCL_TUNER_PLUGIN_SYMBOL,
                 dlsym_error.c_str());
  if (!init_called)
    return failf("plugin init was not attempted for bad_lookup");

  *accepted = init_rc == ncclSuccess;
  *verifier_detail = summarize_verifier_detail(captured);
  return 0;
}

int main(int argc, char **argv) {
  const char *native_plugin_path =
      argc > 1 ? argv[1] : NCCL_POLICY_TEST_NATIVE_CRASH_PLUGIN_PATH;
  const char *ebpf_plugin_path =
      argc > 2 ? argv[2] : NCCL_POLICY_TEST_PLUGIN_PATH;
  int signal_no = 0;
  int ebpf_accepted = 0;
  std::string verifier_detail = "-";

  if (observe_native_crash(native_plugin_path, &signal_no) != 0)
    return 1;
  if (probe_ebpf_rejection(ebpf_plugin_path,
                           NCCL_POLICY_TEST_BAD_LOOKUP_BPF_PATH,
                           &ebpf_accepted, &verifier_detail) != 0) {
    return 1;
  }
  if (signal_no != SIGSEGV)
    return failf("native plugin crashed with signal %d instead of SIGSEGV",
                 signal_no);
  if (ebpf_accepted)
    return failf("bad_lookup was unexpectedly accepted by the eBPF plugin");

  printf("native: CRASH (segfault)\n");
  printf("ebpf: REJECTED (verifier error: %s)\n", verifier_detail.c_str());
  return 0;
}
