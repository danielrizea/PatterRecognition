#include <stdio.h>
//#include <profile.h>
#include <string.h>
#include <spu_mfcio.h>
#include <spu_intrinsics.h>
#include <malloc.h>
#include "math.h"
#include "../utils.h"
#include "libsync.h"


#define waitag(t) mfc_write_tag_mask(1<<t); mfc_read_tag_status_all();



unsigned int data  __attribute__((aligned(128)));
unsigned int addrIn  __attribute__((aligned(128)));
unsigned int addrStruct  __attribute__((aligned(128)));
unsigned int addrOut __attribute__((aligned(128)));

int temp;
int hist_vals[256] __attribute__((aligned(128)));

int values1[8] __attribute__((aligned(128)));
int values2[4] __attribute__((aligned(128)));

int result __attribute__((aligned(128)));

process_data receive_data __attribute__((aligned(16)));

unsigned char *pixels;

unsigned char **stari_pixeli;


unsigned int buffer_addr[256] __attribute__((aligned(128)));
int result_arrived __attribute__ ((aligned(128)));

unsigned int tag = 0;
unsigned int mask = 1;


/* Functie de calculeaza numarul de aparitii valorilot unor pixeli pe o fasie
 *
 */
void h_no(unsigned long long cellId) {

    int i;

    //alocare spatiu
    pixels = (unsigned char *)memalign(128,sizeof(unsigned char)*receive_data.size);

    addrIn = receive_data.in_data;

    int contor = 0;

    int rec_size;

    //aducere fasii din memorie
    while (contor< receive_data.size) {


        rec_size = MIN(ELEMENTS_TRANSFER,receive_data.size-contor);


        mfc_getb(&pixels[contor],addrIn+contor,sizeof(unsigned char)*rec_size,tag ,0,0);
        waitag(tag);

        contor += rec_size;
    }


    for (i=0; i<256; i++)
        buffer_addr[i] = 0;

    int b =0;
    for (i=0;i<receive_data.size;i++) {
        if (pixels[i]==0)
            b++;
    }

    // printf("Cell %lld are %d \n ",cellId,b);
    int a = buffer_addr[0] ;
    for (i=0;i<receive_data.size;i++) {
        buffer_addr[pixels[i]]++;

    }

    mfc_put(buffer_addr,(unsigned int)receive_data.out_data,sizeof(unsigned int)*256,tag ,0,0);
    waitag(tag);


    free(pixels);

}
/*  Functie de egalizeaza o fasie
 *  Face rost prin DMA de vectorul hist_equal si variabila temp
 */
void h_eq(unsigned long long cellId) {

    int i;

    pixels = (unsigned char *)memalign(128,sizeof(unsigned char)*receive_data.size);


    // obtinere vector hist_equal
    mfc_get(hist_vals,(unsigned int)receive_data.out_data,sizeof(int)*256,tag,0,0);
    waitag(tag)

    //obtinere fasie
    mfc_get(pixels,(unsigned int)receive_data.in_data,sizeof(unsigned char)*receive_data.size,tag,0,0);
    waitag(tag);

    //variabila temp este obtinuta din structura transmisa prin DMA
    temp = receive_data.temp;

    //calcul egalizare
    unsigned char a;
    for (i=0; i<receive_data.size; i++) {
        a= pixels[i];

        pixels[i] = (hist_vals[pixels[i]] - hist_vals[0])/(float) (temp) * 255;
        //printf(" valoare veche %d diferenta %d - %d val:%d temp %d  valoare noua %d\n",a,hist_vals[pixels[i]],hist_vals[0],hist_vals[pixels[i]] - hist_vals[0],temp,pixels[i]);

    }

    //punere a rezultatelor prin DMA
    mfc_put(pixels,(unsigned int)receive_data.in_data,sizeof(unsigned char)*receive_data.size,tag,0,0);
    waitag(tag);

    //clean up
    free(pixels);


}
/*Functia calculeaza averge si stddev pentru o stare
 * primeste pixelii fiecarei stari prin DMA si intoarce rezultatul prin DMA
 */

void hmm_model(unsigned long long cellId) {

    int i,j;
    int nr_stari,dimensiune_stare;

    unsigned char *average;
    unsigned char *stddev;

    unsigned int pix_sum;
    float temp_avg, temp_stddev, temp1;

    nr_stari = receive_data.size;
    dimensiune_stare = receive_data.temp;

    // printf("stari %d dimensiune stare %d \n",nr_stari,dimensiune_stare);

    //alocare memorie
    stari_pixeli = (unsigned char**)memalign(128,nr_stari*sizeof(unsigned char *));

    for (i=0;i<nr_stari;i++) {

        stari_pixeli[i] = (unsigned char*)memalign(128,dimensiune_stare * sizeof(unsigned char));

    }

    average = (unsigned char*)memalign(128,dimensiune_stare * sizeof(unsigned char));

    stddev = (unsigned char*)memalign(128,dimensiune_stare * sizeof(unsigned char));


    //printf("Urmzeaza mapari stari \n");


    //primeste vector in care sunt adresele celor 8 fasii (8 imagini pentru un model)
    while (spu_stat_in_mbox()==0);
    addrIn = spu_read_in_mbox();


    mfc_getf(buffer_addr, addrIn,256*sizeof(unsigned int),tag,0,0);
    waitag(tag);


    for (i=0;i<nr_stari;i++) {


        mfc_getf(stari_pixeli[i], buffer_addr[i],dimensiune_stare*sizeof(unsigned char),tag,0,0);
        waitag(tag);

    }


    for (i = 0; i < dimensiune_stare; i++) {
        //printf(" calcul %d pe cell %lld \n",i,cellId);
        temp_avg = 0;
        for (j = 0; j < nr_stari; j++) {
            temp_avg += (float) stari_pixeli[j][i];
        }
        temp_avg /= nr_stari;
        average[i] = (unsigned char) temp_avg;

    }

    /*compute the standard deviation values*/
    temp1= 0 ;
    for (i = 0; i < dimensiune_stare; i++) {

        temp_stddev = 0;
        for (j = 0; j < nr_stari; j++) {
            temp1 = (float) (stari_pixeli[j][i] - average[i]);
            temp1 *= temp1;
            temp_stddev += temp1;
        }
        temp_stddev /= nr_stari;
        temp_stddev = sqrt(temp_stddev);
        stddev[i] = (unsigned char) temp_stddev;
    }

    mfc_put(average,receive_data.temp2,sizeof(unsigned char)*dimensiune_stare,tag,0,0);
    waitag(tag);


    mfc_put(stddev,receive_data.temp3,sizeof(unsigned char)*dimensiune_stare,tag,0,0);
    waitag(tag);


    free(average);

    free(stddev);


    for (i=0;i<nr_stari;i++) {

        free(stari_pixeli[i]);
    }

    free(stari_pixeli);

    // printf("termina %lld \n",cellId);
}
/* Functia determina daca o fasie apartine unui anumit model
 * rezultatul este returnat prin DMA
 */
