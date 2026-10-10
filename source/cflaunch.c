/*
 * Cover Flow launcher - a resident payload that starts games for Cover Flow.
 *
 * Based on PS5_Vulkan's ps5vkctl control payload (Copyright (C) 2026 Mihawk-99),
 * with one command added for Cover Flow, and its own port (9112) and log:
 *
 *   switch <FROM> <TO> [return]
 *                        answer at once, wait for <FROM> (Cover Flow, which
 *                        cannot start another title from inside its sandbox)
 *                        to quit, then start <TO>; with "return", start <FROM>
 *                        again once <TO> has closed and the console is idle
 *   update-check <owner/repo>
 *                        the latest GitHub release of that repository:
 *                        "ok <tag>" when it has SHA256SUMS and eboot.bin, or "err ..."
 *   update-apply <owner/repo> <TITLEID>
 *                        download every file SHA256SUMS lists (a path in the
 *                        title folder; its release asset is named with "__"
 *                        for each "/"), check each against SHA256SUMS, and
 *                        only when all match put them in /data/homebrew/<TITLEID>/:
 *                        "ok <tag> <files>" or "err ..."
 *   list                 the installed titles, read from the console's app
 *                        metadata (which Cover Flow's sandbox cannot see):
 *                        "ok list", then
 *                        "ID<TAB>0<TAB>MTIME<TAB>NAME<TAB>KIND<TAB>CONTENTID"
 *                        a title (KIND: where it is served from, game or
 *                        homebrew), then "end"; sizes come from "size"
 *   art <ID>             "ok <bytes>" and the title's background art (pic1/pic0 PNG
 *                        or JPG from its metadata), or "err ..."
 *   playtime             "ok playtime", "ID<TAB>SECONDS<TAB>LAST<TAB>COUNT" a title
 *                        started from Cover Flow, then "end"
 *   storage              "ok <free bytes> <total bytes>" of the console's storage
 *   backup <TITLEID>     copy Cover Flow's settings, games.txt and the play times
 *                        to <USB>/CoverFlowBackup/: "ok <folder> <files>"
 *   restore <TITLEID>    copy them back from the first USB drive that has them
 *   log                  "ok log", the last lines of this launcher's log, "end"
 *   size <ID>            "ok <bytes>": the largest of the title's install
 *                        places, measured through links and mounts
 *   where <ID>           the places a title has, their sizes and first entries
 *   icon <ID>            "ok <bytes>" and that title's icon0.png, or "err ..."
 *
 * Everything is read-only and answered to this console's own processes and
 * network; nothing is changed on the console.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The original description follows.
 *
 * Every probe battery costs one launch of the runner title, and until this
 * payload existed each of them cost a hand on the console as well: a title
 * cannot launch itself (the system refuses to launch an app that is already
 * running) and closing one is not something the title can be trusted to do after
 * a GPU fault. So the console keeps an agent instead. This payload stays
 * resident once it is loaded and answers one command per connection on port
 * 9111:
 *
 *   ping              is the agent there, and which process is it
 *   status            which application the console is running, and its title
 *   users             what the user service answers: init, foreground, logged-in
 *   procs             the pids of that application's processes
 *   launch <TITLEID>  start a title (sceLncUtilLaunchApp)
 *   kill <TITLEID>    close a title: suspend, SIGKILL its processes, then
 *                     sceLncUtilKillApp, which is the sequence
 *                     ps5-payload-manager's process manager uses
 *   restart <TITLEID> close it if it is the running title, then start it again
 *   quit              leave the console
 *
 * The launch and kill calls are the ones AirPSX (ApplicationRunHandler) and
 * ps5-payload-manager (ps5_launcher.c) already make from a payload on a
 * jailbroken console; this agent makes them reachable without a dashboard.
 * Closing names a title id and refuses to act unless the console's running
 * application really is that title, so a typo cannot close the wrong
 * application, and PPSA99999 -- the default profile this project never launches
 * for a run -- is refused outright.
 *
 * Build with tools/build-ps5vkctl.sh, which needs PS5_PAYLOAD_SDK, and load
 * build/ps5vkctl/ps5vkctl.elf once per console boot with whatever payload
 * loader the console runs. tools/ps5_console.py is the client; docs/DEPLOYMENT.md
 * describes the loop it serves.
 */

#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>
#include <sys/mount.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>

/* The agent's port, the longest command it answers, and its own log. */
#define PS5VKCTL_PORT 9112
#define PS5VKCTL_LINE 256
#define PS5VKCTL_LOG "/data/cflaunch.log"
#define PS5VKCTL_MAX_PIDS 16
#define PS5VKCTL_TITLE 9

/* libSceSystemService reports these for a title that is already up, and for no
 * application at all. */
#define PS5VKCTL_ALREADY_RUNNING 0x8094000cu
#define PS5VKCTL_NO_APP 0xffffffffu

/* The default profile: never launched, and never closed, by a run. */
static const char *const refused_title = "PPSA99999";

/* What sceUserServiceInitialize returned at startup, reported by "users". */
static int g_user_init;

/* The console's own record of a process, as sceKernelGetAppInfo fills it (the
 * layout ps5-payload-manager's process manager reads). */
typedef struct app_info
{
    uint32_t app_id;
    uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
} app_info_t;

/* The launch parameter etaHEN's launcher passes, field for field. */
typedef struct lnc_app_param
{
    uint32_t size;
    int user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint64_t check_flag;
} lnc_app_param_t;

/* The user ids the console has logged in, -1 for an empty slot (the layout
 * AirPSX reads). */
typedef struct login_user_id_list
{
    int user_id[4];
} login_user_id_list_t;

/* libSceSystemService, through the payload SDK's stub library. */
extern uint32_t sceLncUtilGetAppIdOfRunningBigApp(void);
extern int sceLncUtilGetAppTitleId(uint32_t app_id, char *title_id);
extern int sceLncUtilSuspendApp(uint32_t app_id);
extern uint32_t sceLncUtilKillApp(uint32_t app_id);
extern int sceLncUtilLaunchApp(const char *title_id, const char *argv[], lnc_app_param_t *param);
extern int sceSystemServiceLaunchApp(const char *title_id, const char *argv[],
                                     lnc_app_param_t *param);
/* libSceUserService. The service wants initializing before its getters answer,
 * and a launch needs the user it runs as. */
extern int sceUserServiceInitialize(void *param);
extern int sceUserServiceGetForegroundUser(int *user_id);
extern int sceUserServiceGetLoginUserIdList(login_user_id_list_t *list);
/* libkernel. */
extern int sceKernelGetAppInfo(int pid, app_info_t *info);

