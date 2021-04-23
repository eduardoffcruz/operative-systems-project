//_______________________________Projeto Sistemas Operativos @2021
//Eduardo F. Ferreira Cruz 2018285164
//Gonçalo Marinho Barroso 2019216314


//echo "ADDCAR TEAM: A, CAR: 20, SPEED: 30, CONSUMPTION: 0.04, RELIABILITY: 95" > my_pipe
#include "main.h"

#define DEBUG 

int main(void){
    //RACE SIMULATOR PROCESS (main process)
    
    //IMPORT CONFIG from file
    read_config();

    //SHARED MEMORY 
    init_shared_memory();
    #ifdef DEBUG
    printf("[DEBUG] shared memory created and initialized\n");
    #endif

    //SEMAPHOREs
    //mutex para escrita synchronizada no ficheiro de log
    sem_unlink("SEM_LOG");
    sem_log=sem_open("SEM_LOG",O_CREAT|O_EXCL,0700,1); //binary semaphore

/*
    //escrita e leitura da race_state (estado da corrida), em memória partilhada
    sem_unlink("SEM_WRITE_RACE_STATE");
    sem_write_race_state=sem_open("SEM_WRITE_RACE_STATE",O_CREAT|O_EXCL,0700,1); 
    sem_unlink("SEM_MUTEX_RACE_STATE");
    sem_mutex_race_state=sem_open("SEM_MUTEX_RACE_STATE",O_CREAT|O_EXCL,0700,1); */

    //escrita e leitura de carros da memoria partilhada (escrita por 1 race manager, lida por vários team manager's)
    sem_unlink("SEM_READERS_IN");
    sem_readers_in=sem_open("SEM_READERS_IN",O_CREAT|O_EXCL,0700,1); 
    sem_unlink("SEM_READERS_OUT");
    sem_readers_out=sem_open("SEM_READERS_OUT",O_CREAT|O_EXCL,0700,1); 
    sem_unlink("SEM_WRITECAR");
    sem_writecar=sem_open("SEM_WRITECAR",O_CREAT|O_EXCL,0700,0);  

    //para bloquear/desbloquear gerador de avarias no processo malfunction_manager
    sem_unlink("SEM_MALFUNCTION_GENERATOR");
    sem_malfunction_generator=sem_open("SEM_MALFUNCTION_GENERATOR",O_CREAT|O_EXCL,0700,0);  
    
    #ifdef DEBUG
    printf("[DEBUG] semaphores created\n");
    #endif
    /****************************/


    //INIT LOG file
    init_log();
    #ifdef DEBUG
    printf("[DEBUG] log file created/cleared\n");
    #endif

    //NAMED PIPE
    if ((mkfifo(PIPE_NAME, O_CREAT|O_EXCL|0600)<0)&&(errno!=EEXIST)){ //creates the named pipe if it doesn't exist yet
        write_log("[ERROR] unable to create named pipe","");
        shutdown_all(); 
    }

    signal(SIGINT,SIG_IGN); //set "all" processes to ignore SIGINT and 
    signal(SIGTSTP,SIG_IGN); //.."all" processes ignore SIGTSTP
    signal(SIGUSR1,SIG_IGN); //.."all" processes ignore SIGUSR1

    write_log("SIMULATOR STARTING","");

    //create RACE MANAGER PROCESS
    if(fork()==0){
        //TODO:
        #ifdef DEBUG
        printf("[DEBUG] Race Simulator [pid:%d] created Race Manager process [pid:%d]!\n",getppid(),getpid());
        #endif
        race_manager();

        exit(0); 
    }
    //create MALFUNCTION MANAGER PROCESS
    if(fork()==0){
        //TODO:
        #ifdef DEBUG
        printf("[DEBUG] Race Simulator [pid:%d] created Malfunction Manager process [pid:%d]!\n",getppid(),getpid());
        #endif
        malfunction_manager();

        exit(0);        
    }

    //MAIN PROCESS (race simulator) captures and handles SIGINT and SIGTSTP
    signal(SIGINT,handle_sigint_sigtstp); 
    signal(SIGTSTP,handle_sigint_sigtstp);
    #ifdef DEBUG
    printf("[DEBUG] SIGINT and SIGTSTP ready to be handled in main process!\n");
    #endif


    wait(NULL);wait(NULL); //esperar q os 2 processos filhos terminem
    #ifdef DEBUG
    printf("[DEBUG] Race Manager process and Malfunction Manager process ended\n");
    #endif



    shutdown_all(); 

    return 0;
}

