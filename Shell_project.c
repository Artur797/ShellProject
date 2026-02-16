//Artur Vargas Carrión
/**
UNIX Shell Project

Sistemas Operativos
Grados I. Informatica, Computadores & Software
Dept. Arquitectura de Computadores - UMA

Some code adapted from "Fundamentos de Sistemas Operativos", Silberschatz et al.

To compile and run the program:
   $ gcc Shell_project.c job_control.c -o Shell
   $ ./Shell          
	(then type ^D to exit program)

**/

#include "job_control.h"   // remember to compile with module job_control.c 
#include <stdlib.h>
#include <string.h>
#include "parse_redir.h"
#include <fcntl.h>
#include <pthread.h>

#define MAX_LINE 256 /* 256 chars per line, per command, should be enough. */

job * tareas; //lista de tareas

//Delay-thread
typedef struct { //Guarda el tiempo a esperar y el comando a ejecutar
	int time;
	char** command;
} delay;


void manejador(int signal){ //Para que no se queden zombie los hijos
	block_SIGCHLD();
	pid_t pid;
	job * aux = tareas;
	enum status status_res;
	int status, info;
	pid_t pid_fork;
	

	while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED))>0){
		aux = get_item_bypid(tareas,pid); //Saco de la lista el comando
		if(aux !=NULL){ //Es el de la lista
			status_res = analyze_status(status, &info); //Miro que le ha pasado
			

			if(aux -> state == RESPAWNEABLE){ //Si es respawneable
				printf("Respawneable pid: %d, command: %s, %s, info: %d\n", pid, aux -> command, status_strings[status_res], info);

			}else{ //Si no es respawneable está en background
				printf("Background pid: %d, command: %s, %s, info: %d\n", pid, aux -> command, status_strings[status_res], info);

			}

			if(status_res == SUSPENDED){ //Cambio el estado de mi lista
				aux -> state = STOPPED;
			}
			else if(status_res == EXITED || status_res == SIGNALED){ 
				if(aux -> state == RESPAWNEABLE){
					pid_fork=fork(); //creo un nuevo proceso y lo lanzo de nuevo
					if(pid_fork==-1){
						perror("fork");
					}
					else if(pid_fork == 0) { //Hijo
						new_process_group(getpid());
						restore_terminal_signals();
						execvp(aux -> command, aux -> args);  //Ejecutamos de nuevo
						fprintf(stderr,"ERROR, command not found: %s\n", aux -> command); //Si se ejecuta esta linea es que el execvp ha fallado
						exit(-1);			

					}else{ //Padre
						printf("Respawneable job running again... pid: %d, command: %s\n", pid_fork, aux->command);
						new_process_group(pid_fork);
						aux -> pgid = pid_fork;
					}
				}
				else{ //Si no es respawneable lo borro de la lista
					if(delete_job(tareas, aux)==0){
						printf("No se pudo borrar la tarea\n");
					}
				}
				
			}else{ //Cambio el estado de mi lista
				aux -> state = BACKGROUND;
			}
		}		
	}
	unblock_SIGCHLD();

}

void manejador_sighup(int signal){ 
	FILE *fp;
	fp=fopen("hup.txt","a"); // abre un fichero en modo 'append'
	if(fp==NULL){
		perror("Error al crear el fichero");
		return;
	}
    fprintf(fp, "SIGHUP recibido.\n"); //escribe en el fichero 
	fclose(fp);	
}

void* alarmFunc(void* arg){ //alarm-thread
	int segundo = *((int *) arg); //saca el primer argumento del array
	pid_t pid = *(pid_t *)((int*)arg +1); //saca el segundo argumento del array
	
	sleep(segundo); //duerme
	killpg(pid,SIGKILL); //mata al otro proceso y a sus hijos

	return NULL; //Los void* necesitan un return
	
}

void* delayFunc(void* arg) { //delay-thread
	delay* args = (delay*) arg; //saco el struct
	//divido el struct
	int segundo = args->time; 
	char** command = args->command;  //Es char** porque es un array de un string
				

    sleep(segundo); // duerme

	int pid_fork = fork(); //Parecido a lo que hacemos en el main
		
		if(pid_fork == -1){
			perror("fork");
		}		
		else if(pid_fork == 0){ //El hijo hace la acción con execvp
			pid_fork = getpid();
			new_process_group(pid_fork);
			
			execvp(command[0], command);  
			fprintf(stderr,"ERROR, command not found: %s\n", command[0]); //Si se ejecuta esta linea es que el execvp ha fallado
			exit(-1);
			
		}else { //Padre
			new_process_group(pid_fork);			
			// Solo ponemos la parte de background porque siempre se lanza en background
			block_SIGCHLD();
			add_job(tareas, new_job(pid_fork,command[0], BACKGROUND));
            printf("Background job running... pid: %d, command: %s\n", pid_fork, command[0]);
			unblock_SIGCHLD();			
        }

		int n = 0;

		while(command[n] != NULL){ //Libero la memoria del struct
			free(command[n]);
			n++;
		}

		free(command);
		free(args);


	return NULL;
}

