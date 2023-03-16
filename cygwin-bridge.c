//
// Cygwin bridge for Poderosa
//
// Copyright (c) 2023 Poderosa Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <utmp.h>
#include <wait.h>

#if defined(TRACE_FILE)
    #include <stdarg.h>
    #define TOSTR(F) #F
    #define XTOSTR(F) TOSTR(F)
    static void tracef(const char *format, ...) {
        va_list va;
        va_start(va, format);
        FILE *f = fopen(XTOSTR(TRACE_FILE), "a");
        if (f) {
            vfprintf(f, format, va);
            fclose(f);
        }
        va_end(va);
    }
    #define TRACE(...) tracef(__VA_ARGS__);
    #define TRACE_ERROR(PREFIX) TRACE("%s: %s\n", PREFIX, strerror(errno))
#else
    #define TRACE(...)
    #define TRACE_ERROR(PREFIX)
#endif

struct Options {
    int port;
    bool echo;
    bool utf8;
    short columns;
    short rows;
    const char **envs;
    const char **command;
};

void usage(void) {
    fputs(
        "usage: cygwin-bridge -p PORT [options] [--] [COMMAND ARGS...]\n"
        "  -p --port PORT            server port number\n"
        "Options\n"
        "  -e --echo                 echo on (default: echo off)\n"
        "  -h --help                 show help\n"
        "  -u --utf8                 pass UTF-8 (default: no)\n"
        "  -v --env NAME=VALUE       environment variable to add (one or more envs can be set)\n"
        "  -z --size COLxROW         size (default: not specified)\n"
        , stderr
    );
}

bool parse_size(const char *s, short *columns, short *rows) {
    char *ss = strdup(s);
    if (!ss) {
        TRACE_ERROR("parse_size")
        return false;
    }

    bool ret = false;

    char *sep = strchr(ss, 'x');
    if (!sep) {
        fputs("size must be specified as COLxROW\n", stderr);
        goto exit;
    }

    *sep = '\0';
    char *end;
    long col = strtol(ss, &end, 10);
    if (end == ss || *end != 0 || col <= 0) {
        fputs("invalid size (columns)\n", stderr);
        goto exit;
    }
    if (col > SHRT_MAX) {
        col = SHRT_MAX;
    }

    long row = strtol(sep + 1, &end, 10);
    if (end == sep + 1 || *end != 0 || row <= 0) {
        fputs("invalid size (rows)\n", stderr);
        goto exit;
    }
    if (row > SHRT_MAX) {
        row = SHRT_MAX;
    }

    *columns = (short)col;
    *rows = (short)row;
    ret = true;

exit:
    free(ss);
    return ret;
}

bool parse_args(int argc, const char**argv, struct Options *opt) {
    opt->port = -1;
    opt->command = malloc(sizeof(char *) * argc);
    for (int i = 0; i < argc; i++) {
        opt->command[i] = NULL;
    }
    int command_count = 0;

    opt->envs = malloc(sizeof(char *) * argc);
    for (int i = 0; i < argc; i++) {
        opt->envs[i] = NULL;
    }
    int env_count = 0;

    bool on_command = false;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (on_command) {
            opt->command[command_count++] = arg;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            on_command = true;
            continue;
        }
        if (strcmp(arg, "-e") == 0 || strcmp(arg, "--echo") == 0) {
            opt->echo = true;
            continue;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage();
            return false;
        }
        if (strcmp(arg, "-p") == 0 || strcmp(arg, "--port") == 0) {
            i++;
            if (i >= argc) {
                fputs("missing port number\n", stderr);
                return false;
            }
            arg = argv[i];
            char *end;
            long port = strtol(arg, &end, 10);
            if (end == arg || *end != 0 || port < 0 || port > 65535) {
                fputs("invalid port number\n", stderr);
                return false;
            }
            opt->port = (int)port;
            continue;
        }
        if (strcmp(arg, "-u") == 0 || strcmp(arg, "--utf8") == 0) {
            opt->utf8 = true;
            continue;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--env") == 0) {
            i++;
            if (i >= argc) {
                fputs("missing environment variable\n", stderr);
                return false;
            }
            arg = argv[i];
            const char *eq = strchr(arg, '=');
            if (!eq || eq == arg) {
                fputs("environment variable must be specified as NAME=VALUE\n", stderr);
                return false;
            }
            opt->envs[env_count++] = arg;
            continue;
        }
        if (strcmp(arg, "-z") == 0 || strcmp(arg, "--size") == 0) {
            i++;
            if (i >= argc) {
                fputs("missing size value\n", stderr);
                return false;
            }
            if (!parse_size(argv[i], &opt->columns, &opt->rows)) {
                return false;
            }
            continue;
        }
        if (arg[0] == '-') {
            usage();
            return false;
        }
        opt->command[command_count++] = arg;
        on_command = true;
    }

    if (opt->port < 0) {
        fputs("port number must be specified\n", stderr);
        return false;
    }

    return true;
}

bool ensure_std_fds(void) {
    int null_fd;
    do {
        null_fd = open("/dev/null", O_RDWR);
        if (null_fd == -1) {
            TRACE_ERROR("open /dev/null")
            return false;
        }
    } while (null_fd <= 2);

    close(null_fd);
    return true;
}

