/*

   uWSGI fastrouter

*/

#include "../../uwsgi.h"
#include "../corerouter/cr.h"

struct uwsgi_fastrouter {
	struct uwsgi_corerouter cr;
} ufr;

extern struct uwsgi_server uwsgi;

struct fastrouter_session {
	struct corerouter_session session;
	int has_key;
};

struct uwsgi_option fastrouter_options[] = {
	{"fastrouter", required_argument, 0, "run the fastrouter on the specified port", uwsgi_opt_corerouter, &ufr, 0},
	{"fastrouter-processes", required_argument, 0, "prefork the specified number of fastrouter processes", uwsgi_opt_set_int, &ufr.cr.processes, 0},
	{"fastrouter-workers", required_argument, 0, "prefork the specified number of fastrouter processes", uwsgi_opt_set_int, &ufr.cr.processes, 0},
	{"fastrouter-zerg", required_argument, 0, "attach the fastrouter to a zerg server", uwsgi_opt_corerouter_zerg, &ufr, 0},
	{"fastrouter-use-cache", optional_argument, 0, "use uWSGI cache as hostname->server mapper for the fastrouter", uwsgi_opt_set_str, &ufr.cr.use_cache, 0},

	{"fastrouter-use-pattern", required_argument, 0, "use a pattern for fastrouter hostname->server mapping", uwsgi_opt_corerouter_use_pattern, &ufr, 0},
	{"fastrouter-use-base", required_argument, 0, "use a base dir for fastrouter hostname->server mapping", uwsgi_opt_corerouter_use_base, &ufr, 0},

	{"fastrouter-fallback", required_argument, 0, "fallback to the specified node in case of error", uwsgi_opt_add_string_list, &ufr.cr.fallback, 0},

	{"fastrouter-use-cluster", no_argument, 0, "load balance to nodes subscribed to the cluster", uwsgi_opt_true, &ufr.cr.use_cluster, 0},

	{"fastrouter-use-code-string", required_argument, 0, "use code string as hostname->server mapper for the fastrouter", uwsgi_opt_corerouter_cs, &ufr, 0},
	{"fastrouter-use-socket", optional_argument, 0, "forward request to the specified uwsgi socket", uwsgi_opt_corerouter_use_socket, &ufr, 0},
	{"fastrouter-to", required_argument, 0, "forward requests to the specified uwsgi server (you can specify it multiple times for load balancing)", uwsgi_opt_add_string_list, &ufr.cr.static_nodes, 0},
	{"fastrouter-gracetime", required_argument, 0, "retry connections to dead static nodes after the specified amount of seconds", uwsgi_opt_set_int, &ufr.cr.static_node_gracetime, 0},
	{"fastrouter-events", required_argument, 0, "set the maximum number of concurrent events", uwsgi_opt_set_int, &ufr.cr.nevents, 0},
	{"fastrouter-quiet", required_argument, 0, "do not report failed connections to instances", uwsgi_opt_true, &ufr.cr.quiet, 0},
	{"fastrouter-cheap", no_argument, 0, "run the fastrouter in cheap mode", uwsgi_opt_true, &ufr.cr.cheap, 0},
	{"fastrouter-subscription-server", required_argument, 0, "run the fastrouter subscription server on the spcified address", uwsgi_opt_corerouter_ss, &ufr, 0},
	{"fastrouter-subscription-slot", required_argument, 0, "*** deprecated ***", uwsgi_opt_deprecated, (void *) "useless thanks to the new implementation", 0},

	{"fastrouter-timeout", required_argument, 0, "set fastrouter timeout", uwsgi_opt_set_int, &ufr.cr.socket_timeout, 0},
	{"fastrouter-post-buffering", required_argument, 0, "enable fastrouter post buffering", uwsgi_opt_set_64bit, &ufr.cr.post_buffering, 0},
	{"fastrouter-post-buffering-dir", required_argument, 0, "put fastrouter buffered files to the specified directory", uwsgi_opt_set_str, &ufr.cr.pb_base_dir, 0},