// Log one line to stdout (the console's klog carries a payload's output) and to
// the log file the FTP server can hand back.
static void log_line(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void log_line(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    vprintf(format, arguments);
    printf("\n");
    fflush(stdout);
    FILE *const file = fopen(PS5VKCTL_LOG, "a");
    if (file != NULL)
    {
        vfprintf(file, format, copy);
        fputc('\n', file);
        fclose(file);
    }
    va_end(copy);
    va_end(arguments);
}

// Answer one line, newline terminated.
static void reply(int client, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void reply(int client, const char *format, ...)
{
    char line[PS5VKCTL_LINE];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    const size_t length = strlen(line);
    if (send(client, line, length, 0) < 0 || send(client, "\n", 1, 0) < 0)
        log_line("ps5vkctl: reply failed: %s", strerror(errno));
}

// The application the console is running, with its title id ("" when the id
// cannot be read). PS5VKCTL_NO_APP when nothing is running.
static uint32_t running_app(char *title, size_t size)
{
    title[0] = '\0';
    const uint32_t app_id = sceLncUtilGetAppIdOfRunningBigApp();
    if (app_id == PS5VKCTL_NO_APP || app_id == 0)
        return PS5VKCTL_NO_APP;
    char buffer[32] = {0};
    if (sceLncUtilGetAppTitleId(app_id, buffer) != 0)
        buffer[0] = '\0';
    snprintf(title, size, "%s", buffer);
    return app_id;
}

// The pids of the processes that belong to one application, this agent's own
// process excluded.
static int app_pids(uint32_t app_id, int *pids, int capacity)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t bytes = 0;
    if (sysctl(mib, 4, NULL, &bytes, NULL, 0) != 0 || bytes == 0)
        return 0;
    void *const buffer = malloc(bytes);
    if (buffer == NULL)
        return 0;
    int count = 0;
    if (sysctl(mib, 4, buffer, &bytes, NULL, 0) == 0)
    {
        for (const char *cursor = buffer;
             cursor + sizeof(struct kinfo_proc) <= (const char *)buffer + bytes &&
             count < capacity;)
        {
            const struct kinfo_proc *const process = (const struct kinfo_proc *)cursor;
            if (process->ki_structsize <= 0 ||
                (size_t)process->ki_structsize < sizeof(struct kinfo_proc))
                break;
            cursor += process->ki_structsize;
            if (process->ki_pid == getpid())
                continue;
            app_info_t info;
            memset(&info, 0, sizeof(info));
            if (sceKernelGetAppInfo(process->ki_pid, &info) != 0)
                continue;
            if (info.app_id == app_id)
                pids[count++] = process->ki_pid;
        }
    }
    free(buffer);
    return count;
}

// Close a title. Returns 0 when it is gone, 1 when it was not running, and -1
// when it could not be closed or is not the running application.
static int close_title(const char *title, char *detail, size_t size)
{
    char running[32] = {0};
    const uint32_t app_id = running_app(running, sizeof(running));
    if (app_id == PS5VKCTL_NO_APP)
    {
        snprintf(detail, size, "no application is running");
        return 1;
    }
    if (strncmp(running, title, PS5VKCTL_TITLE) != 0)
    {
        snprintf(detail, size, "the running application is %s", running);
        return -1;
    }
    log_line("ps5vkctl: closing %s (app id %u): suspend, then SIGKILL", title, app_id);
    (void)sceLncUtilSuspendApp(app_id);
    sleep(2);
    int pids[PS5VKCTL_MAX_PIDS] = {0};
    const int count = app_pids(app_id, pids, PS5VKCTL_MAX_PIDS);
    for (int index = 0; index < count; index++)
        if (kill(pids[index], SIGKILL) == 0)
            log_line("ps5vkctl: SIGKILL to pid %d", pids[index]);
    const uint32_t result = sceLncUtilKillApp(app_id);
    log_line("ps5vkctl: sceLncUtilKillApp(%u) -> 0x%08x", app_id, result);
    for (int wait = 0; wait < 50; wait++)
    {
        char after[32] = {0};
        if (running_app(after, sizeof(after)) == PS5VKCTL_NO_APP)
        {
            snprintf(detail, size, "%s is gone (app id %u, %d processes killed, result 0x%08x)",
                     title, app_id, count, result);
            return 0;
        }
        usleep(100000);
    }
    snprintf(detail, size, "%s is still running (app id %u, result 0x%08x)", title, app_id, result);
    return -1;
}

// The foreground user's id, or -1 when the console does not name one.
static int foreground_user(void)
{
    int user_id = -1;
    if (sceUserServiceGetForegroundUser(&user_id) != 0)
        return -1;
    return user_id;
}

// The first logged-in user's id, or -1 when nobody is logged in.
static int login_user(void)
{
    login_user_id_list_t users;
    memset(&users, 0, sizeof(users));
    if (sceUserServiceGetLoginUserIdList(&users) != 0)
        return -1;
    for (int index = 0; index < 4; index++)
        if (users.user_id[index] >= 0)
            return users.user_id[index];
    return -1;
}

// One way of asking the console to start a title, and the user it runs as.
typedef struct launch_attempt
{
    const char *name;
    int (*call)(const char *title_id, const char *argv[], lnc_app_param_t *param);
    int user_id;
    uint64_t check_flag;
} launch_attempt_t;

// Start a title. A payload's launch call needs a user id and the right entry
// point, and which of them the console accepts is not worth guessing: AirPSX
// launches through sceSystemServiceLaunchApp with a logged-in user and etaHEN
// through sceLncUtilLaunchApp with the foreground one, so try both users through
// both calls -- and the skip-check flag last -- and report what each returned.
// Returns 0 when the title is up or already was, with the winning attempt and
// its result in *name and *code.
static int start_title(const char *title, const char **name, uint32_t *code, char *detail,
                       size_t size)
{
    const int foreground = foreground_user();
    const int login = login_user();
    char report[384] = {0};
    const launch_attempt_t attempts[] = {
        {"lncUtilApp/foreground", sceLncUtilLaunchApp, foreground, 0},
        {"lncUtilApp/login", sceLncUtilLaunchApp, login, 0},
        {"systemServiceApp/login", sceSystemServiceLaunchApp, login, 0},
        {"lncUtilApp/login+skipCheck", sceLncUtilLaunchApp, login, 1},
    };
    for (size_t index = 0; index < sizeof(attempts) / sizeof(attempts[0]); index++)
    {
        const launch_attempt_t *const attempt = &attempts[index];
        if (attempt->user_id < 0)
            continue;
        lnc_app_param_t param;
        memset(&param, 0, sizeof(param));
        param.size = (uint32_t)sizeof(param);
        param.user_id = attempt->user_id;
        param.check_flag = attempt->check_flag;
        *code = (uint32_t)attempt->call(title, NULL, &param);
        log_line("ps5vkctl: launch %s as %s user %d -> 0x%08x", title, attempt->name,
                 attempt->user_id, *code);
        char entry[96] = {0};
        snprintf(entry, sizeof(entry), "%s%s(user %d)=0x%08x", report[0] == '\0' ? "" : ", ",
                 attempt->name, attempt->user_id, *code);
        strncat(report, entry, sizeof(report) - strlen(report) - 1);
        // The console decides, not the return code: a launch that has started
        // the title reads as success even when the call reports something else
        // (a console that already had the title coming up answers the next
        // attempt with "already running"), so ask which application is running
        // before trying another way of asking for it.
        char running[32] = {0};
        bool started = false;
        for (int wait = 0; wait < 20 && !started; wait++)
        {
            started = running_app(running, sizeof(running)) != PS5VKCTL_NO_APP &&
                      strncmp(running, title, PS5VKCTL_TITLE) == 0;
            if (!started)
                usleep(250000);
        }
        if (*code == 0 || *code == PS5VKCTL_ALREADY_RUNNING || started)
        {
            *name = attempt->name;
            snprintf(detail, size, "via %s as user %d%s", attempt->name, attempt->user_id,
                     started && *code != 0 && *code != PS5VKCTL_ALREADY_RUNNING
                         ? " (the console reports the title running)"
                         : "");
            return 0;
        }
    }
    *name = NULL;
    snprintf(detail, size, "foreground user %d, login user %d, attempts: %s", foreground, login,
             report[0] == '\0' ? "none (no user id to launch as)" : report);
    return -1;
}

// A title id is nine characters, as the console's own routes require.
static int valid_title(const char *title)
{
    return title != NULL && strlen(title) == PS5VKCTL_TITLE;
}


// ----------------------------------------------------------------- Cover Flow

static const char *const meta_roots[] = {"/user/appmeta", "/system_data/priv/appmeta"};

static int send_all(int client, const void *data, size_t size)
{
    const char *cursor = data;
    while (size > 0)
    {
        const ssize_t sent = send(client, cursor, size, 0);
        if (sent <= 0)
            return -1;
        cursor += sent;
        size -= (size_t)sent;
    }
    return 0;
}

// A whole small file (at most 'limit' bytes) into a malloc'd buffer
static char *read_file(const char *path, size_t limit, size_t *size)
{
    const int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > limit)
    {
        close(fd);
        return NULL;
    }
    char *const data = malloc((size_t)st.st_size + 1);
    size_t used = 0;
    while (data != NULL && used < (size_t)st.st_size)
    {
        const ssize_t got = read(fd, data + used, (size_t)st.st_size - used);
        if (got <= 0)
            break;
        used += (size_t)got;
    }
    close(fd);
    if (data == NULL || used != (size_t)st.st_size)
    {
        free(data);
        return NULL;
    }
    data[used] = '\0';
    *size = used;
    return data;
}

// The string value of "key" at or after 'from' in a JSON text, into out
static int json_string(const char *from, const char *key, char *out, size_t size)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *at = strstr(from, pattern);
    if (at == NULL)
        return -1;
    at = strchr(at + strlen(pattern), ':');
    if (at == NULL)
        return -1;
    at = strchr(at, '"');
    if (at == NULL)
        return -1;
    size_t used = 0;
    for (at++; *at != '\0' && *at != '"' && used + 1 < size; at++)
    {
        char c = *at;
        if (c == '\\' && at[1] != '\0')
        {
            c = *++at;
            if (c == 'u')
            {
                // \uXXXX to UTF-8 (the basic plane)
                char hex[5] = {0};
                for (int i = 0; i < 4 && at[1] != '\0'; i++)
                    hex[i] = *++at;
                const unsigned cp = (unsigned)strtoul(hex, NULL, 16);
                if (cp < 0x80)
                    out[used++] = (char)cp;
                else if (cp < 0x800 && used + 2 < size)
                {
                    out[used++] = (char)(0xC0 | (cp >> 6));
                    out[used++] = (char)(0x80 | (cp & 0x3F));
                }
                else if (used + 3 < size)
                {
                    out[used++] = (char)(0xE0 | (cp >> 12));
                    out[used++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    out[used++] = (char)(0x80 | (cp & 0x3F));
                }
                continue;
            }
            if (c == 'n' || c == 't')
                c = ' ';
        }
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
        out[used++] = c;
    }
    out[used] = '\0';
    return used > 0 ? 0 : -1;
}

static uint32_t le16(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const unsigned char *p) { return le16(p) | (le16(p + 2) << 16); }

