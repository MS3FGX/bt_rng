/*
 *  bt_rng - PRNG using commodity Bluetooth adapters
 * 
 *  This is an experimental program designed to generate a large number of
 *  random numbers from the PRNG included in CSR-based Bluetooth devices. It is
 *  based heavily on bccmd, from the BlueZ suite written by Marcel Holtmann
 *
 *  Written by Tom Nardi (MS3FGX@gmail.com), released under the GPLv2.
 *  For more information, see: www.digifail.com
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "csr.h"

#define VERSION	"3.0"
#define APPNAME "bt_rng"

// Threading stuff
// Maximum of 4 threads
#define MAX_THREADS	4

// Data to pass to threads
struct thread_data
{
	int thread_id;
	unsigned long iterations;
	int mode;
	int verbose;
	char *device;
	FILE *outfile;
};

struct thread_data thread_data_array[MAX_THREADS];

static void badnum(int thread_id, unsigned long iteration)
{
	printf("\n");
	// Check to see if it was first iteration, if it was, probably permissions
	if ( iteration == 1 )
	{
		printf("Epic Fail: bt_rng was unable to get a random number.\n"
				"This usually means your device isn't supported.\n");
	}
	else
		printf("Bad number on thread %i, iteration: %lu. Exiting\n",
			thread_id, iteration);
	exit(1);	
}

void *thread_rand(void *threadarg)
{
	// Define variables from struct
	int thread_id, mode, verbose;
	unsigned long iterations;
	char *device;
	FILE *outfile;
		
	// Pull data out of struct
	struct thread_data *local_data;
	local_data = (struct thread_data *) threadarg;
	thread_id = local_data->thread_id;
	iterations = local_data->iterations;
	device = local_data->device;
	outfile = local_data->outfile;
	mode = local_data->mode;
	verbose = local_data->verbose;
	
	// Define other variables
	uint8_t array[8];
	int randnum, lastrandnum;
	int err, r;
	unsigned long i;
	
	// String array for binary conversion
	// 32 bit number plus null-terminator
	char binary[33];
	char buffer[33];
	
	// Index values for binary array
	int bw, bi;
	
	// Digits/Numbers thrown away
	unsigned long tossed = 0;
	
	// Init device
	if (csr_open_hci(device) < 0)
		exit(1); // TODO: Proper shutdown

	printf("Initialized device: %s on thread %i.", device, thread_id);
	
	// Define array
	memset(array, 0, sizeof(array));
	
	for ( i = 1; i <= iterations; i++ )
	{			
		// Read from device, capture return value to detect errors
		err = csr_read_hci(CSR_VARID_RAND, array, 8);
		
		// Put data into simple integer
		randnum = array[0] | (array[1] << 8);
		if ( (err == -1) || (randnum == 0) )
		{
			/*
			* This means that the device didn't want to give us a number, which
			* may or may not mean the device is actually incapable of it. So
			* we give it a few chances to come up with a new non-zero random
			* number. If it still won't do what we want, then we have to give up.
			*/
			
			// Save current random number into temporary variable
			lastrandnum = randnum;
			
			// For some reason, it almost always corrects itself after 4 queries
			for ( r = 1; r <= 6; r++ )
			{
				// Throttle down a bit...
				usleep(2000);
				csr_read_hci(CSR_VARID_RAND, array, 8);
				randnum = array[0] | (array[1] << 8);
				printf(
				"Thread %i autocorrection, attempt %i: Old: %i, New: %i\n",
				thread_id, r, lastrandnum, randnum);
				if (randnum != lastrandnum)
					break;
			}
			if ( randnum == 0 )
				badnum(thread_id, i);
		}
		else if ( err < 0 )
		{
				// Some other error has occured, print report and exit
				fprintf(stderr, "Error on thread %i!\n", thread_id);
				fprintf(stderr,
					"Iteration: %lu of %lu, Device: %s, Rand: %i, Error: %i\n",
					i, iterations, device, randnum, err);
				exit(1);
		}
		
		// Process result
		if ( randnum <= 60000 ) // Throw out everything above 60k
		{
			randnum = randnum % 10000; // Divide by 10k
			if ( mode == 0 ) // Binary conversion
			{						
				// Convert integer to string representation
				sprintf(buffer, "%i", randnum);		

				// Reset binary array, index variable
				memset(binary, 0, sizeof(binary));
				bw = 0;
		
				// Loop through character array, minus terminator
				for ( bi = 0; bi <= strlen(buffer) - 1; bi++ )
				{
					/* Range:
					 *  Zero - 0 to 4
					 *  One  - 5 to 9
					 */
					if ( (buffer[bi] >= 48) && (buffer[bi] <= 52) )
					{
						binary[bw] = 48; // Zero
						bw++;
					}
					else if ( (buffer[bi] >= 53) && (buffer[bi] <= 57) )
					{
						binary[bw] = 49; // One
						bw++;
					}
					else
						printf("Bad num: %i\n",buffer[bi]);
				}
				// Output to file, if there is anything there anyway
				if ( strlen(binary) >= 1 )
					fprintf(outfile,"%s", binary);
			}
			else if ( mode == 1 ) //Output integer
			{
				fprintf(outfile,"%i",randnum);
			}
		}
		else
				tossed++;
		
		// Print progress every 10,000 iterations
		if ( ( i % 10000 == 0 ) && ( verbose == 1 ) )
			printf("Thread %i completed %lu of %lu iterations\n",
				thread_id, i, iterations);
	}
	printf("Thread %i tossed %lu numbers\n", thread_id, tossed);
	printf("Thread %i done.\n", thread_id);
	pthread_exit(NULL);
}

