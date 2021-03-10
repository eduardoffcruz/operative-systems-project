#include <stdlib.h>
#include <stdio.h>
#include <semaphore.h> // include POSIX semaphores
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/fcntl.h> //for O_* flags
#include <sys/types.h>
#include <wait.h>
#include <time.h>


#define CONFIG_FILENAME "config.txt"


typedef struct Config{
    int time_unit; //numero de unidades de tempo por segundo
    int track_len,laps_qnt; //em metros //numero de voltas da corrida
    int teams_qnt; //numero de equipas (minimo de 3 equipas)
    int avaria_time_interval; //nr de unidades de tempo entre novo calculo de avaria 
    int reparacao_min_time, reparacao_max_time; //tempo minimo e maximo de reparacao (em unidades de tempo)
    int fuel_capacity; //capacidade do deposito de combustivel (em litros)
}Config;

typedef struct mem_struct{
    //TODO: preencher à medida das necessidade
    //contém todos os dados necessários à boa gestão da corrida
    int i;
    //...

}mem_struct;


//GLOBAL VARIABLES
Config config;
FILE *log_fp;
int shmid; //shared memory id
mem_struct *shared_memory; 

char curr_time[9]; 


//FUNCTION DECLARATION
void read_config(void);
void update_curr_time(void);
void destroy_all(void);


