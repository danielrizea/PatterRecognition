#include <stdio.h>
#include <stdlib.h>
#include <libspe2.h>
#include <pthread.h>
#include <string.h>
#include <ppu_intrinsics.h>
#include <imglib.h>
#include <malloc.h>
#include <time.h>
#include "utils.h"


#define PASSIVE 0
#define ACTIVE 1
#define EXITING 2



extern spe_program_handle_t simple_spu;

typedef struct
{
    spe_context_ptr_t spe;
    spe_event_unit_t pevents;
    int args;
} thread_arg_t;

spe_context_ptr_t spes[NO_SPU];
thread_arg_t arg[NO_SPU];


//L * W
typedef struct {
    int start_height;
    int stop_height;
    int aux2;

    unsigned char *average;
    unsigned char *stddev;

    unsigned char is_deleted __attribute__((aligned(128)));//used for state merging
    int size ;

    unsigned char aux_char;


}state __attribute__((aligned(16)));



void * run_spu(void *thread_arg)
{

    thread_arg_t *arg = (thread_arg_t *) thread_arg;
    unsigned int entry;
    entry = SPE_DEFAULT_ENTRY;
    spe_stop_info_t stop_info;


    if ( spe_context_run(arg->spe, &entry, 0, NULL,(int*)arg->args,&stop_info)<0)
        perror("spe_context_run");

    return NULL;
}

float values[100] __attribute__((aligned(128)));
float values2[100];
unsigned int adr_values[10] __attribute__((aligned(128)));

volatile process_data send_data[NO_SPU] __attribute__((aligned(16)));

process_data *aux;

unsigned int inAddr __attribute__((aligned(128)));;
unsigned int outAddr __attribute__((aligned(128)));;
unsigned int out __attribute__((aligned(128)));;

unsigned char aux1 __attribute__((aligned(128)));

int numar_stari_egale[NO_SPU]  __attribute__((aligned(128)));

//define max no files training 256

unsigned int buffer_addr[NO_SPU][256] __attribute__((aligned(128)));
//definire vector imagini

unsigned char spu_status[NO_SPU] = {0};
//int results_arrived[NO_SPU] __attribute__ ((aligned(128)));

char traininguri_nume[2][30];
char test_nume[20];

int nr_imagini;

int nr_traininguri = 2;

image **traininguri;
image *test;



//stari
int no_states;

state *stari[2];

int nr_stari;
//dimensiune valori
int l;
int p;
int numar_imagini_test;

int hmm_tiger_states;
int hmm_elephant_states;

/*Functia realizeaza citirea datelor
 *imaginile trebuie sa se gaseasca in folderul images
 */
void citire_date(char **argv) {

    int i;
    int j;
    char file_name[100];
    FILE * pf;

    strcpy(traininguri_nume[0],argv[1]);
    strcpy(traininguri_nume[1],argv[2]);

    nr_imagini = atoi(argv[3]);

    sscanf(argv[4],"-l%d",&l);
    sscanf(argv[5],"-p%d",&p);

    strcpy(test_nume,argv[7]);
    numar_imagini_test = atoi(argv[8]);

    printf(" l%d p%d c%d \n",l,p,numar_imagini_test);

    traininguri = (image**)calloc(nr_traininguri,sizeof(image*));


    traininguri[0] = (image*)calloc(nr_imagini,sizeof(image));
    traininguri[1] = (image*)calloc(nr_imagini,sizeof(image));
    test = (image*)calloc(numar_imagini_test,sizeof(image));


    //imaginile trebuie sa se afle in images/

    //incarcare imagini training
    for (j=0; j<nr_traininguri; j++)
        for (i=0; i<nr_imagini; i++) {
            sprintf(file_name,"images/%s/%s_%d.pgm",traininguri_nume[j],traininguri_nume[j],i+1);
            //  printf("open %s\n",file_name);
            pf = fopen(file_name,"r");

            traininguri[j][i] = read_ppm(pf);
            fclose(pf);
        }

    //incarcare imagini teste
    for (i=0;i<numar_imagini_test; i++) {
        sprintf(file_name,"images/%s/%s_%d.pgm",test_nume,test_nume,i+1);
        // printf("open test %s\n",file_name);
        pf = fopen(file_name,"r");

        test[i] = read_ppm(pf);

        fclose(pf);
    }

}


