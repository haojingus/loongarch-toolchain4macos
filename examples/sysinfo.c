/*
 * sysinfo — LoongArch / Linux system info dump.
 *
 * Pure C, glibc-only, no third-party deps. Designed to be a real-ish
 * smoke test for the cross-toolchain at ~/Project/aarch64/loongarch.
 *
 * Build:
 *   . ~/Project/aarch64/loongarch/env.sh
 *   loongarch64-linux-gnu-gcc -O2 sysinfo.c -o sysinfo
 *   scp sysinfo target:/tmp/ && ssh target /tmp/sysinfo
 *
 * Note: dynamic-link only. Static-link with glibc 2.28 segfaults inside
 * getifaddrs (it dlopen's libnss_files at runtime).
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* ---------- helpers ---------- */

static void rule(const char *title) {
    printf("\n\033[1;36m── %s ────────────────────────────────────\033[0m\n", title);
}

static int read_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = 0;
    return (int)n;
}

/* Strip leading/trailing whitespace + trailing quote chars. */
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '"') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' ||
                     e[-1] == '\r' || e[-1] == '"'))
        *--e = 0;
    return s;
}

/* Look up KEY= in os-release-style file. Result borrows from a static buf. */
static const char *kv(const char *path, const char *key) {
    static char buf[8192];
    static char out[512];
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    char *p = buf;
    size_t klen = strlen(key);
    while (*p) {
        char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        if (!strncmp(p, key, klen) && p[klen] == '=') {
            size_t L = (size_t)(eol - p - klen - 1);
            if (L >= sizeof(out)) L = sizeof(out) - 1;
            memcpy(out, p + klen + 1, L);
            out[L] = 0;
            return trim(out);
        }
        p = (*eol) ? eol + 1 : eol;
    }
    return NULL;
}

/* Find first matching ": value" line in /proc-style key:value file. */
static const char *colon_kv(const char *path, const char *key) {
    static char buf[16384];
    static char out[512];
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    char *p = buf;
    size_t klen = strlen(key);
    while (*p) {
        char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        if (!strncmp(p, key, klen) && (p[klen] == '\t' || p[klen] == ' ' || p[klen] == ':')) {
            char *colon = strchr(p, ':');
            if (colon && colon < eol) {
                size_t L = (size_t)(eol - colon - 1);
                if (L >= sizeof(out)) L = sizeof(out) - 1;
                memcpy(out, colon + 1, L);
                out[L] = 0;
                return trim(out);
            }
        }
        p = (*eol) ? eol + 1 : eol;
    }
    return NULL;
}

static void fmt_bytes(uint64_t b, char out[32]) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int u = 0;
    double v = (double)b;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
    snprintf(out, 32, "%.2f %s", v, units[u]);
}

static void fmt_uptime(long secs, char out[64]) {
    long d = secs / 86400;
    long h = (secs % 86400) / 3600;
    long m = (secs % 3600) / 60;
    long s = secs % 60;
    if (d) snprintf(out, 64, "%ld day%s, %02ld:%02ld:%02ld", d, d > 1 ? "s" : "", h, m, s);
    else snprintf(out, 64, "%02ld:%02ld:%02ld", h, m, s);
}

/* ---------- sections ---------- */

static void show_host(void) {
    char host[256] = "?";
    gethostname(host, sizeof(host));
    struct utsname u;
    uname(&u);
    rule("Host");
    printf("  Hostname    : %s\n", host);
    printf("  Architecture: %s\n", u.machine);

    const char *pretty = kv("/etc/os-release", "PRETTY_NAME");
    if (pretty) printf("  Distribution: %s\n", pretty);

    char ver[128];
    if (read_file("/etc/debian_version", ver, sizeof(ver)) > 0)
        printf("  Debian base : %s\n", ver);
}

static void show_kernel(void) {
    struct utsname u;
    if (uname(&u) != 0) return;
    rule("Kernel");
    printf("  Sysname     : %s\n", u.sysname);
    printf("  Release     : %s\n", u.release);
    printf("  Version     : %s\n", u.version);
}