void handle_addcar_command(char *command){
    //takes care of ADDCAR command
    char team_name[128];
    char car_number[32];
    int speed, reliability;
    float consumption;
    int is_valid;
    int sucess=0;

    is_valid=validate_addcar_command(command, team_name, car_number, &speed, &consumption, &reliability);
    if(is_valid==-1){
        //write_log("[ERROR] Buffer size (9) exceeded for speed, consumption or reliability command input","");
        write_log("WRONG COMMAND => ",command);
        return;
    }else if(is_valid==0){
        write_log("WRONG COMMAND => ",command);
        return;
    }

    
    //implementaçao do caso clássico de write/readers sem starvation:
    //a ideia consiste em o writer indicar aos readers a sua necessidade de escrever. A partir daí nenhum reader pode entrar na zona critica.
    //ao sairem da zona critica cada reader verifica se o writer está waiting e o ultimo reader a sair liberta o writer para q ele possa entrar na zona e escrever
    //depois de escrever, o write liberta os readers q estão waiting para q eles possam novamente efetuar leitura
    sem_wait(sem_readers_in); //mutual exclusion for readers_in 
    sem_wait(sem_readers_out); //and readers_out shm vars
    if(shared_memory->readers_in==shared_memory->readers_out){ //se não houverem leitores na zona crítica 
        shared_memory->readers_in=0;//reset
        shared_memory->readers_out=0;//reset
        sem_post(sem_readers_out);
    }
    else{ //caso existam leitores na zona crítica
        shared_memory->wait=1;//a flag wait é colocada a 1, não entram mais leitores na zona crítica e no momento em q todos os leitores q ainda estivessem na zona crítica, saírem, é feito sem_post(sem_write), para que se possa escrever
        sem_post(sem_readers_out);
        sem_wait(sem_writecar);
        shared_memory->wait=0; //reset        
    }
    //WRITE (CRITICAL SECTION)
    sucess=add_car_to_teams_shm(team_name,car_number,speed,consumption,reliability); //add car to shared memory 

    sem_post(sem_readers_in); //podem entrar leitores

    if(sucess){
        pthread_mutex_lock(&shared_memory->mutex_race_state);
        shared_memory->new_cars_counter++;
        pthread_cond_broadcast(&shared_memory->race_state_cond);
        pthread_mutex_unlock(&shared_memory->mutex_race_state);
        write_log("NEW CAR LOADED => ",command);
    }

}

int validate_addcar_command(char *command, char *team_name, char* car_num, int *speed, float* cons, int *rel){
    //exemplo:    "ADDCAR TEAM: A, CAR: 20, SPEED: 30, CONSUMPTION: 0.04, RELIABILITY: 95"
    char aux[10];

    //get team name
    int i=7;
    int j=i;
    if(strncmp(&command[i],"TEAM: ",6)!=0){
        return 0; //invalid command
    }
    i+=6;
    j=i;
    while(command[++j]!=','); //chega à virgula e já n incrementa
    strncpy(team_name,&command[i],j-i);
    team_name[j-i]='\0';
    j++; //j tem o index da virgula, logo deve ser incrementado

    //get car_number
    i=j;
    if(strncmp(&command[i]," CAR: ",5)!=0){
        return 0; //invalid command
    }
    i+=6;
    j=i;
    while(command[++j]!=',');
    strncpy(car_num,&command[i],j-i);
    car_num[j-i]='\0';
    if(!is_valid_integer(car_num)){
        return 0; //invalid command
    }
    j++;

    //get speed
    i=j;
    if(strncmp(&command[i]," SPEED: ",8)!=0){
        return 0; //invalid command
    }
    i+=8;
    j=i;
    while(command[++j]!=',');
    if(j-i>=10){return -1;}
    strncpy(aux,&command[i],j-i);
    aux[j-i]='\0';
    if(!is_valid_integer(aux)){
        return 0; //invalid command
    }
    *speed=atoi(aux);
    j++;

    //get consumption
    i=j;
    if(strncmp(&command[i]," CONSUMPTION: ",14)!=0){
        return 0; //invalid command
    }
    i+=14;
    j=i;
    while(command[++j]!=',');
    if(j-i>=10){return -1;}
    strncpy(aux,&command[i],j-i);
    aux[j-i]='\0';
    if(!is_valid_positive_float(aux)){
        return 0; //invalid command
    }
    *cons=atof(aux);
    j++;

    //get reliability
    i=j;
    if(strncmp(&command[i]," RELIABILITY: ",14)!=0){
        return 0; //invalid command
    }
    i+=14;
    j=i;
    while(command[++j]!='\0');
    if(j-i>=10){return -1;}
    strncpy(aux,&command[i],j-i);
    aux[j-i]='\0';
    if(!is_valid_integer(aux)){
        return 0; //invalid command
    }
    *rel=atoi(aux);
    
    return 1;
} 