int states_are_equal(unsigned long long cellId) {

    unsigned char *average;
    unsigned char *stddev;
    unsigned char *img_pixels;
    int i;
    int dimensiune_stare;

    int count = 0;

    dimensiune_stare = receive_data.size;

    average = (unsigned char*)memalign(128,sizeof(unsigned char) * dimensiune_stare);

    stddev = (unsigned char*)memalign(128,sizeof(unsigned char) * dimensiune_stare);

    img_pixels = (unsigned char*)memalign(128,sizeof(unsigned char) * dimensiune_stare);

    // printf("Dimensiune stare %d %d %d\n",dimensiune_stare,receive_data.status,receive_data.temp3);


    mfc_getf(average,receive_data.temp2,sizeof(unsigned char)*dimensiune_stare,tag,0,0);
    waitag(tag);
    //printf("here   1\n ");

    mfc_getf(stddev,receive_data.temp3,sizeof(unsigned char)*dimensiune_stare,tag,0,0);
    waitag(tag);
    // printf("here   2\n ");

    mfc_getf(&img_pixels[0],(unsigned int)receive_data.in_data,sizeof(unsigned char)*dimensiune_stare,tag,0,0);
    waitag(tag);

    mfc_getf(buffer_addr,(unsigned int)receive_data.out_data,256*sizeof(unsigned int),tag,0,0);
    waitag(tag);
    // printf("here   3\n ");


    int m1;
    int sd1;
    int m2;

    for (i=0;i<dimensiune_stare;i++) {
        m1 = (int) average[i];
        sd1 = (int) stddev[i];
        m2 = img_pixels[i];

        // printf(" %d %d %d\n",average[i],stddev[i],img_pixels[i]);
        if ((m1 - sd1 < m2) && (m2 < m1 + sd1))
            count++;
    }

    float state_threshold =
        ((receive_data.status == TIGER) ? TEST_TIGER_STATE_THRESHOLD : TEST_ELEPH_STATE_THRESHOLD);

    if (count / (float) (dimensiune_stare) > state_threshold) {
        buffer_addr[0]++;
        //  printf(" cell %lld starea seamana\n",cellId);

    }


    //punere rezultat
    mfc_putf(buffer_addr,(unsigned int)receive_data.out_data,256*sizeof(unsigned int),tag,0,0);
    waitag(tag);

    //   printf("Rezultat pus\n");

    //clean up

    free(average);
    free(stddev);
    free(img_pixels);

}
//speid=IDul acestui SPU;
//argv=adresa de inceput a structurii care contine datele de size+adrese ale datelor ce vor veni
//		poate fi doar o simpla adresa de unde se citesc anumite date;
//bufSize=un numar; folosit pt a da dimensiunea datelor de la adresa argv



int main(unsigned long long speid, unsigned long long argv, unsigned long long cellId)
{

    tag = mfc_tag_reserve();

    if (tag ==MFC_TAG_INVALID) {

        printf("SPU: ERROR can't allocate tag ID\n");
        return -1;

    }

//printf("SPU %d tag %d\n",(int)cellId,tag);

    //vector int* v1 = (vector int*) &values1[0];
    //vector int* v2 = (vector int*) &values1[4];


    while (1) {

        data = (int)cellId;

        //semnalizare PPU ca vrea task, trimite cellId
        while (spu_stat_out_intr_mbox()==0);
        spu_write_out_intr_mbox(data);



        //primeste de la PPU adresa unei structuri din care realizeaza ce procesari sa faca
        while (spu_stat_in_mbox()==0);
        addrStruct = spu_read_in_mbox();
        //printf("Cell %lld#am primit#%d\n",cellId, addrIn);

        //daca adresa este 0, atunci semnal de exit
        if (addrStruct == 0)
            break;


        //obtinere a structurii prin transfer DMA
        mfc_get(&receive_data,addrStruct ,sizeof(process_data),tag,0,0);
        waitag(tag);


        //decizie procesare in functie de tipul operatiei
        switch (receive_data.op_type) {

        case HISTOGRAM_NO :
            h_no(cellId);
            break;

        case HISTOGRAM_EQ :
            h_eq(cellId);
            break;

        case HMM_MODEL :
            hmm_model(cellId);
            break;

        case STATES_EQUAL :
            states_are_equal(cellId);
            break;

        case SPU_WAIT :
            break;


        }


    }

    mfc_tag_release(tag);

    return 0;
}
