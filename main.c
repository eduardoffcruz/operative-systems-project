//_______________________________Projeto Sistemas Operativos @2021
//Eduardo F. Ferreira Cruz 2018285164
//Gonçalo Marinho Barroso 2019216314

#include "main.h"

#define DEBUG 

int main(void){

    //RACE SIMULATOR PROCESS (main process)
    
    //IMPORT CONFIG from file
    read_config();

    //INIT LOG file
    init_log();

    //SHARED MEMORY 
    init_shared_memory();
 
    //create RACE MANAGER PROCESS
    if(fork()==0){
        //TODO:
        #ifdef DEBUG
        printf("Race Simulator [pid:%d] created Race Manager process [pid:%d]!\n",getppid(),getpid());
        #endif
        race_manager();

        exit(0); 
    }
    //create MALFUNCTION MANAGER PROCESS
    if(fork()==0){
        //TODO:
        #ifdef DEBUG
        printf("Race Simulator [pid:%d] created Malfunction Manager process [pid:%d]!\n",getppid(),getpid());
        #endif
        malfunction_manager();

        exit(0);        
    }

    //wait(NULL);wait(NULL); //esperar q os 2 processos filhos terminem

    destroy_all(); 
    return 0;
}

void race_manager(void){
    //TODO: 
    int i;
    int teams_pid[config.teams_qnt];
    //create TEAM MANAGER PROCESSes (1 per team)
    for(i=0;i<config.teams_qnt;i++){
        if((teams_pid[i]=fork())==0){
            #ifdef DEBUG
            printf("Race Manager [pid:%d] created Team %d process [pid:%d]!\n",getppid(),i,getpid());
            #endif
            //TEAM i MANAGER PROCESS
            team_manager(teams_pid[i]);


            exit(0);
        }
    }
    
    //for(i=0;i<config.teams_qnt;i++) wait(NULL);    
}

void set_race_state(){
    //só pode aceder um processo de cada vez à variavel shared_variable->race_flag, para escrita 
    //só pode ser acedida para escrita qnd nenhum processo estiver a ler
    //problema clássico do escritor/leitor
    ;
}

race_state get_race_state(){
    //todos os processos podem ler a variável shared_variable->race_flag (desde q nenhum processo esteja a escrever nela!)
    

}

void team_manager(int team_pid){
    //TODO: team manager processes create car threads
    //TODO: team manager escreve as informações de cada carro, recebidas do Named Pipe, na shared_emmory
    //TODO: manter atualizada na shared_memory, o estado da box! (LIVRE;OCUPADA;RESERVADA)
    //car threads sao criadas através da receção de comandos através do named pipe 
    /*
    if(pthread_create(..., NULL, car_thread){
        //erro
        fprintf(stderr,"Error: unable to create car thread\n");
        exit(-1);
    }
    pthread_exit(NULL);*/
    ;
}

void *car_thread(void){
    //car thread function
    ;
}

void malfunction_manager(void){
    //TODO:
    ;
}

