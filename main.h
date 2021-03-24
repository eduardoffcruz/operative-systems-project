//_______________________________Projeto Sistemas Operativos @2021
//Eduardo F. Ferreira Cruz 2018285164
//Gonçalo Marinho Barroso 2019216314

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
#include <string.h>

#define CONFIG_FILENAME "config.txt"
#define LOG_FILENAME "log.txt"

typedef struct Config{
    int time_unit; //numero de unidades de tempo por segundo
    int track_len,laps_qnt; //em metros //numero de voltas da corrida
    int teams_qnt; //numero de equipas (minimo de 3 equipas)
    int avaria_time_interval; //nr de unidades de tempo entre novo calculo de avaria 
    int reparacao_min_time, reparacao_max_time; //tempo minimo e maximo de reparacao (em unidades de tempo)
    int fuel_capacity; //capacidade do deposito de combustivel (em litros)
}Config;

enum box_state_type{LIVRE,OCUPADA,RESERVADA};
enum race_state_type{OFF,ON,PAUSE}; //se está a decorrer corrida ou não
enum car_state_type{CORRIDA,SEGURANCA,BOX,DESISTENCIA,TERMINADO};
typedef struct mem_struct{
    //TODO: preencher à medida das necessidade
    //contém todos os dados necessários à boa gestão da corrida
    enum race_state_type race_state; 
    struct team* *teams; //dinamically allocated array of struct team
    int curr_teams_qnt; //current teams qnt
    //box_state *boxes_states;
    //...

}mem_struct;

//Node of Car_Threads LinkedList
typedef struct car{
    /******CAR*******/
    pthread_t car_thread;
    char* car_number;
    int speed;
    int consumption;
    int reliability;

    enum car_state_type car_state;
    /****************/
    struct car *next;
}car;

//Teams 
typedef struct team{
    /****TEAM****/
    char* team_name;
    enum box_state_type box_state;
    struct car *cars;//linked list head
    /************/
}team;


//GLOBAL VARIABLES
Config config;
FILE *log_fp;
int shmid; //shared memory id
mem_struct *shared_memory; 
//semaphores
sem_t* sem_log; //used to assure mutual exclusion when writing to log file and to stdout
//sem_t* sem_add_car_shm;
char curr_time[9]; 

struct sigaction sa;


//FUNCTION DECLARATION
void race_manager(void);
void malfunction_manager(void);
void team_manager(int );
void read_config(void);
void update_curr_time(void);
void destroy_all(void);
void init_log(void);
void write_log(char *log);
void update_curr_time(void);
void init_shared_memory(void);
void *car_thread(void);
enum race_state_type get_race_state();
void set_race_state();
void print_stats();
void sigtstp_handler();
void sigint_sigusr1_handler(int signal);
void add_car_to_teams_list(char* team_name, car *c);
team* create_team(char *team_name);
void add_car_to_shm(char *command);
car* create_car(char* car_number, int speed, int consumption, int reliability);