int is_valid_integer(char *str){
    int len=strlen(str);
    for(int i=0;i<len;i++)
        if(str[i]<'0'||str[i]>'9')
            return 0;
    return 1;
}

int is_valid_positive_float(char* str){ //devolve 0 se str nao tiver o formato de um valor float, devolve 1 otherwise
	int i=0,flag=0,count=0;
	char ch;
	while((ch=str[i++])!='\0'){
		count++;
		if(ch<'0'||ch>'9'){ //not a digit
			if(ch=='.'&&flag==0){ //valores negativos, com mais de um . ou com caracteres inválidos serão completamente descartados por se considerarem inválidos
				flag=1;
				if(count-1>7) //valores acima de 9999999.99 sao considerados inválidos
					return 0;
				count=0; //para comecar a contar casas decimais
			}
			else
				return 0;
		}
	}
	return 1;
}

void add_team_to_shm(char *team_name, int i){
    int len=strlen(team_name);
    memcpy(teams[i].team_name,team_name,len*sizeof(char));
    teams[i].team_name[len]='\0';
    teams[i].box_state=LIVRE;
    teams[i].curr_car_qnt=0;

    shared_memory->curr_teams_qnt++;
}

void add_car_to_team(int i,int j,char* car_number, int speed, float consumption, int reliability){
    int len=strlen(car_number);
    int car_index=i*config.max_car_qnt_per_team+j;
    memcpy(cars[car_index].car_number,car_number,len*sizeof(char));
    cars[car_index].car_number[len]='\0';
    cars[car_index].car_state=BOX;
    cars[car_index].speed=speed;
    cars[car_index].consumption=consumption;
    cars[car_index].reliability=reliability;

    teams[i].curr_car_qnt++;
}


int add_car_to_teams_shm(char* team_name, char* car_number,int speed,float consumption, int reliability){
    //verifica se já existe uma equipa com o nome team_name, no array de team's na SHM. Se existir, adiciona o car ao inicio da linked list que a struct team possui.
    //se n existir nenhuma equipa com esse nome e se ainda houver espaço para mais equipas, uma team é criada e adicionada à array de team's e o car é adicionado a essa team.
    int i,j,k;
    for(i=0;i<shared_memory->curr_teams_qnt;i++){
        if(strcmp(teams[i].team_name,team_name)==0){
            break;
        }
    }
    if(i==shared_memory->curr_teams_qnt){
        //n existe nenhuma team com a team_name ainda
        if(shared_memory->curr_teams_qnt==config.teams_qnt){
            write_log("UNABLE TO ADD MORE TEAMS, TEAM LIMIT EXCEEDED","");
            return 0; //FAILED
        } 
        add_team_to_shm(team_name,i);
        add_car_to_team(i,0,car_number,speed,consumption,reliability);

        #ifdef DEBUG
        printf("[DEBUG] CAR ADDED TO NEW TEAM %s with consumption: %.2f ; speed : %d ; reliability : %d \n",teams[i].team_name,cars[i*config.max_car_qnt_per_team+0].consumption,cars[i*config.max_car_qnt_per_team+0].speed,cars[i*config.max_car_qnt_per_team+0].reliability);
        #endif
    }
    else{
        //já existe team com este nome no index i
        j=teams[i].curr_car_qnt;
        if(j==config.max_car_qnt_per_team){
            write_log("UNABLE TO ADD CAR, CAR LIMIT EXCEEDED FOR TEAM ",team_name);
            return 0; //FAILED
        }
        for(k=0;k<j;k++){
            if(strcmp(cars[i*config.max_car_qnt_per_team+k].car_number,car_number)==0){
                write_log("UNABLE TO ADD CAR, CAR ALREADY EXISTS IN TEAM ",team_name);
                return 0; //FAILDE
            }
        }

        add_car_to_team(i,j,car_number,speed,consumption,reliability);

        #ifdef DEBUG
        printf("[DEBUG] CAR ADDED TO ALREADY EXISTING TEAM %s with consumption: %.2f ; speed : %d ; reliability : %d \n",teams[i].team_name,cars[i*config.max_car_qnt_per_team+j].consumption,cars[i*config.max_car_qnt_per_team+j].speed,cars[i*config.max_car_qnt_per_team+j].reliability);
        #endif
    }
    return 1; //SUCESS
}

