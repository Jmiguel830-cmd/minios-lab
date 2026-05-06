#include "shell.h"
#include "scheduler.h"
#include "timer.h"
#include "monitor.h"
#include "pcb.h"
#include "ready_queue.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_LINE 256

static sigset_t alarm_mask;

static void block_alarm(void) {
    sigprocmask(SIG_BLOCK, &alarm_mask, NULL);
}

static void unblock_alarm(void) {
    sigprocmask(SIG_UNBLOCK, &alarm_mask, NULL);
}

static void cmd_help(void) {
    printf("\nComandos disponibles:\n");
    printf("  run <binario>       Lanzar un proceso nuevo\n");
    printf("  ps                  Mostrar tabla de procesos\n");
    printf("  kill <pid>          Terminar un proceso\n");
    printf("  slice <ms>          Cambiar time slice\n");
    printf("  stats               Mostrar metricas\n");
    printf("  help                Ayuda\n");
    printf("  exit                Salir\n\n");
}

// ============================
// ✅ cmd_run
// ============================
static void cmd_run(const char *path, const char *arg) {

    if (!path || strlen(path) == 0) {
        printf("Uso: run <binario> [argumento]\n");
        return;
    }

    if (access(path, X_OK) != 0) {
        printf("Error: '%s' no existe o no es ejecutable.\n", path);
        return;
    }

    int idx = scheduler_create_process(path, arg);
    if (idx < 0) return;

    if (!scheduler_is_running() && !rq_is_empty()) {
        scheduler_start(timer_get_slice());
    }

    printf("Proceso creado: %s\n", path);
}

// ============================
// ✅ cmd_ps
// ============================
static void cmd_ps(void) {

    block_alarm();

    if (process_count == 0) {
        printf("No hay procesos.\n");
        unblock_alarm();
        return;
    }

    printf("\n");
    pcb_print_table();
    printf("\n");
    rq_print();
    printf("\n");

    unblock_alarm();
}

// ============================
// ✅ cmd_kill_proc
// ============================
static void cmd_kill_proc(const char *arg) {

    if (!arg) {
        printf("Uso: kill <pid>\n");
        return;
    }

    int pid = atoi(arg);
    if (pid <= 0) {
        printf("PID invalido\n");
        return;
    }

    block_alarm();

    int found = 0;

    for (int i = 0; i < process_count; i++) {
        if (process_table[i].pid == pid) {
            found = 1;

            kill(pid, SIGKILL);

            int status;
            waitpid(pid, &status, 0);

            process_table[i].state = PROC_TERMINATED;

            rq_remove(i);

            printf("Proceso %d eliminado.\n", pid);
            break;
        }
    }

    if (!found) {
        printf("No existe el PID %d\n", pid);
    }

    unblock_alarm();
}

// ============================
// ✅ cmd_stats
// ============================
static void cmd_stats(void) {

    block_alarm();

    int active = 0;
    int terminated = 0;
    double total_cpu = 0;
    double total_wait = 0;
    int switches = 0;

    for (int i = 0; i < process_count; i++) {

        if (process_table[i].state == PROC_TERMINATED)
            terminated++;
        else
            active++;

        total_cpu += process_table[i].cpu_time_ms;
        total_wait += process_table[i].wait_time_ms;
        switches += process_table[i].context_switches;
    }

    printf("\n=== STATS ===\n");
    printf("Activos: %d\n", active);
    printf("Terminados: %d\n", terminated);
    printf("Slice: %d ms\n", timer_get_slice());
    printf("CPU total: %.2f ms\n", total_cpu);
    printf("Switches: %d\n", switches);

    if (process_count > 0) {
        printf("CPU promedio: %.2f ms\n", total_cpu / process_count);
        printf("Wait promedio: %.2f ms\n", total_wait / process_count);
    }

    printf("\n");

    unblock_alarm();
}

// ============================
// 🧠 LOOP PRINCIPAL
// ============================
void shell_run(void) {

    char line[MAX_LINE];

    sigemptyset(&alarm_mask);
    sigaddset(&alarm_mask, SIGALRM);

    printf("miniOS iniciado. Escribe help\n");

    while (1) {

        printf("miniOS> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        line[strcspn(line, "\n")] = 0;

        char *cmd = strtok(line, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");

        if (!cmd) continue;

        if (strcmp(cmd, "run") == 0) {
            block_alarm();
            cmd_run(arg1, arg2);
            unblock_alarm();
        }

        else if (strcmp(cmd, "ps") == 0) {
            cmd_ps();
        }

        else if (strcmp(cmd, "kill") == 0) {
            cmd_kill_proc(arg1);
        }

        else if (strcmp(cmd, "slice") == 0) {
            cmd_slice(arg1);
        }

        else if (strcmp(cmd, "stats") == 0) {
            cmd_stats();
        }

        else if (strcmp(cmd, "help") == 0) {
            cmd_help();
        }

        else if (strcmp(cmd, "exit") == 0) {
            printf("Saliendo...\n");
            break;
        }

       
