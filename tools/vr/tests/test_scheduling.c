#define _GNU_SOURCE
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <linux/capability.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char *program_invocation_short_name;

static int realtime_unavailable(void) {
    struct rlimit priority;
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct caps[2] = {{0}};
    assert(getrlimit(RLIMIT_RTPRIO, &priority) == 0);
    assert(syscall(SYS_capget, &header, caps) == 0);
    return priority.rlim_cur == 0 &&
        !(caps[CAP_SYS_NICE / 32].effective & (UINT32_C(1) << (CAP_SYS_NICE % 32)));
}

static void check(int compositor) {
    program_invocation_short_name = compositor ? "vrcompositor" : "vrserver";
    struct rlimit before, after, low = {100000, 200000};
    assert(getrlimit(RLIMIT_RTTIME, &before) == 0);
    assert(before.rlim_cur > low.rlim_cur && before.rlim_max > low.rlim_max);
    const int blocked = compositor && realtime_unavailable();
    int result = setrlimit(RLIMIT_RTTIME, &low);
    if (blocked) {
        assert(result == -1 && errno == EPERM);
        assert(getrlimit(RLIMIT_RTTIME, &after) == 0);
        assert(after.rlim_cur == before.rlim_cur && after.rlim_max == before.rlim_max);
        assert(setrlimit(RLIMIT_RTTIME, &before) == 0);
    } else {
        assert(result == 0);
        assert(getrlimit(RLIMIT_RTTIME, &after) == 0);
        assert(after.rlim_cur == low.rlim_cur && after.rlim_max == low.rlim_max);
    }
    assert(getrlimit(RLIMIT_NOFILE, &before) == 0);
    before.rlim_cur = 64;
    assert(setrlimit(RLIMIT_NOFILE, &before) == 0);
    assert(getrlimit(RLIMIT_NOFILE, &after) == 0 && after.rlim_cur == 64);
    assert(setrlimit(-1, &before) == -1 && errno == EINVAL);
}

static void check_display_priority(int compositor, const char *name) {
    program_invocation_short_name = compositor ? "vrcompositor" : "vrserver";
    struct rlimit nice;
    assert(getrlimit(RLIMIT_NICE, &nice) == 0);
    const id_t tid = (id_t)syscall(SYS_gettid);
    const int before = getpriority(PRIO_PROCESS, tid);
    assert(pthread_setname_np(pthread_self(), name) == 0);
    const int after = getpriority(PRIO_PROCESS, tid);
    const int target = before < -8 ? before : -8;
    if (compositor && strcmp(name, "background") && realtime_unavailable() && nice.rlim_cur >= 28)
        assert(after == target);
    else assert(after == before);
    int policy;
    struct sched_param parameter;
    assert(pthread_getschedparam(pthread_self(), &policy, &parameter) == 0);
    assert(policy == SCHED_OTHER && parameter.sched_priority == 0);
    assert(pthread_setname_np(pthread_self(), "name-too-long-for-linux") == ERANGE);
    assert(getpriority(PRIO_PROCESS, tid) == after);
}

int main(void) {
    for (int compositor = 0; compositor <= 1; ++compositor) {
        pid_t child = fork();
        assert(child >= 0);
        if (!child) { check(compositor); _exit(0); }
        int status;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    const char *names[] = {"RenderThread", "SignalThread", "VulkanVblankThr", "XRFrameSender", "background"};
    for (int compositor = 0; compositor <= 1; ++compositor) for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        pid_t child = fork();
        assert(child >= 0);
        if (!child) { check_display_priority(compositor, names[i]); _exit(0); }
        int status;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    puts("SteamVR scheduling regression passed");
}
