# UNIX Shell Implementation

![Language](https://img.shields.io/badge/Language-C-00599C?style=flat-square&logo=c&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL-FCC624?style=flat-square&logo=linux&logoColor=black)
![Course](https://img.shields.io/badge/Course-Operating%20Systems-red?style=flat-square)

**Shell Project** es un intérprete de comandos para sistemas UNIX desarrollado en C. Este proyecto actúa como interfaz entre el usuario y el Kernel, gestionando la ejecución de procesos, el control de trabajos (Job Control), redirecciones y manipulación avanzada de señales e hilos (Threads).

## Sobre el Proyecto

Este desarrollo forma parte de la asignatura **Sistemas Operativos** de la **Universidad de Málaga (UMA)** (Grados en Ingeniería Informática, Computadores & Software).

El objetivo fue transformar un esqueleto de código básico (`Shell_project.c` incompleto) en una shell robusta, segura y funcional, implementando características avanzadas de concurrencia.

## Funcionalidades Principales

La shell soporta las operaciones estándar de cualquier terminal moderna:

* **Ejecución de Comandos:** Busca y ejecuta binarios del sistema (ej: `ls`, `grep`, `sleep`).
* **Redirecciones:**
    * Entrada (`<`): `sort < archivo.txt`
    * Salida (`>`): `ls > lista.txt`
* **Gestión de Directorios:** Comando interno `cd` (soporta ruta home por defecto).
* **Job Control Básico:**
    * Ejecución en segundo plano (`&`).
    * Listado de tareas (`jobs`).
    * Traer a primer plano (`fg`).
    * Continuar en segundo plano (`bg`).

## Características Avanzadas (Threads & Signals)

Esta implementación destaca por incluir comandos internos exclusivos que manejan concurrencia y señales:

### 1. Trabajos Persistentes (Respawnable Jobs)
Procesos "inmortales" que se reinician automáticamente si terminan o mueren.
* **Sintaxis:** Añadir `+` al final del comando.
* **Comportamiento:** El manejador `SIGCHLD` detecta la muerte del proceso y lo vuelve a lanzar (`fork` + `execvp`).
* **Ejemplo:** `gedit +`

### 2. Ejecución Diferida (Delay-Thread)
Lanza un comando tras un tiempo de espera **sin bloquear la terminal**.
* **Implementación:** Un hilo independiente duerme el tiempo indicado y luego ejecuta el comando en background.
* **Sintaxis:** `delay-thread <segundos> <comando>`
* **Ejemplo:** `delay-thread 5 ls -l`

### 3. Timeout de Ejecución (Alarm-Thread)
Limita el tiempo de vida de un proceso.
* **Implementación:** Un hilo auxiliar monitoriza el tiempo. Si expira, mata el proceso con `SIGKILL`.
* **Sintaxis:** `alarm-thread <segundos> <comando>`
* **Ejemplo:** `alarm-thread 10 ./proceso_largo`

### 4. Enmascaramiento de Señales (Mask)
Ejecuta un comando protegiéndolo de señales específicas (las ignora durante su ejecución).
* **Sintaxis:** `mask <señal_n> ... -c <comando>`
* **Ejemplo:** `mask 2 3 -c sleep 20` (Ignora SIGINT y SIGQUIT).

### 5. Ejecución Múltiple (Bgteam)
Lanza múltiples instancias del mismo comando en segundo plano simultáneamente.
* **Sintaxis:** `bgteam <cantidad> <comando>`
* **Ejemplo:** `bgteam 3 xterm` (Abre 3 terminales).

## Manejo de Señales Específico

| Señal | Comportamiento Implementado |
| :--- | :--- |
| **SIGINT / SIGTSTP** | Ignoradas en la shell principal (para no cerrarla con Ctrl+C/Z), pero restauradas para los procesos hijos. |
| **SIGCHLD** | "Reaper" avanzado. Limpia procesos zombies, gestiona cambios de estado y resucita trabajos *respawnable*. |
| **SIGHUP** | Capturada. No cierra la shell, sino que escribe "SIGHUP recibido." en un archivo de log `hup.txt`. |

## Lista de Comandos Internos

| Comando | Descripción |
| :--- | :--- |
| `cd [dir]` | Cambia el directorio actual. |
| `jobs` | Muestra la lista de trabajos activos. |
| `fg [pos]` | Pasa un trabajo a primer plano (Foreground). |
| `bg [pos]` | Reactiva un trabajo detenido en segundo plano. |
| `currjob` | Muestra información del trabajo actual (cabeza de lista). |
| `deljob` | Elimina el trabajo actual de la lista (si no está suspendido). |
| `bgteam` | Lanza N copias de un proceso en background. |
| `mask` | Enmascara señales para un comando específico. |
| `alarm-thread` | Ejecuta un comando con límite de tiempo. |
| `delay-thread` | Programa un comando para ejecución futura. |

## Compilación e Instalación

Dado que el proyecto utiliza la librería de hilos POSIX, es necesario enlazar con `pthread` al compilar.

```bash
# 1. Compilar el proyecto (asegúrate de incluir -lpthread)
gcc Shell_project.c job_control.c -o Shell -lpthread

# 2. Ejecutar la shell
./Shell