static void show_uptime_load(void) {
    char buf[256];
    rule("Uptime / Load");
    if (read_file("/proc/uptime", buf, sizeof(buf)) > 0) {
        double up = atof(buf);
        char human[64];
        fmt_uptime((long)up, human);
        printf("  Uptime      : %s (%.0f s)\n", human, up);
    }
    if (read_file("/proc/loadavg", buf, sizeof(buf)) > 0) {
        double l1, l5, l15;
        if (sscanf(buf, "%lf %lf %lf", &l1, &l5, &l15) == 3)
            printf("  Load avg    : %.2f / %.2f / %.2f  (1m / 5m / 15m)\n", l1, l5, l15);
    }
}

static void show_cpu(void) {
    rule("CPU");
    /* On LoongArch /proc/cpuinfo lists per-core entries with "model name",
     * "Model Name", "cpu model", or just "Loongson-...". Try a few. */
    const char *model = colon_kv("/proc/cpuinfo", "Model Name");
    if (!model) model = colon_kv("/proc/cpuinfo", "model name");
    if (!model) model = colon_kv("/proc/cpuinfo", "cpu model");
    if (!model) model = colon_kv("/proc/cpuinfo", "system type");
    if (model) printf("  Model       : %s\n", model);

    long online = sysconf(_SC_NPROCESSORS_ONLN);
    long total  = sysconf(_SC_NPROCESSORS_CONF);
    printf("  Cores       : %ld online / %ld configured\n", online, total);

    /* CPU MHz from cpuinfo if present (often blank on LA loonarch). */
    const char *mhz = colon_kv("/proc/cpuinfo", "cpu MHz");
    if (mhz && *mhz) printf("  Frequency   : %s MHz (cpuinfo)\n", mhz);

    /* sysfs cpufreq is more reliable. */
    char path[256], buf[128];
    int printed = 0;
    for (int i = 0; i < (int)online; i++) {
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        if (read_file(path, buf, sizeof(buf)) > 0) {
            long khz = atol(buf);
            if (!printed) {
                printf("  Per-core    : ");
                printed = 1;
            }
            printf("c%d=%.2fGHz%s", i, khz / 1e6, i == online - 1 ? "\n" : "  ");
        }
    }

    const char *isa = colon_kv("/proc/cpuinfo", "isa");
    if (isa) printf("  ISA         : %s\n", isa);
    const char *features = colon_kv("/proc/cpuinfo", "features");
    if (features) printf("  Features    : %s\n", features);
}

static void show_memory(void) {
    rule("Memory");
    char buf[8192];
    if (read_file("/proc/meminfo", buf, sizeof(buf)) <= 0) return;
    long mem_total_kb = 0, mem_avail_kb = 0;
    long swap_total_kb = 0, swap_free_kb = 0;
    char *p = buf;
    while (*p) {
        long kb;
        if (sscanf(p, "MemTotal: %ld", &kb) == 1) mem_total_kb = kb;
        else if (sscanf(p, "MemAvailable: %ld", &kb) == 1) mem_avail_kb = kb;
        else if (sscanf(p, "SwapTotal: %ld", &kb) == 1) swap_total_kb = kb;
        else if (sscanf(p, "SwapFree: %ld", &kb) == 1) swap_free_kb = kb;
        p = strchr(p, '\n');
        if (!p) break;
        p++;
    }
    char tot[32], avail[32], used_s[32];
    long used_kb = mem_total_kb - mem_avail_kb;
    fmt_bytes((uint64_t)mem_total_kb * 1024, tot);
    fmt_bytes((uint64_t)mem_avail_kb * 1024, avail);
    fmt_bytes((uint64_t)used_kb * 1024, used_s);
    double pct = mem_total_kb ? 100.0 * used_kb / mem_total_kb : 0;
    printf("  Total       : %s\n", tot);
    printf("  Used        : %s  (%.1f %%)\n", used_s, pct);
    printf("  Available   : %s\n", avail);
    if (swap_total_kb) {
        char st[32], sf[32];
        fmt_bytes((uint64_t)swap_total_kb * 1024, st);
        fmt_bytes((uint64_t)swap_free_kb * 1024, sf);
        printf("  Swap        : %s total, %s free\n", st, sf);
    }
}

