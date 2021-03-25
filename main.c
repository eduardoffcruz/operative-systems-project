//_______________________________Projeto Sistemas Operativos @2021
//Eduardo F. Ferreira Cruz 2018285164
//Gonçalo Marinho Barroso 2019216314

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
    sem_unlink("SEM_LOG");
    sem_log=sem_open("SEM_LOG",O_CREAT|O_EXCL,0700,1); //binary semaphore

    sem_unlink("SEM_READERS_IN");
    sem_readers_in=sem_open("SEM_READERS_IN",O_CREAT|O_EXCL,0700,1); //binary semaphore
    sem_unlink("SEM_READERS_OUT");
    sem_readers_out=sem_open("SEM_READERS_OUT",O_CREAT|O_EXCL,0700,1); //binary semaphore
    sem_unlink("SEM_WRITECAR");
    sem_writecar=sem_open("SEM_WRITECAR",O_CREAT|O_EXCL,0700,0); //binary semaphore 
    
    #ifdef DEBUG
    printf("[DEBUG] semaphores created\n");
    #endif

    //INIT LOG file
    init_log();
    #ifdef DEBUG
    printf("[DEBUG] log file created/cleared\n");
    #endif
    write_log("SIMULATOR STARTING");


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


    //
    sa.sa_flags=0;
    sa.sa_handler=sigtstp_handler;
    //captura sinal SIGTSTP
    if(sigaction(SIGTSTP,&sa,NULL)==-1){
        destroy_all();
    }
    //

    wait(NULL);wait(NULL); //esperar q os 2 processos filhos terminem
    #ifdef DEBUG
    printf("[DEBUG] Race Manager process and Malfunction Manager process ended\n");
    #endif



    destroy_all(); 
    return 0;
}

car create_car(char* car_number, int speed, float consumption, int reliability){
    car c;
    int len=strlen(car_number);
    memcpy(c.car_number,car_number,len*sizeof(char));
    c.car_number[len]='\0';
    c.car_state=BOX;
    c.speed=speed;
    c.consumption=consumption;
    c.reliability=reliability;

    return c;
}