void init_shared_memory(void){
    //SHM Create
	if ((shmid=shmget(IPC_PRIVATE,sizeof(int),IPC_CREAT|0700)) < 0){
		sprintf(stderr,"Error: in shmget with IPC_CREAT\n");
		exit(-1);
	} 
    //SHM Attach
    if((shared_memory=(mem_struct*)shmat(shmid,NULL,0))==(mem_struct*)-1){
        sprintf(stderr,"Error: in shmat\n");
        exit(-1);
    }

    //allocate memory para o array com o estado da box de cada equipa (na SHM)
    if((shared_memory->boxes_states=(box_state*)malloc(config.teams_qnt*sizeof(box_state))==NULL){
        sprintf(stderr,"Error: allocating memory!\n");
        shmdt(shared_memory); //detach
        shmctl(shmid,IPC_RMID,NULL); //destroy
        exit(-1);
    }

    //initialize values
    for(int i=0;i<config.teams_qnt;i++) shared_memory->boxes_states[i]=LIVRE; //inicialmente todas as boxes estão livres
    shared_memory->race_flag=OFF; 
}

void destroy_all(void){
    write_log("SIMULATOR CLOSING");

    //SHARED MEMORY
    //TODO: free dinamically alocated memory in shared_memory struct or nah? 
    shmdt(shared_memory); //detach
    shmctl(shmid,IPC_RMID,NULL); //destroy
    //SEMAPHOREs

	//sem_close(mutex);	//destroy the semaphore
	//sem_unlink("MUTEX");



    //close log file
    fclose(log_fp);

    #ifdef DEBUG
    printf("--------------------------\n");
    printf("sucessefuly destroyed everything!\n");
    printf("--------------------------\n");
    #endif
    //TODO: Não esquecer de chamar no final o kill_ipcs.sh dado pelo prof!
}

void update_curr_time(void){
    //call this function to update time on curr_time 
    time_t t;
    struct tm *curr_time_struct;
    t=time(NULL); curr_time_struct=localtime(&t);

    strftime(curr_time,9,"%H:%M:%S",curr_time_struct);
}

void write_log(char *log){
    update_curr_time();
    fprintf(stdio,"%s %s\n",curr_time,log); //write to stdout
    fprintf(log_fp,"%s %s\n",curr_time,log); //write to log file
}

void init_log(void){
    //create/reset log file 
    if((log_fp=fopen(LOG_FILENAME,"w"))==NULL){
        fprintf(stderr,"Error: creating %s file\n",LOG_FILENAME);
        exit(-1);
    }
    write_log("SIMULATOR STARTING");
}

void read_config(void){
    FILE *fp;
    char *aux; 
    size_t size=1024;

    if((fp=fopen(CONFIG_FILENAME,"r"))==NULL){
        fprintf(stderr,"Error: opening %s file\n",CONFIG_FILENAME);
        exit(-1);
    }
    //allocate aux buffer
    if((aux=(char*)malloc(size*sizeof(char)))==NULL){
        fprintf(stderr,"Error: unable to allocate aux buffer\n");
        fclose(fp);
        exit(-1);
    }
    //read file formatted lines
    //1st line
    if(getline(&aux,&size,fp)==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    if(sscanf(aux,"%d",&(config.time_unit))!=1){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //2nd line
    if(getline(&aux,&size,fp)==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    if(sscanf(aux,"%d, %d",&(config.track_len),&(config.laps_qnt))!=2){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //3rd line
    if(getline(&aux,&size,fp)==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    if(sscanf(aux,"%d",&(config.teams_qnt))!=1){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    if(config.teams_qnt<3){
        fprintf(stderr,"Error: invalid config parameter [line 3] \n\t-> got %d teams, 3 or more are required\n",config.teams_qnt);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //4th line
    if(getline(&aux,&size,fp)==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    if(sscanf(aux,"%d",&(config.avaria_time_interval))!=1){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //5th line
    if(getline(&aux,&size,fp)==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    if(sscanf(aux,"%d, %d",&(config.reparacao_min_time),&(config.reparacao_max_time))!=2){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //last line
    if(getline(&aux,&size,fp)==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    if(sscanf(aux,"%d",&(config.fuel_capacity))!=1){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }

    #ifdef DEBUG
    printf("IMPORTED CONFIG:\n");
    printf("--------------------------\n");
    printf("time_units: %d\ntrack_length: %d , laps_num: %d\nteams_qnt: %d\nT_Avaria: %d\nT_Box_Min: %d, T_Box_Max: %d\nfuel_capacity: %d\n",config.time_unit,config.track_len,config.laps_qnt,config.teams_qnt,config.avaria_time_interval,config.reparacao_min_time,config.reparacao_max_time,config.fuel_capacity);
    printf("--------------------------\n");
    #endif

    free(aux);
    fclose(fp);

}

