/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _process_h
#define _process_h

#include <glib.h>
#include <glib-2.0/glib.h>

#include "socket.h"
#include "socketset.h"
#include "sim.h"
#include "dsim_utils.h"
#include "timer.h"
#include "vector.h"

typedef struct {
	gint in_worker;
	pipecloud_tp pipecloud;
	guint process_id;
	guint total_workers;
} dvn_global_worker_data_t ;

typedef struct dvninstance_master_t {
	/** nonzero if we are to run in daemon mode */
	gint is_daemon_mode;

	/** simulation master logic */
	sim_master_tp sim_master;

	/** listen socket for incoming controller connections - daemon master mode only */
	socket_tp controller_sock;

	/** vector of any connected controllers - daemon master mode only */
	vector_tp controller_sockets;

} dvninstance_master_t, * dvninstance_master_tp;

typedef struct dvninstance_slave_connection_t {
	socket_tp sock;
	gint id;
} dvninstance_slave_connection_t, * dvninstance_slave_connection_tp ;

typedef struct dvninstance_slave_t {
	/** listen socket for connections from master or other slaves - daemon slave mode only */
	socket_tp slave_sock;

	/** simulation slave logic */
	sim_slave_tp sim_slave;

	/** pipecloud for IPC */
	pipecloud_tp pipecloud;

	/** by-ID mapping of sockets to for remote slaves - daemon master/slave mode only */
	GHashTable *slave_connection_lookup;
	vector_tp slave_connections;

	guint num_processes;
	gint * worker_process_ids;

} dvninstance_slave_t, * dvninstance_slave_tp;

typedef struct dvninstance_t {
	/** dvn master  */
	dvninstance_master_tp master;

	/** dvn slave */
	dvninstance_slave_tp slave;

	/** socketset for all watched sockets */
	socketset_tp socketset;

	/** DVN instance ID for this instance - typically, one ID per machine */
	gint my_instid;

	/** number of connected slaves */
	guint num_active_slaves;

	/** becomes nonzero if DVN should end */
	gint ending;
} dvninstance_t, * dvninstance_tp;

struct DVN_CONFIG {
	/** mode to run in */
	enum { dvn_mode_normal, dvn_mode_master, dvn_mode_slave } dvn_mode;

	/** port to listen on for incoming controller connections (master mode) */
	guint controller_listen_port;

	/** port to listen on for incoming slave connections */
	guint slave_listen_port;

	/** total worker processes to spawn - at least one */
	guint num_processes;

	/** nonzero to dump version and quit */
	gchar version;

	/** nonzero if we should go detach to background (daemon) */
	gchar background;

	/** DSIM file path to load for 'normal_mode' operation */
	gchar dsim_file[200];

	/** config file path to load */
	gchar config_file[200];

	/** nonzero if we should dump our config file and quit */
	gint config_dump;

	/** for 'normal_mode', array of log destination configuration strings (per-channel) */
	gchar log_destinations[10][256];
};

gint dvn_controller_process (dvninstance_tp dvn, socket_tp sock);

gint dvn_worker_main (guint process_id, guint total_workers, pipecloud_tp pipecloud);
void dvn_master_heartbeat (dvninstance_tp dvn);
void dvn_slave_deposit (dvninstance_tp dvn, nbdf_tp net_frame);
gint dvn_slave_socketprocess(dvninstance_tp dvn, dvninstance_slave_connection_tp slave_connection);
void dvn_slave_heartbeat (dvninstance_tp dvn);
dvninstance_master_tp dvn_create_master (gint is_daemon, guint controller_port, socketset_tp socketset);
void dvn_destroy_master (dvninstance_master_tp master);
dvninstance_slave_tp dvn_create_slave (gint daemon, guint num_processes, guint slave_listen_port, socketset_tp socketset);
void dvn_destroy_slave (dvninstance_slave_tp slave);
dvninstance_tp dvn_create_instance (struct DVN_CONFIG * config);
void dvn_destroy_instance (dvninstance_tp dvn);

gint dvn_main (struct DVN_CONFIG * config);

//void dvn_packet_write(socket_tp socket, guchar dest_type, guchar dest_layer, gint dest_major, gint frametype, nbdf_tp frame);
///void dvn_packet_route(guchar dest_type, guchar dest_layer, gint dest_major, gint frametype, nbdf_tp frame);

#endif

