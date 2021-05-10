//_______________________________Projeto Sistemas Operativos @2021
//Eduardo F. Ferreira Cruz 2018285164
//Gonçalo Marinho Barroso 2019216314

//echo "ADDCAR TEAM: A, CAR: 20, SPEED: 30, CONSUMPTION: 0.04, RELIABILITY: 95" > my_pipe
//(...)
//echo "START RACE!" > my_pipe

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

    sem_unlink("SEM_STOP_RACE_READERS_IN");
    sem_stop_race_readers_in=sem_open("SEM_STOP_RACE_READERS_IN",O_CREAT|O_EXCL,0700,1);
    sem_unlink("SEM_STOP_RACE_READERS_OUT");
    sem_stop_race_readers_out=sem_open("SEM_STOP_RACE_READERS_OUT",O_CREAT|O_EXCL,0700,1);
    sem_unlink("SEM_WRITE_STOP_RACE");
    sem_write_stop_race=sem_open("SEM_WRITE_STOP_RACE",O_CREAT|O_EXCL,0700,0);
    
    sem_unlink("SEM_STATS");
    sem_stats=sem_open("SEM_STATS",O_CREAT|O_EXCL,0700,1); 

    #ifdef DEBUG
    printf("[DEBUG] semaphores created\n");
    #endif
    /****************************/

    //INIT LOG file
    init_log();
    #ifdef DEBUG
    printf("[DEBUG] log file created/cleared\n");
    #endif

    //allocate space for UNNAMED PIPEs file descriptors
    if((fd_unnamed_pipe=(int(*)[2])malloc(config.teams_qnt*sizeof(*fd_unnamed_pipe)))==NULL){
        write_log("[ERROR] allocationg memory for unnamed pipe's file descriptors");
        forced_shutdown();
    }

    //NAMED PIPE
    if ((mkfifo(PIPE_NAME, O_CREAT|O_EXCL|0600)<0)&&(errno!=EEXIST)){ //creates the named pipe if it doesn't exist yet
        write_log("[ERROR] unable to create named pipe");
        forced_shutdown(); 
    }

    //set
    signal(SIGINT,SIG_IGN); //"all" processes to ignore SIGINT and 
    signal(SIGTSTP,SIG_IGN); //"all" processes ignore SIGTSTP
    signal(SIGUSR1,SIG_IGN); //"all" processes ignore SIGUSR1

    //CREATE MESSAGE QUEUE
    if((mq_id = msgget(IPC_PRIVATE, IPC_CREAT|0777))<0){
        write_log("[ERROR] unable to create message queue");
        forced_shutdown();
    }

    write_log("SIMULATOR STARTING");

    //create RACE MANAGER PROCESS
    if(fork()==0){
        #ifdef DEBUG
        printf("[DEBUG] Race Simulator [pid:%d] created Race Manager process [pid:%d]!\n",getppid(),getpid());
        #endif

        race_manager();
    }
    //create MALFUNCTION MANAGER PROCESS
    if(fork()==0){
        #ifdef DEBUG
        printf("[DEBUG] Race Simulator [pid:%d] created Malfunction Manager process [pid:%d]!\n",getppid(),getpid());
        #endif

        malfunction_manager();
    }

    //MAIN PROCESS (race simulator) captures and handles SIGINT and SIGTSTP
    signal(SIGINT,handle_sigint_sigtstp); 
    signal(SIGTSTP,handle_sigint_sigtstp);
    #ifdef DEBUG
    printf("[DEBUG] SIGINT and SIGTSTP ready to be handled in main process!\n");
    #endif

    while(1){
        pause(); //keep waiting for signals..
    }
    
    return 0;
}

int handle_addcar_command(char *command){
    //takes care of ADDCAR command
    char team_name[128];
    char car_number[32];
    int speed, reliability;
    float consumption;
    int is_valid;
    int team_index=0;
    char aux[20+strlen(command+1)];

    is_valid=validate_addcar_command(command, team_name, car_number, &speed, &consumption, &reliability);
    if(is_valid==-1){
        sprintf(aux,"WRONG COMMAND => %s",command);
        write_log(aux);
        return 0;
    }else if(is_valid==0){
        sprintf(aux,"WRONG COMMAND => %s",command);
        write_log(aux);
        return 0;
    }
    
    pthread_mutex_lock(&shared_memory->mutex_race_state);
    //WRITE (CRITICAL SECTION)
    team_index=add_car_to_teams_shm(team_name,car_number,speed,consumption,reliability); //add car to shared memory 

    if(team_index!=-1){
        //shared_memory->new_car_team=team_index;
        pthread_cond_broadcast(&shared_memory->race_state_cond);
        pthread_mutex_unlock(&shared_memory->mutex_race_state);
        sprintf(aux,"NEW CAR LOADED => %s",&command[7]);
        write_log(aux);
        return is_valid;
    }
    pthread_mutex_unlock(&shared_memory->mutex_race_state);
    return is_valid;

}

int validate_addcar_command(char *command, char *team_name, char* car_num, int *speed, float* cons, int *rel){
    //exemplo:    "ADDCAR TEAM: A, CAR: 20, SPEED: 30, CONSUMPTION: 0.04, RELIABILITY: 95"
    char aux[10];
    int n=-1;

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
    while(command[++j]!='\0' && command[j]!='\n');
    if(j-i>=10){return -1;}
    if(command[j]=='\n'){
        command[j]='\0';
        if(command[j+1]!='\0')
            n=j+1;
    }
    strncpy(aux,&command[i],j-i);
    aux[j-i]='\0';
    if(!is_valid_integer(aux)){
        return 0; //invalid command
    }
    *rel=atoi(aux);

    if(n!=-1){
        return n; //there might be another command 
    }

    return -2;//sucess!
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
    teams[i].curr_car_qnt=0;

    shared_memory->curr_teams_qnt++;

    //BOX
    teams[i].box_state=LIVRE; 
    teams[i].cars_in_safety_mode=0;
    teams[i].car_in_box=0;
}

void add_car_to_team(int i,int j,char* car_number, int speed, float consumption, int reliability){
    int len=strlen(car_number);
    int car_index=i*config.max_car_qnt_per_team+j;
    memcpy(cars[car_index].car_number,car_number,len*sizeof(char));
    cars[car_index].car_number[len]='\0';
   //cars[car_index].car_state=CORRIDA; 
    cars[car_index].speed=speed;
    cars[car_index].consumption=consumption;
    cars[car_index].reliability=reliability;
    cars[car_index].team_index=i;
    cars[car_index].laps=0;
    cars[car_index].box_stops=0;
    teams[i].curr_car_qnt++;
}


int add_car_to_teams_shm(char* team_name, char* car_number,int speed,float consumption, int reliability){
    //verifica se já existe uma equipa com o nome team_name, no array de team's na SHM. Se existir, adiciona o car ao inicio da linked list que a struct team possui.
    //se n existir nenhuma equipa com esse nome e se ainda houver espaço para mais equipas, uma team é criada e adicionada à array de team's e o car é adicionado a essa team.
    int i,j,k;
    char aux[50+strlen(team_name)+1];
    for(i=0;i<shared_memory->curr_teams_qnt;i++){
        if(strcmp(teams[i].team_name,team_name)==0){
            break;
        }
    }
    if(i==shared_memory->curr_teams_qnt){
        //n existe nenhuma team com a team_name ainda
        if(shared_memory->curr_teams_qnt==config.teams_qnt){
            write_log("UNABLE TO ADD MORE TEAMS, TEAM LIMIT EXCEEDED");
            return -1; //FAILED
        } 
        add_team_to_shm(team_name,i);
        add_car_to_team(i,0,car_number,speed,consumption,reliability);

        #ifdef DEBUG
        printf("[DEBUG] CAR ADDED TO NEW TEAM [%s] with consumption: %.2f ; speed : %d ; reliability : %d \n",teams[i].team_name,cars[i*config.max_car_qnt_per_team+0].consumption,cars[i*config.max_car_qnt_per_team+0].speed,cars[i*config.max_car_qnt_per_team+0].reliability);
        #endif
    }
    else{
        //já existe team com este nome no index i
        j=teams[i].curr_car_qnt;
        if(j==config.max_car_qnt_per_team){
            sprintf(aux,"UNABLE TO ADD CAR, CAR LIMIT EXCEEDED FOR TEAM [%s]",team_name);
            write_log(aux);
            return -1; //FAILED
        }
        for(k=0;k<j;k++){
            if(strcmp(cars[i*config.max_car_qnt_per_team+k].car_number,car_number)==0){
                sprintf(aux,"UNABLE TO ADD CAR %s, CAR ALREADY EXISTS IN TEAM [%s]",car_number,team_name);
                write_log(aux);
                return -1; //FAILED
            }
        }

        add_car_to_team(i,j,car_number,speed,consumption,reliability);

        #ifdef DEBUG
        printf("[DEBUG] CAR ADDED TO ALREADY EXISTING TEAM [%s] with consumption: %.2f ; speed : %d ; reliability : %d \n",teams[i].team_name,cars[i*config.max_car_qnt_per_team+j].consumption,cars[i*config.max_car_qnt_per_team+j].speed,cars[i*config.max_car_qnt_per_team+j].reliability);
        #endif
    }
    return i; //SUCESS
}