int hist_vals[256] __attribute__ ((aligned(128)));
unsigned int temp __attribute__ ((aligned(128)));
float sum __attribute__ ((aligned(128)));
unsigned int data __attribute__ ((aligned(128)));

unsigned int op_type   __attribute__ ((aligned(128)));



void SPU_wait(spe_event_unit_t *pevents,spe_event_unit_t event_received,
              spe_event_handler_ptr_t event_handler) {


    int i=0;
    int nevents;
    while (i< NO_SPU) {

        nevents = spe_event_wait(event_handler,&event_received,1,-1);

        if (nevents<0)
        {
            printf("SPU:Error la spe_event_wait(res=%d)\n",nevents);
        }

        else
        {
            //printf("PPE: spe_event_wait exited OK; cod=%d \n",nevents);
            //The spe_stop_info_read loop should check for SPE_EVENT_SPE_STOPPED event received in the events mask
            if (event_received.events & SPE_EVENT_OUT_INTR_MBOX)
            {

                while (spe_out_intr_mbox_status(event_received.spe) < 1);
                spe_out_intr_mbox_read(event_received.spe, &data, 1, SPE_MBOX_ANY_NONBLOCKING);// in cazul in care nu se lucreaza cu eventuri se va folosi flagul SPE_MBOX_ANY_BLOCKING
                //printf("PPE: data+exit id_spu=%d data=%d\n", i, data);

                i++;


            }
            else if (event_received.events & SPE_EVENT_SPE_STOPPED)
            {
                printf("PPE: spu %d finished and exited\n",i);
            }
            else
            {
                printf("PPE:Caz nefericit avem alt exit status din SPU; spe=%d.\n",i);
            }
        }

    }


    out = 0;
    for (i=0; i<NO_SPU; ++i)
    {

        send_data[i].op_type = SPU_WAIT;
        out = (unsigned int)&send_data[i];
        if ( spe_in_mbox_write(spes[i], &out, 1, SPE_MBOX_ANY_NONBLOCKING) <0)
        {
            perror("ERROR, writing messages to spe failed\n");
        }


    }

}

/*  Functia semnaleaza SPU-urile sa iasa din bucla
 */
void exit_SPU(spe_event_unit_t *pevents,spe_event_unit_t event_received,
              spe_event_handler_ptr_t event_handler) {


    int no = 0;
    int i=0,nevents;

    while (no < NO_SPU) {

        nevents = spe_event_wait(event_handler,&event_received,1,-1);

        if (nevents<0)
        {
            printf("SPU:Error la spe_event_wait(res=%d)\n",nevents);
        }

        else
        {
            
            if (event_received.events & SPE_EVENT_OUT_INTR_MBOX)
            {

                while (spe_out_intr_mbox_status(event_received.spe) < 1);
                spe_out_intr_mbox_read(event_received.spe, &data, 1, SPE_MBOX_ANY_BLOCKING);// in cazul in care nu se lucreaza cu eventuri se va folosi flagul SPE_MBOX_ANY_BLOCKING

                //send exit
                outAddr = 0;

                no++;

                if ( spe_in_mbox_write(event_received.spe, &outAddr, 1, SPE_MBOX_ANY_NONBLOCKING) <0)
                {
                    perror("ERROR, writing messages to spe failed\n");
                }

            }
            else if (event_received.events & SPE_EVENT_SPE_STOPPED)
            {
                printf("PPE: spu %d finished and exited\n",i);
            }
            else
            {
                printf("PPE:Caz nefericit avem alt exit status din SPU; spe=%d.\n",i);
            }
        }

    }

}
/*Functia realizeaza egalizarea unei poze
 *
 */
