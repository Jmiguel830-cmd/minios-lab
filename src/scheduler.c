/*
 * scheduler.c — ESQUELETO DEL LABORATORIO
 *
 * Este archivo contiene el núcleo del scheduler round-robin de miniOS.
 * Las funciones de infraestructura (init, getters, install_sigchld, stop,
 * timespec_diff_ms) ya están implementadas.
 *
 * Tu trabajo es implementar las CUATRO funciones marcadas con [TODO]:
 *   1. scheduler_create_process  — fork + exec + SIGSTOP + PCB init
 *   2. scheduler_start           — arrancar el primer proceso y el timer
 *   3. scheduler_tick            — handler de SIGALRM (context switch)
 *   4. scheduler_sigchld         — handler de SIGCHLD (terminación)
 *
 * REGLAS DE SEGURIDAD EN SEÑALES:
 *   - NO uses printf/fprintf dentro de los handlers.
 */

#include "scheduler.h"
#include "timer.h"
#include "monitor.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <libgen.h>

// Estado global del scheduler
static volatile int current_running = -1;   // índice en process_table del proceso RUNNING, -1 si ninguno
static volatile int scheduler_active = 0;   // 1 si el scheduler está corriendo

// ============================================================
// Helpers ya implementados — NO los modifiques
// ============================================================

double timespec_diff_ms(struct timespec end, struct timespec start) {
    double sec = (double)(end.tv_sec - start.tv_sec);
    double nsec = (double)(end.tv_nsec - start.tv_nsec);
    return sec * 1000.0 + nsec / 1000000.0;
}

void scheduler_init(void) {
    process_count = 0;
    current_running = -1;
    scheduler_active = 0;
    rq_init();
}

int scheduler_get_running(void) {
    return current_running;
}

int scheduler_is_running(void) {
    return scheduler_active;
}

void scheduler_install_sigchld(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = scheduler_sigchld;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
}

void scheduler_stop(void) {
    timer_stop();
    scheduler_active = 0;

    for (int i = 0; i < process_count; i++) {
        if (process_table[i].state != PROC_TERMINATED) {
            kill(process_table[i].pid, SIGKILL);
            int status;
            waitpid(process_table[i].pid, &status, 0);
            process_table[i].state = PROC_TERMINATED;
        }
    }
    current_running = -1;
}


// ============================================================
// [TODO 1/4] scheduler_create_process
// ============================================================
int scheduler_create_process(const char *path, const char *arg) {
    // Paso 1
    if (process_count >= MAX_PROCESSES) {
        fprintf(stderr, "Error: process_table llena (MAX_PROCESSES=%d)\n", MAX_PROCESSES);
        return -1;
    }

    // Paso 2
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    // Paso 3 (HIJO)
    if (pid == 0) {
        if (platform_uses_ptrace()) {
            platform_trace_child();
        }

        if (arg && strlen(arg) > 0) {
            execl(path, path, arg, (char *)NULL);
        } else {
            execl(path, path, (char *)NULL);
        }

        // Si execl retorna, falló
        perror("execl");
        _exit(1);
    }

    // PADRE
    int status = 0;

    // Paso 4 (solo ptrace)
    if (platform_uses_ptrace()) {
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return -1;
        }

        if (!WIFSTOPPED(status)) {
            // No se detuvo como esperábamos
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            fprintf(stderr, "Error: hijo no quedó STOPPED tras exec/ptrace\n");
            return -1;
        }
    }

    // Paso 5: crear PCB
    int idx = process_count;

    char *path_copy = strdup(path);
    if (!path_copy) {
        perror("strdup");
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        return -1;
    }

    char *short_name = basename(path_copy);
    pcb_init(&process_table[idx], pid, short_name);
    free(path_copy);

    // Paso 6: capturar registros iniciales si aplica ptrace
    if (platform_uses_ptrace()) {
        if (platform_get_registers(pid, &process_table[idx].registers) == 0) {
            process_table[idx].regs_valid = 1;
        }
        platform_detach(pid);
    }

    // Paso 7: detener proceso
    if (platform_stop_process(pid) != 0) {
        perror("platform_stop_process");
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        return -1;
    }

    // Paso 8: confirmar STOP
    if (waitpid(pid, &status, WUNTRACED) < 0) {
        perror("waitpid(WUNTRACED)");
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        return -1;
    }

    // Paso 9: listo -> READY, enqueue, monitor
    process_table[idx].state = PROC_READY;
    process_count++;

    rq_enqueue(idx);

    monitor_emit_created(process_table[idx].pid, process_table[idx].name);
    if (process_table[idx].regs_valid) {
        monitor_emit_registers(process_table[idx].pid,
                               process_table[idx].registers.program_counter,
                               process_table[idx].registers.stack_pointer);
    }

    return idx;
}