static void help(void)
{
	printf("%s (v%s) by MS3FGX\n", APPNAME, VERSION);
	printf("----------------------------------------------------------------\n");
	printf("bt_rng is designed to pull random numbers from the RNG included\n"
		"in Bluetooth adpaters that use the Cambridge Silicon Radio chipset.\n"
		"It is intended as a proof of concept to test the validity of random\n"
		"numbers generated by this method, and it is in no way guaranteed\n"
		"that the numbers produced are of sufficient quality to use in\n"
		"security related applications. You have been warned.\n"
		"\n"
		"Generation speed is very very slow, so it is highly recommended you\n"
		"run bt_rng on some sort of cluster if possible. You can use multiple\n"
		"adapters on one machine, but probably not more than two safely.\n");
	printf("\n");
	printf("For more information, see www.digifail.com\n");
	printf("\n");
	printf("Options:\n"
		"\t-i <iterations>    Number of iterations to perform\n"
		"\t-t <threads>       Number of threads to run, up to 4\n"
		"\t-o <filename>      Sets output filename, default is output.txt\n"
		"\t-m <mode>          Sets output mode, default is binary\n"
		"\t-v                 Enables verbose output, default is disabled\n"
		"\n");
}

static struct option main_options[] = {
	{ "iterations", 1, 0, 'i' },
	{ "threads",	1, 0, 't' },
	{ "output",	1, 0, 'o' },
	{ "mode", 1, 0, 'm' },
	{ "verbose", 0, 0, 'v' },
	{ "help", 0, 0, 'h' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char *argv[])
{
	// Misc variables
	int t, opt;
	
	// Pointer to file, initialize to NULL
	FILE *outfile = NULL;
	
	// Pointer to filenames, with default output filename.
	char *outfilename = "output.txt";

	// Buffer for generating device names
	char devbuff[5];
	
	// Thread ID
	pthread_t threads[MAX_THREADS];
	
	// Threads to run, default 1
	int numthreads = 1;
	
	// Default number of iterations
	unsigned long iterations = 1;
	
	// Default mode and verbosity
	int mode = 0;
	int verbose = 0;
	
	while ((opt=getopt_long(argc,argv,"+t:i:o:m:vh", main_options, NULL)) != EOF)
	{
		switch (opt)
		{
		case 'o':
			outfilename = strdup(optarg);
			break;
		case 'm':
			mode = atoi(optarg);
			if ( mode > 1 || mode < 0 )
			{
				printf("Invalid mode. See README.\n");
				exit(1);
			}
			break;
		case 'i':
			iterations = atoi(optarg);
			if ( iterations <= 0 )
			{
				printf("Cannot have negative iterations.\n");
				exit(1);
			}
			break;
		case 't':
			numthreads = atoi(optarg);
			if ( numthreads > MAX_THREADS || numthreads <= 0 )
			{
				printf("Invalid number of threads. See README.\n");
				exit(1);
			}
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
			help();
			exit(0);
		default:
			printf("Unknown option. Use -h for help, or see README.\n");
			exit(1);
		}
	}

	// If no argument given, show help and exit with error
	if (argc <= 1)
	{
		help();
		exit(1);
	}
	
	// Check if we are running as root
	if(getuid() != 0)
	{
		printf("You need to be root to run bt_rng!\n");
		exit(1);
	}
	
	// Check to see if iterations is divisible by thread count
	if ( iterations % numthreads != 0 )
	{
		printf(
		"Iterations cannot be evenly distributed among the current number of"
		" threads.\nAdjust input variables and try again.\n");
		exit(1);
	}

	// Boilerplate
	printf("%s (v%s) by MS3FGX\n", APPNAME, VERSION);
	printf("---------------------------\n");
	
	// Open output file
	printf("Opening output file: %s...", outfilename);
	if ((outfile = fopen(outfilename,"a+")) == NULL)
	{
		printf("\n");
		printf("Error opening output file.\n");
		exit(1);
	}
	printf("OK\n");
	
	// Print mode
	printf(
		"Running %lu iterations on %i threads. Mode: ", iterations, numthreads);
	switch (mode)
	{
		case 0:
			printf("Binary\n");	
			break;
		case 1:
			printf("Integer\n");
			break;
	}
	
	for( t = 0; t < numthreads; t++ )
	{
		// Thread ID number
		thread_data_array[t].thread_id = t;
		
		// Make string out of "hci" and value of t
		sprintf(devbuff, "hci%i", t);

		// Apply it to array
		thread_data_array[t].device = devbuff;
		
		// Default information for all threads
		thread_data_array[t].iterations = iterations / numthreads;
		thread_data_array[t].outfile = outfile;
		thread_data_array[t].mode = mode;
		thread_data_array[t].verbose = verbose;
		
		// Start thread
		pthread_create(&threads[t], NULL, thread_rand, (void *)
			&thread_data_array[t]);
	}
	
	// Wait for threads to complete
	for ( t = 0; t < numthreads; t++ )
		pthread_join(threads[t], NULL);
			
	// Close files
	printf("Closing files...\n");
	fclose(outfile);
	csr_close_hci();
	
	printf("Main Done.\n");
	exit(0);
}
