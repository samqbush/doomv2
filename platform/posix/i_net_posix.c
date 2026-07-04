// Phase 5 modernization: portable POSIX BSD-sockets network transport.
//
// Replaces the dead 1997 linuxdoom-1.10/i_net.c behind the unchanged i_net.h
// contract. Scope (decided): Linux + macOS only, raw POSIX sockets, no SDL_net
// and no new dependencies. Windows/Winsock stays a future consideration.
//
// What changed vs the 1997 original (and WHY):
//   * gethostbyname()/GetLocalAddress()  -> getaddrinfo() (thread-safe, the
//     legacy resolvers are removed/deprecated on modern libc).
//   * ioctl(FIONBIO)                     -> fcntl(O_NONBLOCK).
//   * Added SO_REUSEADDR                 -> lets two nodes on one host rebind
//     cleanly (loopback consistency test).
//   * Peer addressing now supports "host:port"  -> two processes on 127.0.0.1
//     need DISTINCT destination ports; the original used a single shared
//     -port for BOTH local bind and every peer, so same-host peers would send
//     to themselves. -port remains the LOCAL bind port; each peer may carry its
//     own :port (defaults to -port when omitted).
//   * PacketGet matches a peer by source IP *and* port  -> on one host, IP
//     alone can't tell two nodes apart (and could match our own loopback echo).
//   * Receive-side validation of numtics / datalength    -> a malformed or
//     short datagram can no longer make d_net.c decode past the valid command
//     count on a 64-bit host.
//
// The wire packet is doomdata_t (see PacketSend/PacketGet), NOT doomcom_t, so
// doomcom_t's `long id` (8 bytes on LP64) never crosses the wire and is not a
// packing concern. Multi-byte wire fields (checksum, angleturn, consistancy)
// are byte-swapped explicitly; single-byte fields are copied verbatim, so the
// format is endian-stable across peers.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>

#include "i_system.h"
#include "d_event.h"
#include "d_net.h"
#include "m_argv.h"

#include "doomstat.h"

#include "i_net.h"

// ---- 64-bit wire-format guards --------------------------------------------
//
// d_net.c computes packet length and the checksum window from struct offsets
// (NetbufferSize / NetbufferChecksum). If padding ever creeps into ticcmd_t or
// doomdata_t on a 64-bit target, those offsets drift silently and peers desync.
// Freeze the layout at compile time instead.
_Static_assert(sizeof(ticcmd_t) == 8, "ticcmd_t must be 8 bytes on the wire");
_Static_assert(offsetof(doomdata_t, cmds) == 8,
               "doomdata_t header must be 8 bytes (checksum + 4 bytes)");
_Static_assert(sizeof(doomdata_t) == 8 + 8 * BACKUPTICS,
               "doomdata_t must be tightly packed");
_Static_assert(offsetof(doomdata_t, retransmitfrom) == 4,
               "checksum window in d_net.c starts at retransmitfrom (offset 4)");

// The original i_net.c redefined ntoh*/hton* by hand "for some odd reason"; the
// system implementations are correct and available, so just use them.

void	NetSend (void);
boolean NetListen (void);


//
// NETWORKING
//

int	DOOMPORT =	(IPPORT_USERRESERVED +0x1d );

int			sendsocket;
int			insocket;

struct	sockaddr_in	sendaddress[MAXNETNODES];

void	(*netget) (void);
void	(*netsend) (void);


//
// UDPsocket
//
int UDPsocket (void)
{
    int	s;

    // allocate a socket
    s = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s<0)
	I_Error ("can't create socket: %s",strerror(errno));

    return s;
}

