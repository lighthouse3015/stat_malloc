#include <iostream>

using namespace std;

/*******************************************************************************
 * Filename: stat_server.cpp
 *
 * Purpose: server that keeps statistics on memory allocations and frees via
 * msgQ and periodically prints those statistics.
 *
 * By: Keith Hendley
 * Date: 9/3/19
 *
 ******************************************************************************/


#include <iostream>
#include <map>
#include <vector>
#include <stdlib.h>
#include <time.h> // localtime(), time_t
#include <sys/time.h> //  gettimeofday(), timeval
#include <sys/types.h> // fork
#include <unistd.h> // fork
#include "stat_server.h" 


using namespace std;

// Redirect all printf to stderr
#define printf(args...) fprintf(stderr, ##args)

typedef struct {
    size_t              size;       // for reducing total_current_size upon removal
    uint32_t            size_bin;   // zero based, for fast removal from size array
    timeval             time;  
} data_t;

// Save pertinant data into a map
multimap<void *, data_t> map_data;

long overall_allocations = 0;
long total_current_size  = 0;

// Size array for printing size
#define         NUM_SIZE_BINS    12
uint32_t size_array[NUM_SIZE_BINS] = {0};

// Age array for printing age
#define 		NUM_AGE_BINS	5
typedef enum {
	LESS_THAN_1_SEC,
	LESS_THAN_10_SEC,
	LESS_THAN_100_SEC,
	LESS_THAN_1000_SEC,
	EQUAL_TO_OR_OVER_1000_SEC
} AGE_BIN;

void insert_allocation(void *ptr, size_t size);
void remove_allocation(void *ptr);
void print_stats(void);
uint32_t get_max_bin_num(uint32_t *age_array);
void print_size_symbol(uint32_t bin, uint32_t symbol_size);
void print_age_symbol(uint32_t *age_array, uint32_t bin, uint32_t symbol_size);

int main()
{
	msg_t 	 msg;
	key_t 	 msg_key, shm_key; 
    int 	 msgid, shmid;
	timeval  start_time, intermediate_time;
	uint32_t elapsed_seconds;
	uint32_t *shm;

	cerr << "Server Started, pid: " << getpid() << endl;
  
    // ftok to generate unique key 
    msg_key = ftok(MSG_KEY_STRING, MSG_KEY_INT); 
  
    // msgget creates a message queue and returns identifier 
    msgid = msgget(msg_key, MSG_PERMISSIONS | IPC_CREAT);

	// ftok to generate unique key 
    shm_key = ftok(SHM_KEY_STRING, SHM_KEY_INT); 
	
	// shmget returns an identifier in shmid 
    shmid = shmget(shm_key, SHM_SIZE, SHM_PERMISSIONS | IPC_CREAT);

	// initialize shm mutex area - 4 locks
	shm = (uint32_t *) shmat(shmid, (void*)0, 0);
	for (int i = 0; i < 4; i++) {
		*shm = 0;
		shm++;
	}

	// detach from shared memory
	shmdt(shm); 

	gettimeofday(&start_time, NULL);

	while (1) {
	
		// wait on message receive 
		msgrcv(msgid, &msg, sizeof(msg_t), MSG_TYPE_VERKADA, 0);

		if (msg.msg_data.size) {
			// cerr << "Server Rx: Insertion " << msg.msg_data.ptr << ", "
			//	 << msg.msg_data.size << endl;
			
			insert_allocation(msg.msg_data.ptr, msg.msg_data.size);
		} else {
			// zero size means remove
			// cerr << "Server Rx: Removal " << msg.msg_data.ptr << endl;
			remove_allocation(msg.msg_data.ptr);
		}

		gettimeofday(&intermediate_time, NULL);

		elapsed_seconds = (intermediate_time.tv_sec - start_time.tv_sec) +
			(intermediate_time.tv_usec - start_time.tv_usec) / 1000000;

		if (elapsed_seconds >= 1) {
			// for testing, break
			print_stats(); // only about every 1 seconds
			gettimeofday(&start_time, NULL);
		}
		
	}
                    
    // destroy the message queue
	cerr << "Server: Destroying msgQ" << endl;
    msgctl(msgid, IPC_RMID, NULL); 
	
	return 0;
}


void insert_allocation(void *ptr, size_t size)
{
    // record time
    data_t data;
    gettimeofday(&data.time, NULL);

    // record size
    data.size = size;
	
    // calculate and record bin
    size_t temp_size = size;
    data.size_bin = 0; // save size_bin for fast removal from array_size
    temp_size >>= 1;
    while (temp_size >>= 1)
    {
        data.size_bin++;
    }
    data.size_bin =  min(data.size_bin, (uint32_t)(NUM_SIZE_BINS - 1));

    // update data structures
    map_data.insert(pair <void *, data_t> (ptr, data)); // add to master map data
    overall_allocations++;		  // update total allocations
    total_current_size += size;   // update current total size
    size_array[data.size_bin]++;  // add to correct size bin for printing
}

void remove_allocation(void *ptr)
{
	map<void *, data_t>::iterator it;

	if ((it = map_data.find(ptr)) == map_data.end()) {
		// ptr not in map - it must have been allocated before LD_PRELOAD set
		return;
	}

	total_current_size -= it->second.size;   // reduce current total size
	size_array[it->second.size_bin]--;  // reduce correct size bin by 1
    map_data.erase(it);
}

