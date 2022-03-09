/* 
 * dvbzap - zap DVB adapter
 * Based MuMuDVB - Stream a DVB transport stream.
 * Based on dvbstream by (C) Dave Chapman <dave@dchapman.com> 2001, 2002.
 * 
 * (C) 2022 Roberto TVEpg.eu <l2mrroberto@gmail.com>
 * (C) 2004-2013 Brice DUBOST
 * 
 * Code for dealing with libdvben50221 inspired from zap_ca
 * Copyright (C) 2004, 2005 Manu Abraham <abraham.manu@gmail.com>
 * Copyright (C) 2006 Andrew de Quincey (adq_dvb@lidskialf.net)
 * 
 *
 * The latest version can be found at https://github.com/l2mrroberto/dvbzap
 * 
 * Copyright notice:
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


/** @file
 * @brief This file is the main file of dvbzap
 */


/** @mainpage Documentation for the mumudvb project
 * @section introduction
 * Mumudvb is a program that can redistribute streams from DVB on a network using
 * multicasting or HTTP unicast. It is able to multicast a whole DVB transponder by assigning
 * each channel to a different multicast IP.
 *
 * @section Main features

 * Stream channels from a transponder on different multicast IPs

 * The program can rewrite the PAT Pid in order to announce only present channels (useful for some set-top boxes)

 * Support for scrambled channels (if you don't have a CAM you can use sasc-ng, but check if it's allowed in you country)

 * Support for autoconfiguration

 * Generation of SAP announces

 *@section files
 * mumudvb.h header containing global information 
 *
 * autoconf.c autoconf.h code related to autoconfiguration
 *
 * cam.c cam.h : code related to the support of scrambled channels
 *
 * crc32.c : the crc32 table
 *
 * dvb.c dvb.h functions related to the DVB card : oppening filters, file descriptors etc
 *
 * log.c logging functions
 *
 * pat_rewrite.c rewrite.h : the functions associated with the rewrite of the PAT pid
 *
 * sdt_rewrite.c rewrite.h : the functions associated with the rewrite of the SDT pid
 *
 * sap.c sap.h : sap announces
 *
 * ts.c ts.h : function related to the MPEG-TS parsing
 *
 * tune.c tune.h : tuning of the dvb card
 *
 * network.c network.h : networking ie openning sockets, sending packets
 *
 * unicast_http.c unicast_http.h : HTTP unicast
 */

#define _GNU_SOURCE		//in order to use program_invocation_short_name and pthread_timedjoin_np


#include "config.h"

// Linux includes:
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <stdint.h>
#include <resolv.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#ifdef ANDROID
#include <limits.h>
#else
#include <values.h>
#endif
#include <string.h>
#include <syslog.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <linux/dvb/version.h>
#include <sys/mman.h>
#include <pthread.h>

#include "mumudvb.h"
#include "mumudvb_mon.h"
#include "tune.h"
#include "network.h"
#include "dvb.h"
#ifdef ENABLE_CAM_SUPPORT
#include "cam.h"
#endif
#ifdef ENABLE_SCAM_SUPPORT
#include "scam_capmt.h"
#include "scam_common.h"
#include "scam_getcw.h"
#include "scam_decsa.h"
#endif
#include "ts.h"
#include "errors.h"
#include "autoconf.h"
#include "sap.h"
#include "rewrite.h"
#include "unicast_http.h"
#include "rtp.h"
#include "log.h"

#if defined __UCLIBC__ || defined ANDROID
#define program_invocation_short_name "dvbzap"
#else
extern char *program_invocation_short_name;
#endif

static char *log_module="Main: ";

/* Signal handling code shamelessly copied from VDR by Klaus Schmidinger 
   - see http://www.cadsoft.de/people/kls/vdr/index.htm */

// global variables used by SignalHandler
long now;
long real_start_time;
int *card_tuned;  	  			//Pointer to the card_tuned information
int received_signal = 0;

int timeout_no_diff = ALARM_TIME_TIMEOUT_NO_DIFF;
int tuning_no_diff = 0;

int  write_streamed_channels=1;

/** Do we send scrambled packets ? */
int dont_send_scrambled=0;





//logging
extern log_params_t log_params;

// prototypes
static void SignalHandler (int signum);//below
int read_multicast_configuration(multi_p_t *, mumudvb_channel_t *, char *); //in multicast.c
void init_multicast_v(multi_p_t *multi_p); //in multicast.c

