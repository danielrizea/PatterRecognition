/* 
 * File:   utils.h
 * Author: daniel
 *
 * Created on May 2, 2011, 7:58 PM
 */

#ifndef _UTILS_H
#define	_UTILS_H

#ifdef	__cplusplus
extern "C" {
#endif

#define HISTOGRAM_EQ 2
#define HMM_MODEL 3
#define STATES_MERGEABLE 4
#define HISTOGRAM_NO 1
#define SPU_WAIT 5
#define STATES_EQUAL 6

#define ELEMENTS_TRANSFER 16000


#define TIGER_STATE_THRESHOLD 0.80
#define ELEPH_STATE_THRESHOLD 0.73
#define TIGER_PERC 0.17
#define ELEPH_PERC 0.17


#define TEST_TIGER_STATE_THRESHOLD 0.37
#define TEST_ELEPH_STATE_THRESHOLD 0.37

#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define TRUE 1
#define FALSE 2

#define TIGER 0
#define ELEPHANT 1

#define NO_SPU 8


//structura de pasare a tipului de task catre SPU
typedef struct {

        int *in_data;
	int *out_data;
	int status;
	int size;
        int op_type;
        int temp;
        unsigned int temp2;
        unsigned int temp3;


} process_data ;




#ifdef	__cplusplus
}
#endif

#endif	/* _UTILS_H */