void start_race(){
    int i;
    if(pthread_mutex_lock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
    //READ shared_memory->curr_teams_qnt
    if(shared_memory->curr_teams_qnt!=config.teams_qnt){  
        write_log("CANNOT START, NOT ENOUGH TEAMS");
    }
    else if(shared_memory->race_state==OFF){
        //init cars_in_pista_count and update total_car_count
        sem_wait(sem_stats);
        for(i=0;i<config.teams_qnt;i++) shared_memory->cars_in_pista_qnt+=teams[i].curr_car_qnt; //no more cars will be added at this point so its safe to read
        shared_memory->total_car_count=shared_memory->cars_in_pista_qnt;
        sem_post(sem_stats);

        //start race
        shared_memory->race_state=ON; 
        if(pthread_cond_broadcast(&shared_memory->race_state_cond)!=0){forced_shutdown();} //notify all waiting threads/processes that race has started!
    }
    //se a corrida já estiver ON.. simplesmente ignora!
    if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
}

void race_manager(void){
    //TODO: 
    //recebe as informacoes de cada carro, através do named pipe, e escreve-as na memória partilhada. 
    //OS TEAM MANAGER PROCESSES VERIFICAM A SHM E CRIAM OS CARROS CONSOANTE A INFORMACAO Q LÁ FOI ESCRITA SOBRE OS CARROS
    int i,j;
    int teams_pid[config.teams_qnt];
    fd_set read_set;
    int n;
    char buff[BUFF_SIZE];
    struct notification notif; 
    struct message msg;
    int finished_race_car_count=0;
    int desistiu_race_car_count=0;
    int exited_team_managers=0; //used only when ctrl+c (SIGINT) handler is triggered..team manager process notifies race manager via team manager's unnamed pipe so that he can acknoledge that all team manager's exited sucessfully and race manager can exit too 

    //create TEAM MANAGER PROCESSes (1 per team)
    for(i=0;i<config.teams_qnt;i++){
        //create/open UNNAMED PIPE
        if(pipe(fd_unnamed_pipe[i])==-1){forced_shutdown();}

        if((teams_pid[i]=fork())==0){
            #ifdef DEBUG
            printf("[DEBUG] Race Manager [pid:%d] created Team %d process [pid:%d] with unnamed pipe:%d %d!\n",getppid(),i,getpid(),fd_unnamed_pipe[i][0],fd_unnamed_pipe[i][1]);
            #endif
            //TEAM i MANAGER PROCESS
            team_manager(i); //each team has a identifier number from 0 to config.teams_qnt-1
        }
    }
    signal(SIGUSR1,handle_sigusr1); //catch and handle SIGUSR1 signal
    /***********************************/
    
    //Open NAMED PIPE for reading
    if((fd_named_pipe=open(PIPE_NAME,O_RDWR))<0){ //opens as 'read-write' so that the process doesnt block on open
        write_log("[ERROR] unable to open named pipe for reading (in O_RDWR mode)");
        forced_shutdown();
    }

    #ifdef DEBUG  
    printf("[DEBUG] race manager listening to all pipes!\n");
    #endif
    //could have used 2 threads for reading named pipe and/or unnamed pipe input
    while(1){
        //I/O Multiplexing
        FD_ZERO(&read_set);
        for(i=0;i<config.teams_qnt;i++){
            FD_SET(fd_unnamed_pipe[i][0],&read_set); //SET ALL UNNAMED PIPES FOR EACH TEAM (1 UNNAMED PIPE PER TEAM)
        }
        FD_SET(fd_named_pipe,&read_set);
        if(select(fd_named_pipe+1,&read_set,NULL,NULL,NULL)>0){
            if(FD_ISSET(fd_named_pipe,&read_set)){
                n=read(fd_named_pipe,buff,sizeof(buff));
                buff[n-1] = '\0'; //put a \0 in the end of string
                //printf("BUFF:%s\n",buff);
                handle_command(buff);
            }
            for(i=0;i<config.teams_qnt;i++){
                if(FD_ISSET(fd_unnamed_pipe[i][0],&read_set)){
                    read(fd_unnamed_pipe[i][0], &notif, sizeof(struct notification));
                    if(notif.car_state==-1){ //notification from team manager process
                        //team manager notifies race manager that he (team manager process) died
                        exited_team_managers++;
                        wait(NULL); //ACKNOLEDGE CHILD'S PROCESS DEAD
                        if(exited_team_managers==config.teams_qnt){
                            #ifdef DEBUG  
                            printf("[DEBUG] all Team Manager processes ended!\n");
                            #endif
                            exit(0);
                        }
                    }
                    else if(notif.car_state==-2){
                        //car notifies race_manager that he crossed the line
                        sem_wait(sem_stats);
                        cars[notif.car_index].laps++; //+1 lap
                        sem_post(sem_stats);
                    }
                    else{ //notification from car threads! 
                        sprintf(buff,"CAR %s FROM TEAM [%s] CHANGED STATE! => %s",cars[notif.car_index].car_number,teams[i].team_name,car_state_to_str(notif.car_state));
                        write_log(buff);
                        //count cars in TERMINADO state
                        if(notif.car_state==TERMINADO||notif.car_state==DESISTENCIA){
                            if(notif.car_state==TERMINADO){
                                finished_race_car_count++; //+1
                                if(finished_race_car_count==1){
                                    sprintf(buff,"CAR %s WINS THE RACE",cars[notif.car_index].car_number);
                                    write_log(buff);
                                }
                                else{
                                    sprintf(buff,"CAR %s FINISHED THE RACE [%do PLACE]",cars[notif.car_index].car_number,finished_race_car_count);
                                    write_log(buff);
                                }
                                sem_wait(sem_stats);
                                cars[notif.car_index].laps++; //+1 lap
                                sem_post(sem_stats);
                            }else{
                                //desistencia
                                desistiu_race_car_count++;
                            }

                            sem_wait(sem_stats);
                            shared_memory->cars_in_pista_qnt--; 
                            sem_post(sem_stats);
                            //n é necessário synchronizar o acesso ao total_car_count pq no momento é q é lido temos a certeza q não poderá estar a ser escrito dado q este valor pode apenas ser escrito qnd a corrida n está a decorrer
                            if((desistiu_race_car_count+finished_race_car_count)==shared_memory->total_car_count){ //se todos os carros tiverem terminado a corrida..
                                if(pthread_mutex_lock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
                                shared_memory->race_state=OFF; //todos os carros terminaram a corrida logo..estado da corrida == OFF
                                if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
                                write_log("---RACE FINISHED!---");
                                //PRINT STATS
                                print_stats();

                                //SIGNAL ALL TEAM BOXES that race has finished (new race_state = OFF)
                                //NOTIFY TEAM MANAGER's BOX (to check if the race is already over so it can 'close' the BOX)
                                for(j=0;j<config.teams_qnt;j++){
                                    //printf("race_manager notificou team's box [%s] para ela saber q já acabou a corrida..!\n",teams[j].team_name);
                                    if(pthread_mutex_lock(&teams[j].mutex_car_changed_state)!=0){forced_shutdown();}
                                    if(pthread_cond_signal(&teams[j].car_changed_state_cond)!=0){forced_shutdown();} //signal each TEAM BOX (1 per team) so that it checks for the end of the race and restarts box
                                    if(pthread_mutex_unlock(&teams[j].mutex_car_changed_state)!=0){forced_shutdown();}
                                }
                                if(get_stop_race()==-1){
                                    for(j=0;j<config.teams_qnt;j++) wait(NULL); //wait for team_manager processes to finish.. 
                                    #ifdef DEBUG  
                                    printf("[DEBUG] all Team Manager processes ended!\n");
                                    #endif
                                    exit(0); //end race manager process after ending team_manager processes
                                }
                                //RESET STATS
                                sem_wait(sem_stats);
                                shared_memory->cars_in_pista_qnt=0;
                                shared_memory->total_refuels=0;
                                shared_memory->total_malfunctions=0;
                                for(j=0;j<config.teams_qnt*config.max_car_qnt_per_team;j++){
                                    cars[j].laps=0; 
                                    cars[j].box_stops=0;
                                }
                                sem_post(sem_stats);
                                //clean avarias q não foram lidas from message queue!
                                while(msgrcv(mq_id,&msg,sizeof(struct message)-sizeof(long),0,IPC_NOWAIT)>0);
                                set_stop_race(0);
                                finished_race_car_count=0; //reset
                                desistiu_race_car_count=0; //reset
                            }
                        }
                        else if(notif.car_state==BOX){
                            sem_wait(sem_stats);
                            shared_memory->total_refuels++; //todas as vezes q o carro vai à BOX dá refuel
                            cars[notif.car_index].box_stops++; //increment box stops car counter
                            sem_post(sem_stats);
                        }
                    }
                }
            }
        }
    }
    //processo resposavel pela gestao da corrida(inicio, fim, classificacao final) e das equipas em jogo.
}

char *car_state_to_str(int car_state){
    switch (car_state){
    case CORRIDA:
        return "CORRIDA";
    case SEGURANCA:
        return "SEGURANCA";    
    case BOX:
        return "BOX";
    case DESISTENCIA:
        return "DESISTENCIA";
    case TERMINADO:
        return "TERMINADO";
    default:
        return "";
    }
}

void handle_command(char *command){
    char aux[20+strlen(command+1)];
    int n;
    if(strncmp(command,"ADDCAR",6)==0){
        if(get_race_state()==OFF){
            if((n=handle_addcar_command(command))>0){
                handle_command(&command[n]); //there might be another command in the pipe
            }
        }
        else{
            write_log("ADDCAR COMMAND REJECTED, RACE ALREADY STARTED!");
        }
    }
    else if(strlen(command)==11 && strncmp(command,"START RACE!",11)==0){
        write_log("NEW COMMAND RECEIVED: START RACE");
        start_race();
    }
    else{
        sprintf(aux,"WRONG COMMAND => %s",command);
        write_log(aux);
    }
}

void handle_sigusr1(){
    //para interromper uma corrida q esteja a decorrer
    //a corrida deverá terminar mal todos os carros cheguem à meta (como acontece com o SIGINT)
    //e a sua estatistica final deve ser apresentada.
    //a informacao da interrupcao deve ser escrita no log 
    //(nao termina o programa, se start race for escrito, a corrida poderá ser novamente iniciada)
    write_log("SIGNAL SIGUSR1 RECEIVED");
    signal(SIGUSR1,SIG_IGN); //ignore sigur1 during handling

    if(get_race_state()==ON){
        //TERMINAR CORRIDA COMO CTRL+C (mas sem terminar processos!)
        if(pthread_mutex_lock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
        set_stop_race(1); //não termina o programa
        if(pthread_cond_broadcast(&shared_memory->race_state_cond)!=0){forced_shutdown();}
        if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
        write_log("RACE INTERRUPTED! WAIT FOR ALL CARS TO REACH FINISH LINE..");

        //....stats will be printed after the race finishes
    }

    signal(SIGUSR1,handle_sigusr1); //re-set handler    
}

void handle_sigint_sigtstp(int signum){ //no processo main (!)
    if(signum==SIGINT){
        //sigint: aguardar q todos os carros cruzem a meta (mesmo q n seja a sa ultima volta), os carros q se encontram na box no momento da instrução devem terminar. aposto doso os carros concluirem a corrida deverá imprimir as estatisticas do jogo e terminal/libertar/remover todos os recursos utilizados
        //aguardar q todos os carros cruzem a meta (mesmo q n seja a sua ultima volta)
        //os carros q se encontram na box neste momento devem terminar
        //após todos os carros concluirem a corrida imprimir as estatisticas, terminar,libertar e remover todos os recursos utilizados
        write_log("SIGNAL SIGINT RECEIVED");
        signal(SIGINT,SIG_IGN); //ignore sigint during handling
        
        set_stop_race(-1); //termina o programa

        if(pthread_mutex_lock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
        if(pthread_cond_broadcast(&shared_memory->race_state_cond)!=0){forced_shutdown();}
        if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}

        if(get_race_state()==ON){
            write_log("RACE INTERRUPTED! WAIT FOR ALL CARS TO REACH FINISH LINE..");
        }

        //wait(NULL);wait(NULL); 
        while (wait(NULL) != -1); //esperar q os 2 processos filhos (race_manager & malfunction_manager) terminem
        #ifdef DEBUG
        printf("[DEBUG] Race Manager process and Malfunction Manager process ended\n");
        #endif

        clean_resources(); 
        exit(0); //sucessfully end MAIN process (all child processes exited and their deads were acknoledge bye their fathers!)
        
    }
    else if(signum==SIGTSTP){
        write_log("SIGNAL SIGTSTP RECEIVED");
        if(get_race_state()==ON){
            print_stats(); //imprime estatisticas
        }
        else{
            write_log("THERE IS NO ON GOING RACE [NO STATS AVAILABLE]");
        }
        
    }
}


enum race_state_type get_race_state(){
    int state;
    if(pthread_mutex_lock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
    state=shared_memory->race_state; //READ
    if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
    return state;
}

void team_manager(int team_id){
    //gere a box e carros da equipa. responsavel pela reparacao dos carros da equipa e por atestar o deposito de combustivel
    //team manager processes create car threads
    //team manager escreve as informações de cada carro, recebidas do Named Pipe, na shared_emmory
    //manter atualizada na shared_memory, o estado da box! (LIVRE;OCUPADA;RESERVADA)
    //car threads sao criadas através da receção de comandos através do named pipe 
    int *cars_index;
    
    int i=0,j; //team's current car count (in process)
    char aux[BUFF_SIZE];
    int tmp;
    int aux_stop_race;
    struct notification exited_notif;
    pthread_mutexattr_t attrmutex;
    pthread_condattr_t attrcondv;
    //save last values
    int last_car_in_box; 
    int last_cars_in_safety_mode; 

	if((cars_index = (int*)malloc(sizeof(int)*(config.max_car_qnt_per_team)))==NULL){forced_shutdown();} //heap

    close(fd_unnamed_pipe[team_id][0]); //close unnamed pipe reading file descriptor

    //init mutex and condition variable to be used between processes 
    if(pthread_mutexattr_init(&attrmutex)!=0){forced_shutdown();} //Initialize attribute of mutex
    if(pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED)!=0){forced_shutdown();}
    if(pthread_condattr_init(&attrcondv)!=0){forced_shutdown();} //Initialize attribute of condition variable
    if(pthread_condattr_setpshared(&attrcondv, PTHREAD_PROCESS_SHARED)!=0){forced_shutdown();}
    if(pthread_mutex_init(&teams[team_id].mutex_car_changed_state,&attrmutex)!=0){forced_shutdown();} //INIT MUTEX 
    if(pthread_cond_init(&teams[team_id].car_changed_state_cond,&attrcondv)!=0){forced_shutdown();}//INIT COND VAR 
    if(pthread_mutex_init(&teams[team_id].mutex_write_to_unnamed_pipe,&attrmutex)!=0){forced_shutdown();} //INIT MUTEX 
    if(pthread_mutexattr_destroy(&attrmutex)!=0){forced_shutdown();}
    if(pthread_condattr_destroy(&attrcondv)!=0){forced_shutdown();}

    while(1){
        //CREATE CAR THREADS FROM SHM CARS INFO
        //BEFORE RACE STARTS!
        while(1){
            if(pthread_mutex_lock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
            while(i>=teams[team_id].curr_car_qnt && shared_memory->race_state==OFF && (aux_stop_race=get_stop_race())!=-1){
                //condicao para desbloquear: shared_memoru->new_car_team==team_id || shared_memory->race_state==ON || shared_memory->stop_race==-1
                if(pthread_cond_wait(&shared_memory->race_state_cond,&shared_memory->mutex_race_state)!=0){forced_shutdown();}
            }
            if(aux_stop_race==-1){
                if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
                //wait for car threads to end..
                for(int j=0;j<i;j++) pthread_join(cars[team_id*config.max_car_qnt_per_team+j].thread,NULL); 
                //notify race_manager through unnamed pipe so that race_manager process knows that all car threads from this team sucessfuly exited...and now this team_manager process will exit too :)
                //so race_manager can acknoledge it end exit himself when all team_manager processes finish!  
                //no need to synch this write because all threads are finished and we know for sure that no process or thread will be using this unnamed pipe (one unnamed pipe per team)
                //NOTIFICAR race manager process através de unnamed pipe para ele saber qnd é q todas as threads foram terminadas e poder terminar o processo race_manager
                exited_notif.car_state=-1; //exit
                write(fd_unnamed_pipe[team_id][1],&exited_notif,sizeof(struct notification)); //notifica alteração de estado do carro
                free(cars_index);//deallocate memory
                exit(0); //safely END PROCESS
            }
            //shared_memory->new_car_team=-1;//reset

            //LER NOVO CARRO e criar thread
            while(i<teams[team_id].curr_car_qnt){
                cars_index[i]=team_id*config.max_car_qnt_per_team+i; //index in shm memory's array of cars
                if(pthread_create(&cars[team_id*config.max_car_qnt_per_team+i].thread,NULL,car_thread,&cars_index[i])!=0){
                    //erro
                    write_log("[ERROR] unable to create car thread");
                    forced_shutdown();
                }
                i++;
                #ifdef DEBUG
                printf("[DEBUG] nova car thread criada na team [%s] !\n",teams[team_id].team_name);
                #endif
            }

            if(shared_memory->race_state==ON){
                if(pthread_mutex_lock(&teams[team_id].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                for(j=0;j<i;j++){
                    cars[team_id*config.max_car_qnt_per_team+j].car_state=CORRIDA; //SET all car states to allow car to start racing in car thread
                }  
                if(pthread_mutex_unlock(&teams[team_id].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                if(pthread_cond_broadcast(&shared_memory->race_state_cond)!=0){forced_shutdown();} //check condition in car threads 
                if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
                break; //stop loading cars and start managing team box
            }
            
            if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}

        }
        #ifdef DEBUG
        printf("[DEBUG] team %s detetou q a corrida comecou!\n",teams[team_id].team_name);
        #endif

        //CORRIDA COMEÇOU....GERIR BOX!
        while(1){
            //DESBLOQUEAR ISTO E SAIR QND O RACE_STATE == OFF
            if(pthread_mutex_lock(&teams[team_id].mutex_car_changed_state)!=0){forced_shutdown();}                                                    
            while(teams[team_id].car_in_box==last_car_in_box && teams[team_id].cars_in_safety_mode==last_cars_in_safety_mode && (tmp=get_race_state())==ON){ //UNLOCK ON CHANGE
                //desbloqueia quando: teams[team_id].car_in_box!=last_car_in_box || teams[team_id].cars_in_safety_mode!=last_cars_in_safety_mode || race_state==OFF
                if(pthread_cond_wait(&teams[team_id].car_changed_state_cond,&teams[team_id].mutex_car_changed_state)!=0){forced_shutdown();}
            }
            if(tmp==OFF){
                #ifdef DEBUG
                printf("[DEBUG] team %s closed the BOX!\n",teams[team_id].team_name);
                #endif
                if(pthread_mutex_unlock(&teams[team_id].mutex_car_changed_state)!=0){forced_shutdown();}
                if(get_stop_race()==-1){
                    //wait for car threads to end..
                    for(int j=0;j<i;j++) pthread_join(cars[team_id*config.max_car_qnt_per_team+j].thread,NULL); 
                    //notify race_manager through unnamed pipe so that race_manager process knows that all car threads from this team sucessfuly exited...and now this team_manager process will exit too :)
                    //so race_manager can acknoledge it end exit himself when all team_manager processes finish!  
                    //no need to synch this write because all threads are finished and we know for sure that no process or thread will be using this unnamed pipe (one unnamed pipe per team)
                    //NOTIFICAR race manager process através de unnamed pipe para ele saber qnd é q todas as threads foram terminadas e poder terminar o processo race_manager
                    exited_notif.car_state=-1; //exit
                    write(fd_unnamed_pipe[team_id][1],&exited_notif,sizeof(struct notification)); //notifica alteração de estado do carro
                    free(cars_index);
                    exit(0); //safely end process
                }else{
                    break; //exit while loop & restart
                }
            }
            //UPDATE AND MANAGE BOX STATE
            if(teams[team_id].car_in_box){ //se estiver um carro na box
                teams[team_id].box_state=OCUPADA;
            }
            else{
                if(teams[team_id].cars_in_safety_mode>0){
                    teams[team_id].box_state=RESERVADA;
                }
                else{
                    teams[team_id].box_state=LIVRE;
                }
            }
            if(teams[team_id].car_in_box!=last_car_in_box){last_car_in_box=teams[team_id].car_in_box;}
            if(teams[team_id].cars_in_safety_mode!=last_cars_in_safety_mode){last_cars_in_safety_mode=teams[team_id].cars_in_safety_mode;}
            if(pthread_mutex_unlock(&teams[team_id].mutex_car_changed_state)!=0){forced_shutdown();}

            sprintf(aux,"BOX TEAM [%s] CHANGED STATE! => %s",teams[team_id].team_name,box_state_to_str(teams[team_id].box_state));  
            write_log(aux);
        }

    }
}

int get_stop_race(){
    int ret;

    if(sem_wait(sem_stop_race_readers_in)==-1){forced_shutdown();}
    shared_memory->stop_race_readers_in++;
    if(sem_post(sem_stop_race_readers_in)==-1){forced_shutdown();}
    /************************************/
    ret=shared_memory->stop_race; //READ CRITICAL SECTION
    /************************************/
    if(sem_wait(sem_stop_race_readers_out)==-1){forced_shutdown();}
    shared_memory->stop_race_readers_out++;
    if(shared_memory->wait_to_read==1 && shared_memory->stop_race_readers_in==shared_memory->stop_race_readers_out){
        if(sem_post(sem_write_stop_race)==-1){forced_shutdown();}
    } 
    if(sem_post(sem_stop_race_readers_out)==-1){forced_shutdown();}

    return ret;
}

void set_stop_race(int i){

    if(sem_wait(sem_stop_race_readers_in)==-1){forced_shutdown();} //mutual exclusion for readers_in 
    if(sem_wait(sem_stop_race_readers_out)==-1){forced_shutdown();} //and readers_out shm vars
    if(shared_memory->stop_race_readers_in==shared_memory->stop_race_readers_out){ //se não houverem leitores na zona crítica 
        shared_memory->stop_race_readers_in=0;//reset
        shared_memory->stop_race_readers_out=0;//reset
        if(sem_post(sem_stop_race_readers_out)==-1){forced_shutdown();}
    }
    else{ //caso existam leitores na zona crítica
        shared_memory->wait_to_read=1;//a flag wait é colocada a 1, não entram mais leitores na zona crítica e no momento em q todos os leitores q ainda estivessem na zona crítica, saírem, é feito sem_post(sem_write), para que se possa escrever
        if(sem_post(sem_stop_race_readers_out)==-1){forced_shutdown();}
        if(sem_wait(sem_write_stop_race)==-1){forced_shutdown();}
        shared_memory->wait_to_read=0; //reset        
    }
    /************************************/
    shared_memory->stop_race=i; //WRITE (CRITICAL SECTION)
    /************************************/
    if(sem_post(sem_stop_race_readers_in)==-1){forced_shutdown();} //podem entrar leitores
}

void *car_thread(void *void_index){
    int car_index=*((int*)void_index);
    int team_index=cars[car_index].team_index; 
    struct notification notif; // used to send car state info to race manager process
    int total_meters;
    int laps; //voltas
    float fuel; //starts with Deposit max capacity
    int stop_race_aux,car_state_aux;

    int low_fuel; //flag

    unsigned int seed = getpid()*time(NULL)+car_index; //thread seed
    unsigned int repair_time; //rand between min and max from config^

    struct message msg; //malfunction message
    long msg_id=car_index+1;
    //int malfunction_counter=0; 

    notif.car_index=car_index;

    //quando a corrida começar as threads podem ler à vontade as infos do carro a q estão associados, na shared_memory 
    //sem q haja a necessidade de assegurar sincronização, visto q durante a corrida nenhuma informação relativa a carros 
    //é escrita em memória partilhada 

    while(1){
        //ANTES DA CORRIDA
        low_fuel=0;
        fuel=config.fuel_capacity;
        total_meters=0;
        laps=0;
        if(pthread_mutex_lock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
        while((shared_memory->race_state==OFF || get_car_state_by_index(team_index,car_index)!=CORRIDA) && (stop_race_aux=get_stop_race())!=-1){
            //desbloqueia quando: (shared_memory->race_state==ON && notif.car_state==CORRIDA) || shared_memory->stop_race==-1
            if(pthread_cond_wait(&shared_memory->race_state_cond,&shared_memory->mutex_race_state)!=0){forced_shutdown();}
        }
        if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
        if(stop_race_aux==-1){ //terminar o programa
            #ifdef DEBUG
            printf("[DEBUG] car %s thread from team %s exited\n",cars[car_index].car_number,teams[team_index].team_name);
            #endif
            pthread_exit(NULL);
        }
        notif.car_state=CORRIDA;
        //RACING
        while(1){
            if(usleep((unsigned int)(1000000/config.time_unit))==-1){forced_shutdown();} //as contas dos metros percorridos e da gasolina gasta são feitas de (1/config.time_unit) em (1/config.time_unit) segundos
            if(notif.car_state==CORRIDA){
                fuel-=cars[car_index].consumption; //consumption per time unit
                total_meters+=cars[car_index].speed; //meters per time unit
                #ifdef DEBUG
                printf("[DEBUG] car %s from team %s -> metros percorridos: %d | gasolina: %f\n",cars[car_index].car_number,teams[team_index].team_name,total_meters,fuel);
                #endif

                //while(msgrcv(mq_id,&msg,sizeof(struct message)-sizeof(long),msg_id,IPC_NOWAIT)>0) malfunction_counter++; //read(clean) all messages from message queue with mtype==msg_id ... para evitar que a message queue fique cheia
                if(msgrcv(mq_id,&msg,sizeof(struct message)-sizeof(long),msg_id,IPC_NOWAIT)>0 || fuel<((2*config.track_len)/cars[car_index].speed)*cars[car_index].consumption){ // IPC_NOWAIT para que a thread n bloqueie caso n exista nenhuma mensagem com o mtype == msg_id na message queue!
                    //se receber msg do malfucntion_manager ou se não tiver COMBUSTIVEL NECESSÁRIO PARA REALIZAR +4 VOLTAS entra em modo SEGURANÇA
                    notif.car_state=SEGURANCA;
                    low_fuel=0; //reset

                    //NOTIFICAR MUDANÇA DE ESTADO ao race manager process através de unnamed pipe
                    if(pthread_mutex_lock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                    write(fd_unnamed_pipe[team_index][1],&notif,sizeof(notif)); //notifica alteração de estado do carro
                    cars[car_index].car_state=SEGURANCA;
                    if(pthread_mutex_unlock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}

                    //NOTIFICAR TEAM'S BOX
                    if(pthread_mutex_lock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}
                    teams[team_index].cars_in_safety_mode++;
                    if(teams[team_index].cars_in_safety_mode==1){ //signal BOX only if its the first car in SEGURANCA mode
                        if(pthread_cond_signal(&teams[team_index].car_changed_state_cond)!=0){forced_shutdown();} //one process only
                    }
                    if(pthread_mutex_unlock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}

                }
                else if(!low_fuel && fuel<((4*config.track_len)/cars[car_index].speed)*cars[car_index].consumption){
                    low_fuel=1; //set true
                    #ifdef DEBUG
                    printf("[DEBUG] car %s from team %s -> deverá abastecer qnd conseguir entrar na box (sem combustivel para +4 voltas)\n",cars[car_index].car_number,teams[team_index].team_name);
                    #endif
                }
            }
            else if(notif.car_state==SEGURANCA){
                //AVARIA ou POUCO COMBUSTIVEL
                fuel-=0.4*cars[car_index].consumption; //40% consumption per time unit
                total_meters+=0.3*cars[car_index].speed; //30% speed
                #ifdef DEBUG
                printf("[DEBUG] car %s from team %s -> metros percorridos: %d | gasolina: %f (em seguranca)\n",cars[car_index].car_number,teams[team_index].team_name,total_meters,fuel);
                #endif
                 
            }


            if(total_meters/config.track_len>laps){ //PASSOU PELA META
                laps++; //adiciona volta
                #ifdef DEBUG
                printf("[DEBUG] car %s from team %s -> acabou de passar pela meta | volta: %d\n",cars[car_index].car_number,teams[team_index].team_name,laps);
                #endif
                if(laps==config.laps_qnt || (stop_race_aux=get_stop_race())!=0){ 
                    //se chegou ao fim da corrida ou se tiver havido um 'pedido' para terminar a corrida
                    car_state_aux=notif.car_state;
                    notif.car_state=TERMINADO; 

                    //NOTIFICAR MUDANÇA DE ESTADO ao race manager process através de unnamed pipe
                    if(pthread_mutex_lock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                    write(fd_unnamed_pipe[team_index][1],&notif,sizeof(notif)); //notifica alteração de estado do carro
                    cars[car_index].car_state=TERMINADO;
                    if(pthread_mutex_unlock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}

                    if(car_state_aux==SEGURANCA){
                        //SE O CARRO ESTIVER EM SEGURANÇA QUANDO TERMINAR A CORRIDA, É NECESSÁRIO DECREMENTAR O NÚMERO DE CARROS EM SEGURANÇA EM MEMÓRIA PARTILHADA JÁ Q O ESTADO DA BOX DEPENDE DISSO
                        if(pthread_mutex_lock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}
                        teams[team_index].cars_in_safety_mode--;
                        if(teams[team_index].cars_in_safety_mode==0){ 
                            if(pthread_cond_signal(&teams[team_index].car_changed_state_cond)!=0){forced_shutdown();} //BOX WILL BECOME LIVRE INSTEAD OF RESERVADA
                        }
                        if(pthread_mutex_unlock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}
                    }

                    if(stop_race_aux==-1){ //exit thread!
                        #ifdef DEBUG
                        printf("[DEBUG] car %s thread from team %s exited\n",cars[car_index].car_number,teams[team_index].team_name);
                        #endif
                        pthread_exit(NULL);
                    }
                    break; //TERMINOU CORRIDA!
                }
                else if(notif.car_state==SEGURANCA){ 
                    #ifdef DEBUG
                    printf("[DEBUG] car %s from team %s -> a tentar entrar na box em modo de seguranca\n",cars[car_index].car_number,teams[team_index].team_name);
                    #endif
                    //ENTRA NA BOX SE ESTIVER LIVRE OU RESERVADA
                    if(pthread_mutex_lock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}
                    if(teams[team_index].box_state!=OCUPADA){
                        teams[team_index].car_in_box++;
                        if(pthread_cond_signal(&teams[team_index].car_changed_state_cond)!=0){forced_shutdown();} //BOX BECOMES OCUPADA
                        if(pthread_mutex_unlock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();} //unlock so that Team manager can change state of BOX and other cars can read 
                        
                        notif.car_state=BOX;
                        //NOTIFICAR race manager process através de unnamed pipe
                        if(pthread_mutex_lock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                        write(fd_unnamed_pipe[team_index][1],&notif,sizeof(notif)); //notifica alteração de estado do carro
                        cars[car_index].car_state=BOX;//CARRO NA BOX
                        if(pthread_mutex_unlock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                        
                        //REPARA O CARRO (demora um intervalo de tempo aleatorio entre reparacao_min_time e reparacao_max_time)
                        repair_time=(unsigned int)((config.reparacao_min_time*1000000+rand_r(&seed)%(unsigned int)(config.reparacao_max_time*1000000+1-config.reparacao_min_time*1000000))*(1/config.time_unit));
                        if(usleep(repair_time)==-1){forced_shutdown();} //rand_r is reentrant..thread safe :) 
                        //ATESTA O CARRO 
                        fuel=config.fuel_capacity; 
                        if(usleep((unsigned int)(2*(1000000/config.time_unit)))==-1){forced_shutdown();}

                        if(pthread_mutex_lock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}
                        teams[team_index].car_in_box--; //LEAVE BOX
                        teams[team_index].cars_in_safety_mode--;
                        if(pthread_cond_signal(&teams[team_index].car_changed_state_cond)!=0){forced_shutdown();} //BOX BECOMES LIVRE AGAIN
                        if(pthread_mutex_unlock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}

                        /***********/
                        if((stop_race_aux=get_stop_race())!=0){ //caso o carro esteja na box no momento em q é triggered o sigint ou o sigusr1..termina corrida imediatamente
                            notif.car_state=TERMINADO; 

                            //NOTIFICAR race manager process através de unnamed pipe
                            if(pthread_mutex_lock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                            write(fd_unnamed_pipe[team_index][1],&notif,sizeof(notif)); //notifica alteração de estado do carro
                            cars[car_index].car_state=TERMINADO;
                            if(pthread_mutex_unlock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}

                            if(stop_race_aux==-1){ //exit thread!
                                #ifdef DEBUG
                                printf("[DEBUG] car %s thread from team %s exited\n",cars[car_index].car_number,teams[team_index].team_name);
                                #endif
                                pthread_exit(NULL);
                            }
                            break; //TERMINOU CORRIDA!
                        }
                        /******************/
                        low_fuel=0; //reset
                        notif.car_state=CORRIDA;//CARRO DEIXA DE ESTAR EM SEGURANÇA E VOLTA A ESTAR EM MODO CORRIDA
                        //NOTIFICAR race manager process através de unnamed pipe
                        if(pthread_mutex_lock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                        write(fd_unnamed_pipe[team_index][1],&notif,sizeof(notif)); //notifica alteração de estado do carro através de unnamed pipe
                        cars[car_index].car_state=CORRIDA; //update shm
                        if(pthread_mutex_unlock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                    }
                    else{
                       if(pthread_mutex_unlock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}
                    }                    
                }
                else if(low_fuel){ 
                    #ifdef DEBUG
                    printf("[DEBUG] car %s from team %s -> a tentar entrar na box porque tem pouca gasolina\n",cars[car_index].car_number,teams[team_index].team_name);
                    #endif
                    //ENTRA NA BOX SE ESTIVER LIVRE
                    if(pthread_mutex_lock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}
                    if(teams[team_index].box_state==LIVRE){ //|| (notif.car_state==SEGURANCA && teams[team_index].box_state==RESERVADA)
                        teams[team_index].car_in_box++;
                        if(pthread_cond_signal(&teams[team_index].car_changed_state_cond)!=0){forced_shutdown();} //BOX BECOMES OCUPADA
                        if(pthread_mutex_unlock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();} //unlock so that Team manager can change state of BOX and other cars can read 
                        
                        notif.car_state=BOX;  //CARRO NA BOX
                        //NOTIFICAR race manager process através de unnamed pipe
                        if(pthread_mutex_lock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                        write(fd_unnamed_pipe[team_index][1],&notif,sizeof(notif)); //notifica alteração de estado do carro
                        cars[car_index].car_state=BOX; //update shm
                        if(pthread_mutex_unlock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                        
                        //ATESTA O CARRO 
                        fuel=config.fuel_capacity; 
                        if(usleep((unsigned int)(2*(1000000/config.time_unit)))==-1){forced_shutdown();}

                        if(pthread_mutex_lock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}
                        teams[team_index].car_in_box--; //LEAVE BOX
                        if(pthread_cond_signal(&teams[team_index].car_changed_state_cond)!=0){forced_shutdown();} //BOX BECOMES LIVRE AGAIN
                        if(pthread_mutex_unlock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}

                        low_fuel=0; //reset
                        notif.car_state=CORRIDA; //CARRO DE VOLTA EM MODO CORRIDA
                        //NOTIFICAR race manager process através de unnamed pipe
                        if(pthread_mutex_lock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                        write(fd_unnamed_pipe[team_index][1],&notif,sizeof(notif)); //notifica alteração de estado do carro
                        cars[car_index].car_state=CORRIDA; //update shm
                        if(pthread_mutex_unlock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                    }else{
                        if(pthread_mutex_unlock(&teams[team_index].mutex_car_changed_state)!=0){forced_shutdown();}
                    } 
                }
                //NOTIFICAR +1 VOLTA
                car_state_aux=notif.car_state; //save current car state
                notif.car_state=-2; //notifies race_manager, através de unnamed pipe, que passou na meta (synch eficiency purposes)
                if(pthread_mutex_lock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                write(fd_unnamed_pipe[team_index][1],&notif,sizeof(notif)); //notifica alteração de estado do carro
                if(pthread_mutex_unlock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                notif.car_state=car_state_aux;
            }
            if(fuel<=0){
                #ifdef DEBUG
                printf("[DEBUG] car %s from team %s -> ficou sem gasolina antes de conseguir acabar a corrida!\n",cars[car_index].car_number,teams[team_index].team_name);
                #endif
                notif.car_state=DESISTENCIA;
                //NOTIFICAR race manager process através de unnamed pipe
                if(pthread_mutex_lock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
                write(fd_unnamed_pipe[team_index][1],&notif,sizeof(notif)); //notifica alteração de estado do carro
                cars[car_index].car_state=DESISTENCIA; //update shm
                if(pthread_mutex_unlock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}

                if(get_stop_race()==-1){ //exit thread!
                    #ifdef DEBUG
                    printf("[DEBUG] car %s thread from team %s exited\n",cars[car_index].car_number,teams[team_index].team_name);
                    #endif
                    pthread_exit(NULL);
                }
                break; //TERMINOU CORRIDA 
            }
        }
    }
    //car thread function. cada car thread é responsavel pela gestao das voltas a pista, pela gestao do combustivel, e pela gestao do modo de circulacao(normal ou em segurança)    
    return NULL;
}

int get_car_state_by_index(int team_index,int car_index){
    int st;
    if(pthread_mutex_lock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();} //TENDO EM CONTA Q TODAS AS ALTERAÇÕES AO ESTADO DO CARRO SÃO NOTIFICADAS ATRAVÉS DO UNNAMED PIPE DE CADA EQUIPA, PODEMOS UTILIZAR, EFICIENTEMENTE, ESTE MUTEX Q GARANTE A SYNCH DA ESCRITA NESSE UNNAMED PIPE, COMO MECANISMO DE SYNCH DE LEITURA E ESCRITA DO PROPRIO ESTADO DO CARRO
    st=cars[car_index].car_state;
    if(pthread_mutex_unlock(&teams[team_index].mutex_write_to_unnamed_pipe)!=0){forced_shutdown();}
    return st;
}

void malfunction_manager(void){
    //TODO:processo responsavel por gerar aleatoriamente as avarias dos carros a partir da informacao da sua fiabilidade
    srand(getpid()*time(NULL)); //set seed
    int rand_num;
    int i,j;
    int car_index;
    int aux_stop_race;
    struct message msg;
    msg.val=0; //payload da mensagem de avaria

    while(1){
        if(pthread_mutex_lock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
        while(shared_memory->race_state==OFF && (shared_memory->race_state==ON || (aux_stop_race=get_stop_race())!=-1)){ //fica em espera enquanto a corrida não começa
            //desbloqueia quando shared_memory->race_state==ON || (shared_memory->race_state==ON && shared_memory->stop_race==-1)
            if(pthread_cond_wait(&shared_memory->race_state_cond,&shared_memory->mutex_race_state)!=0){forced_shutdown();}
        }
        if(shared_memory->race_state==OFF && aux_stop_race==-1){ //se a corrida estiver terminada ou ainda n tiver começado
            if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}
            exit(0); //END PROCESS
        }
        if(pthread_mutex_unlock(&shared_memory->mutex_race_state)!=0){forced_shutdown();}      
        //sem_wait(sem_malfunction_generator); 
        if(usleep((unsigned int)(config.avaria_time_interval*(1000000/config.time_unit)))==-1){forced_shutdown();}
        //all this values can be acessed during RACE time because, 
        //no changes are made or allowed to be made, to this values, during race time. 
        for(i=0;i<shared_memory->curr_teams_qnt;i++){ 
            for(j=0;j<teams[i].curr_car_qnt;j++){
                car_index=i*config.max_car_qnt_per_team+j;
                if(get_car_state_by_index(i,car_index)==CORRIDA){ //GERAR AVARIAS APENAS EM CARROS Q SE ENCONTRAM EM MODO DE CORRIDA (desnecessário estar a mandar msgs de avaria pela msg queue para carros aos quais essa avaria não terá qlqr impacto)
                    rand_num=rand()%101; //generate random number from [0-100]
                    if(rand_num>=cars[car_index].reliability){ 
                        //AVARIA NO CARRO se rand_num>=cars[car_index]
                        #ifdef DEBUG
                        printf("[DEBUG] avaria gerada no car number [%s] da team [%s]\n",cars[car_index].car_number,teams[i].team_name);
                        #endif
                        //...comunicar avaria ao carro, pela message queue 
                        msg.mtype = i*config.max_car_qnt_per_team+(j+1);
                        if(msgsnd(mq_id,&msg,sizeof(struct message)-sizeof(long),0)==-1){forced_shutdown();} //flag==0...blocks if message queue is full!
                        sem_wait(sem_stats); //podiamos incrementar o nr de avarias nos carros mas isso poderia trazer problemas de eficiência..daí termos escolhido incrementar o valor neste processo
                        shared_memory->total_malfunctions++;
                        sem_post(sem_stats);
                    }
                }
            }
        }
    }
}

void init_shared_memory(void){
    //int i,j;
    pthread_mutexattr_t attrmutex;
    pthread_condattr_t attrcondv;
    //cond_vars and mutexes created using static initialization cannot be used between processes!
    
    //SHM Create
	if ((shmid=shmget(IPC_PRIVATE,sizeof(mem_struct)+config.teams_qnt*sizeof(team)+config.teams_qnt*config.max_car_qnt_per_team*sizeof(car),IPC_CREAT|0700)) < 0){
        write_log("[ERROR] in shmget with IPC_CREAT");
		exit(-1);
	} 
    //SHM Attach
    if((shared_memory=(mem_struct*)shmat(shmid,NULL,0))==(mem_struct*)-1){
        write_log("[ERROR] in shmat");
        clean_resources();
        exit(-1);
    }

    shared_memory->race_state=OFF; 
    shared_memory->curr_teams_qnt=0; 
    shared_memory->wait_to_read=0; shared_memory->stop_race_readers_in=0; shared_memory->stop_race_readers_out=0;
    //shared_memory->new_car_team=-1;
    shared_memory->stop_race=0;

    //INIT STATS
    shared_memory->total_malfunctions=0;
    shared_memory->total_refuels=0;
    shared_memory->cars_in_pista_qnt=0;
    shared_memory->total_car_count=0;
    
    teams=(team*)(shared_memory+1);
    cars=(car*)(teams+config.teams_qnt);

    //init mutex and condition variable used between processes 
    if(pthread_mutexattr_init(&attrmutex)!=0){clean_resources();exit(-1);}//Initialize attribute of mutex
    if(pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED)!=0){clean_resources();exit(-1);}
    if(pthread_condattr_init(&attrcondv)!=0){clean_resources();exit(-1);}//Initialize attribute of condition variable
    if(pthread_condattr_setpshared(&attrcondv, PTHREAD_PROCESS_SHARED)!=0){clean_resources();exit(-1);}
    //note: these ^ attrs can be used to init multiple mutexes and cond vars
    if(pthread_mutex_init(&shared_memory->mutex_race_state, &attrmutex)!=0){clean_resources();exit(-1);}//init mutex
    if(pthread_cond_init(&shared_memory->race_state_cond, &attrcondv)!=0){clean_resources();exit(-1);}//init cond var
    if(pthread_mutexattr_destroy(&attrmutex)!=0){clean_resources();exit(-1);} 
    if(pthread_condattr_destroy(&attrcondv)!=0){clean_resources();exit(-1);}
/*
    for(i=0;i<config.teams_qnt;i++){
        
        cars[i].consumption=0;
        for(j=0;j<config.max_car_qnt_per_team;j++){
            cars[i*config.max_car_qnt_per_team+j].speed=-1;
            cars[i*config.max_car_qnt_per_team+j].team_index=i;
            cars[i*config.max_car_qnt_per_team+j].laps=0;
        }
    }*/
}

int compareLaps(const void *a, const void *b){
    int laps_a=((struct car*)a)->laps;
    int laps_b=((struct car*)b)->laps;
    //multiply by -1 so it gets ordered by descending order
    return -1*(laps_a-laps_b); //if laps_a > laps_b..the result will be > 0 ; if laps_a==laps_b..the result will be 0 ; ..
}

void print_stats(){
    //stats são colocados no ecrã apenas enquanto decorre uma corrida
    char log[BUFF_SIZE];
    int i,j,count=0;
    int top_limit=5;
    int cars_qnt=shared_memory->total_car_count;
    struct car *sorted_cars; //é assegurada synch dado q as stats são impressas apenas enquanto decorre a corrida e enquanto decorre a corrida o total_car_count n pode ser escrito

    sorted_cars=(struct car*)malloc(sizeof(struct car)*cars_qnt);
    //copy cars in shared memory to aux array 
    for(i=0;i<config.teams_qnt;i++)
        for(j=0;j<teams[i].curr_car_qnt;j++)
            memcpy(&sorted_cars[count++],&cars[i*config.max_car_qnt_per_team+j],sizeof(struct car));
        
    count=0;
    qsort(sorted_cars,cars_qnt,sizeof(struct car),compareLaps); //SORT CARS BY LAPS QNT

    //WRITE TO LOG & STDOUT 
    if(sem_wait(sem_log)==-1){
        forced_shutdown(); //erro
    }
    if(sem_wait(sem_stats) == -1){
        forced_shutdown();    
    }

    update_curr_time();

    fprintf(stdout,"%s\t[STATISTICS]\nTop 5:\n",curr_time); //write to stdout
    fprintf(log_fp,"%s\t[STATISTICS]\nTop 5:\n",curr_time); //write to log file
    if(cars_qnt<5){
        top_limit=cars_qnt;
    }
    for(i=0;i<top_limit;i++){
        sprintf(log,"[%d]-| Car %s | Team %s | Laps %d | Box Stops %d |\n",++count,sorted_cars[i].car_number,teams[sorted_cars[i].team_index].team_name,sorted_cars[i].laps,sorted_cars[i].box_stops);
        fprintf(stdout,"%s",log); //write to stdout
        fprintf(log_fp,"%s",log); //write to log file
    }

    sprintf(log,"\n[Last]-| Car %s | Team %s | Laps %d | Box Stops %d |\n",sorted_cars[cars_qnt-1].car_number,teams[sorted_cars[cars_qnt-1].team_index].team_name,sorted_cars[cars_qnt-1].laps,sorted_cars[cars_qnt-1].box_stops);
    fprintf(stdout,"%s",log); //write to stdout
    fprintf(log_fp,"%s",log); //write to log file
        
    sprintf(log,"\nTotal Malfunctions: %d\nTotal Refuels: %d\n\tOn Track: %d cars\n--------------------",shared_memory->total_malfunctions,shared_memory->total_refuels,shared_memory->cars_in_pista_qnt);
    fprintf(stdout,"%s\n",log); //write to stdout
    fprintf(log_fp,"%s\n",log); //write to log file
    fflush(log_fp);

    if(sem_post(sem_stats) == -1){
        forced_shutdown();   
    }
    if(sem_post(sem_log)==-1){
        forced_shutdown(); //erro
    } 

    free(sorted_cars);
}

char* box_state_to_str(int state){
    switch (state)
    {
    case OCUPADA:
        return "OCUPADA";
    case LIVRE:
        return "LIVRE";
    case RESERVADA:
        return "RESERVADA";
    default:
        return "";
    }
}

void clean_resources(){
    int i;
    signal(SIGINT, SIG_IGN); //ignore sigint signals

    write_log("SIMULATOR CLOSING");
    
    pthread_mutex_destroy(&shared_memory->mutex_race_state);
    pthread_cond_destroy(&shared_memory->race_state_cond);
    for(i=0;i<config.teams_qnt;i++){
        pthread_mutex_destroy(&teams[i].mutex_write_to_unnamed_pipe);
        pthread_mutex_destroy(&teams[i].mutex_car_changed_state);
        pthread_cond_destroy(&teams[i].car_changed_state_cond);
    }
    #ifdef DEBUG
    printf("[DEBUG] mutexes and cond vars cleaned!\n");
    #endif

    //SHARED MEMORY
    if(shmid>=0){
        shmdt(shared_memory); //detach
        shmctl(shmid,IPC_RMID,NULL); //destroy
    }

    #ifdef DEBUG
    printf("[DEBUG] SHM cleaned!\n");
    #endif
    
    //SEMAPHOREs 
    if(sem_log>=0){
       sem_close(sem_log);	//destroy the semaphore
	   sem_unlink("SEM_LOG"); 
    }
    
    if(sem_stop_race_readers_in>=0){
    sem_close(sem_stop_race_readers_in);
    sem_unlink("SEM_STOP_RACE_READERS_IN");
    }
    if(sem_stop_race_readers_out>=0){
    sem_close(sem_stop_race_readers_out);
    sem_unlink("SEM_STOP_RACE_READERS_OUT");
    }
    if(sem_write_stop_race>=0){
    sem_close(sem_write_stop_race);
    sem_unlink("SEM_WRITE_STOP_RACE");
    }

    if(sem_stats>=0){
    sem_close(sem_stats);
    sem_unlink("SEM_STATS");
    }
    #ifdef DEBUG
    printf("[DEBUG] semaphores cleaned!\n");
    #endif

    //unlink named pipe
    unlink(PIPE_NAME);
    #ifdef DEBUG
    printf("[DEBUG] named pipe cleaned!\n");
    #endif
    //close unnamed pipes file descriptors
    for(i=0;i<config.teams_qnt;i++){
        close(fd_unnamed_pipe[i][0]); //fd for reading
        close(fd_unnamed_pipe[i][1]); //fd for writing
    }
    free(fd_unnamed_pipe);
    #ifdef DEBUG
    printf("[DEBUG] unnamed pipes cleaned!\n");
    #endif
    
    //destroy msg queue
    msgctl(mq_id,IPC_RMID,NULL); //msg queue
    #ifdef DEBUG
    printf("[DEBUG] message queue cleaned!\n");
    #endif
    
    //close log file
    fclose(log_fp);
    #ifdef DEBUG
    printf("[DEBUG] log file fd closed!\n");
    #endif

    system("./kill_ipcs.sh");
    #ifdef DEBUG
    printf("[DEBUG] ipcs cleaned!\n");
    #endif

    #ifdef DEBUG
    printf("[DEBUG] sucessfuly cleaned everything!\n");
    #endif

}

void forced_shutdown(void){
    //CHAMAR APENAS EM CASO DE ERRO
    #ifdef DEBUG
    printf("[DEBUG] FORCED SHUTTED DOWN!\n");
    #endif

    clean_resources(); 

    //forçar matar todos os processos deste programa
    system("killall -9 main"); 
}

void update_curr_time(void){
    //call this function to update time on curr_time 
    time_t t;
    struct tm *curr_time_struct;
    t=time(NULL); curr_time_struct=localtime(&t);

    strftime(curr_time,9,"%H:%M:%S",curr_time_struct);
}

void write_log(char *log){
    //synchronized
    if(sem_wait(sem_log)==-1){
        forced_shutdown(); //erro
    }

    update_curr_time();

    fprintf(stdout,"%s %s\n",curr_time,log); //write to stdout
    fprintf(log_fp,"%s %s\n",curr_time,log); //write to log file
    fflush(log_fp);

    if(sem_post(sem_log)==-1){
        forced_shutdown(); //erro
    } 
}

void init_log(void){
    //create/reset log file 
    if((log_fp=fopen(LOG_FILENAME,"w"))==NULL){
        write_log("[ERROR] creating LOG file");
        clean_resources();
        exit(-1);
    }
}

void read_config(void){
    FILE *fp;
    char *aux; 
    size_t size=1024;
    int len;

    if((fp=fopen(CONFIG_FILENAME,"r"))==NULL){
        write_log("[ERROR] opening CONFIG file");
        exit(-1);
    }
    //allocate aux buffer
    if((aux=(char*)malloc(size*sizeof(char)))==NULL){
        write_log("[ERROR] allocating aux buffer");
        fclose(fp);
        exit(-1);
    }
    //read file formatted lines
    //1st line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 1st line from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%f",&(config.time_unit))!=1){
        write_log("[ERROR] anomality in expected structure from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //2nd line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 2nd line from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d, %d",&(config.track_len),&(config.laps_qnt))!=2){
        write_log("[ERROR] anomality in expected structure from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //3rd line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 3rd line from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.teams_qnt))!=1){
        write_log("[ERROR] anomality in expected structure from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    if(config.teams_qnt<3){
        write_log("[ERROR] invalid config parameter (line 3 -> number of teams), 3 or more teams are required! Correct CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //4th line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 4th line from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.max_car_qnt_per_team))!=1){
        write_log("[ERROR] anomality in expected structure from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //5th line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 5th line from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%f",&(config.avaria_time_interval))!=1){
        write_log("[ERROR] anomality in expected structure from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //6th line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 6th line from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%f, %f",&(config.reparacao_min_time),&(config.reparacao_max_time))!=2){
        write_log("[ERROR] anomality in expected structure from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //last line
    if((len=getline(&aux,&size,fp))==-1){
        write_log("[ERROR] reading 7th line from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.fuel_capacity))!=1){
        write_log("[ERROR] anomality in expected structure from CONFIG file ");
        fclose(fp);
        free(aux);
        exit(-1);
    }

    #ifdef DEBUG
    printf("IMPORTED CONFIG:\n");
    printf("--------[DEBUG]-----------\n");
    printf("time_units: %f\ntrack_length: %d , laps_num: %d\nteams_qnt: %d\nT_Avaria: %f\nT_Box_Min: %f, T_Box_Max: %f\nfuel_capacity: %d\n",config.time_unit,config.track_len,config.laps_qnt,config.teams_qnt,config.avaria_time_interval,config.reparacao_min_time,config.reparacao_max_time,config.fuel_capacity);
    printf("--------------------------\n");
    #endif

    free(aux);
    fclose(fp);
}