//
// BindToLocalPort
//
void
BindToLocalPort
( int	s,
  int	port )
{
    int			v;
    int			on = 1;
    struct sockaddr_in	address;

    // Allow two nodes on one host (loopback test) to rebind without waiting
    // out TIME_WAIT.
    setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset (&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = port;

    v = bind (s, (void *)&address, sizeof(address));
    if (v == -1)
	I_Error ("BindToPort: bind: %s", strerror(errno));
}


//
// PacketSend
//
void PacketSend (void)
{
    ssize_t	c;
    doomdata_t	sw;

    // byte swap
    sw.checksum = htonl(netbuffer->checksum);
    sw.player = netbuffer->player;
    sw.retransmitfrom = netbuffer->retransmitfrom;
    sw.starttic = netbuffer->starttic;
    sw.numtics = netbuffer->numtics;
    for (c=0 ; c< netbuffer->numtics ; c++)
    {
	sw.cmds[c].forwardmove = netbuffer->cmds[c].forwardmove;
	sw.cmds[c].sidemove = netbuffer->cmds[c].sidemove;
	sw.cmds[c].angleturn = htons(netbuffer->cmds[c].angleturn);
	sw.cmds[c].consistancy = htons(netbuffer->cmds[c].consistancy);
	sw.cmds[c].chatchar = netbuffer->cmds[c].chatchar;
	sw.cmds[c].buttons = netbuffer->cmds[c].buttons;
    }

    c = sendto (sendsocket , &sw, doomcom->datalength
		,0,(void *)&sendaddress[doomcom->remotenode]
		,sizeof(sendaddress[doomcom->remotenode]));

    //	if (c == -1)
    //		I_Error ("SendPacket error: %s",strerror(errno));
    (void)c;
}


//
// PacketGet
//
void PacketGet (void)
{
    int			i;
    int			c;
    int			expected;
    struct sockaddr_in	fromaddress;
    socklen_t		fromlen;
    doomdata_t		sw;

    fromlen = sizeof(fromaddress);
    c = (int)recvfrom (insocket, &sw, sizeof(sw), 0
		  , (struct sockaddr *)&fromaddress, &fromlen );
    if (c == -1 )
    {
	if (errno != EWOULDBLOCK && errno != EAGAIN)
	    I_Error ("GetPacket: %s",strerror(errno));
	doomcom->remotenode = -1;		// no packet
	return;
    }

    // Reject anything too short to even carry the header, so the numtics/length
    // checks below read valid memory.
    if (c < (int)offsetof(doomdata_t, cmds))
    {
	doomcom->remotenode = -1;
	return;
    }

    // find remote node number. On a single host two nodes share 127.0.0.1, so
    // the source PORT is what distinguishes them (and rejects our own echo).
    for (i=0 ; i<doomcom->numnodes ; i++)
	if ( fromaddress.sin_addr.s_addr == sendaddress[i].sin_addr.s_addr
	     && fromaddress.sin_port == sendaddress[i].sin_port )
	    break;

    if (i == doomcom->numnodes)
    {
	// packet is not from one of the players (new game broadcast)
	doomcom->remotenode = -1;		// no packet
	return;
    }

    // Validate the tic count and datagram length before decoding, so a
    // malformed packet cannot drive the decode loop past the received bytes.
    if (sw.numtics > BACKUPTICS)
    {
	doomcom->remotenode = -1;
	return;
    }
    expected = (int)offsetof(doomdata_t, cmds) + sw.numtics * (int)sizeof(ticcmd_t);
    if (c != expected)
    {
	doomcom->remotenode = -1;
	return;
    }

    doomcom->remotenode = i;			// good packet from a game player
    doomcom->datalength = c;

    // byte swap
    netbuffer->checksum = ntohl(sw.checksum);
    netbuffer->player = sw.player;
    netbuffer->retransmitfrom = sw.retransmitfrom;
    netbuffer->starttic = sw.starttic;
    netbuffer->numtics = sw.numtics;

    for (c=0 ; c< netbuffer->numtics ; c++)
    {
	netbuffer->cmds[c].forwardmove = sw.cmds[c].forwardmove;
	netbuffer->cmds[c].sidemove = sw.cmds[c].sidemove;
	netbuffer->cmds[c].angleturn = ntohs(sw.cmds[c].angleturn);
	netbuffer->cmds[c].consistancy = ntohs(sw.cmds[c].consistancy);
	netbuffer->cmds[c].chatchar = sw.cmds[c].chatchar;
	netbuffer->cmds[c].buttons = sw.cmds[c].buttons;
    }
}


//
// ResolvePeer
// Fills a sockaddr_in from a "host", ".dotted-ip", or "host:port" / ".ip:port"
// spec. When no :port is given, defaults to DOOMPORT.
//
static void ResolvePeer (const char* spec, struct sockaddr_in* out)
{
    char	host[256];
    int		port = DOOMPORT;
    const char*	colon;
    size_t	hostlen;

    // Split optional ":port".
    colon = strrchr (spec, ':');
    if (colon)
    {
	port = atoi (colon + 1);
	if (port <= 0 || port > 65535)
	    I_Error ("I_InitNetwork: bad peer port in '%s'", spec);
	hostlen = (size_t)(colon - spec);
    }
    else
    {
	hostlen = strlen (spec);
    }
    if (hostlen == 0 || hostlen >= sizeof(host))
	I_Error ("I_InitNetwork: bad peer host in '%s'", spec);
    memcpy (host, spec, hostlen);
    host[hostlen] = '\0';

    memset (out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons ((unsigned short)port);

    if (host[0] == '.')
    {
	// Legacy shorthand: ".1.2.3.4" is a raw dotted IP.
	if (inet_pton (AF_INET, host + 1, &out->sin_addr) != 1)
	    I_Error ("I_InitNetwork: bad dotted IP in '%s'", spec);
    }
    else
    {
	struct addrinfo	hints;
	struct addrinfo* res;
	int		rc;

	memset (&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	rc = getaddrinfo (host, NULL, &hints, &res);
	if (rc != 0 || !res)
	    I_Error ("I_InitNetwork: couldn't resolve %s: %s",
		     host, gai_strerror (rc));
	out->sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
	freeaddrinfo (res);
    }
}


//
// I_InitNetwork
//
void I_InitNetwork (void)
{
    int			i;
    int			p;

    doomcom = malloc (sizeof (*doomcom) );
    memset (doomcom, 0, sizeof(*doomcom) );

    // set up for network
    i = M_CheckParm ("-dup");
    if (i && i< myargc-1)
    {
	doomcom->ticdup = myargv[i+1][0]-'0';
	if (doomcom->ticdup < 1)
	    doomcom->ticdup = 1;
	if (doomcom->ticdup > 9)
	    doomcom->ticdup = 9;
    }
    else
	doomcom-> ticdup = 1;

    if (M_CheckParm ("-extratic"))
	doomcom-> extratics = 1;
    else
	doomcom-> extratics = 0;

    p = M_CheckParm ("-port");
    if (p && p<myargc-1)
    {
	DOOMPORT = atoi (myargv[p+1]);
	printf ("using alternate port %i\n",DOOMPORT);
    }

    // parse network game options,
    //  -net <consoleplayer> <host> <host> ...
    i = M_CheckParm ("-net");
    if (!i)
    {
	// single player game
	netgame = false;
	doomcom->id = DOOMCOM_ID;
	doomcom->numplayers = doomcom->numnodes = 1;
	doomcom->deathmatch = false;
	doomcom->consoleplayer = 0;
	return;
    }

    netsend = PacketSend;
    netget = PacketGet;
    netgame = true;

    // parse player number and host list
    if (i >= myargc - 1)
	I_Error ("I_InitNetwork: -net requires <consoleplayer> <host> ...");

    p = myargv[i+1][0] - '1';
    if (p < 0 || p >= MAXPLAYERS)
	I_Error ("I_InitNetwork: bad -net consoleplayer '%s'", myargv[i+1]);
    doomcom->consoleplayer = p;

    doomcom->numnodes = 1;	// this node for sure

    i++;
    while (++i < myargc && myargv[i][0] != '-')
    {
	if (doomcom->numnodes >= MAXNETNODES)
	    I_Error ("I_InitNetwork: too many -net hosts (max %d)",
		     MAXNETNODES);
	ResolvePeer (myargv[i], &sendaddress[doomcom->numnodes]);
	doomcom->numnodes++;
    }

    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes;

    // build message to receive
    insocket = UDPsocket ();
    BindToLocalPort (insocket,htons(DOOMPORT));
    if (fcntl (insocket, F_SETFL, O_NONBLOCK) == -1)
	I_Error ("I_InitNetwork: fcntl O_NONBLOCK: %s", strerror(errno));

    // Send from the SAME bound socket, so our datagrams' source port equals our
    // bound port -- which is exactly what peers hold in sendaddress[] and match
    // on (IP+port). The 1997 original sent from a separate unbound socket with
    // an ephemeral source port and matched peers by IP alone; that can't tell
    // two nodes on one host apart, so it does not work for the loopback test.
    sendsocket = insocket;
}


void I_NetCmd (void)
{
    if (doomcom->command == CMD_SEND)
    {
	netsend ();
    }
    else if (doomcom->command == CMD_GET)
    {
	netget ();
    }
    else
	I_Error ("Bad net cmd: %i\n",doomcom->command);
}
