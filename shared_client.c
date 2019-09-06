/**************************************************************************
 * Filename: shared_client.c
 *
 * Purpose: shared library to replace malloc(), free(), calloc(), and
 *          realloc() with debug versions send messages via msgQ to 
 *          stat_server that periodically prints statistics.
 *
 * By: Keith Hendley
 * Date: 9/3/19
 *
 *************************************************************************/
#include <stdlib.h>
#include <sys/types.h> // fork
#include <unistd.h> // fork
#include <malloc.h> // __malloc_hook, ...
#include <stdatomic.h>
#include "stat_server.h" // messageQ

/*
 * From malloc man page:
 * NAME
 *       malloc, free, calloc, realloc - allocate and free dynamic memory
 * 
 * SYNOPSIS
 *       #include <stdlib.h>
 * 
 *       void *malloc(size_t size);
 *       void free(void *ptr);
 *       void *calloc(size_t nmemb, size_t size);
 *       void *realloc(void *ptr, size_t size);
 */

// Redirect all printf to stderr
#define printf(args...) fprintf(stderr, ##args)

static void init(void);
static void	send_allocation(void *ptr, size_t size);
static void	send_free(void *ptr);

// following functions point to official libc versions
extern void *__libc_malloc(size_t size);
extern void  __libc_free(void *ptr);
extern void *__libc_calloc(size_t nmemb, size_t size); // TODO: may not exist
extern void *__libc_realloc(void *ptr, size_t size);
volatile int malloc_hook_active  = 1;
volatile int free_hook_active 	 = 1;
volatile int calloc_hook_active  = 1;
volatile int realloc_hook_active = 1;

static void *my_malloc_hook (size_t size, const void *caller);
static void  my_free_hook (void *ptr, const void *caller);
static void *my_calloc_hook(size_t nmemb, size_t size, const void *caller);
static void *my_realloc_hook(void *ptr, size_t size, const void *caller);

#define LOCK_TYPE_MALLOC		1
#define LOCK_TYPE_FREE			2
#define LOCK_TYPE_CALLOC		3
#define LOCK_TYPE_REALLOC		4


void *shm_attach(void);
void shm_spin_lock(int lock_type);
void shm_spin_unlock(int lock_type);
	
// returns start of shared memory
void *shm_attach(void)
{
	key_t 	shm_key;
	int 	shmid;

	// ftok to generate unique key 
    shm_key = ftok(SHM_KEY_STRING, SHM_KEY_INT); 
	
	// shmget returns an identifier in shmid 
    shmid = shmget(shm_key, SHM_SIZE, SHM_PERMISSIONS | IPC_CREAT);

	// return pointer to start of shared memory
	return (shmat(shmid, (void*)0, 0));
}

// multi-core, multi-procesor spin lock using shared memory
void shm_spin_lock(int lock_type)
{
	volatile atomic_flag *lock = (atomic_flag *)shm_attach();

	while (atomic_flag_test_and_set(lock + lock_type) == 1) {
		sleep(0);
	}

	// detach from shared memory
	shmdt((void *)lock); 
}


// multi-core, multi-procesor spin unlock using shared memory
void shm_spin_unlock(int lock_type)
{
	volatile atomic_flag *lock = (atomic_flag *)shm_attach();

	atomic_flag_clear(lock + lock_type);

	// detach from shared memory
	shmdt((void *)lock); 
}


void *malloc (size_t size)
{
	void *ptr;
	void *caller;

	caller = __builtin_return_address(0);
	if (malloc_hook_active) {
		return my_malloc_hook(size, caller);
	}
	ptr = __libc_malloc(size);
	// printf("Client: __libc_malloc 0x%08LX  %ld\n",
	//	   (long long unsigned int) ptr, size);
	
	return ptr;
}

void *my_malloc_hook(size_t size, const void *caller)
{
	void *ptr;

	shm_spin_lock(LOCK_TYPE_MALLOC);
	
	// deactivate hooks to avoid recurssion issues
	malloc_hook_active = 0;

    ptr = malloc(size);
	
    // printf("Client: my_malloc_hook 0x%08LX  %ld\n",
	//	   (long long unsigned int) ptr, size);

	// reactivate hooks
	malloc_hook_active = 1;

	shm_spin_unlock(LOCK_TYPE_MALLOC);

	// doesn't use malloc
	send_allocation(ptr, size);

    return ptr;
}


void free(void *ptr)
{
	void *caller;

	caller = __builtin_return_address(0);
	if (free_hook_active) {
		my_free_hook(ptr, caller);
		return;
	}
	__libc_free(ptr);
	//printf("Client: __libc_free 0x%08LX\n",
	//	   (long long unsigned int) ptr);
}

void my_free_hook(void *ptr, const void *caller)
{
	shm_spin_lock(LOCK_TYPE_FREE);
	
	// deactivate hooks to avoid recurssion issues
	free_hook_active = 0;

    free(ptr);

	//printf("Client: my_free_hook 0x%08LX\n",
	//	   (long long unsigned int) ptr);

	// reactivate hooks
	free_hook_active = 1;

	shm_spin_unlock(LOCK_TYPE_FREE);

	// doesn't use malloc
	send_free(ptr);
}


void *calloc (size_t nmemb, size_t size)
{
	void *caller;
	void *ptr;

	caller = __builtin_return_address(0);
	if (calloc_hook_active) {
		return my_calloc_hook(nmemb, size, caller);
	}
	ptr = __libc_calloc(nmemb, size);

    //printf("Client: __libc_calloc 0x%08LX  %ld\n",
    // (long long unsigned int) ptr, nmemb * size);

	return ptr;
}