	{"fastrouter-stats", required_argument, 0, "run the fastrouter stats server", uwsgi_opt_set_str, &ufr.cr.stats_server, 0},
	{"fastrouter-stats-server", required_argument, 0, "run the fastrouter stats server", uwsgi_opt_set_str, &ufr.cr.stats_server, 0},
	{"fastrouter-ss", required_argument, 0, "run the fastrouter stats server", uwsgi_opt_set_str, &ufr.cr.stats_server, 0},
	{"fastrouter-harakiri", required_argument, 0, "enable fastrouter harakiri", uwsgi_opt_set_int, &ufr.cr.harakiri, 0},
	{0, 0, 0, 0, 0, 0, 0},
};

void fr_get_hostname(char *key, uint16_t keylen, char *val, uint16_t vallen, void *data) {

	struct corerouter_peer *peer = (struct corerouter_peer *) data;
	struct fastrouter_session *fr = (struct fastrouter_session *) peer->session;

	//uwsgi_log("%.*s = %.*s\n", keylen, key, vallen, val);
	if (!uwsgi_strncmp("SERVER_NAME", 11, key, keylen) && !peer->key_len) {
		peer->key = val;
		peer->key_len = vallen;
		return;
	}

	if (!uwsgi_strncmp("HTTP_HOST", 9, key, keylen) && !fr->has_key) {
		peer->key = val;
		peer->key_len = vallen;
		return;
	}

	if (!uwsgi_strncmp("UWSGI_FASTROUTER_KEY", 20, key, keylen)) {
		fr->has_key = 1;
		peer->key = val;
		peer->key_len = vallen;
		return;
	}
}

// writing client body to the instance
ssize_t fr_instance_write_body(struct corerouter_peer *peer) {
	ssize_t len = cr_write(peer, "fr_instance_write_body()");
        // end on empty write
        if (!len) return 0;

        // the chunk has been sent, start (again) reading from client and instances
        if (cr_write_complete(peer)) {
                // reset the original read buffer
                peer->out->pos = 0;
                cr_reset_hooks(peer);
        }

        return len;
}


// read client body
ssize_t fr_read_body(struct corerouter_peer *main_peer) {
	ssize_t len = cr_read(main_peer, "fr_read_body()");
        if (!len) return 0;

        main_peer->session->peers->out = main_peer->in;
        main_peer->session->peers->out_pos = 0;

        cr_write_to_backend(main_peer->session->peers, fr_instance_write_body);
        return len;	
}

// write to the client
ssize_t fr_write(struct corerouter_peer *main_peer) {
	ssize_t len = cr_write(main_peer, "fr_write()");
        // end on empty write
        if (!len) return 0;

        // ok this response chunk is sent, let's start reading again
        if (cr_write_complete(main_peer)) {
                // reset the original read buffer
                main_peer->out->pos = 0;
                cr_reset_hooks(main_peer);
        }

        return len;
}

// data from instance
ssize_t fr_instance_read(struct corerouter_peer *peer) {
	ssize_t len = cr_read(peer, "fr_instance_read()");
        if (!len) return 0;

        // set the input buffer as the main output one
        peer->session->main_peer->out = peer->in;
        peer->session->main_peer->out_pos = 0;

        cr_write_to_main(peer, fr_write);
        return len;
}

// send the uwsgi request header and vars
ssize_t fr_instance_send_request(struct corerouter_peer *peer) {
	ssize_t len = cr_write(peer, "fr_instance_send_request()");
        // end on empty write
        if (!len) return 0;

        // the chunk has been sent, start (again) reading from client and instances
        if (cr_write_complete(peer)) {
                // reset the original read buffer
                peer->out->pos = 0;
		// start waiting for body
		peer->session->main_peer->last_hook_read = fr_read_body;
                cr_reset_hooks(peer);
        }

	return len;
}

