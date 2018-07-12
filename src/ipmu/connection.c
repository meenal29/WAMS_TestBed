#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <math.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include <unistd.h>
#include "connection.h"
#include "function.h"
#include "iPMU.h"


int yes = 1; 	/* argument to setsockopt */

/* Initialize the pthread_mutex for PDC Objects */
pthread_mutex_t mutex_data = PTHREAD_MUTEX_INITIALIZER;

/* ----------------------------------------------------------------------------	*/
/* FUNCTION  void* SEND_DATA()       	               					*/
/* This function run by a seprate thread only for data transmission.            */
/* Function to generate and send the data frame periodically to client's   	*/
/* destination address or to PDC (client).                                      */
/* ----------------------------------------------------------------------------	*/

void* SEND_DATA()
{
     /* Calculate the waiting time during sending data frames */
	int data_waiting, i=0;

	data_waiting = 1e9/cfg_info->cfg_dataRate;

     struct timespec *cal_timeSpec, *cal_timeSpec1;
     cal_timeSpec = malloc(sizeof(struct timespec));
     cal_timeSpec1 = malloc(sizeof(struct timespec));

     /* int clock_gettime(clockid_t clk_id, struct timespec *tp);
        The functions clock_gettime() and retrieve and set the time of the specified clock clk_id.
        CLOCK_REALTIME : System-wide realtime clock. Setting this clock requires appropriate privileges.
     */
	clock_gettime(CLOCK_REALTIME, cal_timeSpec);

    	while(1)
    	{
        	clock_gettime(CLOCK_REALTIME, cal_timeSpec1);
        	clock_gettime(CLOCK_REALTIME, cal_timeSpec);

        	if (cal_timeSpec->tv_sec > cal_timeSpec1->tv_sec)
        	{
            		fsecNum = 1;
            		break;
        	}
    	} // End of while-1

	while(1)
	{
        	if (i != 0)
        	{
               cal_timeSpec->tv_nsec += data_waiting;
        	}
        	else
        	{
            	cal_timeSpec->tv_nsec = data_waiting;
        	}
        	if ((cal_timeSpec->tv_nsec) >= 1e9)
        	{
            	cal_timeSpec->tv_sec++;
            	cal_timeSpec->tv_nsec-=1e9;
        	}

		/* Call the function generate_data_frame() to create a fresh new Data Frame */
		generate_data_frame();
    	
          /* int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *request, struct timespec *remain);
              clock_nanosleep - high resolution sleep with specifiable clock (ADVANCED REALTIME). Link with -lrt.
              The suspension time caused by this function may be longer than requested because the argument value is rounded
              up to an integer multiple of the sleep resolution, or because of the scheduling of other activity by the system.
          */
		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, cal_timeSpec, cal_timeSpec1);
		
          pthread_mutex_lock(&mutex_data);

		if(status_info->udp_send_status == 0)
		{
			/* UDP Data Send */
			if (sendto(UDP_sockfd,data_frm,data_frm_size, 0,(struct sockaddr *)&UDP_addr,sizeof(UDP_addr)) == -1) {

				perror("sendto");
					
			} else {

				// KK				
/*				char *idcode,*soc,*fracsec;
				idcode = malloc(2*sizeof(unsigned char));
				soc = malloc(4*sizeof(unsigned char));
				fracsec = malloc(3*sizeof(unsigned char));

				c_copy(idcode,data_frm,4,2);
				c_copy(soc,data_frm,6,4);
				c_copy(fracsec,data_frm,11,3);

				logFile = fopen(logfilename,"a");
				char *eBuf = malloc(100);
				memset(eBuf,'\0',100);
				sprintf(eBuf,"%u,%u,%u\n",to_int_convertor(idcode),to_long_int_convertor(soc),to_long_int_convertor1(fracsec));				
				fputs(eBuf,logFile);
				free(eBuf);
				free(idcode);
				free(soc);
				free(fracsec);	
				fclose(logFile);		
*/
			}
		}
		
		if(status_info->tcp_send_status == 0)
		{
			/* TCP Data Send */
			if (send(status_info->tcp_new_fd,data_frm,data_frm_size,0)== -1) {

				perror("sendto");
			}
		}
          pthread_mutex_unlock(&mutex_data);

		i++;
		clock_gettime(CLOCK_REALTIME, cal_timeSpec1);

	} // End of while-2	
} // End of SEND_DATA


/* ----------------------------------------------------------------------------	*/
/* FUNCTION void* UDP_PMU():							                    */
/* This is a UDP Server of PMU and it will continuously on listening mode.      */
/* Function for receives frames from authentic PDC & reply back the 	     	*/
/* requested frame (if available) to PDC.					               */
/* ----------------------------------------------------------------------------	*/