// A title's name: param.json (English first) or param.sfo's TITLE
static int title_name(const char *folder, char *out, size_t size)
{
    char path[256];
    size_t length = 0;
    snprintf(path, sizeof(path), "%s/param.json", folder);
    char *text = read_file(path, 1 << 20, &length);
    if (text != NULL)
    {
        int found = -1;
        const char *const languages[] = {"\"en-US\"", "\"en-GB\""};
        for (int i = 0; i < 2 && found != 0; i++)
        {
            const char *const at = strstr(text, languages[i]);
            if (at != NULL)
                found = json_string(at, "titleName", out, size);
        }
        if (found != 0)
            found = json_string(text, "titleName", out, size);
        free(text);
        if (found == 0)
            return 0;
    }
    snprintf(path, sizeof(path), "%s/param.sfo", folder);
    unsigned char *const d = (unsigned char *)read_file(path, 1 << 20, &length);
    if (d == NULL)
        return -1;
    int found = -1;
    if (length >= 0x14 && memcmp(d, "\0PSF", 4) == 0)
    {
        const uint32_t keys = le32(d + 8), data = le32(d + 12), count = le32(d + 16);
        for (uint32_t i = 0; i < count && i < 256 && 0x14 + i * 16 + 16 <= length; i++)
        {
            const unsigned char *const e = d + 0x14 + i * 16;
            const size_t key_at = keys + le16(e);
            if (key_at >= length || strcmp((const char *)d + key_at, "TITLE") != 0)
                continue;
            const size_t at = data + le32(e + 12), len = le32(e + 4);
            if (at + len <= length && len > 0)
            {
                size_t n = 0;
                for (; n < len && n + 1 < size && d[at + n] != '\0'; n++)
                    out[n] = (d[at + n] == '\t' || d[at + n] == '\n') ? ' ' : (char)d[at + n];
                out[n] = '\0';
                found = n > 0 ? 0 : -1;
            }
            break;
        }
    }
    free(d);
    return found;
}

// A title's content id: param.json's contentId or param.sfo's CONTENT_ID
static void title_content_id(const char *folder, char *out, size_t size)
{
    out[0] = '\0';
    char path[256];
    size_t length = 0;
    snprintf(path, sizeof(path), "%s/param.json", folder);
    char *text = read_file(path, 1 << 20, &length);
    if (text != NULL)
    {
        (void)json_string(text, "contentId", out, size);
        free(text);
        if (out[0] != '\0')
            return;
    }
    snprintf(path, sizeof(path), "%s/param.sfo", folder);
    unsigned char *const d = (unsigned char *)read_file(path, 1 << 20, &length);
    if (d == NULL)
        return;
    if (length >= 0x14 && memcmp(d, "\0PSF", 4) == 0)
    {
        const uint32_t keys = le32(d + 8), data = le32(d + 12), count = le32(d + 16);
        for (uint32_t i = 0; i < count && i < 256 && 0x14 + i * 16 + 16 <= length; i++)
        {
            const unsigned char *const e = d + 0x14 + i * 16;
            const size_t key_at = keys + le16(e);
            if (key_at >= length || strcmp((const char *)d + key_at, "CONTENT_ID") != 0)
                continue;
            const size_t at = data + le32(e + 12), len = le32(e + 4);
            size_t n = 0;
            for (; at + n < length && n < len && n + 1 < size && d[at + n] != '\0'; n++)
                out[n] = (char)d[at + n];
            out[n] = '\0';
            break;
        }
    }
    free(d);
}

static int is_title_id(const char *s)
{
    if (strlen(s) != 9)
        return 0;
    for (int i = 0; i < 4; i++)
        if (s[i] < 'A' || s[i] > 'Z')
            return 0;
    for (int i = 4; i < 9; i++)
        if (s[i] < '0' || s[i] > '9')
            return 0;
    return 1;
}


// Where a title's files can be: the console's storage, extended storage (USB),
// the M.2 drive, and the homebrew folders ShadowMountPlus serves titles from
static const char *const app_roots[] = {"/user/app", "/mnt/ext0/user/app", "/mnt/ext1/user/app", "/system_ex/app"};
static const char *const homebrew_roots[] = {"/data/homebrew", "/mnt/ext0/homebrew", "/mnt/ext1/homebrew",
                                             "/mnt/usb0/homebrew", "/mnt/usb1/homebrew"};

// The bytes under a folder: its files, recursively (links not followed); at
// most 'budget' entries are visited so a huge folder cannot stall the list
static long long folder_bytes(const char *path, int depth, int *budget)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    if (!S_ISDIR(st.st_mode))
        return S_ISREG(st.st_mode) ? (long long)st.st_size : 0;
    if (depth > 24)
        return 0;
    DIR *const dir = opendir(path);
    if (dir == NULL)
        return 0;
    long long total = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *budget > 0)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        (*budget)--;
        char child[1024];
        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= (int)sizeof(child))
            continue;
        total += folder_bytes(child, depth + 1, budget);
    }
    closedir(dir);
    return total;
}

static int is_folder(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Whether a title is served from a homebrew folder
static int title_homebrew(const char *id)
{
    char path[256];
    for (size_t i = 0; i < sizeof(homebrew_roots) / sizeof(homebrew_roots[0]); i++)
    {
        snprintf(path, sizeof(path), "%s/%s", homebrew_roots[i], id);
        if (is_folder(path))
            return 1;
    }
    return 0;
}

// The largest file in a homebrew root whose name starts with the title id (a
// disk image ShadowMountPlus mounts, like PPSA01234.exfat)
static long long image_bytes(const char *root, const char *id)
{
    DIR *const dir = opendir(root);
    if (dir == NULL)
        return 0;
    long long best = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strncmp(entry->d_name, id, 9) != 0)
            continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && (long long)st.st_size > best)
            best = (long long)st.st_size;
    }
    closedir(dir);
    return best;
}

// A title's size: the largest of every place it can be installed or mounted
static long long title_bytes(const char *id)
{
    long long best = 0;
    char path[256];
    const char *const *groups[] = {app_roots, homebrew_roots};
    const size_t counts[] = {sizeof(app_roots) / sizeof(app_roots[0]), sizeof(homebrew_roots) / sizeof(homebrew_roots[0])};
    for (int g = 0; g < 2; g++)
        for (size_t i = 0; i < counts[g]; i++)
        {
            snprintf(path, sizeof(path), "%s/%s", groups[g][i], id);
            int budget = 200000;
            const long long bytes = folder_bytes(path, 0, &budget);
            if (bytes > best)
                best = bytes;
            if (g == 1)
            {
                const long long image = image_bytes(groups[g][i], id);
                if (image > best)
                    best = image;
            }
        }
    return best;
}

static void where_title(int client, const char *id)
{
    if (id == NULL || !is_title_id(id))
    {
        reply(client, "err usage: where <TITLEID>");
        return;
    }
    char line[4000] = "ok";
    const char *const *groups[] = {app_roots, homebrew_roots};
    const size_t counts[] = {sizeof(app_roots) / sizeof(app_roots[0]), sizeof(homebrew_roots) / sizeof(homebrew_roots[0])};
    for (int g = 0; g < 2; g++)
        for (size_t i = 0; i < counts[g]; i++)
        {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s", groups[g][i], id);
            struct stat st;
            if (lstat(path, &st) != 0)
                continue;
            int budget = 200000;
            char entry[320];
            snprintf(entry, sizeof(entry), " %s(%s%s,%lld)", path, S_ISLNK(st.st_mode) ? "link," : "",
                     S_ISDIR(st.st_mode) ? "dir" : "file", folder_bytes(path, 0, &budget));
            strncat(line, entry, sizeof(line) - strlen(line) - 1);
            // Its first entries, to see what the place holds
            DIR *const dir = opendir(path);
            if (dir != NULL)
            {
                struct dirent *e;
                int shown = 0;
                strncat(line, " [", sizeof(line) - strlen(line) - 1);
                while ((e = readdir(dir)) != NULL && shown < 10)
                {
                    if (e->d_name[0] == '.')
                        continue;
                    char child[512], item[160];
                    snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
                    struct stat cs;
                    const int ok = lstat(child, &cs) == 0;
                    snprintf(item, sizeof(item), "%s%s:%lld ", e->d_name, ok && S_ISLNK(cs.st_mode) ? "@" : "",
                             ok ? (long long)cs.st_size : -1LL);
                    strncat(line, item, sizeof(line) - strlen(line) - 1);
                    shown++;
                }
                closedir(dir);
                strncat(line, "]", sizeof(line) - strlen(line) - 1);
            }
        }
    for (size_t i = 0; i < sizeof(homebrew_roots) / sizeof(homebrew_roots[0]); i++)
    {
        const long long image = image_bytes(homebrew_roots[i], id);
        if (image > 0)
        {
            char entry[320];
            snprintf(entry, sizeof(entry), " image-in:%s(%lld)", homebrew_roots[i], image);
            strncat(line, entry, sizeof(line) - strlen(line) - 1);
        }
    }
    char total[64];
    snprintf(total, sizeof(total), " size=%lld\n", title_bytes(id));
    strncat(line, total, sizeof(line) - strlen(line) - 1);
    send_all(client, line, strlen(line));
}

