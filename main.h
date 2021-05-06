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
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h> //mkfifo
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>

#define CONFIG_FILENAME "config.txt"
#define LOG_FILENAME "log.txt"
#define PIPE_NAME "my_pipe"

#define BUFF_SIZE 512

typedef struct Config{
    float time_unit; //numero de unidades de tempo por segundo
    int track_len,laps_qnt; //em metros //numero de voltas da corrida
    int teams_qnt; //numero de equipas (minimo de 3 equipas)
    int max_car_qnt_per_team; 
    float avaria_time_interval; //nr de unidades de tempo entre novo calculo de avaria 
    float reparacao_min_time, reparacao_max_time; //tempo minimo e maximo de reparacao (em unidades de tempo)
    int fuel_capacity; //capacidade do deposito de combustivel (em litros) de cada carro
}Config;

enum box_state_type{LIVRE,OCUPADA,RESERVADA};
enum race_state_type{OFF,ON}; //se está a decorrer corrida ou não
enum car_state_type{CORRIDA,SEGURANCA,BOX,DESISTENCIA,TERMINADO};

typedef struct mem_struct{
    int curr_teams_qnt; //current teams qnt

    int new_car_team; //indicação da equipa onde foi adicionado o novo carro (protected by mutex_race_state)

    pthread_mutex_t mutex_race_state;
    pthread_cond_t race_state_cond;
    enum race_state_type race_state; 
    //
    int stop_race_readers_in;
    int stop_race_readers_out;
    int wait_to_read;
    int stop_race; //signal triggered
    //STATS
    int total_malfunctions; //contador de avarias durante a corrida
    int total_refuels; //contador de abastecimentos realizados durante a corrida
    int cars_in_pista_qnt; 
}mem_struct;

//Car
typedef struct car{
    pthread_t thread; 
    char car_number[32];
    int speed; 
    float consumption;
    int reliability;
    int team_index;
    enum car_state_type car_state;
    //STATS
    int laps; //counter
    int box_stops; //counter
}car;

//Teams 
typedef struct team{
    char team_name[64];
    int curr_car_qnt;
    pthread_mutex_t mutex_write_to_unnamed_pipe; //mutex para synchronizar escrita por vários carros de uma equipa no unnamed pipe dessa equipa
    //^todas as alterações de estado dos carros são comunicadas através do unnamed pipe de cada equipa logo podemos aproveitar este mecanismo de synch para atualizar o estado do carro em memória partilhada
    //de forma mais eficiente
    /**********BOX**************/
    enum box_state_type box_state;
    int cars_in_safety_mode;
    int car_in_box;
    pthread_mutex_t mutex_car_changed_state;
    pthread_cond_t car_changed_state_cond;
}team;

//CAR STATE CHANGED NOTIFICATION
typedef struct notification{
    int car_index;
    enum car_state_type car_state;
}notification;

//MESSAGE 
typedef struct message{
    //header:
    long mtype;
    //payload:
    int val;
}message;

//GLOBAL VARIABLES
Config config;
FILE *log_fp;
char curr_time[9]; 
//named pipe fd
int fd_named_pipe;
//array of unnamed pipe file descriptors
int (*fd_unnamed_pipe)[2];
//shared memory id
int shmid; 
/*SHARED MEMORY*/
mem_struct *shared_memory; 
team *teams;
car *cars;
/********************/
//msg queue identifier
int mq_id;
//semaphores
sem_t* sem_log; //used to assure mutual exclusion when writing to log file and to stdout
sem_t* sem_stop_race_readers_in;
sem_t* sem_stop_race_readers_out;
sem_t* sem_write_stop_race;
sem_t* sem_stats;

//FUNCTION DECLARATION
void race_manager(void);
void malfunction_manager(void);
void team_manager(int );
void read_config(void);
void update_curr_time(void);
void clean_resources(void);
void forced_shutdown(void);
void init_log(void);
void write_log(char *log);
void update_curr_time(void);
void init_shared_memory(void);
void *car_thread(void*);
enum race_state_type get_race_state();
void set_race_state();
void print_stats();
void handle_sigusr1();
void handle_sigint_sigtstp(int sig);
int add_car_to_teams_shm(char* team_name, char* car_number,int speed,float consumption, int reliability);
void add_car_to_team(int i,int j,char* car_number, int speed, float consumption, int reliability);
void add_team_to_shm(char *team_name, int i);
void handle_addcar_command(char *command);
int is_valid_positive_float(char* str);
int is_valid_integer(char *str);
int validate_addcar_command(char *command, char *team_name, char* car_num, int *speed, float* cons, int *rel);
void handle_command(char *command);
void set_stop_race(int i);
int get_stop_race();
char* box_state_to_str(int state);
char *car_state_to_str(int car_state);
int get_total_car_count();
int get_car_state_by_index(int team_index,int car_index);