// ============================================================
// [TODO 2/4] scheduler_start
// ============================================================
void scheduler_start(int slice_ms) {
    // Paso 1
    if (rq_is_empty()) {
        printf("No hay procesos en la ready queue.\n");
        return;
    }

    // Paso 2
    int idx = rq_dequeue();

    // Paso 3
    process_table[idx].state = PROC_RUNNING;
    clock_gettime(CLOCK_MONOTONIC, &process_table[idx].last_started);
    current_running = idx;

    // Paso 4
    platform_resume_process(process_table[idx].pid);

    // Paso 5
    scheduler_active = 1;
    timer_init(slice_ms, scheduler_tick);
    timer_start();
}


// ============================================================
// [TODO 3/4] scheduler_tick — handler de SIGALRM
// ============================================================
void scheduler_tick(int signum) {
    (void)signum;

    // Paso 1
    if (current_running < 0 || !scheduler_active) return;

    // Paso 2
    pcb_t *current = &process_table[current_running];

    // Paso 3
    platform_stop_process(current->pid);

    // Paso 4
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = timespec_diff_ms(now, current->last_started);

    current->cpu_time_ms += elapsed;
    current->state = PROC_READY;
    current->context_switches++;

    // Paso 5
    rq_enqueue(current_running);

    // Paso 6
    if (rq_is_empty()) {
        current_running = -1;
        timer_stop();
        return;
    }

    // Paso 7
    int next_idx = rq_dequeue();
    pcb_t *next = &process_table[next_idx];

    next->state = PROC_RUNNING;
    clock_gettime(CLOCK_MONOTONIC, &next->last_started);

    platform_resume_process(next->pid);
    monitor_emit_switch(current->pid, next->pid, timer_get_slice());

    current_running = next_idx;
}


// ============================================================
// [TODO 4/4] scheduler_sigchld — handler de SIGCHLD
// ============================================================
void scheduler_sigchld(int signum) {
    (void)signum;

    int status = 0;
    pid_t pid;

    // Paso 1
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {

        // Paso 2: ignorar paradas (solo nos interesan terminaciones)
        if (!WIFEXITED(status) && !WIFSIGNALED(status)) {
            continue;
        }

        // Paso 3: localizar PCB
        int idx = -1;
        for (int i = 0; i < process_count; i++) {
            if (process_table[i].pid == pid) {
                idx = i;
                break;
            }
        }
        if (idx < 0) continue;

        if (process_table[idx].state == PROC_TERMINATED) continue;

        int was_running = (idx == current_running);

        // Si estaba corriendo, contabiliza CPU hasta ahora
        if (was_running) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = timespec_diff_ms(now, process_table[idx].last_started);
            process_table[idx].cpu_time_ms += elapsed;
        }

        // Paso 4: marcar terminado + evento
        process_table[idx].state = PROC_TERMINATED;
        monitor_emit_terminated(pid,
                               process_table[idx].cpu_time_ms,
                               process_table[idx].context_switches);

        // Paso 5: si era el RUNNING, despachar siguiente
        if (was_running) {
            current_running = -1;

            if (!rq_is_empty()) {
                int next = rq_dequeue();
                pcb_t *pnext = &process_table[next];
                pnext->state = PROC_RUNNING;
                clock_gettime(CLOCK_MONOTONIC, &pnext->last_started);
                platform_resume_process(pnext->pid);
                current_running = next;
                // el timer puede seguir corriendo; si estaba parado por alguna razón, lo reanudamos
                timer_start();
            } else {
                timer_stop();
                scheduler_active = 0;
            }
        } else {
            // Paso 6: si estaba esperando, quitarlo de la cola
            rq_remove(idx);
        }
    }
}