void start_race(){
    //synchronized exacly like when reading car infos from shared-memory in team_manager process to create car threads etc
    sem_wait(sem_readers_in); 
    shared_memory->readers_in++;
    sem_post(sem_readers_in);
    
    //READ shared_memory->curr_teams_qnt
    if(shared_memory->curr_teams_qnt!=config.teams_qnt){
        write_log("CANNOT START, NOT ENOUGH TEAMS","");
    }
    else{
        //start race
        set_race_state(ON);
    }

    sem_wait(sem_readers_out);
    shared_memory->readers_out++;
    if(shared_memory->wait==1 && shared_memory->readers_in==shared_memory->readers_out){
        sem_post(sem_writecar);
    } 
    sem_post(sem_readers_out);
}

void race_manager(void){
    //TODO: 
    //recebe as informacoes de cada carro, através do named pipe, e escreve-as na memória partilhada. 
    //OS TEAM MANAGER PROCESSES VERIFICAM A SHM E CRIAM OS CARROS CONSOANTE A INFORMACAO Q LÁ FOI ESCRITA SOBRE OS CARROS
    int i;
    int teams_pid[config.teams_qnt];
    fd_set read_set;
    int n;
    char buff[BUFF_SIZE];

    //create/open UNNAMED PIPE
    pipe(fd_unnamed_pipe);

    //Open NAMED PIPE for reading
    if((fd_named_pipe=open(PIPE_NAME,O_RDWR))<0){ //opens as 'read-write' so that the process doesnt block on open
        write_log("[ERROR] unable to open named pipe for reading (in O_RDWR mode)","");
        shutdown_all();
    }

    //create TEAM MANAGER PROCESSes (1 per team)
    for(i=0;i<config.teams_qnt;i++){
        if((teams_pid[i]=fork())==0){
            #ifdef DEBUG
            printf("[DEBUG] Race Manager [pid:%d] created Team %d process [pid:%d]!\n",getppid(),i,getpid());
            #endif
            //TEAM i MANAGER PROCESS
            team_manager(i); //each team has a identifier number from 0 to config.teams_qnt-1

            exit(0);
        }
    }
    signal(SIGUSR1,handle_sigusr1); //catch and handle SIGUSR1 signal
    /***********************************/

    #ifdef DEBUG  
    printf("[DEBUG] LISTENING TO ALL PIPES!\n");
    #endif
    //could have used 2 threads for reading named pipe and/or unnamed pipe input
    while(1){

        //I/O Multiplexing
        FD_ZERO(&read_set);
        FD_SET(fd_unnamed_pipe[0],&read_set);
        FD_SET(fd_named_pipe,&read_set);
        if(select(fd_named_pipe+1,&read_set,NULL,NULL,NULL)>0){
            if(FD_ISSET(fd_named_pipe,&read_set)){
                n=read(fd_named_pipe,buff,sizeof(buff));
                buff[n-1] = '\0'; //put a \0 in the end of string
                handle_command(buff);
            }
            if(FD_ISSET(fd_unnamed_pipe[0],&read_set)){
                n=read(fd_unnamed_pipe[0],buff,sizeof(buff));
                buff[n-1] = '\0'; //put a \0 in the end of string

            }
        }

    }


    for(i=0;i<config.teams_qnt;i++) wait(NULL);  
    #ifdef DEBUG  
    printf("[DEBUG] all Team Manager processes ended\n");
    #endif
    //TODO:processo resposavel pela gestao da corrida(inicio, fim, classificacao final) e das equipas em jogo.
}

