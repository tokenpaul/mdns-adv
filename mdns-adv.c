/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://illumos.org.
 *
 *
 * Copyright 2026 token.paul
 */

/*
 * Tiny mDNS Service Advertiser for OmniOS / illumos
 *
 * Reads service definitions from a configuration file and registers them
 * with the system mDNSResponder daemon (mdnsd) via libdns_sd.
 */

#include <dns_sd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>

#define MAX_REFS 50
#define MAX_LINE 512
#define DEFAULT_CONFIG_FILE "/etc/mdnsd.conf"
#define DEFAULT_TIMER_MINUTES 0

static volatile sig_atomic_t keep_running = 1;

int debug_mode = 0;
int timer_on = 0;
char *config_file = DEFAULT_CONFIG_FILE;
int timer_minutes = DEFAULT_TIMER_MINUTES;

#define DEBUG_LOG(fmt, ...) \
    do { if (debug_mode) { printf("[DEBUG] " fmt, ##__VA_ARGS__); fflush(stdout); } } while (0)

static void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

void print_usage(const char *prog_name) {
    printf("Tiny mDNS Advertiser for OmniOS / illumos\n");
    printf("Reads and broadcasts network services defined in a configuration file.\n\n");
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  -c <path>      Specify configuration file path (default: %s)\n", DEFAULT_CONFIG_FILE);
    printf("  -t <minutes>   Set re-registration interval in minutes (default: %d, disabled)\n", DEFAULT_TIMER_MINUTES);
    printf("  -d, --debug    Enable verbose debugging output\n");
    printf("  -h, --help     Show this help and usage message\n\n");
}

void trim_clean(char *str) {
    size_t len = strlen(str);
    if (len == 0) return;

    char *end = str + len - 1;
    while (end >= str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    char *start = str;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    len = strlen(str);
    if (len >= 2 && str[0] == '"' && str[len - 1] == '"') {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
}

void fix_service_type(char *stype) {
    size_t len = strlen(stype);
    if (len > 0 && stype[len - 1] == '.') {
        stype[len - 1] = '\0';
        DEBUG_LOG("Stripped trailing dot from service type.\n");
    }
}

/* Encode space-separated TXT tokens into DNS-SD length-prefixed format */
uint16_t encode_txt_record(char *txt_record, unsigned char *txt_buf, size_t buf_size) {
    uint16_t total_len = 0;
    char *p = txt_record;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        char *token_start = p;
        if (*p == '"') {
            token_start = ++p;
            while (*p != '\0' && *p != '"') p++;
        } else {
            while (*p != '\0' && *p != ' ' && *p != '\t') p++;
        }

        size_t item_len = (size_t)(p - token_start);
        if (*p == '"') p++;

        if (item_len > 0 && item_len < 255 && (total_len + 1 + item_len) <= buf_size) {
            txt_buf[total_len++] = (unsigned char)item_len;
            memcpy(&txt_buf[total_len], token_start, item_len);
            total_len += (uint16_t)item_len;
        }
    }
    return total_len;
}

int register_all_services(DNSServiceRef *refs) {
    FILE *file = fopen(config_file, "r");
    if (!file) {
        perror("Error opening configuration file");
        return -1;
    }

    int ref_count = 0;
    char line[MAX_LINE];
    DEBUG_LOG("Parsing config file: %s\n", config_file);

    while (fgets(line, sizeof(line), file) && ref_count < MAX_REFS) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        line[strcspn(line, "\r\n")] = 0;

        char name[128] = {0};
        char stype[128] = {0};
        char domain[64] = {0};
        int port = 0;
        char txt_record[256] = {0};

        int tokens = sscanf(line, "%[^\t\n] %s %s %d %[^\n]", name, stype, domain, &port, txt_record);
        if (tokens < 4) {
            tokens = sscanf(line, "%s %s %s %d %[^\n]", name, stype, domain, &port, txt_record);
            if (tokens < 4) continue;
        }

        trim_clean(name);
        trim_clean(stype);
        fix_service_type(stype);
        trim_clean(txt_record);
        trim_clean(domain);
        const char *reg_domain = (strlen(domain) == 0 || strcmp(domain, "local") == 0 ||
                          strcmp(domain, "local.") == 0) ? NULL : domain;

        if (strlen(name) == 0 || strlen(stype) == 0) continue;

        unsigned char txt_buf[256];
        uint16_t txt_len = encode_txt_record(txt_record, txt_buf, sizeof(txt_buf));

        printf("Registering: [%s] Type: [%s] Port: [%d]\n", name, stype, port);
        fflush(stdout);

        DNSServiceErrorType err = DNSServiceRegister(
            &refs[ref_count], 0, 0, name, stype, reg_domain, NULL,
            htons(port), txt_len, txt_len ? txt_buf : NULL, NULL, NULL);

        if (err == kDNSServiceErr_NoError) {
            DEBUG_LOG("Successfully registered service index %d.\n", ref_count);
            ref_count++;
        } else {
            fprintf(stderr, " -> Failed to register service '%s': error code %d\n", name, err);
        }
    }
    fclose(file);
    return ref_count;
}

/*
 * Wait up to `seconds` (or indefinitely if seconds <= 0) while draining
 * incoming IPC events from mdnsd on all active DNSServiceRef sockets.
 * Returns 0 on normal timeout/signal, or -1 if an IPC error occurs.
 */
static int wait_and_process(DNSServiceRef *refs, int count, int seconds) {
    time_t deadline = (seconds > 0) ? (time(NULL) + seconds) : 0;

    while (keep_running) {
        fd_set readfds;
        int max_fd = -1;

        FD_ZERO(&readfds);
        for (int i = 0; i < count; i++) {
            int fd = DNSServiceRefSockFD(refs[i]);
            if (fd >= 0 && fd < FD_SETSIZE) {
                FD_SET(fd, &readfds);
                if (fd > max_fd) max_fd = fd;
            }
        }

        if (max_fd < 0) {
            return -1;
        }

        struct timeval tv;
        if (seconds > 0) {
            time_t now = time(NULL);
            if (now >= deadline) return 0;
            time_t rem = deadline - now;
            tv.tv_sec = (rem > 5) ? 5 : rem;
            tv.tv_usec = 0;
        } else {
            tv.tv_sec = 5;
            tv.tv_usec = 0;
        }

        int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            return -1;
        }

        if (ready > 0) {
            for (int i = 0; i < count; i++) {
                int fd = DNSServiceRefSockFD(refs[i]);
                if (fd >= 0 && FD_ISSET(fd, &readfds)) {
                    DNSServiceErrorType err = DNSServiceProcessResult(refs[i]);
                    if (err != kDNSServiceErr_NoError) {
                        fprintf(stderr, "DNSServiceProcessResult error %d on index %d; will re-register.\n", err, i);
                        return -1;
                    }
                    DEBUG_LOG("Processed IPC message from mdnsd on ref %d.\n", i);
                }
            }
        }
    }
    return 0;
}


int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 < argc) {
                config_file = argv[++i];
            } else {
                fprintf(stderr, "Error: -c requires a file path argument.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 < argc) {
                char *endptr = NULL;
                long val = strtol(argv[++i], &endptr, 10);
                if (*endptr != '\0' || val < 0) {
                    fprintf(stderr, "Error: Invalid timer interval value specified.\n");
                    return 1;
                }
                timer_minutes = (int)val;
                timer_on = (timer_minutes > 0) ? 1 : 0;
            } else {
                fprintf(stderr, "Error: -t requires an interval value in minutes.\n");
                return 1;
            }
        } else {
            fprintf(stderr, "Unknown argument: %s\n\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    char *env_debug = getenv("MDNS_DEBUG");
    if (env_debug && (strcmp(env_debug, "1") == 0 || strcasecmp(env_debug, "true") == 0)) {
        debug_mode = 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (debug_mode) {
        printf("[DEBUG] Verbose debugging enabled.\n");
        printf("[DEBUG] Using configuration file: %s\n", config_file);
        printf("[DEBUG] Re-registration timer: %s (%d minutes)\n",
               timer_on ? "enabled" : "disabled", timer_minutes);
    }

    DNSServiceRef refs[MAX_REFS];
    int current_refs = 0;

    while (keep_running) {
        if (current_refs > 0) {
            DEBUG_LOG("Timer reached. Clearing old registrations for fresh broadcast cycle.\n");
            for (int i = 0; i < current_refs; i++) {
                DNSServiceRefDeallocate(refs[i]);
            }
            current_refs = 0;
            sleep(2);
            if (!keep_running) break;
        }

        current_refs = register_all_services(refs);
        if (current_refs <= 0) {
            fprintf(stderr, "No services registered successfully. Retrying in 60 seconds...\n");
            for (int s = 0; s < 60 && keep_running; s++) {
                sleep(1);
            }
            continue;
        }

        if (timer_on == 1) {
            printf("Successfully registered %d advertisement(s). Next cycle in %d minute(s).\n",
                   current_refs, timer_minutes);
            fflush(stdout);
            DEBUG_LOG("Re-register timer active.\n");
            
            wait_and_process(refs, current_refs, timer_minutes * 60);
            
        } else {
            printf("Successfully registered %d advertisement(s). Re-registration disabled.\n",
                   current_refs);
            fflush(stdout);
            DEBUG_LOG("Re-register timer inactive.\n");
            
            wait_and_process(refs, current_refs, 0);
        
        }
    }

    if (current_refs > 0) {
        DEBUG_LOG("Shutting down. Deallocating %d service(s)...\n", current_refs);
        for (int i = 0; i < current_refs; i++) {
            DNSServiceRefDeallocate(refs[i]);
        }
    }

    return 0;
}