void* UDP_PMU()
{
	unsigned char c;
	char udp_command[18];
	printf("\n Size of configuration frame is :- %d",cfg2_frm_size);
	printf("\n size of string buffer cfg frm is:-%d",strlen(cfg2_frm));

     /* This while is always in listening mode to receiving frames from a PDC and their respective reply */
	while(1)	
	{
		memset(udp_command,'\0',18);

          /* New datagram has been received */
		if ((numbytes = recvfrom(UDP_sockfd, udp_command, 18, 0, (struct sockaddr *)&UDP_addr, (socklen_t *)&UDP_addr_len)) == -1)
		{ 
			perror("recvfrom");
			exit(1);
		}
		else		
		{
			c = udp_command[1];
			c <<= 1;
			c >>= 5;

			if(c  == 0x04) 		/* Check if it is a command frame from PDC? */ 
			{
				c = udp_command[15];

				if(((c & 0x05) == 0x05) || ((c & 0x04) == 0x04))		/* If Command frame for Configuration Frame-2/1? */
				{
					printf("\nCommand Frame Received : Configuration Frame-2/1.\n"); 

					if (sendto(UDP_sockfd,cfg2_frm,cfg2_frm_size, 0,(struct sockaddr *)&UDP_addr,sizeof(UDP_addr)) == -1) {

						perror("sendto");
					}
					
					//udp_send_status = 0;
					printf("\niPMU CFG-2/1 frame [of %d Bytes] is sent to the PDC.\n", cfg2_frm_size);
				}
				else if((c & 0x01) == 0x01)		/* Command frame for Turn off transmission request from PDC */
				{ 
					printf("\nCommand Frame Received : Turn OFF data transmission.");

				     pthread_mutex_lock(&mutex_data);
					status_info->udp_send_status = 1;
				     pthread_mutex_unlock(&mutex_data);

					printf(" --> Data Put OFF.\n");
				}
				else if((c & 0x02) == 0x02)		/* Command frame for Turn ON transmission request from PDC */
				{ 				
					printf("\nCommand Frame Received : Turn ON data transmission.");

				     pthread_mutex_lock(&mutex_data);
					status_info->udp_send_status = 0;
				     pthread_mutex_unlock(&mutex_data);

					printf(" --> Data is ON.\n");
				}
				else
				{
					printf("\nCan't recognize the received Command Frame!\n");						
					continue;				
				}	
			}	
			else
			{
				printf("\nReceived Frame is not a command frame! Try Again.\n");						
				continue;				
			}	
		}
	}
} // End of UDP_PMU


/* ----------------------------------------------------------------------------	*/
/* FUNCTION  TCP_PMU();                 	     		                    */
/* This function will call by the thread for TCP communication for PMU Server.  */
/* It will accept new connections from PDC-clients and create thread for every  */
/* PDC via function call of TCP_CONNECTIONS.                                    */
/* ----------------------------------------------------------------------------	*/

void* TCP_PMU()
{
	unsigned char c;
	int new_fd, err;
	char tcp_command[18];

	while(1)
	{
		while(1)
		{
			/* New TCP connection has been received*/ 
			if (((new_fd = accept(TCP_sockfd, (struct sockaddr *)&TCP_addr, (socklen_t *)&TCP_sin_size)) == -1))
			{ 
				perror("accept");
			}
			else
			{
				pthread_mutex_lock(&mutex_data);
				status_info->tcp_new_fd = new_fd;
				pthread_mutex_unlock(&mutex_data);
				break;
			}	
		} // End of while-2
	
		while(1)
		{
			memset(tcp_command,18,'\0');	
		
		     /* TCP data Received For new_fd */
			err = recv(new_fd,tcp_command,18,0);

			if((err == -1) || (err == 0))  
		     {
				printf("\nConnection closed by PDC*, iPMU still listening:\n");
		     	break;
			}
			else
			{		
				c = tcp_command[1];
				c <<= 1;
				c >>= 5;

				if(c  == 0x04) 		/* Check if it is a command frame from PDC */ 
				{	
					c = tcp_command[15];

					if(((c & 0x05) == 0x05) || ((c & 0x04) == 0x04))		/* If Command frame for Configuration Frame-2/1? */
					{ 
						printf("\nCommand Frame Received : Configuration Frame-2/1.\n"); 

					     if (send(new_fd,cfg2_frm,cfg2_frm_size,0) == -1)
					     {
						     perror("sendto");
					     }
						printf("\nPMU CFG-2/1 frame [of %d Bytes] is sent to the PDC.\n", cfg2_frm_size);
					}	
					else if((c & 0x01) == 0x01)		/* Command frame for Turn off transmission request from PDC */
					{ 
						printf("\nCommand Frame Received : Turn OFF data transmission.");

						pthread_mutex_lock(&mutex_data);
						status_info->tcp_send_status = 1;
						pthread_mutex_unlock(&mutex_data);

						printf(" --> Data Put OFF.\n");
					}
					else if((c & 0x02) == 0x02)		/* Command frame for Turn ON transmission request from PDC */
					{ 				
						printf("\nCommand Frame Received : Turn ON data transmission.");

						pthread_mutex_lock(&mutex_data);
						status_info->tcp_send_status = 0;
						pthread_mutex_unlock(&mutex_data);
					
						printf(" --> Data is ON.\n");
					}
					else
					{
						printf("\nCan't recognize the received Command Frame!\n");						
						continue;				
					}	
				}	
				else
				{
					printf("\nReceived Frame is not a command frame! Try Again.\n");						
					continue;				
				}
			}
		} // End of while-3
	} // End of while-1
	
} // End of TCP_PMU


