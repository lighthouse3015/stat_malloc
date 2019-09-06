/*******************************************************************************
 * Filename: stat_server.h
 *
 * Purpose: header file for client-server msgQ.
 *
 * By: Keith Hendley
 * Date: 9/3/19
 *
 ******************************************************************************/

#ifndef STAT_SERVER_H_INCLUDED
#define STAT_SERVER_H_INCLUDED

#include <sys/ipc.h> // msgQ/shared memory
#include <sys/shm.h> // shared memory
#include <sys/msg.h> // msgQ


#define MSG_KEY_STRING			"verkada_msg"
#define	MSG_KEY_INT	   			2019

#define SHM_KEY_STRING			"verkada_shm"
#define	SHM_KEY_INT	   			2019

#define MSG_TYPE_VERKADA		1
#define MSG_PERMISSIONS			(0666)

#define SHM_SIZE				64
#define SHM_PERMISSIONS			(0666)


typedef struct {
	void 	*ptr;
	size_t 	size;
} msg_data_t;

// message from shared_client to stat_server
typedef struct {
	long		type;
	msg_data_t	msg_data;
} msg_t;


#endif // STAT_SERVER_H_INCLUDE