void handle_command(char *command){
    if(strncmp(command,"ADDCAR",6)==0){
        if(get_race_state()==OFF){
            handle_addcar_command(command);
        }
        else{
            write_log("ADDCAR COMMAND REJECTED, RACE ALREADY STARTED!","");
        }
    }
    else if(strlen(command)==11 && strncmp(command,"START RACE!",11)==0){
        write_log("NEW COMMAND RECEIVED: START RACE","");
        start_race();
    }
    else{
        write_log("WRONG COMMAND => ",command);
    }
}

void handle_sigusr1(){
    //para interromper uma corrida q esteja a decorrer
    //a corrida deverá terminar mal todos os carros cheguem à meta (como acontece com o SIGINT)
    //e a sua estatistica final deve ser apresentada.
    //a informacao da interrupcao deve ser escrita no log 
    //(nao termina o programa, se start race for escrito, a corrida poderá ser novamente iniciada)
    //TODO:
    #ifdef DEBUG  
    printf("[DEBUG] SIGUSR1 CATCHED!\n");
    #endif
    write_log("SIGNAL SIGUSR1 RECEIVED","");

    if(get_race_state()==ON){
        //set_race_state(PAUSE); 
        //TODO: TERMINAR CORRIDA COMO CTRL+C 
        write_log("RACE INTERRUPTED! WAIT FOR ALL CARS TO REACH FINISH LINE","");
    }
    //...
    
}

void handle_sigint_sigtstp(int signum){ //no processo main
    if(signum==SIGINT){
        #ifdef DEBUG  
        printf("[DEBUG] SIGINT CATCHED!\n");
        #endif
        write_log("SIGNAL SIGINT RECEIVED","");
        //sigint: aguardar q todos os carros cruzem a meta (mesmo q n seja a sa ultima volta), os carros q se encontram na box no momento da instrução devem terminar. aposto doso os carros concluirem a corrida deverá imprimir as estatisticas do jogo e terminal/libertar/remover todos os recursos utilizados
        //aguardar q todos os carros cruzem a meta (mesmo q n seja a sua ultima volta)
        //os carros q se encontram na box neste momento devem terminar
        //após todos os carros concluirem a corrida imprimir as estatisticas, terminar,libertar e remover todos os recursos utilizados
        //TODO:
        write_log("RACE INTERRUPTED! WAIT FOR ALL CARS TO REACH FINISH LINE","");
        //TODO:
        shutdown_all();
    }else if(signum==SIGTSTP){
        #ifdef DEBUG  
        printf("[DEBUG] SIGTSTP CATCHED!\n");
        #endif
        write_log("SIGNAL SIGTSTP RECEIVED","");
        print_stats(); //imprime estatisticas
    }
}


void set_race_state(enum race_state_type state){
    pthread_mutex_lock(&shared_memory->mutex_race_state); //exclusão mutua
    shared_memory->race_state=state; //write (zona critica)
    pthread_cond_broadcast(&shared_memory->race_state_cond); //notify all waiting threads/processes
    pthread_mutex_unlock(&shared_memory->mutex_race_state);
    if(state==ON){
        //if race is set to start (ON), release lock on malfunctions generator in malfunction manager process
        sem_post(sem_malfunction_generator);
    }else{
        //if race is OFF, grab lock (pause malfunctions generator) 
        sem_wait(sem_malfunction_generator); 
    }
}

enum race_state_type get_race_state(){
    int state;
    pthread_mutex_lock(&shared_memory->mutex_race_state);
    state=shared_memory->race_state; //READ
    pthread_mutex_unlock(&shared_memory->mutex_race_state);
    return state;
}