/* ----------------------------------------------------------------------------	*/
/* void server(int id, int uport, int tport);							*/
/* ----------------------------------------------------------------------------	*/

void server(int id, int uport, int tport)
{
	int err;
   	int udp_port = uport;
   	int tcp_port = tport;

	printf("\n\t\t|-------------------------------------------------------|\n");      
	printf("\t\t|\t\tWelcom to iPMU SERVER\t\t\t|\n");      
	printf("\t\t|-------------------------------------------------------|\n");      

	printf("\niPMU ID Code : %d\n", id);

	/* Create UDP socket and bind to port */
	if ((UDP_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {

		perror("socket");
		exit(1);

	} else {

		printf("\nUDP Socket\t\t: Sucessfully Created\n");
	} 	

	if (setsockopt(UDP_sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) == -1) {

		perror("setsockopt");
		exit(1);
	}

	UDP_my_addr.sin_family = AF_INET;            	// host byte order
	UDP_my_addr.sin_port = htons(udp_port);       	// short, network byte order
	UDP_my_addr.sin_addr.s_addr = INADDR_ANY;    	// automatically fill with my IP
	memset(&(UDP_my_addr.sin_zero),'\0', 8);     	// zero the rest of the struct

	if (bind(UDP_sockfd, (struct sockaddr *)&UDP_my_addr, sizeof(struct sockaddr)) == -1) {

		perror("bind");
		exit(1);

	} else {

		printf("UDP Socket Bind\t\t: Sucessfull\n");
	} 

	/* UDP created socket and is litening for connections */
	printf("UDP Listening Port\t: %d\n\n",udp_port);

	/* Create TCP socket and bind and listen on port */
	if ((TCP_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {

		perror("socket");
		exit(1);

	} else {

		printf("TCP Socket\t\t: Sucessfully created\n");
	}

	if (setsockopt(TCP_sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) == -1) {

		perror("setsockopt");
		exit(1);
	}

	TCP_my_addr.sin_family = AF_INET;          	// host byte order
	TCP_my_addr.sin_port = htons(tcp_port);     	// short, network byte order
	TCP_my_addr.sin_addr.s_addr = INADDR_ANY;  	// automatically fill with my IP
	memset(&(TCP_my_addr.sin_zero), '\0', 8);  	// zero the rest of the struct

	if (bind(TCP_sockfd, (struct sockaddr *)&TCP_my_addr, sizeof(struct sockaddr)) == -1) {

		perror("bind");
		exit(1);

	} else {

		printf("TCP Socket Bind\t\t: Sucessfull\n");
	}

	if (listen(TCP_sockfd, BACKLOG) == -1) {

		perror("listen");
		exit(1);
	}
	
	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {

		perror("sigaction");
		exit(1);
	}

	/* TCP created socket and is litening for connections */
	printf("TCP Listening Port\t: %d\n",tcp_port);

	printf("\niPMU Server now listening for new connection:\n");

	UDP_addr_len = sizeof(struct sockaddr);
	TCP_sin_size = sizeof(struct sockaddr_in);

	/* Allocate the memory for the ConfigurationFrame object and assigned fields */
	status_info = malloc(sizeof(struct status));
	status_info->udp_send_status = 1;
	status_info->tcp_send_status = 1;

	/* Threads are created for UDP and TCP to listen on ports given by user */
	if((err = pthread_create(&UDP_thread,NULL,UDP_PMU,NULL))) { 

		perror(strerror(err));
		exit(1);	
	}

	if((err = pthread_create(&TCP_thread,NULL,TCP_PMU,NULL))) {

		perror(strerror(err));
		exit(1);	
	}    

	/* Create the SEND_DATA thread for sending Data */
	if((err = pthread_create(&DATA_thread,NULL,SEND_DATA,NULL))) {

		perror(strerror(err));
		exit(1);	
	}

	pthread_join(UDP_thread, NULL);
	pthread_join(TCP_thread, NULL);
	pthread_join(DATA_thread, NULL);

	close(UDP_sockfd);	
	close(TCP_sockfd);
} /* end of main */

/*************************************** End of Program ***********************************************/ 
