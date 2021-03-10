#include "main.h"

#define DEBUG 

int main(void){
    //IMPORT CONFIG
    read_config();

    //TODO: create/clear log file





    //SHARED MEMORY 
    //Create
	if ((shmid=shmget(IPC_PRIVATE,sizeof(int),IPC_CREAT|0700)) < 0){
		sprintf(stderr,"Error: in shmget with IPC_CREAT\n");
		exit(-1);
	} 
    //Attach
    if((shared_memory=(mem_struct*)shmat(shmid,NULL,0))==(mem_struct*)-1){
        sprintf(stderr,"Error: in shmat\n");
        exit(-1);
    }




    return 0;
}

void destroy_all(void){
    //SHARED MEMORY
    shmdt(shared_memory); //detach
    shmctl(shmid,IPC_RMID,NULL); //destroy
    //SEMAPHOREs
	//sem_close(mutex);	//destroy the semaphore
	//sem_unlink("MUTEX");


    //TODO: Não esquecer de chamar no final o kill_ipcs.sh dado pelo prof!
}

void update_curr_time(void){
    //call this function to update time on curr_time 
    time_t t;
    struct tm *curr_time_struct;
    t=time(NULL); curr_time_struct=localtime(&t);

    strftime(curr_time,9,"%H:%M:%S",curr_time_struct);
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