void obtinere_poze_histogram_eq(spe_event_unit_t *pevents,spe_event_unit_t event_received,
                                spe_event_handler_ptr_t event_handler,image img) {

    int i,j,k,nevents;

    int chunck_size=0;
    int nr_pixeli;
    float sum=0;
    int contor;

    sum = 0.0;

    nr_pixeli = img->height * img->width;

    for (j=0;j<256;j++)
        hist_vals[j] = 0;

    // calcul vector hist_vals pe SPE-uri
    i=0;
    chunck_size = nr_pixeli/NO_SPU;
    contor = 0;

    k= 0;

    //calculul distibuit al vectorului hist_vals
    //fiecare SPU primeste o fasie de 80000 pixeli (8 SPU-uri si 800 * 800 poza)

    for (i=0; i<NO_SPU; ++i)
    {

        nevents = spe_event_wait(event_handler,&event_received,1,-1);

        if (nevents<0)
        {
            printf("SPU:Error la spe_event_wait(res=%d)\n",nevents);
        }

        else
        {
            
            if (event_received.events & SPE_EVENT_OUT_INTR_MBOX)
            {

                while (spe_out_intr_mbox_status(event_received.spe) < 1);
                spe_out_intr_mbox_read(event_received.spe, &data, 1, SPE_MBOX_ANY_NONBLOCKING);// in cazul in care nu se lucreaza cu eventuri se va folosi flagul SPE_MBOX_ANY_BLOCKING
                
                send_data[(int)data].size = chunck_size;
                send_data[(int)data].temp = ELEMENTS_TRANSFER;
                send_data[(int)data].op_type = HISTOGRAM_NO;
                send_data[(int)data].temp2 = 0;
                send_data[(int)data].in_data =(void *) (&img->buf[contor]);

                send_data[(int)data].out_data = (void *)(&buffer_addr[(int)data][0]);

                outAddr = (unsigned int)&send_data[(int)data];

                //   printf("Trimitere date la %d %d %d %d\n",(int)data, send_data[(int)data].out_data,contor,send_data[(int)data].op_type);
                if ( spe_in_mbox_write(event_received.spe, &outAddr, 1, SPE_MBOX_ANY_BLOCKING) <0)
                {
                    perror("ERROR, writing messages to spe failed\n");


                }

            }
            else if (event_received.events & SPE_EVENT_SPE_STOPPED)
            {
                printf("PPE: spu %d finished and exited\n",i);
            }
            else
            {
                printf("PPE:Caz nefericit avem alt exit status din SPU; spe=%d.\n",i);
            }
        }
        contor += chunck_size;

    }

    //asteapta SPU-uri
    SPU_wait(pevents,event_received,event_handler);


    //combinarea informatiei de la fiecare SPU pentru a obtine vectorul hist_vals
    for (j=0;j<256;j++)
        for (i=0;i<NO_SPU;i++) {

            hist_vals[j] += buffer_addr[i][j];

        }

    sum = 0.0;
    for (k=0; k<256; k++) {

        sum += hist_vals[k];
        hist_vals[k] = sum;

    }
    temp = nr_pixeli - hist_vals[0];

    chunck_size = ELEMENTS_TRANSFER;

    contor = 0;

    //egalizarea imaginei

    while (contor < nr_pixeli) {


        // printf("Contor %d %d chunck %d %d \n",contor,size,send_data.size,&img->buf[contor]-img->buf);


        nevents = spe_event_wait(event_handler,&event_received,1,-1);

        if (nevents<0)
        {
            printf("SPU:Error la spe_event_wait(res=%d)\n",nevents);
        }

        else
        {
            //printf("PPE: spe_event_wait exited OK; cod=%d\n",nevents);
            //The spe_stop_info_read loop should check for SPE_EVENT_SPE_STOPPED event received in the events mask
            if (event_received.events & SPE_EVENT_OUT_INTR_MBOX)
            {

                while (spe_out_intr_mbox_status(event_received.spe) < 1);
                spe_out_intr_mbox_read(event_received.spe, &data, 1, SPE_MBOX_ANY_NONBLOCKING);// in cazul in care nu se lucreaza cu eventuri se va folosi flagul SPE_MBOX_ANY_BLOCKING
                //printf("PPE: data+exit id_spu=%d data=%d\n", i, data);


                send_data[(int)data].op_type = HISTOGRAM_EQ;

                send_data[(int)data].out_data = (void *)(&hist_vals[0]);
                send_data[(int)data].temp = temp;
                send_data[(int)data].in_data =(void *) (&img->buf[contor]);


                if (contor + chunck_size > nr_pixeli) {
                    // printf(" Exit \n");
                    send_data[(int)data].size = nr_pixeli - contor;
                    break;
                }
                else

                    send_data[(int)data].size = chunck_size;

                outAddr = (unsigned int)&send_data[(int)data];

                if ( spe_in_mbox_write(event_received.spe, &outAddr, 1, SPE_MBOX_ANY_NONBLOCKING) <0)
                {
                    perror("ERROR, writing messages to spe failed\n");
                }

            }
            else if (event_received.events & SPE_EVENT_SPE_STOPPED)
            {
                printf("PPE: spu %d finished and exited\n",i);
            }
            else
            {
                printf("PPE:Caz nefericit avem alt exit status din SPU; spe=%d.\n",i);
            }
        }


        contor += chunck_size;

    }

    //wait SPUs
    SPU_wait(pevents,event_received,event_handler);

}
/* Realizeaza egalizarea pozelor pentru fiecare poza
 *
 */