void chan_new_pmt(unsigned char *ts_packet, mumu_chan_p_t *chan_p, int pid);

int processt2(unsigned char* input_buf, unsigned int input_buf_offset, unsigned char* output_buf, unsigned int output_buf_offset, unsigned int output_buf_size, uint8_t plpId);

int
main (int argc, char *argv[])
{
	// file descriptors
	fds_t fds; /** File descriptors associated with the card */
	memset(&fds,0,sizeof(fds_t));

	//Thread information
	pthread_t signalpowerthread=0;
	pthread_t cardthread;
	pthread_t monitorthread=0;
	card_thread_parameters_t cardthreadparams;
	memset(&cardthreadparams,0,sizeof(card_thread_parameters_t));

	//Channel information
	mumu_chan_p_t chan_p;
	memset(&chan_p,0,sizeof(chan_p));

	pthread_mutex_init(&chan_p.lock,NULL);
	chan_p.psi_tables_filtering=PSI_TABLES_FILTERING_NONE;

	//sap announces variables
	sap_p_t sap_p;
	init_sap_v(&sap_p);

	//Statistics
	stats_infos_t stats_infos;
	init_stats_v(&stats_infos);

	//unicast
	//Parameters for HTTP unicast
	unicast_parameters_t unic_p;
	init_unicast_v(&unic_p);

	//multicast
	//multicast parameters
	multi_p_t multi_p;
	init_multicast_v(&multi_p);

	//tuning parameters
	tune_p_t tune_p;
	init_tune_v(&tune_p);
	card_tuned=&tune_p.card_tuned;

#ifdef ENABLE_CAM_SUPPORT
	//CAM (Conditionnal Access Modules : for scrambled channels)
	cam_p_t cam_p;
	init_cam_v(&cam_p);
	cam_p_t *cam_p_ptr=&cam_p;
#else
	void *cam_p_ptr=NULL;
#endif

#ifdef ENABLE_SCAM_SUPPORT
	//SCAM (software conditionnal Access Modules : for scrambled channels)
	scam_parameters_t scam_vars={
			.scam_support = 0,
			.getcwthread = 0,
			.getcwthread_shutdown = 0,
	};
	scam_vars.epfd = epoll_create(MAX_CHANNELS);
	scam_parameters_t *scam_vars_ptr=&scam_vars;
#else
	void *scam_vars_ptr=NULL;
#endif

	//autoconfiguration
	auto_p_t auto_p;
	init_aconf_v(&auto_p);

	//Parameters for rewriting
	rewrite_parameters_t rewrite_vars;
	init_rewr_v(&rewrite_vars);

	int no_daemon = 1;

	char filename_channels_not_streamed[DEFAULT_PATH_LEN];
	char filename_channels_streamed[DEFAULT_PATH_LEN];
	char filename_pid[DEFAULT_PATH_LEN]=PIDFILE_PATH;

	int server_id = 0; /** The server id for the template %server */

	int iRet;


	//MPEG2-TS reception and sort
	int pid;			/** pid of the current mpeg2 packet */
	int ScramblingControl;
	int continuity_counter;

	/** The buffer for the card */
	card_buffer_t card_buffer;
	memset (&card_buffer, 0, sizeof (card_buffer_t));
	card_buffer.dvr_buffer_size=DEFAULT_TS_BUFFER_SIZE;
	card_buffer.max_thread_buffer_size=DEFAULT_THREAD_BUFFER_SIZE;
	unsigned int t2mi_buf_size = 0;
	/** List of mandatory pids */
	uint8_t mandatory_pid[MAX_MANDATORY_PID_NUMBER];

	struct timeval tv;

	//files
	char *conf_filename = NULL;
	FILE *conf_file;
	FILE *channels_diff;
	FILE *channels_not_streamed;
#ifdef ENABLE_CAM_SUPPORT
	FILE *cam_info;
#endif
	FILE *pidfile;
	char *dump_filename = NULL;
	FILE *dump_file;


	int listingcards=0;
	//Getopt
	parse_cmd_line(argc,argv,
			&conf_filename,
			&tune_p,
			&stats_infos,
			&server_id,
			&no_daemon,
			&dump_filename,
			&listingcards);


	//List the detected cards
	if(listingcards)
	{
		print_info ();
		list_dvb_cards ();
		exit(0);
	}

	// DO NOT REMOVE (make MuMuDVB a deamon)
	if(!no_daemon)
		if(daemon(42,0))
		{
			log_message( log_module,  MSG_WARN, "Cannot daemonize: %s\n",
					strerror (errno));
			exit(666); //Right code for a bad daemon no ?
		}

	//If the user didn't defined a preferred logging way, and we daemonize, we set to syslog
	if (!no_daemon)
	{
		if(log_params.log_type==LOGGING_UNDEFINED)
		{
			openlog ("MUMUDVB", LOG_PID, 0);
			log_params.log_type=LOGGING_SYSLOG;
			log_params.syslog_initialised=1;
		}
	}

	//Display general information
	print_info ();


	// configuration file parsing
	int ichan = 0;
	int ipid = 0;
	int send_packet=0;
	char current_line[CONF_LINELEN];
	char *substring=NULL;
	char delimiteurs[] = CONFIG_FILE_SEPARATOR;
	/******************************************************/
	// config file displaying
	/******************************************************/
	ichan=-1;
	mumudvb_channel_t *c_chan;
	int curr_channel_old=-1;
if (conf_filename != NULL)
{
		//log_message( log_module,  MSG_ERROR, "No configuration file specified");
		//exit(ERROR_CONF_FILE);
//} else {
	conf_file = fopen (conf_filename, "r");
	if (conf_file == NULL)
	{
		log_message( log_module,  MSG_ERROR, "%s: %s\n",
				conf_filename, strerror (errno));
		free(conf_filename);
		exit(ERROR_CONF_FILE);
	}
	log_message( log_module, MSG_FLOOD,"==== Configuration file ====");
	int line_num=1;
	while (fgets (current_line, CONF_LINELEN, conf_file))
	{
		int line_len;
		//We suppress the end of line
		line_len=strlen(current_line);
		if(current_line[line_len-1]=='\r' ||current_line[line_len-1]=='\n')
			current_line[line_len-1]=0;
		log_message( log_module, MSG_FLOOD,"%03d %s\n",line_num,current_line);
		line_num++;
	}
	log_message( log_module, MSG_FLOOD,"============ done ===========\n");
	fclose (conf_file);
	/******************************************************/
	// config file reading
	/******************************************************/
	conf_file = fopen (conf_filename, "r");
	if (conf_file == NULL)
	{
		log_message( log_module,  MSG_ERROR, "%s: %s\n",
				conf_filename, strerror (errno));
		free(conf_filename);
		exit(ERROR_CONF_FILE);
	}
	// we scan config file
	// see doc/README_CONF* for further information
	int line_len;
	while (fgets (current_line, CONF_LINELEN, conf_file))
	{
		//We suppress the end of line (this can disturb atoi if there is spaces at the end of the line)
		//Thanks to Pierre Gronlier pierre.gronlier at gmail.com for finding that bug
		line_len=strlen(current_line);
		if(current_line[line_len-1]=='\r' ||current_line[line_len-1]=='\n')
			current_line[line_len-1]=0;

		//Line without "=" we continue
		if(strstr(current_line,"=")==NULL)
		{
			//We check if it's not a new_channel line
			substring = strtok (current_line, delimiteurs);
			//If nothing in the substring we avoid the segfault in the next line
			if(substring == NULL)
				continue;
			if(strcmp (substring, "new_channel") )
				continue;
		}
		//commentary
		if (current_line[0] == '#')
			continue;
		//We split the line
		substring = strtok (current_line, delimiteurs);

		//If nothing in the substring we avoid the segfault in the next line
		if(substring == NULL)
			continue;

		//commentary
		if (substring[0] == '#')
			continue;

		if(ichan<0)
			c_chan=NULL;
		else
			c_chan=&chan_p.channels[ichan];

		if((iRet=read_tuning_configuration(&tune_p, substring))) //Read the line concerning the tuning parameters
		{
			if(iRet==-1)
				exit(ERROR_CONF);
		}
		else if((iRet=read_autoconfiguration_configuration(&auto_p, substring))) //Read the line concerning the autoconfiguration parameters
		{
			if(iRet==-1)
				exit(ERROR_CONF);
		}
		else if((iRet=read_sap_configuration(&sap_p, c_chan, substring))) //Read the line concerning the sap parameters
		{
			if(iRet==-1)
				exit(ERROR_CONF);
		}
#ifdef ENABLE_CAM_SUPPORT
		else if((iRet=read_cam_configuration(&cam_p, c_chan, substring))) //Read the line concerning the cam parameters
		{
			if(iRet==-1)
				exit(ERROR_CONF);
		}
#endif
#ifdef ENABLE_SCAM_SUPPORT
		else if((iRet=read_scam_configuration(scam_vars_ptr, c_chan, substring))) //Read the line concerning the software cam parameters
		{
			if(iRet==-1)
				exit(ERROR_CONF);
		}
#endif
		else if((iRet=read_unicast_configuration(&unic_p, c_chan, substring))) //Read the line concerning the unicast parameters
		{
			if(iRet==-1)
				exit(ERROR_CONF);
		}
		else if((iRet=read_multicast_configuration(&multi_p, c_chan, substring))) //Read the line concerning the multicast parameters
		{
			if(iRet==-1)
				exit(ERROR_CONF);
		}
		else if((iRet=read_rewrite_configuration(&rewrite_vars, substring))) //Read the line concerning the rewrite parameters
		{
			if(iRet==-1)
				exit(ERROR_CONF);
		}
		else if((iRet=read_logging_configuration(&stats_infos, substring))) //Read the line concerning the logging parameters
		{
			if(iRet==-1)
				exit(ERROR_CONF);
		}
		else if (!strcmp (substring, "new_channel"))
		{
			ichan++;
			chan_p.channels[ichan].channel_ready=ALMOST_READY;
			log_message( log_module, MSG_INFO,"New channel, current number %d", ichan);
		}
		else if (!strcmp (substring, "timeout_no_diff"))
		{
			substring = strtok (NULL, delimiteurs);
			timeout_no_diff= atoi (substring);
		}
		else if (!strcmp (substring, "tuning_no_diff"))
		{
			substring = strtok (NULL, delimiteurs);
			tuning_no_diff= atoi (substring);
		}
		else if (!strcmp (substring, "dont_send_scrambled"))
		{
			substring = strtok (NULL, delimiteurs);
			dont_send_scrambled = atoi (substring);
		}
		else if (!strcmp (substring, "filter_transport_error"))
		{
			substring = strtok (NULL, delimiteurs);
			chan_p.filter_transport_error = atoi (substring);
		}
		else if (!strcmp (substring, "psi_tables_filtering"))
		{
			substring = strtok (NULL, delimiteurs);
			if (!strcmp (substring, "pat"))
				chan_p.psi_tables_filtering = PSI_TABLES_FILTERING_PAT_ONLY;
			else if (!strcmp (substring, "pat_cat"))
				chan_p.psi_tables_filtering = PSI_TABLES_FILTERING_PAT_CAT_ONLY;
			else if (!strcmp (substring, "none"))
				chan_p.psi_tables_filtering = PSI_TABLES_FILTERING_NONE;
			if (chan_p.psi_tables_filtering == PSI_TABLES_FILTERING_PAT_ONLY)
				log_message( log_module,  MSG_INFO, "You have enabled PSI tables filtering, only PAT will be send\n");
			if (chan_p.psi_tables_filtering == PSI_TABLES_FILTERING_PAT_CAT_ONLY)
				log_message( log_module,  MSG_INFO, "You have enabled PSI tables filtering, only PAT and CAT will be send\n");
		}
		else if (!strcmp (substring, "dvr_buffer_size"))
		{
			substring = strtok (NULL, delimiteurs);
			card_buffer.dvr_buffer_size = atoi (substring);
			if(card_buffer.dvr_buffer_size<=0)
			{
				log_message( log_module,  MSG_WARN,
						"The buffer size MUST be >0, forced to 1 packet\n");
				card_buffer.dvr_buffer_size = 1;
			}
			stats_infos.show_buffer_stats=1;
		}
		else if (!strcmp (substring, "dvr_thread"))
		{
			substring = strtok (NULL, delimiteurs);
			card_buffer.threaded_read = atoi (substring);
			if(card_buffer.threaded_read)
			{
				log_message( log_module,  MSG_WARN,
						"You want to use a thread for reading the card, please report bugs/problems\n");
			}
		}
		else if (!strcmp (substring, "dvr_thread_buffer_size"))
		{
			substring = strtok (NULL, delimiteurs);
			card_buffer.max_thread_buffer_size = atoi (substring);
		}
		else if ((!strcmp (substring, "service_id")) || (!strcmp (substring, "ts_id")))
		{
			if(!strcmp (substring, "ts_id"))
				log_message( log_module,  MSG_WARN, "The option ts_id is depreciated, use service_id instead.\n");
			if ( c_chan == NULL)
			{
				log_message( log_module,  MSG_ERROR,
						"service_id : You have to start a channel first (using new_channel)\n");
				exit(ERROR_CONF);
			}
			substring = strtok (NULL, delimiteurs);
			c_chan->service_id = atoi (substring);
		}
		else if (!strcmp (substring, "pids"))
		{
			ipid = 0;
			if ( c_chan == NULL)
			{
				log_message( log_module,  MSG_ERROR,
						"pids : You have to start a channel first (using new_channel)\n");
				exit(ERROR_CONF);
			}
			//Pids are now user set, they won't be overwritten by autoconfiguration
			c_chan->pid_i.pid_f=F_USER;
			//Enable PMT rewrite
			c_chan->pmt_rewrite = 1;
			while ((substring = strtok (NULL, delimiteurs)) != NULL)
			{
				c_chan->pid_i.pids[ipid] = atoi (substring);
				// we see if the given pid is good
				if (c_chan->pid_i.pids[ipid] < 10 || c_chan->pid_i.pids[ipid] >= 8193)
				{
					log_message( log_module,  MSG_ERROR,
							"Config issue : %s in pids, given pid : %d\n",
							conf_filename, c_chan->pid_i.pids[ipid]);
					exit(ERROR_CONF);
				}
				ipid++;
				if (ipid >= MAX_PIDS)
				{
					log_message( log_module,  MSG_ERROR,
							"Too many pids : %d channel : %d\n",
							ipid, ichan);
					exit(ERROR_CONF);
				}
			}
			c_chan->pid_i.num_pids = ipid;
		}
		else if (!strcmp (substring, "pmt_pid"))
		{
			if ( c_chan == NULL)
			{
				log_message( log_module,  MSG_ERROR,
						"pmt_pid : You have to start a channel first (using new_channel)\n");
				return -1;
			}
			substring = strtok (NULL, delimiteurs);
			c_chan->pid_i.pmt_pid = atoi (substring);
			if (c_chan->pid_i.pmt_pid < 10 || c_chan->pid_i.pmt_pid > 8191){
				log_message( log_module,  MSG_ERROR,
						"Configuration issue in pmt_pid, given PID : %d\n",
						c_chan->pid_i.pmt_pid);
				return -1;
			}
			MU_F(c_chan->pid_i.pmt_pid)=F_USER;
		}
		else if (!strcmp (substring, "name"))
		{
			if ( c_chan == NULL)
			{
				log_message( log_module,  MSG_ERROR,
						"name : You have to start a channel first (using new_channel)\n");
				exit(ERROR_CONF);
			}
			//name is now user set
			MU_F(c_chan->name)=F_USER;
			// other substring extraction method in order to keep spaces
			substring = strtok (NULL, "=");
			strncpy(c_chan->name,strtok(substring,"\n"),MAX_NAME_LEN-1);
			c_chan->name[MAX_NAME_LEN-1]='\0';
			//We store the user name for being able to use templates
			strncpy(c_chan->user_name,strtok(substring,"\n"),MAX_NAME_LEN-1);
			c_chan->user_name[MAX_NAME_LEN-1]='\0';
			if (strlen (substring) >= MAX_NAME_LEN - 1)
				log_message( log_module,  MSG_WARN,"Channel name too long\n");
		}
		else if (!strcmp (substring, "server_id"))
		{
			substring = strtok (NULL, delimiteurs);
			server_id = atoi (substring);
		}
		else if (!strcmp (substring, "filename_pid"))
		{
			substring = strtok (NULL, delimiteurs);
			if(strlen(substring)>=DEFAULT_PATH_LEN)
			{
				log_message(log_module,MSG_WARN,"filename_pid too long \n");
			}
			else
				strcpy(filename_pid,substring);
		}
		else if (!strcmp (substring, "check_cc"))
		{
			substring = strtok (NULL, delimiteurs);
			chan_p.check_cc = atoi (substring);
		}
		else if (!strcmp (substring, "t2mi_pid"))
		{
			substring = strtok (NULL, delimiteurs);
			chan_p.t2mi_pid = atoi (substring);
			log_message(log_module,MSG_INFO,"Demuxing T2-MI stream on pid %d as input\n", chan_p.t2mi_pid);
			if(chan_p.t2mi_pid < 1 || chan_p.t2mi_pid > 8192)
			{
				log_message(log_module,MSG_WARN,"wrong t2mi pid, forced to 4096\n");
				chan_p.t2mi_pid=4096;
			}
		}
		else if (!strcmp (substring, "t2mi_plp"))
		{
			substring = strtok (NULL, delimiteurs);
			chan_p.t2mi_plp = atoi (substring);
		}

		else
		{
			if(strlen (current_line) > 1)
				log_message( log_module,  MSG_WARN,
						"Config issue : unknow symbol : %s\n\n", substring);
			continue;
		}

		if (ichan > MAX_CHANNELS)
		{
			log_message( log_module,  MSG_ERROR, "Too many channels : %d limit : %d\n",
					ichan, MAX_CHANNELS);
			exit(ERROR_TOO_CHANNELS);
		}

		//A new channel have been defined
		if(curr_channel_old != ichan)
		{
			curr_channel_old = ichan;
		}
	}
	fclose (conf_file);
	free(conf_filename);
}

	//Set default card if not specified
	if(tune_p.card==-1)
		tune_p.card=0;


	/*************************************/
	//End of configuration file reading
	/*************************************/




	//if Autoconfiguration, we set other option default
	if(auto_p.autoconfiguration!=AUTOCONF_MODE_NONE)
	{
		if((sap_p.sap == OPTION_UNDEFINED) && (multi_p.multicast))
		{
			log_message( log_module,  MSG_INFO,
					"Autoconfiguration, we activate SAP announces. if you want to disable them see the README.\n");
			sap_p.sap=OPTION_ON;
		}
		if(rewrite_vars.rewrite_pat == OPTION_UNDEFINED)
		{
			rewrite_vars.rewrite_pat=OPTION_ON;
			log_message( log_module,  MSG_INFO,
					"Autoconfiguration, we activate PAT rewriting. if you want to disable it see the README.\n");
		}
		if(rewrite_vars.rewrite_sdt == OPTION_UNDEFINED)
		{
			rewrite_vars.rewrite_sdt=OPTION_ON;
			log_message( log_module,  MSG_INFO,
					"Autoconfiguration, we activate SDT rewriting. if you want to disable it see the README.\n");
		}
	}

	if(chan_p.t2mi_pid > 0 && card_buffer.dvr_buffer_size < 20)
	{
		log_message( log_module,  MSG_WARN,
				"Warning : You set a DVR buffer size too low to accept T2-MI frames, I increase your dvr_buffer_size to 20 ...\n");
		card_buffer.dvr_buffer_size=20;
	}

	if(card_buffer.max_thread_buffer_size<card_buffer.dvr_buffer_size)
	{
		log_message( log_module,  MSG_WARN,
				"Warning : You set a thread buffer size lower than your DVR buffer size, it's not possible to use such values. I increase your dvr_thread_buffer_size ...\n");
		card_buffer.max_thread_buffer_size=card_buffer.dvr_buffer_size;
	}



	//Template for the card dev path
	char number[10];
	sprintf(number,"%d",tune_p.card);
	int l=sizeof(tune_p.card_dev_path);
	mumu_string_replace(tune_p.card_dev_path,&l,0,"%card",number);

	//If we specified a string for the unicast port out, we parse it
	if(unic_p.portOut_str!=NULL)
	{
		int len;
		len=strlen(unic_p.portOut_str)+1;
		sprintf(number,"%d",tune_p.card);
		unic_p.portOut_str=mumu_string_replace(unic_p.portOut_str,&len,1,"%card",number);
		sprintf(number,"%d",tune_p.tuner);
		unic_p.portOut_str=mumu_string_replace(unic_p.portOut_str,&len,1,"%tuner",number);
		sprintf(number,"%d",server_id);
		unic_p.portOut_str=mumu_string_replace(unic_p.portOut_str,&len,1,"%server",number);
		unic_p.portOut=string_comput(unic_p.portOut_str);
		log_message( "Unicast: ", MSG_DEBUG, "computed unicast master port : %d\n",unic_p.portOut);
	}

	if(log_params.log_file_path!=NULL)
	{
		int len;
		len=strlen(log_params.log_file_path)+1;
		sprintf(number,"%d",tune_p.card);
		log_params.log_file_path=mumu_string_replace(log_params.log_file_path,&len,1,"%card",number);
		sprintf(number,"%d",tune_p.tuner);
		log_params.log_file_path=mumu_string_replace(log_params.log_file_path,&len,1,"%tuner",number);
		sprintf(number,"%d",server_id);
		log_params.log_file_path=mumu_string_replace(log_params.log_file_path,&len,1,"%server",number);
		log_params.log_file = fopen (log_params.log_file_path, "a");
		if (log_params.log_file)
			log_params.log_type |= LOGGING_FILE;
		else
			log_message(log_module,MSG_WARN,"Cannot open log file %s: %s\n", substring, strerror (errno));
	}
	/******************************************************/
	//end of config file reading
	/******************************************************/

	// Show in log that we are starting
	log_message( log_module,  MSG_INFO,"========== End of configuration, MuMuDVB version %s is starting ==========",VERSION);

	// + 1 Because of the new syntax
	pthread_mutex_lock(&chan_p.lock);
	chan_p.number_of_channels = ichan+1;
	pthread_mutex_unlock(&chan_p.lock);

	//We disable things depending on multicast if multicast is suppressed
	if(!multi_p.ttl)
	{
		log_message( log_module,  MSG_INFO, "The multicast TTL is set to 0, multicast will be disabled.\n");
		multi_p.multicast=0;
	}
	if(!multi_p.multicast)
	{
		if(multi_p.rtp_header)
		{
			multi_p.rtp_header=0;
			log_message( log_module,  MSG_INFO, "NO Multicast, RTP Header is disabled.\n");
		}
		if(sap_p.sap==OPTION_ON)
		{
			log_message( log_module,  MSG_INFO, "NO Multicast, SAP announces are disabled.\n");
			sap_p.sap=OPTION_OFF;
		}
	}



	if(!multi_p.multicast && !unic_p.unicast)
	{
		log_message( log_module,  MSG_ERROR, "NO Multicast AND NO unicast. No data can be send :(, Exciting ....\n");
		set_interrupted(ERROR_CONF<<8);
		goto mumudvb_close_goto;
	}



	// we clear them by paranoia
	sprintf (filename_channels_streamed, STREAMED_LIST_PATH,
			tune_p.card, tune_p.tuner);
	sprintf (filename_channels_not_streamed, NOT_STREAMED_LIST_PATH,
			tune_p.card, tune_p.tuner);
#ifdef ENABLE_CAM_SUPPORT
	sprintf (cam_p.filename_cam_info, CAM_INFO_LIST_PATH,
			tune_p.card, tune_p.tuner);
#endif
	channels_diff = fopen (filename_channels_streamed, "w");
	if (channels_diff == NULL)
	{
		write_streamed_channels=0;
		log_message( log_module,  MSG_WARN,
				"Can't create %s: %s\n",
				filename_channels_streamed, strerror (errno));
	}
	else
		fclose (channels_diff);

	channels_not_streamed = fopen (filename_channels_not_streamed, "w");
	if (channels_not_streamed == NULL)
	{
		write_streamed_channels=0;
		log_message( log_module,  MSG_WARN,
				"Can't create %s: %s\n",
				filename_channels_not_streamed, strerror (errno));
	}
	else
		fclose (channels_not_streamed);


#ifdef ENABLE_CAM_SUPPORT
	if(cam_p.cam_support)
	{
		cam_info = fopen (cam_p.filename_cam_info, "w");
		if (cam_info == NULL)
		{
			log_message( log_module,  MSG_WARN,
					"Can't create %s: %s\n",
					cam_p.filename_cam_info, strerror (errno));
		}
		else
			fclose (cam_info);
	}
#endif


	log_message( log_module,  MSG_INFO, "Streaming. Freq %f\n",
			tune_p.freq);


	/******************************************************/
	// Card tuning
	/******************************************************/
	if (signal (SIGALRM, SignalHandler) == SIG_IGN)
		signal (SIGALRM, SIG_IGN);
	if (signal (SIGUSR1, SignalHandler) == SIG_IGN)
		signal (SIGUSR1, SIG_IGN);
	if (signal (SIGUSR2, SignalHandler) == SIG_IGN)
		signal (SIGUSR2, SIG_IGN);
	if (signal (SIGHUP, SignalHandler) == SIG_IGN)
		signal (SIGHUP, SIG_IGN);
	// alarm for tuning timeout
	if(tune_p.tuning_timeout)
	{
		alarm (tune_p.tuning_timeout);
	}


	// We tune the card
	iRet =-1;


	if(strlen(tune_p.read_file_path))
	{
		log_message( log_module,  MSG_DEBUG,
				"Opening source file %s", tune_p.read_file_path);

		iRet = open_fe (&fds.fd_frontend, tune_p.read_file_path, tune_p.tuner,1,1);
	}
	else
		iRet = open_fe (&fds.fd_frontend, tune_p.card_dev_path, tune_p.tuner,1,0);
	if (iRet>0)
	{

		/*****************************************************/
		//daemon part two, we write our PID as we are tuned
		/*****************************************************/

		// We write our pid in a file if we deamonize
		if (!no_daemon)
		{
			int len;
			len=DEFAULT_PATH_LEN;
			sprintf(number,"%d",tune_p.card);
			mumu_string_replace(filename_pid,&len,0,"%card",number);
			sprintf(number,"%d",tune_p.tuner);
			mumu_string_replace(filename_pid,&len,0,"%tuner",number);
			sprintf(number,"%d",server_id);
			mumu_string_replace(filename_pid,&len,0,"%server",number);;
			log_message( log_module, MSG_INFO, "The pid will be written in %s", filename_pid);
			pidfile = fopen (filename_pid, "w");
			if (pidfile == NULL)
			{
				log_message( log_module,  MSG_INFO,"%s: %s\n",
						filename_pid, strerror (errno));
				exit(ERROR_CREATE_FILE);
			}
			fprintf (pidfile, "%d\n", getpid ());
			fclose (pidfile);
		}

		if(strlen(tune_p.read_file_path))
			iRet = 1; //no tuning if file input
		else
			iRet =
				tune_it (fds.fd_frontend, &tune_p);
	}
	else
		iRet =-1;

	if (iRet < 0)
	{
		log_message( log_module,  MSG_INFO, "Tuning issue, card %d\n", tune_p.card);
		// we close the file descriptors
		close_card_fd(&fds);
		set_interrupted(ERROR_TUNE<<8);
		goto mumudvb_close_goto;
	}
	log_message( log_module,  MSG_INFO, "Card %d, tuner %d tuned\n", tune_p.card, tune_p.tuner);
	tune_p.card_tuned = 1;

		close_card_fd(&fds);
		log_message( log_module, MSG_INFO, "The pid will be written in %s", filename_pid);


		goto mumudvb_close_goto;

	mumudvb_close_goto:
	//If the thread is not started, we don't send the nonexistent address of monitor_thread_params
	return mumudvb_close(no_daemon,
					NULL,
					&rewrite_vars,
					&auto_p,
					&unic_p,
					&tune_p.strengththreadshutdown,
					cam_p_ptr,
					scam_vars_ptr,
					filename_channels_not_streamed,
					filename_channels_streamed,
					filename_pid,
					get_interrupted(),
					&chan_p,
					&signalpowerthread,
					&monitorthread,
					&cardthreadparams,
					&fds,
					&card_buffer);
}







/******************************************************
 * Signal Handler Function
 *
 * This function is called periodically
 *  It checks for the tuning timeouts
 *
 * This function also catches SIGPIPE, SIGUSR1, SIGUSR2 and SIGHUP
 *
 ******************************************************/
static void SignalHandler (int signum)
{
	if (signum == SIGALRM && !get_interrupted())
	{
		if (card_tuned && !*card_tuned)
		{
			log_message( log_module,  MSG_INFO,
					"Card not tuned after timeout - exiting\n");
			exit(ERROR_TUNE);
		}
	}
	else if (signum == SIGUSR1 || signum == SIGUSR2 || signum == SIGHUP)
	{
		received_signal=signum;
	}
	else if (signum != SIGPIPE)
	{
		log_message( log_module,  MSG_ERROR, "Caught signal %d", signum);
		set_interrupted(signum);
	}
	signal (signum, SignalHandler);
}