static void list_titles(int client)
{
    char seen[512][10];
    int seen_count = 0;
    send_all(client, "ok list\n", 8);
    for (size_t r = 0; r < sizeof(meta_roots) / sizeof(meta_roots[0]); r++)
    {
        DIR *const dir = opendir(meta_roots[r]);
        if (dir == NULL)
            continue;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            const char *const id = entry->d_name;
            if (!is_title_id(id) || strncmp(id, "NPXS", 4) == 0)
                continue;
            int duplicate = 0;
            for (int i = 0; i < seen_count && !duplicate; i++)
                duplicate = strcmp(seen[i], id) == 0;
            if (duplicate || seen_count >= 512)
                continue;
            char folder[256], name[256];
            snprintf(folder, sizeof(folder), "%s/%s", meta_roots[r], id);
            if (title_name(folder, name, sizeof(name)) != 0)
                continue;
            snprintf(seen[seen_count++], sizeof(seen[0]), "%s", id);
            struct stat st;
            long long installed = stat(folder, &st) == 0 ? (long long)st.st_mtime : 0;
            char content[64];
            title_content_id(folder, content, sizeof(content));
            for (char *c = content; *c != '\0'; c++)
                if (*c == '\t' || *c == '\n')
                    *c = ' ';
            char line[480];
            const int n = snprintf(line, sizeof(line), "%s\t0\t%lld\t%s\t%s\t%s\n", id, installed, name,
                                   title_homebrew(id) ? "homebrew" : "game", content);
            if (n > 0 && send_all(client, line, (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1)) != 0)
            {
                closedir(dir);
                return;
            }
        }
        closedir(dir);
    }
    send_all(client, "end\n", 4);
    log_line("cflaunch: listed %d titles", seen_count);
}

static void send_icon(int client, const char *id)
{
    if (id == NULL || !is_title_id(id))
    {
        reply(client, "err usage: icon <TITLEID>");
        return;
    }
    for (size_t r = 0; r < sizeof(meta_roots) / sizeof(meta_roots[0]); r++)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s/icon0.png", meta_roots[r], id);
        size_t size = 0;
        char *const data = read_file(path, 8 << 20, &size);
        if (data == NULL)
            continue;
        char head[32];
        const int n = snprintf(head, sizeof(head), "ok %zu\n", size);
        if (send_all(client, head, (size_t)n) == 0)
            send_all(client, data, size);
        free(data);
        return;
    }
    reply(client, "err no icon for %s", id);
}

// ----------------------------------------------------------------- returning

// ----------------------------------------------------------------- play time

#define PLAYTIME_FILE "/data/cflaunch-playtime.txt"
#define PLAYTIME_MAX 512

typedef struct play_record
{
    char id[16];
    long long seconds, last, count;
} play_record_t;

static int load_play(play_record_t *records)
{
    FILE *const file = fopen(PLAYTIME_FILE, "r");
    int n = 0;
    if (file == NULL)
        return 0;
    while (n < PLAYTIME_MAX &&
           fscanf(file, "%15s %lld %lld %lld", records[n].id, &records[n].seconds, &records[n].last, &records[n].count) == 4)
        n++;
    fclose(file);
    return n;
}

// Add one session to a title's record
static void record_play(const char *id, long long seconds, long long ended)
{
    static play_record_t records[PLAYTIME_MAX];
    int n = load_play(records), at = -1;
    for (int i = 0; i < n && at < 0; i++)
        if (strcmp(records[i].id, id) == 0)
            at = i;
    if (at < 0)
    {
        if (n >= PLAYTIME_MAX)
            return;
        at = n++;
        memset(&records[at], 0, sizeof(records[at]));
        snprintf(records[at].id, sizeof(records[at].id), "%s", id);
    }
    records[at].seconds += seconds > 0 ? seconds : 0;
    records[at].last = ended;
    records[at].count++;
    FILE *const file = fopen(PLAYTIME_FILE ".new", "w");
    if (file == NULL)
        return;
    for (int i = 0; i < n; i++)
        fprintf(file, "%s %lld %lld %lld\n", records[i].id, records[i].seconds, records[i].last, records[i].count);
    fclose(file);
    rename(PLAYTIME_FILE ".new", PLAYTIME_FILE);
    log_line("cflaunch: %s played %lld s (total %lld s)", id, seconds, records[at].seconds);
}

static void send_playtime(int client)
{
    static play_record_t records[PLAYTIME_MAX];
    const int n = load_play(records);
    send_all(client, "ok playtime\n", 12);
    for (int i = 0; i < n; i++)
    {
        char line[96];
        const int len = snprintf(line, sizeof(line), "%s\t%lld\t%lld\t%lld\n", records[i].id, records[i].seconds, records[i].last,
                                 records[i].count);
        send_all(client, line, (size_t)len);
    }
    send_all(client, "end\n", 4);
}

// ----------------------------------------------------------------- watching a game

// The game Cover Flow started (timed while it runs), and Cover Flow, to start
// again once it closes when asked to
static char g_watch_game[16];
static char g_watch_home[16];
static int g_watch_return;
static int g_watch_idle;
static long long g_watch_started;
static int g_watch_closed;

static void watch_start(const char *game, const char *home, int come_back)
{
    snprintf(g_watch_game, sizeof(g_watch_game), "%s", game);
    snprintf(g_watch_home, sizeof(g_watch_home), "%s", home);
    g_watch_return = come_back;
    g_watch_idle = 0;
    g_watch_closed = 0;
    g_watch_started = (long long)time(NULL);
    log_line("cflaunch: timing %s%s%s", game, come_back ? "; will bring back " : "", come_back ? home : "");
}

// Called about once a second while nothing else is asked
static void watch_tick(void)
{
    if (g_watch_game[0] == '\0')
        return;
    char running[32] = {0};
    const uint32_t app = running_app(running, sizeof(running));
    const int game_running = app != PS5VKCTL_NO_APP && strncmp(running, g_watch_game, PS5VKCTL_TITLE) == 0;
    if (!g_watch_closed && !game_running)
    {
        // The session is over: count it
        const long long now = (long long)time(NULL);
        record_play(g_watch_game, now - g_watch_started, now);
        g_watch_closed = 1;
    }
    if (game_running)
    {
        g_watch_idle = 0;
        return;
    }
    if (app != PS5VKCTL_NO_APP)
    {
        // Something else was started from the home screen: stand aside
        log_line("cflaunch: %s is running now; not bringing %s back", running, g_watch_home);
        g_watch_game[0] = '\0';
        return;
    }
    if (!g_watch_return)
    {
        g_watch_game[0] = '\0';
        return;
    }
    // The game has closed: after two idle seconds, start Cover Flow again
    if (++g_watch_idle < 2)
        return;
    const char *name = NULL;
    uint32_t code = 0;
    char detail[160] = {0};
    const int started = start_title(g_watch_home, &name, &code, detail, sizeof(detail));
    log_line("cflaunch: %s closed; starting %s: %s %s", g_watch_game, g_watch_home, started == 0 ? "ok" : "failed", detail);
    g_watch_game[0] = '\0';
}

// ----------------------------------------------------------------- artwork, storage, backup, log

static void send_art(int client, const char *id)
{
    if (id == NULL || !is_title_id(id))
    {
        reply(client, "err usage: art <TITLEID>");
        return;
    }
    static const char *const names[] = {"pic1.png", "pic0.png", "pic1.jpg", "pic0.jpg", "pic2.png"};
    for (size_t r = 0; r < sizeof(meta_roots) / sizeof(meta_roots[0]); r++)
        for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); n++)
        {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s/%s", meta_roots[r], id, names[n]);
            size_t size = 0;
            char *const data = read_file(path, 24 << 20, &size);
            if (data == NULL)
                continue;
            char head[32];
            const int len = snprintf(head, sizeof(head), "ok %zu\n", size);
            if (send_all(client, head, (size_t)len) == 0)
                send_all(client, data, size);
            free(data);
            return;
        }
    reply(client, "err no art for %s", id);
}

static void send_storage(int client)
{
    struct statfs fs;
    if (statfs("/user", &fs) != 0)
    {
        reply(client, "err statfs: %s", strerror(errno));
        return;
    }
    reply(client, "ok %lld %lld", (long long)fs.f_bavail * (long long)fs.f_bsize, (long long)fs.f_blocks * (long long)fs.f_bsize);
}

static void make_parents(const char *file);

static int copy_file(const char *from, const char *to)
{
    size_t size = 0;
    char *const data = read_file(from, 16 << 20, &size);
    if (data == NULL)
        return -1;
    make_parents(to);
    FILE *const file = fopen(to, "wb");
    const int ok = file != NULL && fwrite(data, 1, size, file) == size;
    if (file != NULL)
        fclose(file);
    free(data);
    return ok ? 0 : -1;
}

// The files a backup holds: in the title folder, or absolute
static void backup_paths(const char *title, char from[3][192], const char *names[3])
{
    names[0] = "coverflow.ini";
    names[1] = "games.txt";
    names[2] = "playtime.txt";
    snprintf(from[0], 192, "/data/homebrew/%s/coverflow.ini", title);
    snprintf(from[1], 192, "/data/homebrew/%s/covers/games.txt", title);
    snprintf(from[2], 192, "%s", PLAYTIME_FILE);
}