void h_eq(spe_event_unit_t *pevents,spe_event_unit_t event_received,
          spe_event_handler_ptr_t event_handler) {
    int i;
    int j;
    /*

        FILE * fout;

        fout = fopen("imagine_init1.pgm","w");

        write_ppm(fout,test[0]);

        fclose(fout);
    */

    for (j=0;j<nr_traininguri;j++)
        for (i=0;i<nr_imagini;i++)
            obtinere_poze_histogram_eq(pevents,event_received,event_handler,traininguri[j][i]);



    for (i=0;i<numar_imagini_test;i++)
        obtinere_poze_histogram_eq(pevents,event_received,event_handler,test[i]);


    /*
        FILE *fout;
        char file_name[30];

        for(i=0;i<numar_imagini_test;i++){

        sprintf(file_name,"imagine_test_%d.pgm",i);
        fout = fopen(file_name,"w");

        write_ppm(fout,test[i]);

        fclose(fout);
        }
    */

}
/* Determina daca doua stari pot fi unite
 *
 */
int states_are_mergeable(state state1,state state2,
                         float thresho,int dimensiune) {
    /*determine if 2 states can be merged*/
    int common_pixels1 = 0, common_pixels2 = 0, i, common_pixels;
    int m1, m2, sd1, sd2;
    for (i = 0; i < dimensiune; i++) {

        m1 = (int) state1.average[i];
        m2 = (int) state2.average[i];
        sd1 = (int) state1.stddev[i];
        sd2 = (int) state2.stddev[i];
        if ((m1 - sd1 < m2) && (m2 < m1 + sd1))
            common_pixels1++;
        if ((m2 - sd2 < m1) && (m1 < m2 + sd2))
            common_pixels2++;

    }
    common_pixels = (common_pixels1 + common_pixels2) / 2;

    if (common_pixels / (float) (dimensiune) < thresho)
        return 0;

    if (common_pixels1 > common_pixels2)
        return 1;

    return 2;
}

/* Creeaza modelul hmm
 */