void add_car_to_shm(char *command){
    //takes care of ADDCAR command
    car c;
    char team_name[128];
    char car_number[32];
    int speed, reliability;
    float consumption;
    int is_valid;

    is_valid=validate_addcar_command(command, team_name, car_number, &speed, &consumption, &reliability);
    if(is_valid==-1){
        fprintf(stderr,"Error: buff size (9) exceeded for speed, consumption or reliability input\n");
        return;
    }else if(is_valid==0){
        fprintf(stderr,"Error: INVALID COMMAND\n");
        return;
    }

    c=create_car(car_number,speed,consumption,reliability);
    #ifdef DEBUG
    printf("[DEBUG] created car from command. %s %s %f %d %d %d\n",team_name,c.car_number,c.consumption,c.speed,c.reliability,c.car_state);
    #endif
    
    //implementaçao do caso clássico de write/readers sem starvation:
    //a ideia consiste em o writer indicar aos readers a sua necessidade de escrever. A partir daí nenhum reader pode entrar na zona critica.
    //ao sairem da zona critica cada reader verifica se o writer está waiting e o ultimo reader a sair liberta o writer para q ele possa entrar na zona e escrever
    //depois de escrever, o write liberta os readers q estão waiting para q eles possam novamente efetuar leitura
    sem_wait(sem_readcar);
    sem_wait(sem_readcar1);
    if(shared_memory->readers_out==shared_memory->readers_in){
        sem_post(sem_readcar1);
    }
    else{
        shared_memory->wt=1;//true
        sem_post(sem_readcar1);
        sem_wait(sem_writecar);
        shared_memory->wt=0;        
    }
    add_car_to_teams_list(team_name,c); //add car to shared memory (CRITICAL SECTION)
    sem_post(sem_readcar);

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

team create_team(char *team_name){
    team t;
    struct car cs[config.max_car_qnt_per_team];
    int len=strlen(team_name);
    memcpy(t.team_name,team_name,len*sizeof(char));
    t.team_name[len]='\0';
    t.cars=cs;
    t.box_state=LIVRE;
    t.curr_car_qnt=0;

    return t;
}

void add_car_to_teams_list(char* team_name, car c){
    //verifica se já existe uma equipa com o nome team_name, no array de team's na SHM. Se existir, adiciona o car ao inicio da linked list que a struct team possui.
    //se n existir nenhuma equipa com esse nome e se ainda houver espaço para mais equipas, uma team é criada e adicionada à array de team's e o car é adicionado a essa team.
    int i,j;
    team t;
    if(shared_memory->curr_teams_qnt==0){
        t=create_team(team_name);
        t.cars[0]=c;
        t.curr_car_qnt++;       
        shared_memory->teams[0]=t; //TODO: !!
        shared_memory->curr_teams_qnt++; //TODO: !!
        #ifdef DEBUG
        printf("[DEBUG]first team created\n");
        #endif
        return;
    }
    for(i=0;i<shared_memory->curr_teams_qnt;i++){
        if(strcmp(shared_memory->teams[i].team_name,team_name)==0){
            break;
        }
    }
    if(i==shared_memory->curr_teams_qnt){
        //n existe nenhuma team com a team_name ainda
        if(shared_memory->curr_teams_qnt==config.teams_qnt){
            fprintf(stderr,"LIMITE DE EQUIPAS EXCEDIDO (nao pode adicionar mais equipas)!");
            return;
        } 
        t=create_team(team_name);
        t.cars[0]=c;
        t.curr_car_qnt++;  
        shared_memory->teams[i]=t; //TODO: !!
        shared_memory->curr_teams_qnt++; //TODO: !!
        #ifdef DEBUG
        printf("[DEBUG]car added to new team. %s %f %d %d %d\n",c.car_number,c.consumption,c.speed,c.reliability,c.car_state);
        #endif
    }
    else{
        //já existe team com este nome no index i
        //insere o car no inicio da linked list (head);
        j=shared_memory->teams[i].curr_car_qnt;
        if(j==config.max_car_qnt_per_team){
            fprintf(stderr,"LIMITE DE CARROS POR EQUIPA EXCEDIDO (nao pode adicionar mais carros a esta equipa)!");
            return;
        }
        shared_memory->teams[i].cars[j]=c;
        shared_memory->teams[i].curr_car_qnt++;
        #ifdef DEBUG
        printf("[DEBUG]car added to already existing team. %s %f %d %d %d\n",c.car_number,c.consumption,c.speed,c.reliability,c.car_state);
        #endif
    }
}



void race_manager(void){
    //TODO: 
    //recebe as informacoes de cada carro, através do named pipe, e escreve-as na memória partilhada. 
    //OS TEAM MANAGER PROCESSES VERIFICAM A SHM E CRIAM OS CARROS CONSOANTE A INFORMACAO Q LÁ FOI ESCRITA SOBRE OS CARROS
    int i;
    int teams_pid[config.teams_qnt];
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

    //TODO:
    char *exemplo="ADDCAR TEAM: A, CAR: 20, SPEED: 30, CONSUMPTION: 0.04, RELIABILITY: 95";
    if(strncmp(exemplo,"ADDCAR",6)==0){
        add_car_to_shm(exemplo);
    }
    




    
    for(i=0;i<config.teams_qnt;i++) wait(NULL);    
    printf("[DEBUG] all Team Manager processes ended\n");
    //TODO:processo resposavel pela gestao da corrida(inicio, fim, classificacao final) e das equipas em jogo.
}

void sigint_sigusr1_handler(int signal){
    //sigint: aguardar q todos os carros cruzem a meta (mesmo q n seja a sa ultima volta), os carros q se encontram na box no momento da instrução devem terminar. aposto doso os carros concluirem a corrida deverá imprimir as estatisticas do jogo e terminal/libertar/remover todos os recursos utilizados
    
    //sigusr1:  para interromper uma corrida q esteja a decorrer. a corrida deverá terminar mal todos os carros cheguem à meta (como acontece com o SIGINT) e a sua estatistica final deve ser apresentada. a informacao da interrupcao deve ser escrita no log (nao termina o programa, se start race for escrito, volta a iniciar uma corrida)
}

void sigtstp_handler(){ 
    //imprime estatisticas
    print_stats();
}

void set_race_state(){
    //só pode aceder um processo de cada vez à variavel shared_variable->race_flag, para escrita 
    //só pode ser acedida para escrita qnd nenhum processo estiver a ler
    //problema clássico do escritor/leitor
    ;
}

enum race_state_type get_race_state(){
    //todos os processos podem ler a variável shared_variable->race_flag (desde q nenhum processo esteja a escrever nela!)  
    return 1;
}

void team_manager(int team_id){
    //gere a box e carros da equipa. responsavel pela reparacao dos carros da equipa e por atestar o deposito de combustivel
    //TODO: team manager processes create car threads
    //TODO: team manager escreve as informações de cada carro, recebidas do Named Pipe, na shared_emmory
    //TODO: manter atualizada na shared_memory, o estado da box! (LIVRE;OCUPADA;RESERVADA)
    //car threads sao criadas através da receção de comandos através do named pipe 
    
    do{
        //read cars from shared memory
        sem_wait(sem_readers_in); //mutex para 
        shared_memory->readers_in++;
        sem_post(sem_readers_in);

        //verificar se já existe registo da team i através do curr_team_qnt 
        //se sim, LER CARROs e criar threads

        sem_wait(sem_readers_out);
        shared_memory->readers_out++;
        if(shared_memory->wt==1 && shared_memory->readers_in==shared_memory->readers_out){
            sem_post(sem_writecar);
        } 
        sem_post(sem_readers_out);

    }
    while(get_race_state()==OFF);
    /*if(pthread_create(..., NULL, car_thread){
        //erro
        fprintf(stderr,"Error: unable to create car thread\n");
        exit(-1);
    }




    pthread_exit(NULL);*/
    ;
}

void *car_thread(void){
    //car thread function. cada car thread é responsavel pela gestao das voltas a pista, pela gestao do combustivel, e pela gestao do modo de circulacao(normal ou em segurança)
    printf("hello i'm a car");
    return NULL;
}

void malfunction_manager(void){
    //TODO:processo responsavel por gerar aleatoriamente as avarias dos carros a partir da informacao da sua fiabilidade
    ;
}

void init_shared_memory(void){
    struct team ts[config.teams_qnt];
    //SHM Create
	if ((shmid=shmget(IPC_PRIVATE,sizeof(int),IPC_CREAT|0700)) < 0){
		fprintf(stderr,"Error: in shmget with IPC_CREAT\n");
		exit(-1);
	} 
    //SHM Attach
    if((shared_memory=(mem_struct*)shmat(shmid,NULL,0))==(mem_struct*)-1){
        fprintf(stderr,"Error: in shmat\n");
        exit(-1);
    }

    //initialize values
    shared_memory->teams=ts;
    shared_memory->race_state=OFF; 
    shared_memory->curr_teams_qnt=0; 
    shared_memory->wt=0; shared_memory->readers_in=0;shared_memory->readers_out=0;

}

void print_stats(){
    ;
}

void destroy_all(void){
    write_log("SIMULATOR CLOSING");

    //SHARED MEMORY
    shmdt(shared_memory); //detach
    shmctl(shmid,IPC_RMID,NULL); //destroy
    
    //SEMAPHOREs TODO: 
    sem_close(sem_log);	//destroy the semaphore
	sem_unlink("SEM_LOG");

    //pthread_mutex_destroy(&mutex);
    //pthread_cond_destroy(&cond);


    //TODO: Não esquecer de chamar no final o kill_ipcs.sh dado pelo prof!


    //close log file
    fclose(log_fp);

    #ifdef DEBUG
    printf("[DEBUG] sucessfuly destroyed everything!\n");
    #endif
    
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
        destroy_all(); //end
    }

    update_curr_time();
    fprintf(stdout,"%s %s\n",curr_time,log); //write to stdout
    fprintf(log_fp,"%s %s\n",curr_time,log); //write to log file
    fflush(log_fp);

    if(sem_post(sem_log)==-1){
        destroy_all(); //end
    } 
}