// ----------------------------------------------------------------- game/app file backup
// Copies only files the running homebrew process can read. Symlinks are skipped
// to avoid escaping the selected title tree. This is a file backup, not a
// decrypted/reinstallable package; platform-protected files may be inaccessible.
static int copy_tree_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (in == NULL) return -1;
    make_parents(to);
    FILE *out = fopen(to, "wb");
    if (out == NULL) { fclose(in); return -1; }
    unsigned char buffer[1024 * 1024];
    int result = 0;
    for (;;) {
        size_t got = fread(buffer, 1, sizeof(buffer), in);
        if (got && fwrite(buffer, 1, got, out) != got) { result = -1; break; }
        if (got < sizeof(buffer)) {
            if (ferror(in)) result = -1;
            break;
        }
    }
    if (fclose(out) != 0) result = -1;
    fclose(in);
    if (result != 0) unlink(to);
    return result;
}

static int copy_tree(const char *from, const char *to, int depth, long long *files, long long *bytes)
{
    if (depth > 64) return -1;
    struct stat st;
    if (lstat(from, &st) != 0) return -1;
    if (S_ISLNK(st.st_mode)) return 0; // never follow links from inside a title
    if (S_ISREG(st.st_mode)) {
        if (copy_tree_file(from, to) != 0) return -1;
        (*files)++;
        *bytes += (long long)st.st_size;
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) return 0;
    if (mkdir(to, 0777) != 0 && errno != EEXIST) return -1;
    DIR *dir = opendir(from);
    if (dir == NULL) return -1;
    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char src[1024], dst[1024];
        if (snprintf(src, sizeof(src), "%s/%s", from, entry->d_name) >= (int)sizeof(src) ||
            snprintf(dst, sizeof(dst), "%s/%s", to, entry->d_name) >= (int)sizeof(dst)) {
            result = -1; break;
        }
        if (copy_tree(src, dst, depth + 1, files, bytes) != 0) { result = -1; break; }
    }
    closedir(dir);
    return result;
}

static void backup_game_app(int client, const char *title, const char *destination)
{
    if (!is_title_id(title) || destination == NULL ||
        (strcmp(destination, "usb") != 0 && strcmp(destination, "internal") != 0)) {
        reply(client, "err usage: game-backup <TITLEID> <usb|internal>");
        return;
    }

    char source[256] = {0};
    const char *const roots[] = {"/user/app", "/mnt/ext0/user/app", "/mnt/ext1/user/app", "/system_ex/app"};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        char candidate[320];
        snprintf(candidate, sizeof(candidate), "%s/%s", roots[i], title);
        struct stat st;
        if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(source, sizeof(source), "%s", candidate);
            break;
        }
    }
    if (!source[0]) {
        reply(client, "err no accessible installed app folder found for %s", title);
        return;
    }

    char target[512] = {0};
    if (strcmp(destination, "internal") == 0) {
        snprintf(target, sizeof(target), "/data/homebrew/PPSA99123/backups/games/%s", title);
    } else {
        for (int u = 0; u < 8; u++) {
            char drive[32], candidate[512];
            snprintf(drive, sizeof(drive), "/mnt/usb%d", u);
            if (!is_folder(drive)) continue;
            snprintf(candidate, sizeof(candidate), "%s/CoverFlowGameBackups/%s", drive, title);
            struct stat st;
            if (lstat(candidate, &st) == 0) {
                reply(client, "err backup already exists at %s; remove it first", candidate);
                return;
            }
            if (access(drive, W_OK) == 0) {
                snprintf(target, sizeof(target), "%s", candidate);
                break;
            }
        }
        if (!target[0]) {
            reply(client, "err no writable USB drive found");
            return;
        }
    }

    struct stat existing;
    if (lstat(target, &existing) == 0) {
        reply(client, "err backup already exists at %s; remove it first", target);
        return;
    }
    long long files = 0, bytes = 0;
    log_line("cflaunch: backing up accessible files for %s from %s to %s", title, source, target);
    if (copy_tree(source, target, 0, &files, &bytes) != 0 || files == 0) {
        log_line("cflaunch: backup failed for %s after %lld files", title, files);
        reply(client, "err backup incomplete; copied %lld files (%lld bytes) to %s", files, bytes, target);
        return;
    }
    log_line("cflaunch: backup complete for %s: %lld files, %lld bytes at %s", title, files, bytes, target);
    reply(client, "ok %s %lld %lld %s", target, files, bytes,
          "file backup only; not a verified reinstallable package");
}

static void backup_or_restore(int client, const char *title, int restore)
{
    if (!valid_title(title))
    {
        reply(client, "err usage: %s <TITLEID>", restore ? "restore" : "backup");
        return;
    }
    char from[3][192];
    const char *names[3];
    backup_paths(title, from, names);
    for (int u = 0; u < 8; u++)
    {
        char drive[32], folder[64];
        snprintf(drive, sizeof(drive), "/mnt/usb%d", u);
        if (!is_folder(drive))
            continue;
        snprintf(folder, sizeof(folder), "%s/CoverFlowBackup", drive);
        if (restore && !is_folder(folder))
            continue;
        int done = 0;
        for (int i = 0; i < 3; i++)
        {
            char other[256];
            snprintf(other, sizeof(other), "%s/%s", folder, names[i]);
            if (restore ? copy_file(other, from[i]) == 0 : copy_file(from[i], other) == 0)
                done++;
        }
        log_line("cflaunch: %s %s: %d files", restore ? "restored from" : "backed up to", folder, done);
        if (done == 0 && !restore)
        {
            reply(client, "err could not write to %s", folder);
            return;
        }
        reply(client, "ok %s %d", folder, done);
        return;
    }
    reply(client, restore ? "err no USB drive with a CoverFlowBackup folder" : "err no USB drive found");
}

static void send_log(int client)
{
    size_t size = 0;
    char *const data = read_file(PS5VKCTL_LOG, 4 << 20, &size);
    send_all(client, "ok log\n", 7);
    if (data != NULL)
    {
        // The last 40 lines
        size_t at = size;
        int lines = 0;
        while (at > 0 && lines <= 40)
            if (data[--at] == '\n')
                lines++;
        if (lines > 40)
            at++;
        send_all(client, data + at, size - at);
        free(data);
    }
    send_all(client, "end\n", 4);
}

// ----------------------------------------------------------------- SHA-256

typedef struct sha256
{
    uint32_t h[8];
    uint64_t length;
    unsigned char block[64];
    size_t used;
} sha256_t;

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(sha256_t *c, const unsigned char *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++)
    {
        const uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 64; i++)
    {
        const uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + sha256_k[i] + w[i];
        const uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = cc;
        cc = b;
        b = a;
        a = t1 + t2;
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += cc;
    c->h[3] += d;
    c->h[4] += e;
    c->h[5] += f;
    c->h[6] += g;
    c->h[7] += h;
}

static void sha256_init(sha256_t *c)
{
    static const uint32_t h0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(c->h, h0, sizeof(h0));
    c->length = 0;
    c->used = 0;
}

static void sha256_update(sha256_t *c, const void *data, size_t size)
{
    const unsigned char *p = data;
    c->length += size;
    while (size > 0)
    {
        const size_t take = size < 64 - c->used ? size : 64 - c->used;
        memcpy(c->block + c->used, p, take);
        c->used += take;
        p += take;
        size -= take;
        if (c->used == 64)
        {
            sha256_block(c, c->block);
            c->used = 0;
        }
    }
}

static void sha256_hex(sha256_t *c, char out[65])
{
    const uint64_t bits = c->length * 8;
    const unsigned char pad = 0x80, zero = 0;
    sha256_update(c, &pad, 1);
    while (c->used != 56)
        sha256_update(c, &zero, 1);
    unsigned char len[8];
    for (int i = 0; i < 8; i++)
        len[i] = (unsigned char)(bits >> (56 - i * 8));
    sha256_update(c, len, 8);
    for (int i = 0; i < 8; i++)
        snprintf(out + i * 8, 9, "%08x", c->h[i]);
}

// ----------------------------------------------------------------- HTTPS

int sceNetInit(void);
int sceNetPoolCreate(const char *, int, int);
int sceSslInit(size_t);
int sceHttp2Init(int, int, size_t, int);
int sceHttp2CreateTemplate(int, const char *, int, int);
int sceHttp2CreateRequestWithURL(int, const char *, const char *, uint64_t);
int sceHttp2DeleteRequest(int);
int sceHttp2SendRequest(int, const void *, size_t);
int sceHttp2GetStatusCode(int, int *);
int sceHttp2ReadData(int, void *, size_t);

int sceHttpInit(int, int, size_t);
int sceHttpCreateTemplate(int, const char *, int, int);
int sceHttpCreateConnectionWithURL(int, const char *, int);
int sceHttpCreateRequestWithURL(int, int, const char *, uint64_t);
int sceHttpSendRequest(int, const void *, size_t);
int sceHttpGetStatusCode(int, int *);
int sceHttpReadData(int, void *, size_t);
int sceHttpDeleteRequest(int);
int sceHttpDeleteConnection(int);

static int g_http_template = -1;
static int g_http1_template = -1;
static int g_use_http1; // HTTP/2 failed here once: go straight to HTTP/1.1
// Why the last request failed: 1 the name did not resolve, 2 it resolved to a
// blocked address (a DNS filter), 3 the connection or its certificate; and the code
static int g_http_error;
static uint32_t g_http_code;