void build_hmm(spe_event_unit_t *pevents,spe_event_unit_t event_received,
               spe_event_handler_ptr_t event_handler,int type) {

    int nevents;
    int i;
    int j;
    int H;
    int W;
    int aux;
    float state_threshold =
        ((type == TIGER) ? TIGER_STATE_THRESHOLD : ELEPH_STATE_THRESHOLD);


    // printf(" %f stress shold\n",state_threshold);

    nr_stari = (test[0]->height/p); // pana la numar stari
    H = test[0]->height;
    W = test[0]->width;

    stari[type] = (state *)memalign(128,nr_stari * sizeof(state));

    j = 0;

    // printf("Dimensiune alocata %d\n",W*l*sizeof(unsigned char));

    for (i = 0; i < H; i += l) {
        stari[type][j].start_height = i;
        stari[type][j].stop_height = MIN(i + l, H);

        stari[type][j].average = (unsigned char*)memalign(128,W*l*sizeof(unsigned char));
        stari[type][j].stddev = (unsigned char*)memalign(128,W*l*sizeof(unsigned char));
        stari[type][j].is_deleted = 0;
        //printf("Alocare  %d %d\n",stari[type][j].average,stari[type][j].stddev);
        j++;
        i -= p; /*overlap*/
    }



    int stare_curenta = 0;
    int stare_index_start = 0;

    int k = 0;


    //calcul distribuit al average si stddev pentru fiecare stare
    while (stare_curenta < nr_stari) {

        // printf("Depunere rezultate in %d %d\n",send_data.temp2,stari[type][0].average);

        nevents = spe_event_wait(event_handler,&event_received,1,-1);

        if (nevents<0)
        {
            printf("SPU:Error la spe_event_wait(res=%d)\n",nevents);
        }

        else
        {
            //printf("PPE: spe_event_wait exited OK; cod=%d\n",nevents);
            //The spe_stop_info_read loop should check for SPE_EVENT_SPE_STOPPED event received in the events mask
            if (event_received.events & SPE_EVENT_OUT_INTR_MBOX)
            {

                while (spe_out_intr_mbox_status(event_received.spe) < 1);
                spe_out_intr_mbox_read(event_received.spe, &data, 1, SPE_MBOX_ANY_NONBLOCKING);// in cazul in care nu se lucreaza cu eventuri se va folosi flagul SPE_MBOX_ANY_BLOCKING
                //printf("PPE: data+exit id_spu=%d data=%d\n", i, data);

                send_data[(int)data].op_type = HMM_MODEL;

                // se transmite numarul de imagini
                send_data[(int)data].size = nr_imagini;

                send_data[(int)data].temp = W*l;
                //unde trebuie SPE-urile sa intoarca rezultatul
                send_data[(int)data].temp2 = (unsigned int)&stari[type][stare_curenta].average[0];
                send_data[(int)data].temp3 = (unsigned int)&stari[type][stare_curenta].stddev[0];


                outAddr = (unsigned int)&send_data[(int)data];


                if ( spe_in_mbox_write(event_received.spe, &outAddr, 1, SPE_MBOX_ANY_NONBLOCKING) <0)
                {
                    perror("ERROR, writing messages to spe failed\n");
                }

                image img;
                for (k=0; k<nr_imagini; k++) {
                    img = traininguri[type][k];
                    buffer_addr[(int)data][k] = (unsigned int)&img->buf[stare_index_start];

                }
                //  printf(" prima adresa %d %d %d %d\n",buffer_addr[(int)data][0],buffer_addr[(int)data][1],buffer_addr[(int)data][2],buffer_addr[(int)data][3]);
                outAddr = (unsigned int)&buffer_addr[(int)data][0];

                //  printf("Se trimite pentru mapare %d %d\n",outAddr,stare_index_start);
                //  printf("Se trimite pentru rezultat %d %d \n",send_data[(int)data].temp2, send_data[(int)data].temp3);
                if ( spe_in_mbox_write(event_received.spe, &outAddr, 1, SPE_MBOX_ANY_NONBLOCKING) <0)
                {
                    perror("ERROR, writing messages to spe failed\n");
                }


            }
            else if (event_received.events & SPE_EVENT_SPE_STOPPED)
            {
                printf("PPE: spu %d finished and exited\n",i);
            }
            else
            {
                printf("PPE:Caz nefericit avem alt exit status din SPU; spe=%d.\n",i);
            }
        }

        stare_curenta ++;
        stare_index_start += p * W;


    }

    SPU_wait(pevents,event_received,event_handler);

    //merge stari
  i=0;
  j=1;


    while ((i < nr_stari) && (j < nr_stari)) {
        aux = states_are_mergeable(stari[type][i] ,stari[type][j],
                                   state_threshold,l*W);

        if (aux == 0) { /*not mergeable*/
            i=j;
            j++;
            continue;
        }
        if (aux == 1) { /*mergeable, keep i*/
            stari[type][j].is_deleted = 1;
            j++;
            continue;
        }
        if (aux == 2) { /*mergeable, keep j*/
            stari[type][i].is_deleted = 1;
            i = j;
            j = i + 1;
            continue;
        }

    }



    printf("HMM states: ");
    j = 0;
    for (i = 0; i < nr_stari; i++) {
        if (stari[type][i].is_deleted!=1) {
            printf("[%d %d] ", stari[type][i].start_height, stari[type][i].stop_height);
            j++;
        }
    }
    printf("\nChose %d states out of a total of %d states\n", j, nr_stari);

    if (type == TIGER)
        hmm_tiger_states = j;
    else
        hmm_elephant_states = j;


}

/* Fucntia primeste o imagine si un model (tiger sau elephant) si intoarce cate stari
 * similare au
 */
