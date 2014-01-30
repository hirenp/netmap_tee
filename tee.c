/*
 * (C) 2011-2014 Luigi Rizzo, Matteo Landi
 *
 * BSD license
 *
 * A netmap client to bridge two network interfaces
 * (or one interface and the host stack).
 *
 * $FreeBSD: $
 */

#include "nm_util.h"


int verbose = 1;

char *version = "$Id$";

static int do_abort = 0;

static void
sigint_h(int sig)
{
	(void)sig;	/* UNUSED */
	do_abort = 1;
	signal(SIGINT, SIG_DFL);
}


/*
 * Move up to 'limit' pkts from rxring of src to txring of dest
 * swapping buffers.
 */
static int
process_rings(struct netmap_ring *rxring, struct netmap_ring *txring,
	      u_int limit, const char *msg)
{
	u_int j, k, m = 0;

	/* print a warning if any of the ring flags is set (e.g. NM_REINIT) */
	if (rxring->flags || txring->flags)
		D("%s rxflags %x txflags %x",
			msg, rxring->flags, txring->flags);
	j = rxring->cur; /* RX */
	k = txring->cur; /* TX */
	m = nm_ring_space(rxring);
	if (m < limit)
		limit = m;
	m = nm_ring_space(txring);
	if (m < limit)
		limit = m;
	m = limit;
	while (limit-- > 0) {
		struct netmap_slot *rs = &rxring->slot[j];
		struct netmap_slot *ts = &txring->slot[k];
		char *rxbuf = NETMAP_BUF(rxring, rs->buf_idx);
		char *txbuf = NETMAP_BUF(txring, ts->buf_idx);

		/* swap packets */
		if (ts->buf_idx < 2 || rs->buf_idx < 2) {
			D("wrong index rx[%d] = %d  -> tx[%d] = %d",
				j, rs->buf_idx, k, ts->buf_idx);
			sleep(2);
		}

		/* copy the packet length. */
		if (rs->len < 14 || rs->len > 2048)
			D("wrong len %d rx[%d] -> tx[%d]", rs->len, j, k);
		else if (verbose > 1)
			D("%s send len %d rx[%d] -> tx[%d]", msg, rs->len, j, k);
		ts->len = rs->len;

		pkt_copy(rxbuf, txbuf, ts->len);
		j = nm_ring_next(rxring, j);
		k = nm_ring_next(txring, k);
	}
	rxring->head = rxring->cur = j;
	txring->head = txring->cur = k;
	if (verbose && m > 0)
		D("%s sent %d packets to %p", msg, m, txring);

	return (m);
}

/* move packts from src to destination */
static int
move(struct nm_desc_t *src, struct nm_desc_t *dst, u_int limit)
{
	struct netmap_ring *txring, *rxring;
	u_int m = 0, si = src->first_rx_ring, di = dst->first_tx_ring;
	const char *msg = (src->req.nr_ringid & NETMAP_SW_RING) ?
		"host->net" : "net->host";

	while (si <= src->last_rx_ring && di <= dst->last_tx_ring) {
		rxring = src->rx + si;
		txring = dst->tx + di;
		ND("txring %p rxring %p", txring, rxring);
		if (nm_ring_empty(rxring)) {
			si++;
			continue;
		}
		if (nm_ring_empty(txring)) {
			di++;
			continue;
		}
		m += process_rings(rxring, txring, limit, msg);
	}

	return (m);
}


static void
usage(void)
{
	fprintf(stderr,
	    //"usage: tee [-v] -s ifs -d ifd\n");
	    "usage: tee [-v] -s ifs -d1 ifd1 \n");
	exit(1);
}

/*
 * tee [-v] -s netmap:ifs -d netmap:ifd
 * sudo ./tee -s netmap:ix0 -d netmap:ix1
 */