static int http_ready(void)
{
    if (g_http_template >= 0)
        return 0;
    static int net = -1, ssl = -1, http = -1;
    if (net < 0)
    {
        (void)sceNetInit();
        net = sceNetPoolCreate("cflaunch", 64 * 1024, 0);
    }
    if (ssl < 0)
        ssl = sceSslInit(512 * 1024);
    if (http < 0 && net >= 0 && ssl >= 0)
        http = sceHttp2Init(net, ssl, 512 * 1024, 2);
    if (http >= 0)
        g_http_template = sceHttp2CreateTemplate(http, "CoverFlow-cflaunch/7", 3, 1);
    // The HTTP/1.1 library too, for when HTTP/2 will not connect from here
    static int http1 = -1;
    if (http1 < 0 && net >= 0 && ssl >= 0)
        http1 = sceHttpInit(net, ssl, 512 * 1024);
    if (http1 >= 0 && g_http1_template < 0)
        g_http1_template = sceHttpCreateTemplate(http1, "CoverFlow-cflaunch/7", 2, 1);
    log_line("cflaunch: https net=%d ssl=%d http2=%d template=%d http1=%d template=%d", net, ssl, http, g_http_template, http1,
             g_http1_template);
    return g_http_template >= 0 || g_http1_template >= 0 ? 0 : -1;
}

// The body of a request, read with either library, into a buffer and/or a file
typedef int (*read_fn)(int, void *, size_t);
static int read_body(read_fn reader, int request, char **out, size_t *size, size_t limit, FILE *file, sha256_t *hash)
{
    size_t used = 0, capacity = 0;
    char *body = NULL;
    int status = 0;
    char buffer[16384];
    int got;
    while ((got = reader(request, buffer, sizeof(buffer))) > 0)
    {
        if (used + (size_t)got > limit)
        {
            status = -2;
            break;
        }
        if (hash != NULL)
            sha256_update(hash, buffer, (size_t)got);
        if (file != NULL && fwrite(buffer, 1, (size_t)got, file) != (size_t)got)
        {
            status = -3;
            break;
        }
        if (out != NULL)
        {
            if (used + (size_t)got + 1 > capacity)
            {
                capacity = (used + (size_t)got + 1) * 2;
                char *const grown = realloc(body, capacity);
                if (grown == NULL)
                {
                    status = -4;
                    break;
                }
                body = grown;
            }
            memcpy(body + used, buffer, (size_t)got);
            body[used + (size_t)got] = '\0';
        }
        used += (size_t)got;
    }
    if (out != NULL)
        *out = body;
    else
        free(body);
    if (size != NULL)
        *size = used;
    return status;
}

// GET with the HTTP/1.1 library (it follows redirects itself)
static int http1_get(const char *url, char **out, size_t *size, size_t limit, FILE *file, sha256_t *hash)
{
    if (g_http1_template < 0)
        return -1;
    const int connection = sceHttpCreateConnectionWithURL(g_http1_template, url, 1);
    if (connection < 0)
    {
        log_line("cflaunch: http1 connection for %s: 0x%08x", url, (uint32_t)connection);
        g_http_code = (uint32_t)connection;
        return -1;
    }
    const int request = sceHttpCreateRequestWithURL(connection, 0, url, 0);
    int status = -1;
    if (request < 0)
    {
        log_line("cflaunch: http1 request for %s: 0x%08x", url, (uint32_t)request);
        g_http_code = (uint32_t)request;
    }
    else
    {
        const int sent = sceHttpSendRequest(request, NULL, 0);
        if (sent != 0 || sceHttpGetStatusCode(request, &status) != 0)
        {
            log_line("cflaunch: http1 GET %s failed: send 0x%08x", url, (uint32_t)sent);
            g_http_code = (uint32_t)sent;
            g_http_error = 3;
            status = -1;
        }
        else if (status == 200)
        {
            const int body = read_body(sceHttpReadData, request, out, size, limit, file, hash);
            if (body < 0)
                status = body;
        }
        sceHttpDeleteRequest(request);
    }
    sceHttpDeleteConnection(connection);
    log_line("cflaunch: http1 GET %s -> %d", url, status);
    return status;
}

static int http2_get(const char *url, char **out, size_t *size, size_t limit, FILE *file, sha256_t *hash);

// GET a URL with HTTP/2, falling back to HTTP/1.1 (and staying on it) when
// HTTP/2 cannot connect from this process
static int http_get(const char *url, char **out, size_t *size, size_t limit, FILE *file, sha256_t *hash)
{
    if (http_ready() != 0)
        return -1;
    if (!g_use_http1 && g_http_template >= 0)
    {
        const long start = file != NULL ? ftell(file) : 0;
        const int status = http2_get(url, out, size, limit, file, hash);
        if (status != -1)
            return status;
        // Nothing was read when sending failed: try again with HTTP/1.1
        if (file != NULL)
            fseek(file, start, SEEK_SET);
        log_line("cflaunch: HTTP/2 failed (0x%08x); trying HTTP/1.1", g_http_code);
    }
    const int status = http1_get(url, out, size, limit, file, hash);
    if (status >= 0 && !g_use_http1)
    {
        g_use_http1 = 1;
        log_line("cflaunch: using HTTP/1.1 from now on");
    }
    return status;
}

// GET a URL: the body into a malloc'd buffer (when 'out'), or into a file
// (when 'file'), hashing it as it goes. Returns the HTTP status, -1 on failure.
static int http2_get(const char *url, char **out, size_t *size, size_t limit, FILE *file, sha256_t *hash)
{
    const int request = sceHttp2CreateRequestWithURL(g_http_template, "GET", url, 0);
    if (request < 0)
    {
        log_line("cflaunch: request for %s: 0x%08x", url, (uint32_t)request);
        return -1;
    }
    int status = -1;
    const int sent = sceHttp2SendRequest(request, NULL, 0);
    const int got_status = sent == 0 ? sceHttp2GetStatusCode(request, &status) : -1;
    if (sent != 0 || got_status != 0)
    {
        sceHttp2DeleteRequest(request);
        // Which step failed: the name lookup, or the connection and its certificate
        char host[128] = {0};
        const char *const h = strstr(url, "://");
        if (h != NULL)
        {
            snprintf(host, sizeof(host), "%s", h + 3);
            char *const slash = strchr(host, '/');
            if (slash != NULL)
                *slash = '\0';
        }
        struct addrinfo hints, *found = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        const int lookup = host[0] != '\0' ? getaddrinfo(host, "443", &hints, &found) : -1;
        char address[64] = "none";
        if (lookup == 0 && found != NULL)
            inet_ntop(AF_INET, &((struct sockaddr_in *)found->ai_addr)->sin_addr, address, sizeof(address));
        if (found != NULL)
            freeaddrinfo(found);
        log_line("cflaunch: GET %s failed: send 0x%08x, status 0x%08x; %s resolves to %s (lookup %d)", url, (uint32_t)sent,
                 (uint32_t)got_status, host, address, lookup);
        g_http_error = lookup != 0 ? 1 : (strcmp(address, "0.0.0.0") == 0 || strncmp(address, "127.", 4) == 0) ? 2 : 3;
        g_http_code = (uint32_t)sent;
        return -1;
    }
    size_t used = 0, capacity = 0;
    char *body = NULL;
    if (status == 200)
    {
        char buffer[16384];
        int got;
        while ((got = sceHttp2ReadData(request, buffer, sizeof(buffer))) > 0)
        {
            if (used + (size_t)got > limit)
            {
                status = -2;
                break;
            }
            if (hash != NULL)
                sha256_update(hash, buffer, (size_t)got);
            if (file != NULL && fwrite(buffer, 1, (size_t)got, file) != (size_t)got)
            {
                status = -3;
                break;
            }
            if (out != NULL)
            {
                if (used + (size_t)got + 1 > capacity)
                {
                    capacity = (used + (size_t)got + 1) * 2;
                    char *const grown = realloc(body, capacity);
                    if (grown == NULL)
                    {
                        status = -4;
                        break;
                    }
                    body = grown;
                }
                memcpy(body + used, buffer, (size_t)got);
                body[used + (size_t)got] = '\0';
            }
            used += (size_t)got;
        }
    }
    sceHttp2DeleteRequest(request);
    log_line("cflaunch: GET %s -> %d (%zu bytes)", url, status, used);
    if (out != NULL)
        *out = body;
    else
        free(body);
    if (size != NULL)
        *size = used;
    return status;
}