void terminate_child(pid_t pid) {
    kill(pid, SIGTERM);
    int status;
    waitpid(pid, &status, 0);
}

void child_proc(struct Options *opt) {
    for (int i = 0; opt->envs && opt->envs[i]; i++) {
        char *env = strdup(opt->envs[i]);
        if (env) {
            putenv(env);
            TRACE("putenv %s\n", opt->envs[i])
        }
    }

    struct termios attr = {0};
    tcgetattr(STDIN_FILENO, &attr);
    attr.c_cc[VERASE] = CTRL('H');
    attr.c_iflag |= IXANY | IMAXBEL;
    if (opt->utf8) {
        attr.c_iflag |= IUTF8;
    } else {
        attr.c_iflag &= ~IUTF8;
    }
    attr.c_lflag |= ICANON | ECHOE | ECHOK | ECHOCTL | ECHOKE;
    if (opt->echo) {
        attr.c_lflag |= ECHO;
    } else {
        attr.c_lflag &= ~ECHO;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &attr);

    const char **command =
        (opt->command && opt->command[0])
            ? opt->command
            : (const char *[]){"/bin/bash", "-l", "-i", NULL};
    execvp(command[0], (char **)command);
    TRACE_ERROR("execvp")
}

bool write_all(int fd, char *buff, size_t len) {
    size_t wlen = 0;
    while (wlen < len) {
        size_t w = write(fd, &buff[wlen], len - wlen);
        if (w == (size_t)-1) {
            TRACE_ERROR("write_all")
            break;
        }
        wlen += w;
    }
    return wlen == len;
}

int main(int argc, const char **argv) {
    // If this application was built as a non-console application,
    // either Windows or Cygwin do not provide the handles for
    // stdin, stdout, and stderr.
    //
    // In this case, when a new file is opened or a socket is created,
    // its handle value may be zero.
    // This can result in forkpty failing to set up tty settings
    // correctly for the child process.
    //
    // To prevent this issue, open /dev/null as the handle for
    // stdin, stdout, and stderr at the beginning.
    if (!ensure_std_fds()) {
        return 2;
    }

    struct Options opt = {0};
    if (!parse_args(argc, argv, &opt)) {
        return 1;
    }

    int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        TRACE_ERROR("socket")
        return 2;
    }
    TRACE("main: sock_fd = %d\n", sock_fd)
    if (sock_fd <= 2) {
        TRACE("main: sock_fd must be greater than 2\n")
        return 2;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = {
            .s_addr = inet_addr("127.0.0.1"),
        },
        .sin_port = htons(opt.port),
    };
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        TRACE_ERROR("connect")
        return 2;
    }

    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    int amaster = -1;
    const struct winsize *wsize =
        (opt.columns > 0 && opt.rows > 0)
            ? &(struct winsize) {
                .ws_col = opt.columns, 
                .ws_row = opt.rows,
            }
            : NULL;
   
    pid_t pid = forkpty(&amaster, NULL, NULL, wsize);
    if (pid == -1) {
        TRACE_ERROR("forkpty")
        return 2;
    }

    if (pid == 0) {
        // child process
        signal(SIGHUP, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        close(sock_fd);

        TRACE("child: stdin isatty %d\n", isatty(STDIN_FILENO))
        TRACE("child: stdout isatty %d\n", isatty(STDOUT_FILENO))
        TRACE("child: stderr isatty %d\n", isatty(STDERR_FILENO))

        child_proc(&opt);
        return 3; // failed to exec
    }

    // parent process
    const size_t buffSize = 1024 * 32;
    char *buff = malloc(buffSize);
    if (!buff) {
        TRACE_ERROR("main: malloc for buffer")
        return 2;
    }

    struct pollfd pollfds[] = {
        {
            .fd = amaster,
            .events = POLLIN,
        },
        {
            .fd = sock_fd,
            .events = POLLIN,
        },
    };

    for (;;) {
        int poll_ret = poll(pollfds, 2, -1);
        if (poll_ret == -1) {
            TRACE("main: poll failed\n")
            break;
        }
        if (pollfds[0].revents & ~POLLIN) {
            TRACE("main: tty error\n")
            break;
        }
        if (pollfds[1].revents & ~POLLIN) {
            TRACE("main: socket error\n")
            break;
        }
        if (pollfds[0].revents & POLLIN) {
            size_t rlen = read(amaster, buff, buffSize);
            if (rlen == 0 || rlen == (size_t)-1) {
                TRACE("main: tty read error (or closed)\n")
                break;
            }
            if (!write_all(sock_fd, buff, rlen)) {
                TRACE("main: socket write error\n")
                break;
            }
        }
        if (pollfds[1].revents & POLLIN) {
            size_t rlen = read(sock_fd, buff, buffSize);
            if (rlen == 0 || rlen == (size_t)-1) {
                TRACE("main: socket read error (or closed)\n")
                break;
            }
            if (!write_all(amaster, buff, rlen)) {
                TRACE("main: tty write error\n")
                break;
            }
        }
    }

    TRACE("main: terminate child\n")
    terminate_child(pid);

    shutdown(sock_fd, SHUT_RDWR);
    close(sock_fd);

    return 0;
}