void team_manager(int team_id){
    //gere a box e carros da equipa. responsavel pela reparacao dos carros da equipa e por atestar o deposito de combustivel
    //TODO: team manager processes create car threads
    //TODO: team manager escreve as informações de cada carro, recebidas do Named Pipe, na shared_emmory
    //TODO: manter atualizada na shared_memory, o estado da box! (LIVRE;OCUPADA;RESERVADA)
    //car threads sao criadas através da receção de comandos através do named pipe 

    int *cars_index;
    int i=0; //team's current car count (in process)

	cars_index = (int*)malloc(sizeof(int)*(config.max_car_qnt_per_team)); //heap
    close(fd_unnamed_pipe[0]); //close unnamed pipe reading file descriptor
    while(1){
        printf("--------wasting.cpu.?\n");
        pthread_mutex_lock(&shared_memory->mutex_race_state);
        while(shared_memory->new_cars_counter==0 || shared_memory->race_state==OFF){
            pthread_cond_wait(&shared_memory->race_state_cond,&shared_memory->mutex_race_state);
        }
        if(shared_memory->race_state==OFF){
            pthread_mutex_unlock(&shared_memory->mutex_race_state);
            break; //stop loading cars
        }
        pthread_mutex_unlock(&shared_memory->mutex_race_state);
        
        //read cars from shared memory
        sem_wait(sem_readers_in); 
        shared_memory->readers_in++;
        sem_post(sem_readers_in);

        //verificar se já existe registo da team i através do curr_team_qnt 
        //se sim, LER CARROs e criar threads
        //EXEMPLO: esta equipa tem o nr 0. Se a current qnt de equipas for 0, é pq esta equipa ainda n foi criada através do named pipe
        //logo não se faz nada. Se a curr qnt de equipas fosse 2, é pq já existe a equipa com o nr 0 na shared memory, logo é necessário verificar se esta já tem carros e se tiver, criar as respetivas threads!
        //por outro lado se o nr de carros q a equipa tem na shared memory for superior a i, então é porque faltam criar threads para os carros
        if(shared_memory->curr_teams_qnt>=team_id+1){
            if(teams[team_id].curr_car_qnt>=i+1){
                while(i<teams[team_id].curr_car_qnt){
                    cars_index[i]=team_id*config.max_car_qnt_per_team+i; //index in shm memory's array of cars
                    printf("car name: %s\n",cars[team_id*config.max_car_qnt_per_team+i].car_number);
		            if(pthread_create(&cars[team_id*config.max_car_qnt_per_team+i].thread,NULL,car_thread,&cars_index[i])==-1){
                        //erro
                        write_log("[ERROR] unable to create car thread","");
                        shutdown_all();
                    }
                    i++;
                    pthread_mutex_lock(&shared_memory->mutex_race_state); //reuse mutex
                    shared_memory->new_cars_counter--; //protected
                    pthread_mutex_unlock(&shared_memory->mutex_race_state);

                    #ifdef DEBUG
                    printf("[DEBUG] NOVA CAR THREAD CRIADA NA TEAM %s !\n",teams[team_id].team_name);
                    #endif
                }
            }
        }
        
        else{
            printf("------->>>> team %d wasting CPU....!\n",team_id);
        }

        sem_wait(sem_readers_out);
        shared_memory->readers_out++;
        if(shared_memory->wait==1 && shared_memory->readers_in==shared_memory->readers_out){
            sem_post(sem_writecar);
        } 
        sem_post(sem_readers_out);

    }//antes da corrida começar

    printf("TEAM %d DETETOU Q A CORRIDA COMECOU!\n",team_id);

   /* pthread_exit(NULL); */
    for(int j=0;j<i;j++) pthread_join(cars[team_id*config.max_car_qnt_per_team+j].thread,NULL); //wait for threads to end

}

void *car_thread(void *void_index){
    int car_index=*((int*)void_index);
    //car thread function. cada car thread é responsavel pela gestao das voltas a pista, pela gestao do combustivel, e pela gestao do modo de circulacao(normal ou em segurança)
    #ifdef DEBUG
    printf("[DEBUG] hello i'm car number [%s] :)\n",cars[car_index].car_number);
    #endif
    
    return NULL;
}