// instance is connected
ssize_t fr_instance_connected(struct corerouter_peer *peer) {

	cr_peer_connected(peer, "fr_instance_connected()");

	// fix modifiers
	peer->in->buf[0] = peer->session->main_peer->modifier1;
	peer->in->buf[3] = peer->session->main_peer->modifier2;

	// prepare to write the uwsgi packet
	peer->out = peer->session->main_peer->in;
	peer->out_pos = 0;	

	peer->last_hook_write = fr_instance_send_request;
	return fr_instance_send_request(peer);
}

// called after receaving the uwsgi header (read vars)
ssize_t fr_recv_uwsgi_vars(struct corerouter_peer *main_peer) {
	struct uwsgi_header *uh = (struct uwsgi_header *) main_peer->in->buf;
	// increase buffer if needed
	if (uwsgi_buffer_fix(main_peer->in, uh->pktsize+4))
		return -1;
	ssize_t len = cr_read_exact(main_peer, uh->pktsize+4, "fr_recv_uwsgi_vars()");
	if (!len) return 0;

	// headers received, ready to choose the instance
	if (main_peer->in->pos == (size_t)(uh->pktsize+4)) {
		struct uwsgi_corerouter *ucr = main_peer->session->corerouter;

		struct corerouter_peer *new_peer = uwsgi_cr_peer_add(main_peer->session);
		new_peer->last_hook_read = fr_instance_read;

		// find the hostname
		if (uwsgi_hooked_parse(main_peer->in->buf+4, uh->pktsize, fr_get_hostname, (void *) new_peer)) {
			return -1;
		}

		// check the hostname;
		if (new_peer->key_len == 0)
			return -1;

		// find an instance using the key
		if (ucr->mapper(ucr, new_peer))
			return -1;

		// check instance
		if (new_peer->instance_address_len == 0)
			return -1;

		cr_connect(new_peer, fr_instance_connected);
	}

	return len;
}

// called soon after accept()
ssize_t fr_recv_uwsgi_header(struct corerouter_peer *main_peer) {
	ssize_t len = cr_read_exact(main_peer, 4, "fr_recv_uwsgi_header()");
	if (!len) return 0;

	// header ready
	if (main_peer->in->pos == 4) {
		// change the reading default hook
		main_peer->last_hook_read = fr_recv_uwsgi_vars;
		return fr_recv_uwsgi_vars(main_peer);
	}

	return len;
}

// retry connection to the backend
int fr_retry(struct corerouter_peer *peer) {

        struct uwsgi_corerouter *ucr = peer->session->corerouter;

        if (peer->instance_address_len > 0) goto retry;

        if (ucr->mapper(ucr, peer)) {
                return -1;
        }

        if (peer->instance_address_len == 0) {
                return -1;
        }

retry:
        // start async connect (again)
        cr_connect(peer, fr_instance_connected);
        return 0;
}


// called when a new session is created
int fastrouter_alloc_session(struct uwsgi_corerouter *ucr, struct uwsgi_gateway_socket *ugs, struct corerouter_session *cs, struct sockaddr *sa, socklen_t s_len) {
	// set the retry hook
	cs->retry = fr_retry;
	// wait for requests...
	if (uwsgi_cr_set_hooks(cs->main_peer, fr_recv_uwsgi_header, NULL)) return -1;
	return 0;
}

int fastrouter_init() {

	ufr.cr.session_size = sizeof(struct fastrouter_session);
	ufr.cr.alloc_session = fastrouter_alloc_session;
	uwsgi_corerouter_init((struct uwsgi_corerouter *) &ufr);

	return 0;
}

void fastrouter_setup() {
	ufr.cr.name = uwsgi_str("uWSGI fastrouter");
	ufr.cr.short_name = uwsgi_str("fastrouter");
}


struct uwsgi_plugin fastrouter_plugin = {

	.name = "fastrouter",
	.options = fastrouter_options,
	.init = fastrouter_init,
	.on_load = fastrouter_setup
};