void print_stats(void)
{
	map<void *, data_t>::iterator it;
	timeval current_time;
	uint32_t elapsed_time;
	uint32_t age_array[NUM_AGE_BINS] = {0};
	uint32_t age_bin;
    time_t t = time(NULL);
	double unit_sized;
	uint32_t size_unit_index;
	vector<string> size_units;
	uint32_t max_bin_num, symbol_size;
    struct tm tam = *localtime(&t);

    printf(">>>>>>>>>>>>>>>> %d-%02d-%02d %02d:%02d:%02d %s <<<<<<<<<<<<<<<<\n", tam.tm_mon + 1, tam.tm_mday,
           tam.tm_year + 1900, tam.tm_hour, tam.tm_min, tam.tm_sec, tam.tm_zone);
    printf("Overall stats:\n");
    printf("%ld Overall allocations since start\n", overall_allocations);
	// print current total size in appropriate units
	unit_sized = total_current_size;
	size_units.push_back("");
	size_units.push_back("KiB");
	size_units.push_back("MiB");
	size_units.push_back("GiB");
	size_units.push_back("TiB");
	size_unit_index = 0;
	while ((unit_sized > 1024) &&
		   (size_unit_index < (size_units.size() - 1))) {
		unit_sized /= 1024;
		size_unit_index++;
	}
	printf("%.1f", unit_sized);
	cerr << size_units.at(size_unit_index);
	printf(" Current total allocated size\n");
	printf("\n\n");

	// Create age bins
	gettimeofday(&current_time, NULL);
	for (it = map_data.begin(); it != map_data.end(); it++) {
		elapsed_time = (current_time.tv_sec - it->second.time.tv_sec) +
			(current_time.tv_usec - it->second.time.tv_usec) / 1000000;
		
	    age_bin = LESS_THAN_1_SEC;
		while ((elapsed_time > 1) &&
			   (age_bin < EQUAL_TO_OR_OVER_1000_SEC)) {
			elapsed_time /= 10;
			age_bin++;
		}
		age_array[age_bin]++;
		// printf("map size: %ld, current age bin: %d\n", map_data.size(), age_bin);
	}

	// Normalize symbol
	symbol_size = 1;
	max_bin_num = get_max_bin_num(age_array);
	while (max_bin_num > 40) { // reduce symbol count 20 or below
		max_bin_num >>= 1;
		symbol_size <<= 1;
	}
	
	printf("Current allocations by size: (# - %d current allocations)\n",
		   symbol_size);
    printf("0 - 3 bytes: ");
    print_size_symbol(0, symbol_size);
    printf("\n");
    printf("4 - 7 bytes: ");
    print_size_symbol(1, symbol_size);
    printf("\n");
    printf("8 - 15 bytes: ");
    print_size_symbol(2, symbol_size);
    printf("\n");
    printf("16 - 31 bytes: ");
    print_size_symbol(3, symbol_size);
    printf("\n");
    printf("32 - 63 bytes: ");
    print_size_symbol(4, symbol_size);
    printf("\n");
    printf("64 - 127 bytes: ");
    print_size_symbol(5, symbol_size);
    printf("\n");
    printf("128 - 255 bytes: ");
    print_size_symbol(6, symbol_size);
    printf("\n");
    printf("256 - 511 bytes: ");
    print_size_symbol(7, symbol_size);
    printf("\n");
    printf("512 - 1023 bytes: ");
    print_size_symbol(8, symbol_size);
    printf("\n");
    printf("1024 - 2047 bytes: ");
    print_size_symbol(9, symbol_size);
    printf("\n");
    printf("2048 - 4095 bytes: ");
    print_size_symbol(10, symbol_size);
    printf("\n");
    printf("4096+: ");
    print_size_symbol(11, symbol_size);
    printf("\n");

	printf("\n\n");

	// print time table
	printf("Current allocations by age: (# - %d current allocations)\n",
		   symbol_size);
	
	printf("< 1 sec: ");
    print_age_symbol(age_array, (uint32_t)LESS_THAN_1_SEC, symbol_size);
	printf("\n");

	printf("< 10 sec: ");
    print_age_symbol(age_array, (uint32_t)LESS_THAN_10_SEC, symbol_size);
	printf("\n");

	printf("< 100 sec: ");
    print_age_symbol(age_array, (uint32_t)LESS_THAN_100_SEC, symbol_size);
	printf("\n");

	printf("< 1000 sec: ");
    print_age_symbol(age_array, (uint32_t)LESS_THAN_1000_SEC, symbol_size);
	printf("\n");

	printf(">= 1000 sec: ");
    print_age_symbol(age_array, (uint32_t)EQUAL_TO_OR_OVER_1000_SEC, symbol_size);
	printf("\n");
}

uint32_t get_max_bin_num(uint32_t *age_array)
{
	uint32_t max_num = 0;
	for (int i = 0; i < NUM_SIZE_BINS; i++) {
		if (max_num < size_array[i]) {
			max_num = size_array[i];
		}
	}

	for (int i = 0; i < NUM_AGE_BINS; i++) {
		if (max_num < age_array[i]) {
			max_num = age_array[i];
		}
	}
	
	return max_num;
}

void print_size_symbol(uint32_t bin, uint32_t symbol_size)
{
    uint32_t num;

    if (bin >= NUM_SIZE_BINS) {
        return;
    }

    num = size_array[bin];

	// normalize per symbol size
	num /= symbol_size;
    while (num) {
        printf("#");
        num--;
    }
}

void print_age_symbol(uint32_t *age_array, uint32_t bin, uint32_t symbol_size)
{
    uint32_t num;

    if (bin >= NUM_AGE_BINS) {
        return;
    }

    num = age_array[bin];

	// normalize per symbol size
	num /= symbol_size;
    while (num) {
        printf("#");
        num--;
    }
}