void malfunction_manager(void){
    //TODO:processo responsavel por gerar aleatoriamente as avarias dos carros a partir da informacao da sua fiabilidade
    srand(time(0)); //set seed
    int rand_num;
    int i,j;
    int malfunction_count=0;
    while(1){
        sem_wait(sem_malfunction_generator); 
        sleep(config.avaria_time_interval/config.time_unit); 
        //all this values can be acessed during RACE time because, 
        //no changes are made or allowed to be made, to this values, during race time. 
        for(i=0;i<shared_memory->curr_teams_qnt;i++){ 
            for(j=0;j<teams[i].curr_car_qnt;j++){
                rand_num=rand()%101; //generate random number from [0-100]
                if(rand_num>=cars[i*config.max_car_qnt_per_team+j].reliability){ 
                    //AVARIA NO CARRO rand_num>=cars[car_index]
                    malfunction_count++;
                    #ifdef DEBUG
                    printf("[DEBUG] AVARIA NO CARRO number [%s] DA TEAM %s\n",cars[i*config.max_car_qnt_per_team+j].car_number,teams[i].team_name);
                    #endif
                    //...comunicar avaria ao carro, pela message queue 
                }
            }
        }

        /***************************************/                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
        if(malfunction_count>10){
            sem_post(sem_malfunction_generator);
            break;
        }
        /***************************************/

        sem_post(sem_malfunction_generator);
    }
    
}

void init_shared_memory(void){
    //cond_vars and mutexes created using static initialization cannot be used between processes!
    pthread_mutexattr_t attrmutex;
    pthread_condattr_t attrcondv;
    
    //SHM Create
	if ((shmid=shmget(IPC_PRIVATE,sizeof(mem_struct)+config.teams_qnt*sizeof(team)+config.teams_qnt*config.max_car_qnt_per_team*sizeof(car),IPC_CREAT|0700)) < 0){
        write_log("[ERROR] in shmget with IPC_CREAT","");
		exit(-1);
	} 
    //SHM Attach
    if((shared_memory=(mem_struct*)shmat(shmid,NULL,0))==(mem_struct*)-1){
        write_log("[ERROR] in shmat","");
        exit(-1);
    }

    shared_memory->race_state=OFF; 
    shared_memory->curr_teams_qnt=0; 
    shared_memory->wait=0; shared_memory->readers_in=0; shared_memory->readers_out=0;
    shared_memory->new_cars_counter=0;
    
    teams=(team*)(shared_memory+1);
    cars=(car*)(teams+config.teams_qnt);

    //init mutex and condition variable used between processes 
    pthread_mutexattr_init(&attrmutex); //Initialize attribute of mutex
    pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED);
    pthread_condattr_init(&attrcondv); //Initialize attribute of condition variable
    pthread_condattr_setpshared(&attrcondv, PTHREAD_PROCESS_SHARED);
    //note: these ^ attrs can be used to init multiple mutexes and cond vars
    pthread_mutex_init(&shared_memory->mutex_race_state, &attrmutex); //init mutex
    pthread_cond_init(&shared_memory->race_state_cond, &attrcondv); //init cond var

    pthread_mutexattr_destroy(&attrmutex); 
    pthread_condattr_destroy(&attrcondv);
}

void print_stats(){
    return;
}

void clean_resources(){
    signal(SIGINT, SIG_IGN); //ignore sigint signals

    pthread_mutex_destroy(&shared_memory->mutex_race_state);
    pthread_cond_destroy(&shared_memory->race_state_cond);

    //SHARED MEMORY
    if(shmid>=0){
        shmdt(shared_memory); //detach
        shmctl(shmid,IPC_RMID,NULL); //destroy
    }
    
    
    //SEMAPHOREs 
    if(sem_log>=0){
       sem_close(sem_log);	//destroy the semaphore
	    sem_unlink("SEM_LOG"); 
    }
    //fazer para o resto
    
    /*
    sem_close(sem_write_race_state);
    sem_unlink("SEM_WRITE_RACE_STATE");
    sem_close(sem_mutex_race_state);
    sem_unlink("SEM_MUTEX_RACE_STATE");*/
    sem_close(sem_readers_in);
    sem_unlink("SEM_READERS_IN");
    sem_close(sem_readers_out);
    sem_unlink("SEM_READERS_OUT");
    sem_close(sem_writecar);
    sem_unlink("SEM_WRITECAR");
    sem_close(sem_malfunction_generator);
    sem_unlink("SEM_MALFUNCTION_GENERATOR");

    //pthread_mutex_destroy(&mutex);
    //pthread_cond_destroy(&cond);

    //unlink named pipe
    unlink(PIPE_NAME);
    //close unnamed pipes file descriptors
    close(fd_unnamed_pipe[0]); //fd for reading
    close(fd_unnamed_pipe[1]); //fd for writing

    //close log file
    fclose(log_fp);

    system("./kill_ipcs.sh");

    #ifdef DEBUG
    printf("[DEBUG] sucessfuly cleaned everything!\n");
    #endif

}

