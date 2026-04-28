# Trabajo Final — Sistemas Operativos: Implementación de Scheduler y miniShell

Trabajo final del curso de Sistemas Operativos en el que implementas el núcleo de un **scheduler round-robin** que gestiona procesos reales del sistema operativo, **junto con un miniShell interactivo** para controlarlo. Toda la demás infraestructura (estructuras de datos, bridge de eventos y dashboard web) viene dada. Tu trabajo es escribir **8 funciones** repartidas en `src/scheduler.c` (4 funciones) y `src/shell.c` (4 funciones).

---

## Tabla de Contenidos

1. [¿Qué es miniOS?](#qué-es-minios)
2. [Objetivos de aprendizaje](#objetivos-de-aprendizaje)
3. [Requisitos del sistema](#requisitos-del-sistema)
4. [Setup inicial](#setup-inicial)
5. [Lo que debes implementar](#lo-que-debes-implementar)
6. [Flujos esperados](#flujos-esperados)
7. [Cómo probar tu implementación](#cómo-probar-tu-implementación)
8. [Rúbrica de evaluación](#rúbrica-de-evaluación)
9. [Entrega](#entrega)
10. [Recursos](#recursos)

---

## ¿Qué es miniOS?

miniOS es un simulador educativo de un sistema operativo minimalista. Corre en user-space pero **usa mecanismos reales del kernel** (`fork`, `exec`, `SIGSTOP`, `SIGCONT`, `setitimer`, sockets AF_UNIX) para controlar procesos hijos.

**Arquitectura a alto nivel:**

```
+----------------------------------------------------------+
|  Shell `miniOS>`  -->  Scheduler round-robin  -->  PCBs  |
|        |                       |                         |
|  run <bin>                  SIGALRM                      |
|  ps                       cada slice_ms                  |
|  slice <ms>                                              |
+----------------------------------------------------------+
                              |
                              v
         eventos JSON sobre AF_UNIX -> Bridge -> Dashboard
```

Cuando escribes `run programs/bin/countdown 10` en el shell:
1. miniOS llama `fork()` + `execl()` para crear un proceso hijo real.
2. Lo detiene con `SIGSTOP` y lo encola en la ready queue.
3. Cuando toca su turno, le manda `SIGCONT` y el kernel lo ejecuta.
4. Cuando expira el time slice, `SIGALRM` dispara el handler que hace el context switch.

---

## Objetivos de aprendizaje

Al completar este laboratorio, serás capaz de:

### 1. Crear procesos reales con `fork()` y `execl()`

**Demostrar** cómo el sistema operativo crea un proceso a partir de un ejecutable, gestionando correctamente el estado post-exec y preservando el control sobre el hijo desde el padre.

**Evidencia:** `ps aux | grep <binario>` muestra el proceso hijo con su PID asignado por el kernel y en estado detenido (`T`).

### 2. Controlar la ejecución de procesos hijo con señales POSIX

**Aplicar** las señales `SIGSTOP`, `SIGCONT` y `SIGKILL` para pausar, reanudar y terminar procesos hijo desde el scheduler, comprendiendo que la máquina de estados del proceso es administrada conjuntamente por el kernel y el scheduler.

**Evidencia:** los procesos alternan entre los estados `T` (stopped) y `R` (running) en respuesta a las señales enviadas por miniOS.

### 3. Implementar scheduling round-robin con timer

**Diseñar** un scheduler preemptivo que usa `setitimer(ITIMER_REAL)` + `SIGALRM` como fuente de interrupciones periódicas, y una cola circular FIFO como política de selección.

**Evidencia:** con un time slice de 500 ms y 3 procesos encolados, cada proceso recibe CPU cada ~1500 ms en rotación equitativa.

### 4. Realizar context switching preservando el PCB

**Implementar** el handler de `SIGALRM` que efectúa el cambio de contexto: detiene al proceso actual, actualiza los campos del PCB (estado, CPU acumulado, conteo de switches), lo encola, saca al siguiente de la cola, reanuda su ejecución y emite el evento correspondiente.

**Evidencia:** el Gantt chart del dashboard muestra segmentos alternados sincronizados con el time slice configurado.

### 5. Gestionar terminación de procesos evitando zombies

**Manejar** `SIGCHLD` con `waitpid(WNOHANG)` en un loop para recolectar todos los hijos terminados, actualizar sus PCBs y despachar al siguiente proceso de la cola cuando el que termina era el RUNNING.

**Evidencia:** tras `exit` en miniOS, `ps aux | grep defunct` no muestra procesos zombies.

### 6. Construir una interfaz de usuario tipo shell

**Implementar** comandos de un shell que interactúen con la process table: `ps`, `kill`, `run`, `stats`. Entender cómo una capa de UI debe sincronizarse con el scheduler usando `sigprocmask` para evitar race conditions.

**Evidencia:** los comandos en `miniOS>` reflejan el estado real del scheduler y los cambios de estado se ven en el dashboard.

### 7. Integrar el scheduler con observabilidad

**Emitir** eventos JSON estructurados (`PROCESS_CREATED`, `CONTEXT_SWITCH`, `PROCESS_TERMINATED`) desde el scheduler hacia el monitor, respetando las restricciones de async-signal-safety dentro de los handlers de señal.

**Evidencia:** el dashboard web recibe y visualiza los eventos en tiempo real sincronizados con el comportamiento del scheduler.

### Competencias transversales desarrolladas

- **Lectura de código ajeno:** entender e integrarse con una base de código existente.
- **Programación defensiva en sistemas:** validar retornos de syscalls, manejar casos de error, evitar fugas de recursos.
- **Pensamiento en concurrencia:** razonar sobre código que se ejecuta dentro de un signal handler (qué sí y qué no se puede hacer).
- **Debugging de sistemas:** usar herramientas POSIX (`ps`, `kill`, `lldb`/`gdb`) y logs para diagnosticar problemas.

---

## Requisitos del sistema

Este laboratorio está pensado para correrse en **WSL2 con Ubuntu** (recomendado) o **macOS**. No se probó en Windows nativo.

### En WSL2 / Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y build-essential git
```

Instalar Node.js 18+ (NodeSource):

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
node --version    # debe mostrar v20.x.x
```

Alternativa con `nvm`:

```bash
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh | bash
source ~/.bashrc
nvm install 20
```

### En macOS

```bash
xcode-select --install     # Compilador C + Make
brew install node git
```

> **Nota sobre macOS:** Por SIP (System Integrity Protection), `task_for_pid` está bloqueado y no podemos inspeccionar registros CPU de procesos hijo. Todo lo demás funciona igual. **Si quieres ver los registros en el dashboard, usa WSL2.**

---

## Setup inicial

### 1. Clonar y compilar

```bash
git clone https://github.com/fguzman82/minios-lab.git
cd minios-lab
make
```

`make` compila:

- `./minios` — el scheduler y shell (con las 8 funciones tuyas)
- `programs/bin/*` — los programas de ejemplo que vas a lanzar desde miniOS

> En este punto `./minios` compila pero NO FUNCIONA porque las 8 funciones core están vacías. Ese es tu trabajo.

### 2. Instalar dependencias del bridge y dashboard

```bash
cd bridge && npm install && cd ..
cd dashboard && npm install && cd ..
```

### 3. Verificar que los programas de ejemplo corren standalone

```bash
./programs/bin/countdown 5       # cuenta de 5 a 1
./programs/bin/fibonacci 10      # calcula fib(1)..fib(10)
```

Estos son los programas que tu scheduler lanzará como procesos hijo. Funcionan independientemente; tu scheduler los orquesta.

---

## Estructura del proyecto

```
minios-lab/
├── README.md                      # Este documento
├── ARCHITECTURE.md                # Referencia técnica detallada
├── Makefile                       # Build cross-platform (macOS + Linux)
│
├── src/                           # Scheduler en C
│   ├── main.c                     # Entry point + setup de señales
│   ├── scheduler.c  [★ TUYO 1/2 ★] # 4 funciones a implementar
│   ├── scheduler.h                # Interfaz (no tocar)
│   ├── shell.c      [★ TUYO 2/2 ★] # 4 funciones a implementar
│   ├── shell.h                    # Interfaz (no tocar)
│   ├── pcb.c / pcb.h              # PCB, process table (dado)
│   ├── ready_queue.c / .h         # Cola circular round-robin (dado)
│   ├── timer.c / .h               # setitimer + SIGALRM (dado)
│   ├── monitor.c / .h             # Logger JSON sobre AF_UNIX (dado)
│   └── platform/
│       ├── platform.h             # Interfaz común
│       ├── platform_linux.c       # ptrace + user_regs_struct
│       └── platform_darwin.c      # SIGSTOP/SIGCONT sin ptrace
│
├── programs/                      # Programas de ejemplo (C)
│   ├── countdown.c                # Cuenta regresiva
│   ├── primos.c                   # Buscador de primos (CPU-bound)
│   ├── fibonacci.c                # Fibonacci recursivo
│   ├── artista_ascii.c            # Dibuja patrones ASCII
│   ├── loteria.c                  # Busca 3 dígitos iguales
│   ├── bitacora.c                 # IO-bound a archivo
│   ├── memory_hog.c               # Consumidor de memoria
│   ├── busy_loop.c                # Loop CPU puro
│   ├── ping_pong_server.c / ping_pong_client.c
│   └── productor.c / consumidor.c
│
├── scenarios/                     # Scripts de escenarios
│   ├── cpu_contention.sh
│   ├── io_mix.sh
│   └── carga_variable.sh
│
├── bridge/                        # Bridge Node.js (no tocar)
│   ├── package.json
│   └── index.js
│
└── dashboard/                     # Dashboard React + Vite + Tailwind (no tocar)
    └── src/
        ├── App.jsx
        ├── hooks/useSchedulerEvents.js
        └── components/
            ├── GanttChart.jsx
            ├── ProcessTable.jsx
            ├── RegisterView.jsx
            ├── MetricsPanel.jsx
            └── CaptureControls.jsx
```

**Los únicos archivos que debes modificar son `src/scheduler.c` y `src/shell.c`.** Todo lo demás es infraestructura dada.

### Archivos entregados COMPLETOS (no los modifiques)

Estos archivos ya están implementados al 100%. Léelos como **referencia** (especialmente las APIs que vas a llamar), pero **no cambies su contenido**.

| Archivo | Qué hace | Por qué no lo tocas |
|---|---|---|
| `Makefile` | Compila `./minios` y los programas de `programs/` | Build cross-platform ya resuelto |
| `src/main.c` | Entry point, instala handlers, lanza el shell | Boilerplate de inicio |
| `src/pcb.c` / `src/pcb.h` | Estructura `pcb_t`, `process_table[]`, `process_count` | Estructura de datos central — la usas, no la rediseñas |
| `src/ready_queue.c` / `src/ready_queue.h` | Cola circular FIFO (`rq_enqueue`, `rq_dequeue`, `rq_remove`, `rq_is_empty`) | API que vas a llamar desde `scheduler.c` |
| `src/timer.c` / `src/timer.h` | `setitimer` + manejo de `SIGALRM` (`timer_init`, `timer_start`, `timer_stop`, `timer_get_slice`) | API que vas a llamar desde `scheduler_start` y `scheduler_tick` |
| `src/monitor.c` / `src/monitor.h` | Logger JSON sobre socket AF_UNIX (`monitor_emit_*`) | API que vas a llamar para emitir eventos al dashboard |
| `src/scheduler.h` | **Interfaz** del scheduler | Define las firmas que tú implementas en `scheduler.c` |
| `src/shell.h` | **Interfaz** del shell | Define las firmas que tú implementas en `shell.c` |
| `src/platform/platform.h` | Abstracción cross-platform | API que vas a llamar (`platform_stop_process`, etc.) |
| `src/platform/platform_linux.c` | Implementación Linux (con ptrace) | Específica del SO, no la modificas |
| `src/platform/platform_darwin.c` | Implementación macOS (sin ptrace por SIP) | Específica del SO, no la modificas |
| `programs/*.c` | Programas de ejemplo (`countdown`, `primos`, `fibonacci`, etc.) | Son los procesos que tu scheduler va a lanzar |
| `scenarios/*.sh` | Scripts de carga | Lanzan combinaciones de procesos para probar |
| `bridge/index.js` | Bridge Node.js (Unix socket → WebSocket) | Capa de transporte ya hecha |
| `dashboard/**` | Dashboard React + Vite + Tailwind | UI ya hecha — solo la consumes |
| `ARCHITECTURE.md` | Referencia técnica del sistema completo | Documentación de apoyo |

### Archivos donde DEBES IMPLEMENTAR código

Solo dos archivos. En cada uno hay **4 funciones marcadas con `[TODO]`** y pseudocódigo numerado en los comentarios. Las demás funciones del archivo ya están implementadas y sirven como referencia de estilo — **no las modifiques**.

| Archivo | Funciones a implementar | Pts en rúbrica |
|---|---|---:|
| `src/scheduler.c` | `scheduler_create_process`, `scheduler_start`, `scheduler_tick`, `scheduler_sigchld` | 3.0 |
| `src/shell.c` | `cmd_run`, `cmd_ps`, `cmd_kill_proc`, `cmd_stats` | 2.0 |

**Total: 8 funciones, 5.0 pts.**

---

## Lo que debes implementar

Tu trabajo está dividido en **dos partes**:

### Parte 1 — Scheduler (`src/scheduler.c`)

4 funciones, todas con pseudocódigo numerado en los comentarios:

| Función | Qué hace |
|---|---|
| `scheduler_create_process(path, arg)` | `fork` + `exec` + inicializar PCB + encolar |
| `scheduler_start(slice_ms)` | Desencolar el primer proceso y arrancar el timer |
| `scheduler_tick(signum)` | Context switch (handler de `SIGALRM`) |
| `scheduler_sigchld(signum)` | Detectar terminación (handler de `SIGCHLD`) |

### Parte 2 — Shell (`src/shell.c`)

4 funciones, también con pseudocódigo:

| Función | Qué hace |
|---|---|
| `cmd_run(path, arg)` | Validar path y lanzar proceso con el scheduler |
| `cmd_ps()` | Mostrar la process table y la ready queue |
| `cmd_kill_proc(arg)` | Matar un PID y removerlo de la cola |
| `cmd_stats()` | Calcular y mostrar métricas agregadas |

### Funciones YA implementadas (NO las modifiques)

**En `scheduler.c`:** `scheduler_init`, `scheduler_stop`, `scheduler_install_sigchld`, getters, `timespec_diff_ms`.

**En `shell.c`:** `shell_run` (main loop), `cmd_help`, `cmd_slice`, `cmd_inspect`, `cmd_runpair`, los helpers `block_alarm`/`unblock_alarm`. Úsalas como **referencia de estilo**.

### APIs disponibles

- **POSIX:** `fork`, `execl`, `waitpid`, `kill`, `clock_gettime`, `access`
- **Plataforma (`src/platform/platform.h`):** `platform_trace_child`, `platform_stop_process`, `platform_resume_process`, `platform_detach`, `platform_get_registers`, `platform_uses_ptrace`
- **PCB (`src/pcb.h`):** `pcb_init`, `pcb_print_table`, `pcb_state_name`, acceso a `process_table[]`, `process_count`
- **Ready Queue (`src/ready_queue.h`):** `rq_enqueue`, `rq_dequeue`, `rq_is_empty`, `rq_remove`, `rq_print`
- **Timer (`src/timer.h`):** `timer_init`, `timer_start`, `timer_stop`, `timer_get_slice`
- **Monitor (`src/monitor.h`):** `monitor_emit_created`, `monitor_emit_switch`, `monitor_emit_terminated`, `monitor_emit_registers`

### Reglas de oro (importantes)

1. **No uses `printf` dentro de los handlers de señal** (`scheduler_tick`, `scheduler_sigchld`). No son async-signal-safe. Si necesitas debuguear, usa `write(STDERR_FILENO, "msg\n", 4)`.
2. **Envuelve en `block_alarm()` / `unblock_alarm()`** cualquier código del shell que **lea o modifique** `process_table` o la ready queue — para evitar race con `scheduler_tick`.
3. **`SA_RESTART`** ya está configurado en `sigaction` para que `fgets` del shell no se rompa (no tienes que tocarlo).

---

## Flujos esperados

### Creación de un proceso

```
run programs/bin/countdown 10
       |
       v
+----------------------+
|  fork()              |
|    |                 |
|    +- hijo: exec()   |
|    +- padre: waitpid |
|         SIGSTOP      |
|         PCB init     |
|         enqueue      |
+----------------------+
       |
       v
 Proceso en estado READY,
 detenido con SIGSTOP
```

### Context switch (scheduler_tick)

```
SIGALRM dispara
     |
     v
+--------------------------+
| SIGSTOP al actual        |
| Actualizar PCB saliente  |
| rq_enqueue(saliente)     |
| rq_dequeue() -> siguiente|
| Actualizar PCB entrante  |
| SIGCONT al siguiente     |
| monitor_emit_switch()    |
+--------------------------+
```

### Terminación (scheduler_sigchld)

```
Hijo hace exit()
     |
     v SIGCHLD al padre
+------------------------------+
| waitpid(WNOHANG) en loop     |
| Marcar PCB TERMINATED        |
| monitor_emit_terminated()    |
| Si era RUNNING:              |
|   rq_dequeue(siguiente)      |
|   SIGCONT(siguiente)         |
+------------------------------+
```

---

## Cómo probar tu implementación

Necesitas **3 terminales** abiertas.

### Terminal 1 — Scheduler (miniOS)

```bash
cd minios-lab
make                # recompila si cambiaste el código
./minios
```

Dentro del shell:

```
miniOS> run programs/bin/countdown 20
miniOS> run programs/bin/countdown 20
miniOS> ps            # debe mostrar ambos procesos
miniOS> slice 300     # acelera los context switches
miniOS> stats         # metricas agregadas
miniOS> kill <pid>    # termina un proceso especifico
miniOS> exit          # limpieza (sin zombies)
```

### Terminal 2 — Bridge

```bash
cd minios-lab/bridge
node index.js
```

Debe imprimir que está escuchando en `ws://localhost:8080` y en el socket Unix.

### Terminal 3 — Dashboard

```bash
cd minios-lab/dashboard
npm run dev
```

Abre **http://localhost:5173** en tu navegador.

### Qué observar

- **Gantt Chart** creciendo con segmentos alternados por proceso.
- **Process Table** mostrando los PIDs con sus colores asignados, alternando entre `RUNNING` y `READY`.
- **Metricas** incrementando (total de context switches, CPU acumulada).
- **Event Log** con `PROCESS_CREATED`, `CONTEXT_SWITCH`, `PROCESS_TERMINATED` apareciendo en tiempo real.
- Tras `exit`, verifica: `ps aux | grep defunct` no devuelve resultados.

---

## Rúbrica de evaluación

**Escala: 0.0 -- 5.0** (nota final es un número decimal con un dígito, sin ponderaciones ni letras).

La nota se compone de **10 criterios de 0.5 pts cada uno**, distribuidos en las dos partes del laboratorio.

### Parte 1 -- Scheduler (3.0 pts)

| # | Criterio | Pts | Cómo se verifica |
|---|---|---:|---|
| 1 | **Creación de proceso real.** `scheduler_create_process` invoca `fork` y `execl` correctamente y crea un proceso hijo visible en el sistema. | 0.5 | `run programs/bin/countdown 10` y en otra terminal `ps aux \| grep countdown` muestra el proceso. |
| 2 | **Hijo detenido en READY.** El proceso recién creado queda con estado `PROC_READY` en el PCB y detenido con `SIGSTOP`. | 0.5 | En miniOS: `ps` muestra READY. En SO: `ps -o pid,stat` muestra `T`. |
| 3 | **Arranque del scheduler.** `scheduler_start` desencola el primer proceso, le manda `SIGCONT` y arranca el timer. | 0.5 | El primer proceso lanzado empieza a producir salida tras el `run`. |
| 4 | **Context switch entre 2+ procesos.** `scheduler_tick` alterna la CPU respetando el time slice. | 0.5 | 2 procesos `countdown 30` producen salida intercalada; el Gantt muestra segmentos alternados. |
| 5 | **Time slice respetado y configurable.** Con `slice 500`, switches cada ~500 ms. `slice 200` acelera la alternancia. | 0.5 | Timestamps en el log del bridge o en eventos `CONTEXT_SWITCH`. |
| 6 | **Sin zombies + despacho tras terminación.** `scheduler_sigchld` recoge todos los hijos; si el RUNNING muere, el siguiente es despachado. | 0.5 | Tras `exit` no hay defunct; 3 procesos con tiempos distintos terminan secuencialmente sin intervención. |

### Parte 2 -- Shell (2.0 pts)

| # | Criterio | Pts | Cómo se verifica |
|---|---|---:|---|
| 7 | **`cmd_run` valida y lanza.** Rechaza paths inexistentes, acepta argumento opcional, arranca el scheduler si es el primero. | 0.5 | `run /ruta/falsa` muestra error; `run programs/bin/countdown 15` lanza y empieza el count desde 15. |
| 8 | **`cmd_ps` muestra la process table.** Tabla con PID, nombre, estado, CPU, waiting time, switches; también la ready queue. | 0.5 | `ps` con 3 procesos activos muestra los 3 con sus estados correctos; la ready queue refleja orden de round-robin. |
| 9 | **`cmd_kill_proc` mata y limpia.** Termina el PID, lo saca de la ready queue, marca PCB como TERMINATED. | 0.5 | `kill <pid>` seguido de `ps` ya no muestra el proceso como activo; los otros siguen corriendo. |
| 10 | **`cmd_stats` calcula metricas.** CPU total, switches totales, procesos activos/terminados, promedios. | 0.5 | `stats` tras varios segundos muestra números coherentes con lo observado en el Gantt. |

---

## Entrega

**Debes subir a Teams un único archivo PDF** (el "reporte") que contiene:

1. El **link al fork público** del repositorio en GitHub.
2. El **link al video en YouTube** (máximo 10 minutos).
3. Una sección por cada uno de los **10 criterios de la rúbrica**, con **captura de pantalla** que evidencie el cumplimiento.

El repositorio y el video son accesibles **a través de los links dentro del PDF**; no se entregan por separado.

### 1. Fork del repositorio

Haz un **fork público** de `fguzman82/minios-lab`, implementa tu solución, haz commits frecuentes y `git push origin main`. El link al fork debe aparecer en la primera página del PDF.

### 2. Video explicativo en YouTube (máximo 10 minutos)

Sube un video a **YouTube** (puede ser no listado) donde muestres el sistema completo funcionando. Debe incluir las siguientes escenas **en orden**:

| Escena | Qué demostrar |
|---|---|
| **1. Compilación** | `make clean && make` sin errores ni warnings |
| **2. Arranque** | Levantar los 3 componentes: `./minios`, `node bridge/index.js`, `npm run dev` en dashboard |
| **3. Crear al menos 5 procesos** | Lanzar **como mínimo 5 procesos** con `run` (ej. 2× `countdown`, 2× `primos`, 1× `fibonacci`). Mostrar la salida intercalada en la terminal. |
| **4. Visualización en dashboard (Gantt)** | Mostrar el **Gantt chart** con segmentos alternados de los 5 procesos, cada uno con su color. La Process Table con colores, las métricas actualizándose. |
| **5. Comando `ps` en terminal** | Ejecutar `ps` en miniOS y **explicar qué muestra cada columna** (PID, nombre, estado, CPU, espera, switches) y la ready queue. |
| **6. Cambio de time slice** | Cambiar con `slice 200` y con `slice 1500`. Mostrar el efecto visual en el Gantt y en la salida de los procesos. |
| **7. Comando `kill <pid>`** | Matar al menos un proceso y **verificar en `ps` y en el dashboard** que desaparece. Los demás procesos continúan corriendo. |
| **8. `stats`** | Mostrar las métricas agregadas al final. |
| **9. Limpieza** | `exit` y verificación en otra terminal: `ps aux \| grep defunct` vacío. |

**Formato del video:** audio con tu voz explicando (no solo imágenes mudas), pantalla legible, duración máxima 10 min.

### 3. PDF de reporte con capturas por criterio

Documento PDF que sirve como **checklist visual** para el profesor. Estructura:

#### Portada / primera página
- Nombre completo del estudiante
- Código / identificador del curso
- Fecha de entrega
- **Link al fork del repositorio** (GitHub)
- **Link al video de YouTube**

#### Una sección por cada uno de los 10 criterios de la rúbrica
Cada sección debe incluir:

- **Número y título del criterio** (ej. *"Criterio 4: Context switch entre 2+ procesos"*).
- **Captura de pantalla** que evidencia el cumplimiento (ej. el Gantt mostrando alternancia, `ps` con estados correctos, `ps aux | grep defunct` vacío tras el `exit`, etc.).
- **Descripción breve** (1–3 líneas) explicando qué se ve en la captura.

#### Formato del PDF
- Puede generarse desde cualquier herramienta (Word, Google Docs, LaTeX, Markdown + Pandoc, etc.)
- Capturas **embebidas** en el PDF (no en carpetas separadas)
- Nombre del archivo: `reporte_<apellido>_<nombre>.pdf`

### Dónde entregar

**Subir únicamente el PDF a la tarea correspondiente en Microsoft Teams.**

No se aceptan entregas por correo ni en otros canales. El profesor abrirá el PDF y navegará a los links del repositorio y del video desde allí.

### Fecha de entrega

**Viernes 15 de mayo de 2026** (hora límite según configuración en Teams).

---

## Recursos

### Manuales POSIX (en tu terminal)

```bash
man fork
man execl
man execve
man waitpid
man sigaction
man setitimer
man kill
```

### Lecturas recomendadas

- **Operating Systems: Three Easy Pieces** (Remzi Arpaci-Dusseau) -- capítulos sobre procesos (5--6) y scheduling (7--10). [ostep.org](https://pages.cs.wisc.edu/~remzi/OSTEP/)
- **The Linux Programming Interface** (Michael Kerrisk) -- capítulos 20--21 (Signals), 24--26 (Process Creation/Termination), 55--58 (sockets).

### Referencia técnica del proyecto

El archivo [`ARCHITECTURE.md`](./ARCHITECTURE.md) contiene el diseño completo del sistema: estructuras de datos, flujo de eventos, diferencias entre Linux y macOS. **Léelo antes de empezar.**

### Ayuda y debugging

- `lldb ./minios` (macOS) o `gdb ./minios` (Linux) -- debugger
- `strace ./minios` (Linux) -- trazar syscalls
- `valgrind ./minios` (Linux) -- detectar fugas de memoria

Si te quedas atascado:

1. Relee el pseudocódigo del paso correspondiente en `scheduler.c` o `shell.c`.
2. Mira las funciones de referencia ya implementadas en el mismo archivo (mismo estilo y convenciones).
3. Revisa el archivo `ARCHITECTURE.md` sección 3 (Componentes del Scheduler).
4. Consulta el manual POSIX correspondiente.
5. Pregunta en las horas de consulta del curso.

---

¡Éxitos con el laboratorio!