int determina_numar_stari_asemanatoare(spe_event_unit_t *pevents,spe_event_unit_t event_received,
                                       spe_event_handler_ptr_t event_handler,image img,int model_type) {



    int i;
    int j;
    int H;
    int W;
    int nevents;

    int stare_curenta;

    for (i=0;i<NO_SPU;i++)
        buffer_addr[i][0]=0;


    stare_curenta = 0;

    H = img->height;

    W = img->width;


    while (stare_curenta < nr_stari) {

        // printf("Depunere rezultate in %d %d\n",send_data.temp2,stari[type][0].average);


        while (stari[model_type][stare_curenta].is_deleted == 1 && stare_curenta< nr_stari)
            stare_curenta++;


        if (stare_curenta >= nr_stari)
            break;

        nevents = spe_event_wait(event_handler,&event_received,1,-1);

        if (nevents<0)
        {
            printf("SPU:Error la spe_event_wait(res=%d)\n",nevents);
        }

        else
        {
            //printf("PPE: spe_event_wait exited OK; cod=%d\n",nevents);
            //The spe_stop_info_read loop should check for SPE_EVENT_SPE_STOPPED event received in the events mask
            if (event_received.events & SPE_EVENT_OUT_INTR_MBOX)
            {

                while (spe_out_intr_mbox_status(event_received.spe) < 1);
                spe_out_intr_mbox_read(event_received.spe, &data, 1, SPE_MBOX_ANY_NONBLOCKING);// in cazul in care nu se lucreaza cu eventuri se va folosi flagul SPE_MBOX_ANY_BLOCKING
                //printf("PPE: data+exit id_spu=%d data=%d\n", i, data);



                //states equal
                send_data[(int)data].op_type = 6;

                // se transmite numarul de imagini
                send_data[(int)data].size = W*l;



                send_data[(int)data].in_data = (unsigned int*)&img->buf[stari[model_type][stare_curenta].start_height * W];
                //unde trebuie SPE-urile sa intoarca rezultatul

                send_data[(int)data].temp2 = (unsigned int)&stari[model_type][stare_curenta].average[0];
                send_data[(int)data].temp3 = (unsigned int)&stari[model_type][stare_curenta].stddev[0];

                send_data[(int)data].status = model_type;
                send_data[(int)data].out_data = (unsigned int)&buffer_addr[(int)data][0];

                outAddr = (unsigned int)&send_data[(int)data];
           if ( spe_in_mbox_write(event_received.spe, &outAddr, 1, SPE_MBOX_ANY_NONBLOCKING) <0)
                {
                    perror("ERROR, writing messages to spe failed\n");
                }


            }
            else if (event_received.events & SPE_EVENT_SPE_STOPPED)
            {
                printf("PPE: spu %d finished and exited\n",i);
            }
            else
            {
                printf("PPE:Caz nefericit avem alt exit status din SPU; spe=%d.\n",i);
            }
        }

        stare_curenta ++;
    }

    SPU_wait(pevents,event_received,event_handler);

    int total=0;
    //merge informatie
    for (i=0;i<NO_SPU;i++)
        total += buffer_addr[i][0];

//   printf("Total %d\n",total);

    return total;
}
/* Functie clasifica o poza ca apartinand unuia dintr modele
 */
void clasifica_poze_test(spe_event_unit_t *pevents,spe_event_unit_t event_received,
                         spe_event_handler_ptr_t event_handler) {

    int i;

    int tiger;
    int elephant;
    int is_tiger;
    int is_elephant;

    //  printf("Start analiza poze test\n");

    for (i=0;i<numar_imagini_test; i++) {


        tiger = determina_numar_stari_asemanatoare(pevents,event_received,event_handler,test[i],TIGER);
        elephant = determina_numar_stari_asemanatoare(pevents,event_received,event_handler,test[i],ELEPHANT);

        //    printf("tiger %d elefant %d\n",tiger,elephant);

        is_tiger = ((tiger / (float) hmm_tiger_states) > TIGER_PERC);
        is_elephant = ((elephant / (float) hmm_elephant_states) > ELEPH_PERC);

        if (is_tiger && !is_elephant) {
            printf("Image images/test_%d.pgm classified as Tiger [1]\n",i+1);
        }
        if (is_elephant&& !is_tiger) {
            printf("Image images/test_%d.pgm classified as Elephant [1]\n", i+1);
        }
        if (!is_tiger && !is_elephant) {
            printf("Image images/test_%d.pgm classified as None\n", i+1);
        }
        if (is_tiger && is_elephant) {
            if ((tiger / (float) hmm_tiger_states) >
                    (elephant / (float) hmm_elephant_states)) {
                printf("Image images/test_%d.pgm classified as Tiger [2]\n", i+1);
            } else
                printf("Image images/test_%d.pgm classified as Elephant [2]\n", i+1);
        }
    }


}