static void show_disk(void) {
    rule("Filesystem");
    static const char *mounts[] = {"/", "/boot", "/home", "/var", "/data", NULL};
    printf("  %-12s %10s %10s %10s   %s\n", "Mount", "Total", "Used", "Free", "Use%");
    for (int i = 0; mounts[i]; i++) {
        struct statvfs s;
        if (statvfs(mounts[i], &s) != 0) continue;
        uint64_t blksz = s.f_frsize;
        uint64_t tot = (uint64_t)s.f_blocks * blksz;
        uint64_t free = (uint64_t)s.f_bavail * blksz;
        uint64_t used = (uint64_t)(s.f_blocks - s.f_bfree) * blksz;
        if (tot == 0) continue;
        char ts[32], us[32], fs[32];
        fmt_bytes(tot, ts);
        fmt_bytes(used, us);
        fmt_bytes(free, fs);
        double pct = 100.0 * (double)used / (double)tot;
        printf("  %-12s %10s %10s %10s   %5.1f %%\n", mounts[i], ts, us, fs, pct);
    }
}

static void show_network(void) {
    rule("Network");
    struct ifaddrs *ia = NULL;
    if (getifaddrs(&ia) != 0) {
        printf("  (getifaddrs failed)\n");
        return;
    }
    for (struct ifaddrs *p = ia; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (!strcmp(p->ifa_name, "lo")) continue;
        char addr[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr,
                  addr, sizeof(addr));
        printf("  %-12s : %s\n", p->ifa_name, addr);
    }
    freeifaddrs(ia);

    /* Gateway from /proc/net/route — first line with non-zero gateway. */
    FILE *f = fopen("/proc/net/route", "r");
    if (f) {
        char line[256];
        fgets(line, sizeof(line), f);  /* header */
        while (fgets(line, sizeof(line), f)) {
            char iface[32];
            unsigned dest, gw, flags, refcnt, use, metric, mask;
            if (sscanf(line, "%31s %x %x %x %u %u %u %x",
                       iface, &dest, &gw, &flags, &refcnt, &use, &metric, &mask) >= 8 &&
                dest == 0 && gw != 0) {
                struct in_addr in = {.s_addr = gw};
                printf("  Gateway      : %s (via %s)\n", inet_ntoa(in), iface);
                break;
            }
        }
        fclose(f);
    }
}

static void show_drm(void) {
    rule("DRM / GPU");
    DIR *d = opendir("/sys/class/drm");
    if (!d) {
        printf("  (no /sys/class/drm)\n");
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "card", 4) != 0) continue;
        if (strchr(e->d_name, '-')) continue;  /* skip card0-HDMI-A-1 */
        char path[256], buf[64];
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/uevent", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char drv[64] = "?", pci_id[32] = "?";
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "DRIVER=%63s", drv) == 1) continue;
            if (sscanf(line, "PCI_ID=%31s", pci_id) == 1) continue;
        }
        fclose(f);
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/vendor", e->d_name);
        char vendor[16] = "?";
        if (read_file(path, vendor, sizeof(vendor)) <= 0) strcpy(vendor, "?");
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/device", e->d_name);
        char dev[16] = "?";
        if (read_file(path, dev, sizeof(dev)) <= 0) strcpy(dev, "?");
        printf("  /dev/dri/%-7s driver=%-12s pci=%s:%s\n",
               e->d_name, drv, vendor, dev);
    }
    closedir(d);
}

static void show_runtime(void) {
    rule("Runtime");
    /* glibc */
    extern const char *gnu_get_libc_version(void);
    printf("  glibc       : %s\n", gnu_get_libc_version());

    /* Build info baked into this binary itself. */
    printf("  Built-by    : GCC " __VERSION__ "\n");
    printf("  Built-on    : " __DATE__ " " __TIME__ "\n");

    char tz[64] = "";
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(tz, sizeof(tz), "%Z (%z)", &tm);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    printf("  Now         : %s %s\n", ts, tz);
}

int main(void) {
    printf("\033[1;33m");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║  LoongArch sysinfo — toolchain smoke / target probe  ║\n");
    printf("╚══════════════════════════════════════════════════════╝");
    printf("\033[0m");

    show_host();
    show_kernel();
    show_uptime_load();
    show_cpu();
    show_memory();
    show_disk();
    show_network();
    show_drm();
    show_runtime();
    printf("\n");
    return 0;
}
