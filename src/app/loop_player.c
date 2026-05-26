#include "loop_player.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define NAV_KEY_MAX_FDS 16

#ifndef KEY_VOLUMEDOWN
#define KEY_VOLUMEDOWN 114
#endif
#ifndef KEY_VOLUMEUP
#define KEY_VOLUMEUP 115
#endif

typedef enum {
    NAV_KEY_NONE = 0,
    NAV_KEY_PREV = -1,
    NAV_KEY_NEXT = 1,
} nav_key_action_t;

typedef struct {
    int fds[NAV_KEY_MAX_FDS];
    int count;
} nav_key_reader_t;

static int is_running(volatile sig_atomic_t *running) {
    return !running || *running;
}

static int wait_child_nonblocking(pid_t child, int *status) {
    pid_t r;
    do {
        r = waitpid(child, status, WNOHANG);
    } while (r < 0 && errno == EINTR);
    return r == child ? 1 : (r == 0 ? 0 : -1);
}

static int nav_key_reader_open(nav_key_reader_t *reader) {
    if (!reader) return 0;
    memset(reader, 0, sizeof(*reader));

    DIR *dir = opendir("/dev/input");
    if (!dir) return 0;

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        if (reader->count >= NAV_KEY_MAX_FDS) break;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            reader->fds[reader->count++] = fd;
        }
    }
    closedir(dir);
    return reader->count;
}

static void nav_key_reader_close(nav_key_reader_t *reader) {
    if (!reader) return;
    for (int i = 0; i < reader->count; ++i) {
        if (reader->fds[i] >= 0) close(reader->fds[i]);
        reader->fds[i] = -1;
    }
    reader->count = 0;
}

static nav_key_action_t nav_key_reader_poll(nav_key_reader_t *reader) {
    if (!reader) return NAV_KEY_NONE;
    for (int i = 0; i < reader->count; ++i) {
        for (;;) {
            struct input_event ev;
            ssize_t n = read(reader->fds[i], &ev, sizeof(ev));
            if (n == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_KEY && ev.value == 1) {
                    if (ev.code == KEY_VOLUMEUP) return NAV_KEY_NEXT;
                    if (ev.code == KEY_VOLUMEDOWN) return NAV_KEY_PREV;
                }
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break;
        }
    }
    return NAV_KEY_NONE;
}

static int wrap_page_index(int page, int total) {
    if (total <= 0) return 0;
    while (page < 0) page += total;
    return page % total;
}

static void set_loop_child_env(int page, int total, int manual_mode, const char *profile) {
    char value[32];
    snprintf(value, sizeof(value), "%d", page + 1);
    setenv(NAV_ENV_PAGE, value, 1);
    snprintf(value, sizeof(value), "%d", total);
    setenv(NAV_ENV_TOTAL, value, 1);
    setenv(NAV_ENV_MODE, manual_mode ? "MANUAL" : "AUTO", 1);
    setenv(NAV_ENV_PROFILE, profile && profile[0] ? profile : "DEFAULT", 1);
}

static int wait_default_only_child(pid_t child, int seconds,
                                   nav_key_reader_t *nav_reader,
                                   nav_key_action_t *nav_action,
                                   volatile sig_atomic_t *running) {
    int status = 0;
    int elapsed_ms = 0;
    int limit_ms = seconds > 0 ? seconds * 1000 : -1;
    if (nav_action) *nav_action = NAV_KEY_NONE;

    while (is_running(running) && (limit_ms < 0 || elapsed_ms < limit_ms)) {
        int done = wait_child_nonblocking(child, &status);
        if (done != 0) return done > 0 ? status : -1;
        nav_key_action_t action = nav_key_reader_poll(nav_reader);
        if (action != NAV_KEY_NONE) {
            if (nav_action) *nav_action = action;
            break;
        }
        usleep(100000);
        if (limit_ms > 0) elapsed_ms += 100;
    }

    if (!is_running(running)) {
        for (int i = 0; i < 20; ++i) {
            int done = wait_child_nonblocking(child, &status);
            if (done != 0) return done > 0 ? status : -1;
            usleep(100000);
        }
    }

    kill(child, SIGINT);
    for (int i = 0; i < 50; ++i) {
        int done = wait_child_nonblocking(child, &status);
        if (done != 0) return done > 0 ? status : -1;
        usleep(100000);
    }

    kill(child, SIGTERM);
    do {
        if (waitpid(child, &status, 0) == child) return status;
    } while (errno == EINTR);
    return -1;
}

int run_default_only_loop(const char *argv0, int rotate_main,
                          const demo_loop_t *loop,
                          volatile sig_atomic_t *running) {
    char self[PATH_MAX];
    if (!loop) loop = alldemo_customer_loop();
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        self[n] = '\0';
    } else {
        snprintf(self, sizeof(self), "%s", argv0 && argv0[0] ? argv0 : "./alldemo");
    }

    printf("alldemo %s reuses --only module pages. Ctrl+C to stop.\n", loop->name);

    nav_key_reader_t nav_reader = {0};
    int nav_count = rotate_main ? nav_key_reader_open(&nav_reader) : 0;
    if (rotate_main) {
        if (nav_count > 0) {
            (void)nav_key_reader_poll(&nav_reader);
            printf("navigation keys: KEY_VOLUMEUP=next, KEY_VOLUMEDOWN=previous; first key switches to manual mode.\n");
        } else {
            printf("navigation keys: no readable /dev/input/event* device, auto loop only.\n");
        }
    }

    int page = 0;
    int manual_mode = 0;
    int total_pages = loop->total_pages;
    while (is_running(running)) {
        int page_index = rotate_main ? wrap_page_index(page, total_pages) : 0;
        const char *module = loop->pages[page_index];
        pid_t child = fork();
        if (child < 0) {
            perror("fork --only page");
            nav_key_reader_close(&nav_reader);
            return 1;
        }
        if (child == 0) {
            set_loop_child_env(page_index, total_pages, manual_mode, loop->profile);
            execl(self, self, "--only", module, (char *)NULL);
            fprintf(stderr, "exec %s --only %s failed: %s\n", self, module, strerror(errno));
            _exit(127);
        }

        printf("%s page: --only %s (%s)\n", loop->name, module, manual_mode ? "manual" : "auto");
        nav_key_action_t nav_action = NAV_KEY_NONE;
        int page_seconds = rotate_main && !manual_mode ?
            loop_page_rotate_seconds(loop, module) : -1;
        (void)wait_default_only_child(child,
                                      page_seconds,
                                      rotate_main ? &nav_reader : NULL,
                                      &nav_action,
                                      running);
        if (!rotate_main) break;
        if (nav_action != NAV_KEY_NONE) {
            manual_mode = 1;
            page = page_index + (nav_action == NAV_KEY_NEXT ? 1 : -1);
        } else if (!manual_mode) {
            page = page_index + 1;
        } else {
            break;
        }
    }
    nav_key_reader_close(&nav_reader);
    return 0;
}
