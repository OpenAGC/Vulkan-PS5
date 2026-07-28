#ifndef VULKAN_PS5_SYSTEM_SERVICE_EXIT_H
#define VULKAN_PS5_SYSTEM_SERVICE_EXIT_H

#if defined(OPENAGC_PROSPERO)

#include <stdint.h>
#include <stdio.h>

int sceSystemServiceGetAppStatus(void *status);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);
int sceKernelUsleep(unsigned int microseconds);

static _Noreturn void vulkan_ps5_system_service_exit(const char *name)
{
    uint32_t app_status[0x100u / sizeof(uint32_t)] = {0};
    int status_result = sceSystemServiceGetAppStatus(app_status);
    uint32_t app_id = app_status[2];
    if (app_id < 0x10u || app_id == UINT32_MAX)
        app_id = app_status[0];
    if (status_result != 0 || app_id < 0x10u || app_id == UINT32_MAX) {
        printf("%s: system-exit status failure result=0x%x app=0x%x\n",
            name, status_result, app_id);
        fflush(NULL);
    } else {
        printf("%s: system-exit app=0x%x\n", name, app_id);
        fflush(NULL);
        int kill_result = sceSystemServiceKillApp((int)app_id, 0, 0, 0);
        printf("%s: system-exit unexpected return result=0x%x\n",
            name, kill_result);
        fflush(NULL);
    }
    for (;;)
        sceKernelUsleep(1000000u);
}

#endif

#endif