void shutdown_all(void){
    write_log("SIMULATOR CLOSING","");

    //kill(pid[0], SIGKILL); //kill process
    
    //while (wait(NULL) != -1); //<-----

    clean_resources(); 
    //pthread_mutex_destroy(&mutex);
    //pthread_cond_destroy(&cond);


    #ifdef DEBUG
    printf("[DEBUG] SHUTTED DOWN!\n");
    #endif

    //TODO: matar todos os processos e threads

    system("killall -9 main");
    exit(0);
}

void update_curr_time(void){
    //call this function to update time on curr_time 
    time_t t;
    struct tm *curr_time_struct;
    t=time(NULL); curr_time_struct=localtime(&t);

    strftime(curr_time,9,"%H:%M:%S",curr_time_struct);
}

void write_log(char *log,char *concat){
    char *aux_buff;
    int len0=strlen(log);
    int len1=strlen(concat);
    if((aux_buff=(char*)malloc((len0+len1+1)*sizeof(char)))==NULL){
        write_log("[ERROR] allocating memory for log buffer(malloc)","");
        shutdown_all();
    }
    strcpy(aux_buff,log);
    aux_buff[len0]=0;
    if(len1!=0){
        strcat(aux_buff,concat);
        aux_buff[len0+len1]=0;
    }
    
    //synchronized
    if(sem_wait(sem_log)==-1){
        shutdown_all(); //end
    }

    update_curr_time();

    fprintf(stdout,"%s %s\n",curr_time,aux_buff); //write to stdout
    fprintf(log_fp,"%s %s\n",curr_time,aux_buff); //write to log file
    fflush(log_fp);

    if(sem_post(sem_log)==-1){
        shutdown_all(); //end
    } 

    free(aux_buff);
}

void init_log(void){
    //create/reset log file 
    if((log_fp=fopen(LOG_FILENAME,"w"))==NULL){
        write_log("[ERROR] creating file ",LOG_FILENAME);
        exit(-1);
    }
}

void read_config(void){
    FILE *fp;
    char *aux; 
    size_t size=1024;
    int len;

    if((fp=fopen(CONFIG_FILENAME,"r"))==NULL){
        write_log("[ERROR] opening file ",CONFIG_FILENAME);
        exit(-1);
    }
    //allocate aux buffer
    if((aux=(char*)malloc(size*sizeof(char)))==NULL){
        write_log("[ERROR] allocating aux buffer","");
        fclose(fp);
        exit(-1);
    }
    //read file formatted lines
    //1st line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 1st line from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.time_unit))!=1){
        write_log("[ERROR] anomality in expected structure from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //2nd line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 2nd line from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d, %d",&(config.track_len),&(config.laps_qnt))!=2){
        write_log("[ERROR] anomality in expected structure from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //3rd line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 3rd line from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.teams_qnt))!=1){
        write_log("[ERROR] anomality in expected structure from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    if(config.teams_qnt<3){
        write_log("[ERROR] invalid config parameter (line 3 -> number of teams), 3 or more teams are required! Correct file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //4th line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 4th line from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.max_car_qnt_per_team))!=1){
        write_log("[ERROR] anomality in expected structure from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //5th line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 5th line from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.avaria_time_interval))!=1){
        write_log("[ERROR] anomality in expected structure from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //6th line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 6th line from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d, %d",&(config.reparacao_min_time),&(config.reparacao_max_time))!=2){
        write_log("[ERROR] anomality in expected structure from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //last line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 7th line from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.fuel_capacity))!=1){
        write_log("[ERROR] anomality in expected structure from file ",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }

    #ifdef DEBUG
    printf("IMPORTED CONFIG:\n");
    printf("--------[DEBUG]-----------\n");
    printf("time_units: %d\ntrack_length: %d , laps_num: %d\nteams_qnt: %d\nT_Avaria: %d\nT_Box_Min: %d, T_Box_Max: %d\nfuel_capacity: %d\n",config.time_unit,config.track_len,config.laps_qnt,config.teams_qnt,config.avaria_time_interval,config.reparacao_min_time,config.reparacao_max_time,config.fuel_capacity);
    printf("--------------------------\n");
    #endif

    free(aux);
    fclose(fp);

}

