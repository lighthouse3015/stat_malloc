/**************************************************************************
 * Filename: test.cpp
 *
 * Purpose: test file for shared library to replace malloc(), free(),
 *          calloc(), and realloc() with debug versions that periodically
 *          print statistics.
 *
 * By: Keith Hendley
 * Date: 9/3/19
 *
 *************************************************************************/

#include <iostream>
#include <thread> // threads
#include <stdlib.h>
#include <unistd.h> // sleep

using namespace std;

// test notes
// ----------
// pidof stat_server to get pid of runnign stat_server
// kill [pid] to get rid of stat_server

void recurssive_test(uint32_t num_malloc, size_t size);
void multithreaded_test(size_t size);


void recurssive_test(uint32_t num_malloc, size_t size)
{
	malloc(size);

	if (num_malloc != 0) {
		num_malloc--;
		recurssive_test(num_malloc, size);
	}
}

void multithreaded_test(size_t size)
{
	
	thread th1(recurssive_test, 6000, size);
	thread th2(recurssive_test, 5000, size << 1);
	thread th3(recurssive_test, 2000, size << 2);
	thread th4(recurssive_test, 1000, size << 3);
	thread th5(recurssive_test, 5000, size << 4);
	thread th6(recurssive_test, 1000, size << 5);
	thread th7(recurssive_test, 7000, size << 6);
	thread th8(recurssive_test, 2000, size << 7);
	thread th9(recurssive_test, 6000, size << 8);
	thread th10(recurssive_test, 4000, size << 9);
	thread th11(recurssive_test, 1300, size << 10);
	
	th1.join();
	th2.join();
	th3.join();
	th4.join();
	th5.join();
	th6.join();
	th7.join();
	th8.join();
	th9.join();
	th10.join();
	th11.join();
}


int main()
{
    // TEST RECURSION - no deadlocks
	recurssive_test(1024, 16);
	recurssive_test(512, 64);

    // TEST MULTIPLE THREADS - no crashes or deadlocks
	multithreaded_test(4);

    void *alloc_ptr, *calloc_ptr;
    size_t size = 8;

    // cerr << "Test app starting" << endl;

	// cerr << "Test app: malloc" << endl;
    alloc_ptr = malloc(size);

    calloc_ptr = calloc(8, size);
    alloc_ptr = realloc(alloc_ptr, size*2);

    free(calloc_ptr);

	// cerr << "Test app: waiting" << endl;
	sleep(2);

	// cerr << "Test app: free" << endl;
    free(alloc_ptr); 

    return 0;
}