static int valid_repo(const char *repo)
{
    if (repo == NULL || strlen(repo) < 3 || strlen(repo) > 100 || strchr(repo, '/') == NULL)
        return 0;
    for (const char *c = repo; *c != '\0'; c++)
        if (!(*c == '/' || *c == '-' || *c == '_' || *c == '.' || (*c >= '0' && *c <= '9') ||
              (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')))
            return 0;
    return 1;
}

// The latest release: its tag, and the release JSON for finding assets
static int latest_release(const char *repo, char *tag, size_t tag_size, char **json_out, char *error, size_t error_size)
{
    char url[256];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", repo);
    char *json = NULL;
    const int status = http_get(url, &json, NULL, 2 << 20, NULL, NULL);
    if (status != 200 || json == NULL)
    {
        if (status == -1 && g_http_error == 1)
            snprintf(error, error_size, "the PS5 could not look up api.github.com (DNS)");
        else if (status == -1 && g_http_error == 2)
            snprintf(error, error_size, "api.github.com is blocked by a DNS filter on this PS5 (nanodns?)");
        else if (status == -1)
            snprintf(error, error_size, "could not connect to GitHub securely (0x%08x)", g_http_code);
        else
            snprintf(error, error_size, "GitHub answered %d for %s", status, repo);
        free(json);
        return -1;
    }
    if (json_string(json, "tag_name", tag, tag_size) != 0)
    {
        snprintf(error, error_size, "no tag in the latest release");
        free(json);
        return -1;
    }
    *json_out = json;
    return 0;
}

// The download address of the release asset with this name, or -1
static int asset_url(const char *json, const char *name, char *out, size_t size)
{
    const char *at = json;
    const size_t nl = strlen(name);
    while ((at = strstr(at, "\"browser_download_url\"")) != NULL)
    {
        char value[600];
        if (json_string(at, "browser_download_url", value, sizeof(value)) == 0)
        {
            const size_t vl = strlen(value);
            if (vl > nl && value[vl - nl - 1] == '/' && strcmp(value + vl - nl, name) == 0)
            {
                snprintf(out, size, "%s", value);
                return 0;
            }
        }
        at += 22;
    }
    return -1;
}

// A path a release may write in the title folder: relative, no "..", plain characters
static int safe_path(const char *path)
{
    if (path[0] == '\0' || path[0] == '/' || strlen(path) > 160 || strstr(path, "..") != NULL)
        return 0;
    for (const char *c = path; *c != '\0'; c++)
        if (!(*c == '/' || *c == '-' || *c == '_' || *c == '.' || (*c >= '0' && *c <= '9') ||
              (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')))
            return 0;
    return 1;
}

// The folders above a file, made as needed
static void make_parents(const char *file)
{
    char path[256];
    snprintf(path, sizeof(path), "%s", file);
    for (char *c = path + 1; *c != '\0'; c++)
        if (*c == '/')
        {
            *c = '\0';
            mkdir(path, 0777);
            *c = '/';
        }
}

static void update_check(int client, const char *repo)
{
    if (!valid_repo(repo))
    {
        reply(client, "err usage: update-check <owner/repo>");
        return;
    }
    char tag[64], error[200], url[600];
    char *json = NULL;
    if (latest_release(repo, tag, sizeof(tag), &json, error, sizeof(error)) != 0)
        reply(client, "err %s", error);
    else if (asset_url(json, "SHA256SUMS", url, sizeof(url)) != 0 || asset_url(json, "eboot.bin", url, sizeof(url)) != 0)
        reply(client, "err release %s lacks SHA256SUMS or eboot.bin", tag);
    else
        reply(client, "ok %s", tag);
    free(json);
}

#define UPDATE_MAX_FILES 64

static void update_apply(int client, const char *repo, const char *title)
{
    if (!valid_repo(repo) || !valid_title(title))
    {
        reply(client, "err usage: update-apply <owner/repo> <TITLEID>");
        return;
    }
    char folder[128];
    snprintf(folder, sizeof(folder), "/data/homebrew/%s", title);
    if (!is_folder(folder))
    {
        reply(client, "err %s is not installed in /data/homebrew", title);
        return;
    }
    char tag[64], error[200], url[600];
    char *json = NULL;
    if (latest_release(repo, tag, sizeof(tag), &json, error, sizeof(error)) != 0)
    {
        reply(client, "err %s", error);
        return;
    }
    char *sums = NULL;
    if (asset_url(json, "SHA256SUMS", url, sizeof(url)) != 0 ||
        http_get(url, &sums, NULL, 64 * 1024, NULL, NULL) != 200 || sums == NULL)
    {
        free(json);
        free(sums);
        reply(client, "err could not download SHA256SUMS");
        return;
    }
    // The files SHA256SUMS lists
    static char paths[UPDATE_MAX_FILES][168], hashes[UPDATE_MAX_FILES][65];
    int count = 0, has_eboot = 0;
    for (const char *line = sums; line != NULL && *line != '\0' && count < UPDATE_MAX_FILES;)
    {
        char hex[65] = {0}, file[200] = {0};
        if (sscanf(line, "%64s %199s", hex, file) == 2 && strlen(hex) == 64)
        {
            const char *f = file[0] == '*' ? file + 1 : file;
            if (!safe_path(f))
            {
                free(json);
                free(sums);
                reply(client, "err SHA256SUMS names an unsafe path: %s", f);
                return;
            }
            snprintf(paths[count], sizeof(paths[count]), "%s", f);
            for (int i = 0; i < 64; i++)
                hashes[count][i] = (char)(hex[i] >= 'A' && hex[i] <= 'F' ? hex[i] + 32 : hex[i]);
            hashes[count][64] = '\0';
            has_eboot |= strcmp(f, "eboot.bin") == 0;
            count++;
        }
        line = strchr(line, '\n');
        if (line != NULL)
            line++;
    }
    free(sums);
    if (!has_eboot)
    {
        free(json);
        reply(client, "err SHA256SUMS does not list eboot.bin");
        return;
    }
    // Each file to <path>.new, checked; any failure removes them all and changes nothing
    int fetched = 0;
    for (int i = 0; i < count; i++)
    {
        char asset[200], target[320];
        snprintf(asset, sizeof(asset), "%s", paths[i]);
        for (char *c = asset; *c != '\0'; c++)
            if (*c == '/')
            {
                memmove(c + 2, c + 1, strlen(c + 1) + 1);
                c[0] = '_';
                c[1] = '_';
                c++;
            }
        snprintf(target, sizeof(target), "%s/%s.new", folder, paths[i]);
        make_parents(target);
        FILE *file = NULL;
        sha256_t hash;
        sha256_init(&hash);
        int status = -1;
        if (asset_url(json, asset, url, sizeof(url)) == 0 && (file = fopen(target, "wb")) != NULL)
        {
            status = http_get(url, NULL, NULL, 256u << 20, file, &hash);
            fclose(file);
        }
        char got[65];
        sha256_hex(&hash, got);
        if (status != 200 || strcmp(got, hashes[i]) != 0)
        {
            log_line("cflaunch: %s (asset %s) status %d sha256 %s, expected %s", paths[i], asset, status, got, hashes[i]);
            for (int j = 0; j <= i; j++)
            {
                snprintf(target, sizeof(target), "%s/%s.new", folder, paths[j]);
                unlink(target);
            }
            free(json);
            reply(client, "err %s: %s - nothing was changed", paths[i],
                  status != 200 ? "could not download it" : "SHA-256 does not match");
            return;
        }
        fetched++;
    }
    free(json);
    // All verified: put them in place
    for (int i = 0; i < count; i++)
    {
        char from[320], to[320];
        snprintf(from, sizeof(from), "%s/%s.new", folder, paths[i]);
        snprintf(to, sizeof(to), "%s/%s", folder, paths[i]);
        if (rename(from, to) != 0)
        {
            reply(client, "err could not replace %s: %s", to, strerror(errno));
            return;
        }
    }
    log_line("cflaunch: installed %s %s (%d files) into %s", repo, tag, fetched, folder);
    reply(client, "ok %s %d", tag, fetched);
}

// Answer one command. Returns 0 to keep serving, -1 to leave the console.
static int handle_command(int client, char *line)
{
    char *const command = strtok(line, " \t");
    if (command == NULL)
    {
        reply(client, "err empty");
        return 0;
    }
    const char *const title = strtok(NULL, " \t");
    if (strcmp(command, "ping") == 0)
    {
        reply(client, "ok cflaunch 8 pid=%d", (int)getpid());
        return 0;
    }
    if (strcmp(command, "status") == 0)
    {
        char running[32] = {0};
        const uint32_t app_id = running_app(running, sizeof(running));
        if (app_id == PS5VKCTL_NO_APP)
            reply(client, "ok idle");
        else
            reply(client, "ok running app=%u title=%s", app_id, running);
        return 0;
    }
    if (strcmp(command, "users") == 0)
    {
        login_user_id_list_t users;
        memset(&users, 0, sizeof(users));
        const int list_result = sceUserServiceGetLoginUserIdList(&users);
        char list[64] = {0};
        if (list_result == 0)
        {
            for (int index = 0; index < 4; index++)
            {
                char entry[12] = {0};
                snprintf(entry, sizeof(entry), "%s%d", index == 0 ? "" : ",", users.user_id[index]);
                strncat(list, entry, sizeof(list) - strlen(list) - 1);
            }
        }
        reply(client, "ok users init=0x%08x foreground=%d list=0x%08x [%s]", g_user_init,
              foreground_user(), (uint32_t)list_result, list);
        return 0;
    }
    if (strcmp(command, "procs") == 0)
    {
        char running[32] = {0};
        const uint32_t app_id = running_app(running, sizeof(running));
        int pids[PS5VKCTL_MAX_PIDS] = {0};
        const int count = app_id == PS5VKCTL_NO_APP ? 0 : app_pids(app_id, pids, PS5VKCTL_MAX_PIDS);
        char list[PS5VKCTL_MAX_PIDS * 12] = {0};
        for (int index = 0; index < count; index++)
        {
            char entry[16] = {0};
            snprintf(entry, sizeof(entry), "%s%d", index == 0 ? "" : ",", pids[index]);
            strncat(list, entry, sizeof(list) - strlen(list) - 1);
        }
        reply(client, "ok procs app=%u title=%s count=%d pids=%s", app_id,
              app_id == PS5VKCTL_NO_APP ? "" : running, count, list);
        return 0;
    }
    if (strcmp(command, "list") == 0)
    {
        list_titles(client);
        return 0;
    }
    if (strcmp(command, "art") == 0)
    {
        send_art(client, title);
        return 0;
    }
    if (strcmp(command, "playtime") == 0)
    {
        send_playtime(client);
        return 0;
    }
    if (strcmp(command, "storage") == 0)
    {
        send_storage(client);
        return 0;
    }
    if (strcmp(command, "backup") == 0 || strcmp(command, "restore") == 0)
    {
        backup_or_restore(client, title, strcmp(command, "restore") == 0);
        return 0;
    }
    if (strcmp(command, "game-backup") == 0)
    {
        backup_game_app(client, title, strtok(NULL, " \t"));
        return 0;
    }
    if (strcmp(command, "log") == 0)
    {
        send_log(client);
        return 0;
    }
    if (strcmp(command, "update-check") == 0)
    {
        update_check(client, title);
        return 0;
    }
    if (strcmp(command, "update-apply") == 0)
    {
        update_apply(client, title, strtok(NULL, " \t"));
        return 0;
    }
    if (strcmp(command, "size") == 0)
    {
        if (title == NULL || !is_title_id(title))
            reply(client, "err usage: size <TITLEID>");
        else
            reply(client, "ok %lld", title_bytes(title));
        return 0;
    }
    if (strcmp(command, "where") == 0)
    {
        where_title(client, title);
        return 0;
    }
    if (strcmp(command, "icon") == 0)
    {
        send_icon(client, title);
        return 0;
    }
    if (strcmp(command, "quit") == 0)
    {
        reply(client, "ok bye");
        return -1;
    }
    if (!valid_title(title))
    {
        reply(client, "err usage: %s needs a nine-character title id", command);
        return 0;
    }
    if (strcmp(title, refused_title) == 0)
    {
        reply(client, "err refused %s is the default profile", title);
        return 0;
    }
    if (strcmp(command, "switch") == 0)
    {
        const char *const target = strtok(NULL, " \t");
        const char *const option = strtok(NULL, " \t");
        const int come_back = option != NULL && strcmp(option, "return") == 0;
        if (!valid_title(target) || strcmp(target, refused_title) == 0)
        {
            reply(client, "err usage: switch <FROM> <TO>, two title ids");
            return 0;
        }
        if (strcmp(title, target) == 0)
        {
            // Starting a title while it is still closing froze the console
            log_line("cflaunch: refused to restart %s from inside itself", title);
            reply(client, "err refused: a title cannot restart itself; open it again from the home screen");
            return 0;
        }
        // Answer first: the caller quits itself through the shell as soon as it
        // has the answer. Wait for it to be gone (up to 15 s); only a caller
        // that hangs is closed from outside.
        reply(client, "ok switching %s -> %s", title, target);
        char detail[160] = {0};
        char running[32] = {0};
        bool gone = false;
        for (int wait = 0; wait < 150 && !gone; wait++)
        {
            const uint32_t app = running_app(running, sizeof(running));
            gone = app == PS5VKCTL_NO_APP || strncmp(running, title, PS5VKCTL_TITLE) != 0;
            if (!gone)
                usleep(100000);
        }
        if (!gone)
        {
            const int status = close_title(title, detail, sizeof(detail));
            log_line("cflaunch: %s did not quit; closed it: %d %s", title, status, detail);
            if (status < 0)
                return 0;
        }
        else
            log_line("cflaunch: %s has quit", title);
        sleep(1);
        const char *name = NULL;
        uint32_t code = 0;
        const int started = start_title(target, &name, &code, detail, sizeof(detail));
        log_line("cflaunch: start %s: %s result=0x%08x %s", target, started == 0 ? "ok" : "failed", code, detail);
        if (started != 0)
        {
            // Bring Cover Flow back rather than leave the console on the home screen
            (void)start_title(title, &name, &code, detail, sizeof(detail));
        }
        else if (strcmp(title, target) != 0)
            watch_start(target, title, come_back);
        return 0;
    }
    if (strcmp(command, "launch") == 0)
    {
        const char *name = NULL;
        uint32_t code = 0;
        char detail[160] = {0};
        if (start_title(title, &name, &code, detail, sizeof(detail)) == 0)
            reply(client, "ok launched %s result=0x%08x %s", title, code, detail);
        else
            reply(client, "err launch %s %s", title, detail);
        return 0;
    }
    if (strcmp(command, "kill") == 0)
    {
        char detail[160] = {0};
        const int status = close_title(title, detail, sizeof(detail));
        if (status == 0)
            reply(client, "ok killed %s: %s", title, detail);
        else if (status == 1)
            reply(client, "ok idle %s: %s", title, detail);
        else
            reply(client, "err kill %s: %s", title, detail);
        return 0;
    }
    if (strcmp(command, "restart") == 0)
    {
        char detail[160] = {0};
        const int status = close_title(title, detail, sizeof(detail));
        if (status < 0)
        {
            reply(client, "err restart %s: %s", title, detail);
            return 0;
        }
        const char *name = NULL;
        uint32_t code = 0;
        if (start_title(title, &name, &code, detail, sizeof(detail)) == 0)
            reply(client, "ok restarted %s result=0x%08x %s", title, code, detail);
        else
            reply(client, "err restart %s %s", title, detail);
        return 0;
    }
    reply(client, "err unknown %s", command);
    return 0;
}

// Read one command line and answer it. Returns 0 to keep serving, -1 to leave.
static int serve_client(int client)
{
    char line[PS5VKCTL_LINE] = {0};
    size_t used = 0;
    while (used + 1 < sizeof(line))
    {
        const ssize_t bytes = recv(client, line + used, sizeof(line) - 1 - used, 0);
        if (bytes <= 0)
            break;
        used += (size_t)bytes;
        line[used] = '\0';
        if (strchr(line, '\n') != NULL)
            break;
    }
    while (used > 0 && (line[used - 1] == '\n' || line[used - 1] == '\r'))
        line[--used] = '\0';
    if (used == 0)
        return 0;
    log_line("ps5vkctl: command %s", line);
    return handle_command(client, line);
}

int main(int argc, char *argv[])
{
    unsigned port = PS5VKCTL_PORT;
    if (argc > 1)
    {
        const unsigned long requested = strtoul(argv[1], NULL, 10);
        if (requested > 0 && requested < 65536)
            port = (unsigned)requested;
    }
    (void)signal(SIGPIPE, SIG_IGN);
    // The launch calls need the user service, and an uninitialized one answers
    // nothing: initialize it once, whatever the result, and report it.
    g_user_init = sceUserServiceInitialize(NULL);
    log_line("ps5vkctl: starting, port %u, pid %d, user service 0x%08x", port, (int)getpid(),
             (uint32_t)g_user_init);

    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0)
    {
        log_line("ps5vkctl: socket: %s", strerror(errno));
        return 1;
    }
    int reuse = 1;
    (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);
    // An older launcher still holding the port is asked to leave ("quit"),
    // then the port is tried again, a few times
    int bound = bind(server, (struct sockaddr *)&address, sizeof(address));
    for (int attempt = 0; bound != 0 && errno == EADDRINUSE && attempt < 5; attempt++)
    {
        log_line("cflaunch: port %u is taken: asking the running launcher to quit", port);
        const int old = socket(AF_INET, SOCK_STREAM, 0);
        if (old >= 0)
        {
            struct sockaddr_in local;
            memset(&local, 0, sizeof(local));
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            local.sin_port = htons((uint16_t)port);
            if (connect(old, (struct sockaddr *)&local, sizeof(local)) == 0)
            {
                (void)send(old, "quit\n", 5, 0);
                char answer[64];
                (void)recv(old, answer, sizeof(answer), 0);
            }
            close(old);
        }
        sleep(2);
        bound = bind(server, (struct sockaddr *)&address, sizeof(address));
    }
    if (bound != 0 || listen(server, 4) != 0)
    {
        log_line("ps5vkctl: bind or listen failed: %s", strerror(errno));
        close(server);
        return 1;
    }
    log_line("cflaunch 8: listening");
    for (;;)
    {
        struct pollfd ready = {server, POLLIN, 0};
        const int events = poll(&ready, 1, 1000);
        if (events == 0)
        {
            watch_tick();
            continue;
        }
        if (events < 0 && errno == EINTR)
            continue;
        const int client = accept(server, NULL, NULL);
        if (client < 0)
        {
            if (errno == EINTR)
                continue;
            log_line("ps5vkctl: accept failed: %s", strerror(errno));
            break;
        }
        const int leaving = serve_client(client);
        close(client);
        if (leaving != 0)
        {
            log_line("ps5vkctl: leaving");
            break;
        }
    }
    close(server);
    return 0;
}
