#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define EVENT_COUNT 3
#define PROVIDER_DELAY_MS 1000

typedef struct {
    int event_id;
} EventData;

typedef struct {
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cond;
    EventData* data_ptr;
    int ready;
    int done;
} SharedContext;

int context_init(SharedContext* ctx) {
    InitializeCriticalSection(&ctx->cs);
    InitializeConditionVariable(&ctx->cond);
    ctx->data_ptr = NULL;
    ctx->ready = 0;
    ctx->done = 0;
    return 0;
}

void context_destroy(SharedContext* ctx) {
    DeleteCriticalSection(&ctx->cs);
    if (ctx->data_ptr) {
        free(ctx->data_ptr);
        ctx->data_ptr = NULL;
    }
}

DWORD WINAPI provider_thread(LPVOID lpParam) {
    SharedContext* ctx = (SharedContext*)lpParam;
    for (int i = 1; i <= EVENT_COUNT; ++i) {
        Sleep(PROVIDER_DELAY_MS);

        EventData* new_data = (EventData*)malloc(sizeof(EventData));
        if (!new_data) {
            fprintf(stderr, "malloc failed\n");
            EnterCriticalSection(&ctx->cs);
            ctx->done = 1;
            WakeConditionVariable(&ctx->cond);
            LeaveCriticalSection(&ctx->cs);
            return 1;
        }
        new_data->event_id = i;

        EnterCriticalSection(&ctx->cs);
        while (ctx->ready == 1) {
            SleepConditionVariableCS(&ctx->cond, &ctx->cs, INFINITE);
        }
        ctx->data_ptr = new_data;
        ctx->ready = 1;
        WakeConditionVariable(&ctx->cond);
        LeaveCriticalSection(&ctx->cs);

        printf("provided %d\n", new_data->event_id);
    }

    EnterCriticalSection(&ctx->cs);
    while (ctx->ready == 1) {
        SleepConditionVariableCS(&ctx->cond, &ctx->cs, INFINITE);
    }
    ctx->done = 1;
    printf("PROVIDER: all events done.\n");
    WakeConditionVariable(&ctx->cond);
    LeaveCriticalSection(&ctx->cs);

    return 0;
}

DWORD WINAPI consumer_thread(LPVOID lpParam) {
    SharedContext* ctx = (SharedContext*)lpParam;

    while (1) {
        EventData* data_to_process = NULL;

        EnterCriticalSection(&ctx->cs);
        while (ctx->ready == 0 && !ctx->done) {
            SleepConditionVariableCS(&ctx->cond, &ctx->cs, INFINITE);
        }

        if (ctx->ready == 0 && ctx->done) {
            LeaveCriticalSection(&ctx->cs);
            break;
        }

        data_to_process = ctx->data_ptr;
        ctx->data_ptr = NULL;
        ctx->ready = 0;
        WakeConditionVariable(&ctx->cond);
        LeaveCriticalSection(&ctx->cs);

        if (data_to_process) {
            printf("consumed %d\n", data_to_process->event_id);
            free(data_to_process);
        }
    }

    printf("CONSUMER: finished processing.\n");
    return 0;
}

int main(void) {
    SharedContext ctx;
    if (context_init(&ctx) != 0) {
        return 1;
    }

    HANDLE hProvider = CreateThread(NULL, 0, provider_thread, &ctx, 0, NULL);
    if (!hProvider) {
        fprintf(stderr, "CreateThread provider failed: %lu\n", GetLastError());
        context_destroy(&ctx);
        return 1;
    }

    HANDLE hConsumer = CreateThread(NULL, 0, consumer_thread, &ctx, 0, NULL);
    if (!hConsumer) {
        fprintf(stderr, "CreateThread consumer failed: %lu\n", GetLastError());
        EnterCriticalSection(&ctx.cs);
        ctx.done = 1;
        WakeConditionVariable(&ctx.cond);
        LeaveCriticalSection(&ctx.cs);
        WaitForSingleObject(hProvider, INFINITE);
        CloseHandle(hProvider);
        context_destroy(&ctx);
        return 1;
    }

    WaitForSingleObject(hProvider, INFINITE);
    WaitForSingleObject(hConsumer, INFINITE);
    CloseHandle(hProvider);
    CloseHandle(hConsumer);
    context_destroy(&ctx);
    return 0;
}