void *my_calloc_hook(size_t nmemb, size_t size, const void *caller)
{
	void *ptr;

	shm_spin_lock(LOCK_TYPE_CALLOC);
	
	// deactivate hooks to avoid recurssion issues
	calloc_hook_active = 0;
		
    ptr = calloc(nmemb, size);
	
    // printf("Client: my_calloc_hook %ld  %ld\n",
    // 		   nmemb, size);

	// reactivate hooks
	calloc_hook_active = 1;

	shm_spin_unlock(LOCK_TYPE_CALLOC);

    // doesn't use malloc
    send_allocation(ptr, nmemb * size);
	
    return ptr;
}


void *realloc (void *ptr, size_t size)
{
	void *caller;
	void *new_ptr;

	caller = __builtin_return_address(0);
	if (realloc_hook_active) {
		return my_realloc_hook(ptr, size, caller);
	}
	new_ptr = __libc_realloc(ptr, size);
	// printf("Client: __libc_realloc 0x%08LX 0x%08LX  %ld\n",
	//	   (long long unsigned int)new_ptr,
	//	   (long long unsigned int)ptr,
	//	   size);

	return new_ptr;
}
/*
 * From realloc man page:
 *
 * The realloc() function changes the size of the memory block pointed to 
 * by ptr to size bytes. The contents will be unchanged in the range from 
 * the start of the region up to the  minimum of the old and new sizes.
 * 
 * If the new size is larger than the old size, the added memory will 
 * not be initialized. If ptr is NULL, then the call is equivalent to 
 * malloc(size), for all values of size; if size is equal to zero, and 
 * ptr is not NULL, then the call is equivalent to free(ptr).
 * 
 * Unless ptr is NULL, it must have been returned
 * by an earlier call to malloc(), calloc(), or realloc().
 * 
 * If the area pointed to was moved, a free(ptr) is done.
 */
void *my_realloc_hook(void *ptr, size_t size, const void *caller)
{
	void *new_ptr;

	shm_spin_lock(LOCK_TYPE_REALLOC);
	
	// deactivate hooks to avoid recurssion issues
	realloc_hook_active = 0;
		
	new_ptr = realloc(ptr, size);

    // printf("Client: my_ralloc_hook 0x%08LX 0x%08LX  %ld\n",
	//	   (long long unsigned int) ptr,
	//	   (long long unsigned int) new_ptr,
	//	   size);
	
	// reactivate hooks
	realloc_hook_active = 1;

	shm_spin_unlock(LOCK_TYPE_REALLOC);

	// doesn't use malloc
	if (ptr == NULL) {
		// no free, just allocation
		send_allocation(new_ptr, size);
	} else if ((size == 0) && (ptr != NULL)) {
		// no allocation, just free
        send_free(ptr);
    } else {
		// both free and allocation
		send_free(ptr);
        send_allocation(new_ptr, size);
    }

    return new_ptr;
}

// must be called with hooks_active = 0
void send_allocation(void *ptr, size_t size)
{
	msg_t msg;
	key_t key; 
	int msgid;

	if (size == 0) {
		// malformed allocation, don't bother sending
		return;
	}

	if (0) {
		// init only forks and executes stat_server. due to recurssion issues
		// we now start stat_server via shell script intead. 
		init();
	}

	// ftok to generate unique key 
	key = ftok(MSG_KEY_STRING, MSG_KEY_INT); 
  
	// msgget creates a message queue and returns identifier 
	msgid = msgget(key, MSG_PERMISSIONS | IPC_CREAT);

	msg.type 			= MSG_TYPE_VERKADA;
	msg.msg_data.ptr 	= ptr;
	msg.msg_data.size 	= size;

	// will block if msgQ full
	msgsnd(msgid, &msg, sizeof(msg_data_t), 0); 

	return;
}

// must be called with hooks_active = 0
void send_free(void *ptr)
{
	msg_t msg;
	key_t key; 
	int msgid;

	// ftok to generate unique key 
	key = ftok(MSG_KEY_STRING, MSG_KEY_INT); 
  
	// msgget creates a message queue and returns identifier 
	msgid = msgget(key, MSG_PERMISSIONS | IPC_CREAT);

	msg.type 			= MSG_TYPE_VERKADA;
	msg.msg_data.ptr 	= ptr;
	msg.msg_data.size 	= 0; // indicates to server this is a free

	// will block if msgQ full
	msgsnd(msgid, &msg, sizeof(msg_data_t), 0); 

	return;
}

/*
 * init only forks and executes stat_server. due to recurssion issues
 * we now start stat_server via shell script intead. code left for 
 * instructional purposes and future reference. 
 */  
void init(void)
{
	/*
	 * system will return the return value of pidof (0 for found,
	 * 1 for not found). > /dev/null is to hide the output from stdout.
	 */ 
	if (system("pidof stat_server > /dev/null") == 0) {
		// stat_server already running, nothing to do
		return;
	}

	// statserver not running - spawn it
	pid_t  pid;

	printf("Client - statserver not running, spawning...\n");
			
	pid = fork();
	if (pid == -1) {
		// pid == -1 means error occured
		printf("Client: can't fork, error occured\n");
		return;
	} else if (pid == 0) {
		// pid == 0 means child process created
				
		// getpid() returns process id of calling process
		printf("Client: child process, pid = %u\n", getpid());

		/*
		 * the argv list first argument should point to
		 * filename associated with file being executed
		 * the array pointer must be terminated by NULL
		 * pointer.
		 */
		// execute stat_server
		char *execArgs[] = { "./stat_server", NULL };
		execvp(execArgs[0], execArgs);

		_exit(0);
	}
}