int
main(int argc, char **argv)
{
	struct pollfd pollfd[2];
	int ch;
	struct nm_desc_t *ps = NULL, *pd = NULL;
	char *ifs = NULL, *ifd = NULL;
	u_int burst = 1024;

	fprintf(stderr, "%s %s built %s %s\n",
		argv[0], version, __DATE__, __TIME__);

	while ( (ch = getopt(argc, argv, "s:d:vb")) != -1) {
		switch (ch) {
		case 'b':	/* burst size */
			burst = atoi(optarg);
			D("burst= %s", optarg);
			break;
		case 's':	/* src interface */
			ifs = optarg;
			D("ifs= %s", optarg);
			break;
		case 'd':	/* dest interface */
			ifd = optarg;
			D("ifd= %s", optarg);
			break;
		case 'v':
			verbose++;
			break;
		default:
			D("bad option %c %s", ch, optarg);
			usage();
			break;
		}

	}

	argc -= optind;
	argv += optind;

	if (burst < 1 || burst > 8192) {
		D("invalid burst %d, set to 1024", burst);
		burst = 1024;
	}

	ps = netmap_open(ifs, 0, 0);
	if (ps == NULL)
		return (1);

	pd = netmap_open(ifd, 0, 0);
	if (pd == NULL) {
		nm_close(ps);
		return (1);
	}

	/* setup poll(2) variables. */
	memset(pollfd, 0, sizeof(pollfd));
	pollfd[0].fd = ps->fd;
	pollfd[1].fd = pd->fd;

	D("Ready to go, %s 0x%x/%d -> %s 0x%x/%d.",
		ps->req.nr_name, ps->first_rx_ring, ps->req.nr_rx_rings,
		pd->req.nr_name, pd->first_tx_ring, pd->req.nr_tx_rings);

	/* main loop */
	signal(SIGINT, sigint_h);
	while (!do_abort) {
		int ret;

		pollfd[0].events = 0;
		pollfd[0].revents = 0;

		pollfd[1].events = 0;
		pollfd[1].revents = 0;

		/* read from 0 */
		pollfd[0].events |= POLLIN;

		/* write to 1 */
		pollfd[1].events |= POLLOUT;

#if 0 // XXX: Do I really NEED this?
		int n0, n1;
		/* check rx queue of src */
		n0 = pkt_queued(ps, 0);

		/*
		 * if there is something in rx queue,
		 * setup src as ready to read from.
		 */
		if (n0)
			pollfd[0].events |= POLLIN;


		/* check tx queue of dst */
		n1 = pkt_queued(pd, 1);

		/*
		 * if there is nothing in tx queue,
		 * setup dst as ready to write to
		 */
		if (n1 == 0)
			pollfd[1].events |= POLLOUT;
#endif

		ret = poll(pollfd, 2, 0);
#if 0
		if (ret <= 0 || verbose)
		    D("poll %s [0] ev %x %x rx %d@%d tx %d,"
			     " [1] ev %x %x rx %d@%d tx %d",
				ret <= 0 ? "timeout" : "ok",
				pollfd[0].events,
				pollfd[0].revents,
				pkt_queued(ps, 0),
				ps->rx->cur,
				pkt_queued(ps, 1),
				pollfd[1].events,
				pollfd[1].revents,
				pkt_queued(pd, 0),
				pd->rx->cur,
				pkt_queued(pd, 1)
			);
#endif
		if (ret < 0) {
			continue;
		}
		if (pollfd[0].revents & POLLERR) {
			D("error on fd0, rx [%d,%d)",
				ps->rx->cur, ps->rx->tail);
		}
		if (pollfd[1].revents & POLLERR) {
			D("error on fd1, tx [%d,%d)",
				pd->tx->cur, pd->tx->tail);
		}

		/*
		 * if src has data to read and
		 * we can write on dst, do it.
		 */
		if ((pollfd[0].revents & POLLIN) &&
		    (pollfd[1].revents & POLLOUT)) {
			move(ps, pd, burst);
			// XXX we don't need the ioctl */
			// ioctl(me[0].fd, NIOCTXSYNC, NULL);
		}
	}

	D("exiting");
	nm_close(ps);
	nm_close(pd);

	return (0);
}
