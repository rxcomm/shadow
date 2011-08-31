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

#ifndef _socket_h
#define _socket_h

#include <glib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SOCKET_OK 				1
#define SOCKET_NOTENOUGH 		2

#define SOCKET_OPTION_TCP 		1
#define SOCKET_OPTION_NONBLOCK	2
#define SOCKET_OPTION_UDP		4

#define SOCKET_STATE_IDLE 		0
#define SOCKET_STATE_LISTEN		1
#define SOCKET_STATE_CONNECTED	2
#define SOCKET_STATE_DEAD		3

#define SOCKET_DEFAULT_BLOCKSIZE 16384

#ifndef O_ASYNC
#define O_ASYNC O_NONBLOCK
#endif

/*#ifndef O_NONBLOCK
#error "sockets require O_NONBLOCK"
#endif*/

struct SOCKET_PACKETSAVE {
	struct sockaddr_in remoteaddr;
	guint size;
	gchar * data;
};

struct SOCKET_BUFFERLINK {
	guint offset;
	guint sock_offset;
	struct SOCKET_BUFFERLINK * next;

	gchar buffer[SOCKET_DEFAULT_BLOCKSIZE];
};

typedef struct socket_t {
	gint sock;
	struct sockaddr_in remoteaddr;
	guint block_size;

	guint total_incoming_size;
	guint total_outgoing_size;

	/* tcp buffers */
	struct SOCKET_BUFFERLINK * incoming_buffer, * incoming_buffer_end;
	struct SOCKET_BUFFERLINK * outgoing_buffer, * outgoing_buffer_end;

	/* datagram buffers (unused at the moment) */
	struct SOCKET_PACKETSAVE * incoming_buffer_d, * outgoing_buffer_d;
	guint incoming_buffer_d_allocated, incoming_buffer_d_size;
	guint outgoing_buffer_d_allocated, outgoing_buffer_d_size;

	gint options;
	gint state;
} socket_t, * socket_tp;


/* some simple shortcut macros.. */

/*#ifndef DEBUG
#define socket_isvalid(s) (s!=NULL && s->sock != -1)
#define socket_data_outgoing(s) (s->total_outgoing_size)
#define socket_data_incoming(s) (s->total_incoming_size)
#define socket_getfd(s) (s->sock)
#define socket_gethost(s) (inet_ntoa(s->remoteaddr.sin_addr))
#define socket_getport(s) (ntohs(s->remoteaddr.sin_port))
#define socket_islisten(s) (s->state & SOCKET_STATE_LISTEN)
#define socket_set_blocksize(s,bsize) s->block_size = bsize
#else*/

/** @returns true if the given socket is valid and connected */
gint socket_isvalid(socket_tp s);

/** @returns true if the given socket has waiting outgoing data */
size_t socket_data_outgoing(socket_tp s) ;

/** @returns true if the given socket has waiting incoming data */
size_t socket_data_incoming(socket_tp s) ;

/* returns the actual FD associated with this socket */
gint socket_getfd(socket_tp s) ;

gchar * socket_gethost(socket_tp s) ;

short socket_getport(socket_tp s) ;

gint socket_islisten(socket_tp s);

/* sets the ginternal blocksize of this socket. defaults to 16k */
void socket_set_blocksize(socket_tp s,size_t bsize);

///#endif


/* returns nonzero when a socket needs servicing! */
gint socket_needs_servicing(void);
void socket_enable_async(void);
void socket_ignore_sigpipe(void);
void socket_sigio_handler(gint);
gint socket_needs_servicing(void);
void socket_reset_servicing_status(void);

/* Creates a socket with the given options. */
socket_tp socket_create (gint socket_options) ;
socket_tp socket_create_from(gint fd, gint socket_options);

/* Creates a socket by accepting a waiting incoming connection from <mommy> with the given options */
socket_tp socket_create_child ( socket_tp mommy, gint socket_options );

/* Closes a socket */
void socket_close (socket_tp s);

/* Destroys a socket */
void socket_destroy (socket_tp s);

/* Make this socket start listening on the given port */
gint socket_listen (socket_tp s, gint port, gint waiting_size);

/* Connect to a remote host */
gint socket_connect (socket_tp s, gchar * dest_addr, gint port);

/* Issue a read event to the socket. This will cause it to collect and buffer all waiting data from the kernel */
gint socket_issue_read (socket_tp s);

/* Issue a write event to the socket. This should happen when the socket has indicated it has waiting data, and
 * there has been an event that indicates the socket is now ready to write to. */
gint socket_issue_write (socket_tp s);

/* Reads <size> bytes ginto <buffer> from <s>. If <size> bytes are not available, the function will fail and return zero.
 * Otherwise, it will return 1 on success. Bytes are removed from socket buffer. */
gint socket_read (socket_tp s, gchar * buffer, guint size);

/* Reads <size> bytes ginto <buffer> from <s>. If <size> bytes are not available, the function will fail and return zero.
 * Otherwise, it will return 1 on success. Bytes are *not* removed from socket buffer. */
gint socket_peek (socket_tp s, gchar * buffer, guint size);

/* Writes the given bytes to the socket. */
gint socket_write (socket_tp s, gchar * buffer, guint size);

/* unimplemented */
gint socket_write_to (socket_tp s, gchar * dest_addr, gint dest_port, gchar * buffer, guint size);

/* Sets this socket as nonblocking (obviously!) */
void socket_set_nonblock(socket_tp s);


#endif
