#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

enum {
    KI_PID_OFFSET = 72,
    KI_NAME_OFFSET = 447,
};

int sceSystemServiceGetAppStatus(void *status);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);
int sceKernelUsleep(unsigned int microseconds);

static int find_single_stale_eboot(pid_t *target) {
    const int mib[4] = {1, 14, 8, 0};
    size_t size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0)
        return -1;

    uint8_t *buffer = malloc(size);
    if (!buffer)
        return -1;
    if (sysctl(mib, 4, buffer, &size, NULL, 0) != 0) {
        free(buffer);
        return -1;
    }

    const pid_t self = getpid();
    unsigned matches = 0;
    for (uint8_t *entry = buffer; entry < buffer + size;) {
        const int entry_size = *(const int *)entry;
        if (entry_size <= 0)
            break;
        const pid_t pid = *(const pid_t *)(entry + KI_PID_OFFSET);
        const char *name = (const char *)(entry + KI_NAME_OFFSET);
        if (pid != self && strcmp(name, "eboot.elf") == 0) {
            *target = pid;
            ++matches;
        }
        entry += entry_size;
    }
    free(buffer);
    return matches == 1 ? 0 : (int)matches + 1;
}

int main(void) {
    pid_t target = -1;
    const int found = find_single_stale_eboot(&target);
    if (found == 0) {
        uint32_t status[0x100 / sizeof(uint32_t)] = {0};
        const int status_result = sceSystemServiceGetAppStatus(status);
        uint32_t app_id = status[2];
        if (app_id < 0x10u || app_id == UINT32_MAX)
            app_id = status[0];
        if (status_result == 0 && app_id >= 0x10u && app_id != UINT32_MAX) {
            const int app_result = sceSystemServiceKillApp(
                (int)app_id, 0, 0, 0);
            printf("process-cleanup: kill app=0x%x result=0x%x\n",
                   app_id, app_result);
        } else {
            printf("process-cleanup: cannot resolve app status=0x%x\n",
                   status_result);
        }
        sceKernelUsleep(250000);
        pid_t remaining = -1;
        if (find_single_stale_eboot(&remaining) == 0 && remaining == target) {
            const int result = kill(target, SIGKILL);
            printf("process-cleanup: SIGKILL pid=%d result=%d\n",
                   (int)target, result);
        } else {
            printf("process-cleanup: app-level cleanup removed pid=%d\n",
                   (int)target);
        }
    } else {
        printf("process-cleanup: refusing stale eboot count/status=%d\n", found);
    }
    fflush(NULL);
    kill(getpid(), SIGKILL);
    for (;;)
        pause();
}
