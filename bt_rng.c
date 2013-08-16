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
#include <syslog.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "csr.h"

#define VERSION	"3.0"
#define APPNAME "bt_rng"

// Maximum of 4 threads
#define MAX_THREADS	4
#define PID_FILE "/tmp/bt_rng.pid"

// Data to pass to threads
struct thread_data
{
	int thread_id;
	unsigned long iterations;
	int mode;
	int verbose;
	int daemon;
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
	int thread_id, mode, verbose, daemon;
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
	daemon = local_data->daemon;
	
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

	// Show we've started HW on console and syslog
	if (daemon)
		syslog(LOG_INFO,"Initialized device: %s on thread %i.\n", device, thread_id);
	else
		printf("Initialized device: %s on thread %i.\n", device, thread_id);
	
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
				if (verbose)
					printf("Thread %i autocorrection, attempt %i: Old: %i, New: %i\n", thread_id, r, lastrandnum, randnum);
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
				fprintf(stderr, "Iteration: %lu of %lu, Device: %s, Rand: %i, Error: %i\n", i, iterations, device, randnum, err);
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
		if ((i % 10000 == 0) && (verbose == 1))
		{
			if (daemon)
				syslog(LOG_INFO,"Thread %i completed %lu of %lu iterations\n", thread_id, i, iterations);
			else
				printf("Thread %i completed %lu of %lu iterations\n", thread_id, i, iterations);
		}
	}
	if (daemon)
		syslog(LOG_INFO,"Thread %i done.\n", thread_id);
	else
	{
		if (verbose)
			printf("Thread %i tossed %lu numbers\n", thread_id, tossed);
		
		printf("Thread %i done.\n", thread_id);
	}
	pthread_exit(NULL);
}

// Daemon/PID functions from Bluelog
int read_pid (void)
{
	// Any error will return 0
	FILE *pid_file;
	int pid;

	if (!(pid_file=fopen(PID_FILE,"r")))
		return 0;
		
	if (fscanf(pid_file,"%d", &pid) < 0)
		pid = 0;

	fclose(pid_file);	
	return pid;
}

static void write_pid (pid_t pid)
{
	FILE *pid_file;
	
	// Open PID file
	printf("Writing PID file: %s...", PID_FILE);
	if ((pid_file = fopen(PID_FILE,"w")) == NULL)
	{
		printf("\n");
		printf("Error opening PID file!\n");
		exit(1);
	}
	printf("OK\n");
	
	// If open, write PID and close	
	fprintf(pid_file,"%d\n", pid);
	fclose(pid_file);
}

static void daemonize (void)
{
	// Process and Session ID
	pid_t pid, sid;
	
	// Fork off process
	pid = fork();
	if (pid < 0)
		exit(EXIT_FAILURE);
	else if (pid > 0)
		exit(EXIT_SUCCESS);
			
	// Change umask
	umask(0);
 
	// Create a new SID for the child process
	sid = setsid();
	if (sid < 0)
		exit(EXIT_FAILURE);

	// Change current working directory
	if ((chdir("/")) < 0)
		exit(EXIT_FAILURE);
		
	// Write PID file
	write_pid(sid);
	
	printf("Going into background...\n");
		
	// Close file descriptors
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
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
		"\t-v                 Enables verbose output, default is disabled\n"		
		"\t-n                 Use unprocessed random number as given by hardware\n"
		"\t-d                 Run bt_rng in background as daemon\n"
		"\t-k                 Kill running bt_rng process\n"
		"\n");
}

static struct option main_options[] = {
	{ "iterations", 1, 0, 'i' },
	{ "threads", 1, 0, 't' },
	{ "output",	1, 0, 'o' },
	{ "mode", 1, 0, 'n' },
	{ "verbose", 0, 0, 'v' },
	{ "kill", 0, 0, 'k' },
	{ "daemonize", 0, 0, 'd' },
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
	
	// Process ID read from PID file
	int ext_pid;		
		
	// Default settings
	int mode = 0;
	int verbose = 0;
	int daemon = 0;
	
	while ((opt=getopt_long(argc,argv,"+t:i:o:nvhkd", main_options, NULL)) != EOF)
	{
		switch (opt)
		{
		case 'o':
			outfilename = strdup(optarg);
			break;
		case 'n':
			mode = 1;
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
		case 'd':
			daemon = 1;
			break;
		case 'k':
			// Read PID from file into variable
			ext_pid = read_pid();
			if (ext_pid != 0)
			{
				printf("Killing bt_rng process with PID %i...",ext_pid);
				if(kill(ext_pid,15) != 0)
				{
					printf("ERROR!\n");
					printf("Unable to kill bt_rng process. Check permissions.\n");
					exit(1);
				}
				else
					printf("OK.\n");
				
				// Delete PID file
				unlink(PID_FILE);
			}
			else
				printf("No running bt_rng process found.\n");
				
			exit(0);
		case 'h':
			help();
			exit(0);	
		default:
			printf("Unknown option. Use -h for help, or see README.\n");
			exit(1);
		}
	}
	
	// See if there is already a process running
	if (read_pid() != 0)
	{
		printf("Another instance of bt_rng is already running!\n");
		printf("Use the -k option to kill a running bt_rng process.\n");
		exit(1);
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
	
	// Daemon switch
	if (daemon)
		daemonize();
	
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
		thread_data_array[t].daemon = daemon;
		
		// Start thread
		pthread_create(&threads[t], NULL, thread_rand, (void *)
			&thread_data_array[t]);
	}
	
	// Wait for threads to complete
	for ( t = 0; t < numthreads; t++ )
		pthread_join(threads[t], NULL);
			
	// Close files
	if (!daemon)
		printf("Closing files...\n");
		
	unlink(PID_FILE);
	fclose(outfile);
	csr_close_hci();
	
	if (daemon)
		syslog(LOG_INFO,"Main thread done.\n");
	else
		printf("Main thread done.\n");
		
	exit(0);
}