// -----------------------------------------------------------------------
//                            MAIN          
// -----------------------------------------------------------------------

int main(void)
{
	char inputBuffer[MAX_LINE]; /* buffer to hold the command entered */
	int background;             /* equals 1 if a command is followed by '&' */
	int respawneable;			/* equals 1 if a command is followed by '+' */
	char *args[MAX_LINE/2];     /* command line (of 256) has max of 128 arguments */
	// probably useful variables:
	int pid_fork, pid_wait; /* pid for created and waited process */
	int status;             /* status returned by wait */
	enum status status_res; /* status processed by analyze_status() */
	int info;
	int pid_Shell = getpid();				/* info processed by analyze_status() */
	tareas = new_list("Tareas");

	//Alarm-Thread
	int time;
	int alarma; //Bool
	pthread_t alarm_thread;
	int error_alarmThread;

	//delay-thread
	pthread_t delay_thread;
	int errorDelay_thread;

	//Mask
	int mask[32];
	int mascara=0; //Booleano que dice si hay


	//Bgteam
	int veces=1;

	signal(SIGCHLD, manejador);
	signal(SIGHUP, manejador_sighup);

	ignore_terminal_signals();
	while (1)   /* Program terminates normally inside get_command() after ^D is typed*/
	{   		
		alarma=0; //Inicializo alarma a falso
		printf("COMMAND->");
		fflush(stdout);
		get_command(inputBuffer, MAX_LINE, args, &background, &respawneable);  /* get next command */
		
		if(args[0]==NULL) continue;   // if empty command
		
		if(strcmp(args[0],"cd")==0){
			if(args[1]==NULL){
				char* home = getenv("HOME");
				if(chdir(home) == -1){ //Si no se le pasan parametros te lleva al directorio home
					fprintf(stderr,"No se puede cambiar al directorio %s\n", home);
				}	 
			}else{
				if(chdir(args[1]) == -1){ //Te lleva al directorio indicado
					fprintf(stderr,"No se puede cambiar al directorio %s\n", args[1]);
				}
			}
			continue;
		}

		else if(strcmp(args[0],"jobs")==0){ //Imprime la lista de tareas
			print_job_list(tareas);
			continue;
		}

		else if(strcmp(args[0],"fg")==0){
			block_SIGCHLD();
			char nombre[30];
			job * aux = tareas; 
			if(args[1] != NULL){ //Si se especifica apunta a la tarea especificada
				aux = get_item_bypos(tareas, atoi(args[1]));
			}else{ //si no apunto a la primera
				aux = get_item_bypos(tareas, 1);
			}
			if(aux == NULL){
				fprintf(stderr,"No existe esa tarea\n");
			}else{
				pid_fork= aux->pgid;
				if(aux -> state == STOPPED){
					killpg(aux->pgid, SIGCONT);
				}
				strcpy(nombre, aux -> command); //Necesario guardar el nombre antes de eliminarla de la lista
				delete_job(tareas, aux);

				tcsetpgrp(STDIN_FILENO, pid_fork); //le doy el terminañ
				pid_wait = waitpid(pid_fork, &status, WUNTRACED); //espero que termine
				tcsetpgrp(STDIN_FILENO, pid_Shell); //recupero el terminal
				status_res = analyze_status(status, &info);
				printf("Foreground pid: %d, command: %s, %s, info: %d\n", pid_wait, nombre, status_strings[status_res], info);
				if(status_res == SUSPENDED){
					add_job(tareas, new_job(pid_fork,nombre, STOPPED));
				}
			}
			unblock_SIGCHLD();
			continue;
		}

		else if(strcmp(args[0],"bg")==0){
			block_SIGCHLD();
			job * aux = tareas; 
			if(args[1] != NULL){ //Si se especifica apunta a la tarea especificada
				aux = get_item_bypos(tareas, atoi(args[1]));
			}else{ //si no apunto a la primera
				aux = get_item_bypos(tareas, 1);
			}

			if(aux == NULL){
				fprintf(stderr,"No existe esa tarea\n");
			}else if(aux -> state == STOPPED || aux -> state == RESPAWNEABLE ){
				aux -> state = BACKGROUND;
				killpg(aux->pgid, SIGCONT); //La pongo en segundo plano con SIGCONT
			}

			unblock_SIGCHLD();
			continue;

		}


		else if(strcmp(args[0],"alarm-thread")==0){
			if(args[1] != NULL && args[2] != NULL){ //Si detrás de alarm-thread hay un comando
				alarma = 1; //Indico que hay un alarm-thread activo
				time = atoi(args[1]); //guardo el tiempo
				
				if(time <= 0){
					printf("Hay que pasar un numero positivo despues de alarm-thread");
					continue;
				}

				int i = 0;
				
				//Una vez guardado el comando y el tiempo, se pasan todos los argumentos dos lugares a la izquierda para que el principio este en el args[0]
				while(args[i+2]!= NULL){ //mientras que haya comando
					args[i]=strdup(args[i+2]);
					args[i+2] = NULL;
					i++;
				}
				args[i]=NULL;
			}else{
				printf("Error, comando mal estructurado");
			}
		}

		else if(strcmp(args[0],"delay-thread")==0){
			if(args[1] != NULL && args[2] != NULL){ //Si detrás de delay-thread hay un comando
				time = atoi(args[1]); //guardo el tiempo
				if(time < 0){
					printf("Error. Illegal argument %s\n", args[1]);
					continue;
				}
				delay* delay = malloc(sizeof(delay)); //Reservo memoria para el struct

				int i = 0; //Va a guardar la long del comando

				//Una vez guardado el comando y el tiempo, se pasan todos los argumentos dos lugares a la izquierda para que el principio este en el args[0]
				while(args[i+2]!= NULL){ //mientras que haya comando
					args[i]=strdup(args[i+2]);
					args[i+2] = NULL;
					i++;
				}
				args[i]=NULL;

				char ** cmd = malloc(i*sizeof(char*)); //guardo memoria para el comando
				for(int n = 0; n<i; n++){
					cmd[n]=strdup(args[n]); //Copio el comando
				}

				cmd[i]=NULL;
				
				//Relleno el struct
				delay -> time = time;
				delay -> command = cmd;


				errorDelay_thread = pthread_create(&delay_thread, NULL, delayFunc, (void *) delay); //Creo el thread
                if (errorDelay_thread) {
                    fprintf(stderr, "Error creating thread: %s\n", strerror(errorDelay_thread));
                    continue;
                }
                pthread_detach(delay_thread); // Detach el hilo para que se limpie automáticamente al finalizar
				
				continue;
            } else {
                printf("Error, comando mal estructurado\n");
            }
            continue;
		}

		else if(strcmp(args[0],"mask")==0){
			if(args[1]==NULL || args[2]==NULL || args[3]==NULL){
				printf("Error. Comando mal estructurado\n");
				continue;
			}

			for(int i=0; i<32; i++){ //Inicializo todo a 0
				mask[i]=0;
			}

			int i = 1; //Guardara la posicion de -c
			int n = 0; //Tendra los valores de las señales a enmascarar

			while(args[i]!=NULL && strcmp(args[i],"-c")!=0){ //Hasta que encuentre -c voy guardando los numeros a enmascarar
				n=atoi(args[i]);
				if(n==0){
					printf("Error. Numero no valido\n");
					i++;
					continue;
				}


				mask[i-1]=n; //Lo meto en el array
				i++;
			}
			i++;

			int m = 0;
			while(args[m+i]!= NULL){ //Muevo el comando al pricipio para que se pueda ejecutar
				args[m]=strdup(args[m+i]);
				args[m+i] = NULL;
				m++;
			}
			args[m]=NULL;


			mascara=1; //Pongo el bool a 1

		}


		else if(strcmp(args[0],"currjob")==0){
			block_SIGCHLD();
			job * aux;
			aux = get_item_bypos(tareas, 1);
			if(aux == NULL){
				printf("No hay trabajo actual\n");
			}else{
				printf("Trabajo actual: PID=%d command=%s\n", aux->pgid, aux->command);
			}
			unblock_SIGCHLD();
			continue;
		}

		else if(strcmp(args[0], "deljob")==0){
			block_SIGCHLD();
			job * aux = get_item_bypos(tareas, 1);
			if(aux==NULL){
				printf("No hay trabajo actual\n");
			}else{
				if(aux->state==STOPPED){
					printf(" No se permiten borrar trabajos en segundo plano suspendidos\n");
				}else{
					printf(" Borrando trabajo actual de la lista de jobs: PID=%d command=%s\n", aux->pgid, aux->command);
					delete_job(tareas, aux);
				}
			}
			unblock_SIGCHLD();
			continue;
		}

		else if(strcmp(args[0], "bgteam")==0){
			if(args[1]==NULL || args[2]==NULL){
				printf(" El comando bgteam requiere dos argumentos\n");
				continue;
			}

			veces=atoi(args[1]);
			if(veces==0){
				continue;
			}

			int i=0;
			while(args[i+2]!= NULL){ //mientras que haya comando
				args[i]=strdup(args[i+2]);
				args[i+2] = NULL;
				i++;
			}
			args[i]=NULL;

			for(int x=0; x<veces; x++){
				pid_fork = fork();

				if(pid_fork == -1){
					perror("fork");
				}		
				else if(pid_fork == 0){ //El hijo hace la acción con execvp
					pid_fork = getpid();
					new_process_group(pid_fork);
					execvp(args[0], args);  //otra opcion: execve(args[0], args, environ)
					fprintf(stderr,"ERROR, command not found: %s\n", args[0]); //Si se ejecuta esta linea es que el execvp ha fallado
					exit(-1);
			
				}else { //Padre
					new_process_group(pid_fork);
					// Background process
					block_SIGCHLD();
					add_job(tareas, new_job(pid_fork,args[0], BACKGROUND));
					printf("Background job running... pid: %d, command: %s\n", pid_fork, args[0]);
					unblock_SIGCHLD();
				}
        
			}
			continue;
		}
			

		/* the steps are:
			 (1) fork a child process using fork()
			 (2) the child process will invoke execvp()
			 (3) if background == 0, the parent will wait, otherwise continue 
			 (4) Shell shows a status message for processed command 
			 (5) loop returns to get_commnad() function
		*/

		
		pid_fork = fork();
		
		
		
		if(pid_fork == -1){
			perror("fork");
		}		
		else if(pid_fork == 0){ //El hijo hace la acción con execvp
			pid_fork = getpid();
			new_process_group(pid_fork);
			if(background==0 && respawneable==0){ //Si esta en primer plano
				tcsetpgrp(STDIN_FILENO, pid_fork);
			}
			restore_terminal_signals();

			//Ampliacion mask
			if(mascara){
				for(int i=0; mask[i]!=0; i++)
				mask_signal(mask[i], 0); //Bloqueo todas las señales que tengo apuntadas
				mascara=0;
			}

			//Redirección de entrada estandar desde fichero
			char *file_in, *file_out;
			parse_redirections(args, &file_in, &file_out);
			if(file_in){
				int fd = open(file_in, O_RDONLY);
				if(fd == -1){
					perror("Open");
					exit(EXIT_FAILURE);
				}
				if(dup2(fd, STDIN_FILENO) == -1){
					perror("dup2");
					exit(EXIT_FAILURE);
				}
				close(fd); //Tras hacer dup2 tengo que cerrar fd
			}

			//Redirección de salida estandar a fichero
			if(file_out){
				int f_out = open(file_out, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
				if(f_out==-1){
					perror("Open");
					exit(EXIT_FAILURE);
				}
				if(dup2(f_out, STDOUT_FILENO) == -1){
					perror("dup2");
					exit(EXIT_FAILURE);
				}
				close(f_out); //Tras hacer dup2 tengo que cerrar f_out
			}

			

			execvp(args[0], args);  //otra opcion: execve(args[0], args, environ)
			fprintf(stderr,"ERROR, command not found: %s\n", args[0]); //Si se ejecuta esta linea es que el execvp ha fallado
			exit(-1);
			
		}else { //Padre
			new_process_group(pid_fork);

			//Ampliación de alarm-thread
			if(alarma){
				int alarm_args[2]; //creo el array para pasarle los parametros a la funcion
				alarm_args[0] = time;
				alarm_args[1] = pid_fork;

				error_alarmThread = pthread_create(&alarm_thread, NULL, alarmFunc, (void*)alarm_args);
				if(error_alarmThread != 0){ //Falla
					printf("Error, thread no se ha podido crear");
					exit(-1);
				}

				error_alarmThread = pthread_detach(alarm_thread);

				if(error_alarmThread != 0){
					printf("Error, thread no se ha podido separar");
					exit(-1);
				}
				alarma=0;
			}


			if(background == 0 && respawneable == 0){ //En foreground espera
				tcsetpgrp(STDIN_FILENO, pid_fork);
				pid_wait = waitpid(pid_fork, &status, WUNTRACED);
				tcsetpgrp(STDIN_FILENO, pid_Shell);
				status_res = analyze_status(status, &info);
				printf("Foreground pid: %d, command: %s, %s, info: %d\n", pid_wait, args[0], status_strings[status_res], info);
				if(status_res == SUSPENDED){
					block_SIGCHLD();
					add_job(tareas, new_job(pid_fork,args[0], STOPPED));
					unblock_SIGCHLD();
				}
				
            } 
			else if(respawneable == 1) { //En modo respawneable
				block_SIGCHLD();
				add_respawneable_job(tareas, new_job(pid_fork,args[0], RESPAWNEABLE), args);
				printf("Respawneable job running... pid: %d, command: %s\n", pid_fork, args[0]);
				unblock_SIGCHLD();
			}
			else { // Background process
				block_SIGCHLD();
				add_job(tareas, new_job(pid_fork,args[0], BACKGROUND));
                printf("Background job running... pid: %d, command: %s\n", pid_fork, args[0]);
				unblock_SIGCHLD();
			}
        }

	} // end while
}