void init_log(void){
    //create/reset log file 
    if((log_fp=fopen(LOG_FILENAME,"w"))==NULL){
        fprintf(stderr,"Error: creating %s file\n",LOG_FILENAME);
        exit(-1);
    }
}

void read_config(void){
    FILE *fp;
    char *aux; 
    size_t size=1024;
    int len;

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
    if((len=getline(&aux,&size,fp))==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.time_unit))!=1){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //2nd line
    if((len=getline(&aux,&size,fp))==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d, %d",&(config.track_len),&(config.laps_qnt))!=2){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //3rd line
    if((len=getline(&aux,&size,fp))==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
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
    if((len=getline(&aux,&size,fp))==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.max_car_qnt_per_team))!=1){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //5th line
    if((len=getline(&aux,&size,fp))==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.avaria_time_interval))!=1){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //6th line
    if((len=getline(&aux,&size,fp))==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d, %d",&(config.reparacao_min_time),&(config.reparacao_max_time))!=2){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    //last line
    if((len=getline(&aux,&size,fp))==-1){
        fprintf(stderr,"Error: reading line from %s file\n",CONFIG_FILENAME);
        fclose(fp);
        free(aux);
        exit(-1);
    }
    aux[len]='\0';
    if(sscanf(aux,"%d",&(config.fuel_capacity))!=1){
        fprintf(stderr,"Error: anomality in %s file structure\n",CONFIG_FILENAME);
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

