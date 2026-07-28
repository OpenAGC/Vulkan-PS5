#include <stdint.h>
#include <stdio.h>

int sceSystemServiceGetAppStatus(void *status);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);
int sceKernelUsleep(unsigned int microseconds);

static _Noreturn void remain_loaded(void) {
    for (;;)
        sceKernelUsleep(1000000);
}

int main(void) {
    uint32_t status[0x100 / sizeof(uint32_t)] = {0};
    const int status_result = sceSystemServiceGetAppStatus(status);
    uint32_t app_id = status[2];
    if (app_id < 0x10u || app_id == UINT32_MAX)
        app_id = status[0];
    if (status_result != 0 || app_id < 0x10u || app_id == UINT32_MAX) {
        printf("system-exit-probe: status failure result=0x%x app=0x%x\n",
               status_result, app_id);
        fflush(NULL);
        remain_loaded();
    }

    printf("system-exit-probe: ready app=0x%x\n", app_id);
    fflush(NULL);
    const int result = sceSystemServiceKillApp((int)app_id, 0, 0, 0);
    printf("system-exit-probe: unexpected return result=0x%x\n", result);
    fflush(NULL);
    remain_loaded();
}