int main(int argc, char **argv)
{
    int i,res,j;

    clock_t time_start,time_finish;


    pthread_t thread[NO_SPU];

    spe_event_unit_t pevents[NO_SPU], event_received;
    spe_event_handler_ptr_t event_handler;
    event_handler = spe_event_handler_create();

    //deschidere si obtinere imagini;

    citire_date(argv);

    //  printf("final citire date\n");

    for (i=0; i<NO_SPU; ++i)
    {
        spes[i] = spe_context_create(SPE_EVENTS_ENABLE,NULL);
        if (!spes[i]) {
            perror("spe_context_create");
            exit(1);
        }

        res = spe_program_load(spes[i],&simple_spu);
        if (res)
        {
            perror("spe_program_load");
            exit(1);
        }

        pevents[i].events = SPE_EVENT_OUT_INTR_MBOX;
        pevents[i].spe = spes[i];
        spe_event_handler_register(event_handler, &pevents[i]);

        arg[i].spe = spes[i];
        arg[i].args = i;
        arg[i].pevents = pevents[i];

        res = pthread_create(&thread[i],NULL,run_spu, &arg[i]);
        if (res)
        {
            perror("pthread_create");
            exit(1);
        }
    }


    printf("---------------------Task1 started--------------------------\n");
    time_start = clock();

    h_eq(pevents,event_received,event_handler);
    time_finish = clock();
    printf("---------------------Task1 finish %ld miliseconds--------------------------\n",(time_finish - time_start)/1000);

    printf("---------------------Task2 started--------------------------\n");
    time_start = clock();
    build_hmm(pevents,event_received,event_handler,TIGER);

    build_hmm(pevents,event_received,event_handler,ELEPHANT);
    time_finish = clock();
    printf("---------------------Task2 finish %ld miliseconds--------------------------\n",(time_finish - time_start)/1000);
    printf("---------------------Task3 started--------------------------\n");
    time_start = clock();
    clasifica_poze_test(pevents,event_received,event_handler);
    time_finish = clock();
    printf("---------------------Task3 finish %ld miliseconds--------------------------\n",(time_finish - time_start)/1000);

    //printf("Exit spu sequence\n");

    //signal exit to SPUs
    exit_SPU(pevents,event_received,event_handler);


    //join threads
    for (i=0; i<NO_SPU; ++i)
    {
        pthread_join(thread[i], NULL);
        //printf("PPU: Thread %d joinat\n",i);
    }
    //deregister threads and clean up
    for (i=0; i<NO_SPU; ++i)
    {
        //printf("PPE: Start deregister pevents[%d]\n",i);
        spe_event_handler_deregister(event_handler, &pevents[i]);
        //printf("PPE: Succes deregister pevents[%d]\n",i);
        //printf("PPE: Start destroy context %d\n",i);
        res = spe_context_destroy(spes[i]);
        //printf("PPE: Success destroy context %d\n",i);
        if (res)
        {
            perror("spe_context_destroy");
            exit(1);
        }
    }
    //printf("PPE: Toate threadurile joinate+contexte distruse\n");
    spe_event_handler_destroy(event_handler);

    //Clean up
    //eliberare memorie
    //free la stari;

    for (i=0;i<nr_stari;i++) {

        free(stari[TIGER][i].average);
        free(stari[TIGER][i].stddev);

    }
    free(stari[TIGER]);

    for (i=0;i<nr_stari;i++) {

        free(stari[ELEPHANT][i].average);
        free(stari[ELEPHANT][i].stddev);

    }
    free(stari[ELEPHANT]);


    for (j=0;j<nr_traininguri;j++)
        for (i=0; i<nr_imagini; i++) {
            free_img(traininguri[j][i]);
        }
    return 0;
}
