/*
 * Copyright (C) 2012  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 *
 * This file uses code from fpm_stub.c
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#include <errno.h>
#include <syslog.h>
#include <assert.h>

#include "fpm_lsp.h"
#include "FPMServer.hh"

/*
 * create_listen_sock
 *
 * Returns 0 on success, or -1 on error.
 */
int FPMServer::create_listen_sock(int port, int *sock_p) {
    char error[BUFSIZ];
    int sock;
    struct sockaddr_in addr;
    int reuse;

    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_ERR, "FPM Failed to create socket: %s", error);
        return -1;
    }

    reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse,
                     sizeof(reuse)) < 0) {
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_WARNING, "FPM Failed to set reuse addr option: %s", error);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_ERR, "FPM Failed to bind to port %d: %s", port, error);
        close(sock);
        return -1;
    }

    if (listen(sock, 5)) {
        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_ERR, "FPM Failed to listen on socket: %s", error);
        close(sock);
        return -1;
    }

    *sock_p = sock;
    return 0;
}

/*
 * accept_conn
 */
int FPMServer::accept_conn(int listen_sock) {
    char error[BUFSIZ];
    int sock;
    struct sockaddr_in client_addr;
    unsigned int client_len;

    while (1) {
        syslog(LOG_INFO, "FPM Waiting for client connection...");
        client_len = sizeof(client_addr);
        sock = accept(listen_sock, (struct sockaddr *) &client_addr,
                        &client_len);

        if (sock >= 0) {
            syslog(LOG_INFO, "FPM Accepted client %s",
                   inet_ntoa(client_addr.sin_addr));
            return sock;
        }

        strerror_r(errno, error, BUFSIZ);
        syslog(LOG_ERR, "FPM Failed to accept socket: %s", error);
    }
}

/*
 * read_fpm_msg
 */
fpm_msg_hdr_t *
FPMServer::read_fpm_msg(char *buf, size_t buf_len) {
    char *cur, *end;
    int need_len, bytes_read, have_len;
    fpm_msg_hdr_t *hdr;
    bool malformed_msg;

    end = buf + buf_len;
    cur = buf;
    hdr = (fpm_msg_hdr_t *) buf;

    while (true) {
        malformed_msg = true;

        have_len = cur - buf;

        if (have_len < FPM_MSG_HDR_LEN) {
            need_len = FPM_MSG_HDR_LEN - have_len;
        } else {
            need_len = fpm_msg_len(hdr) - have_len;
            assert(need_len >= 0 && need_len < (end - cur));

            if (!need_len) {
                return hdr;
            }

            malformed_msg = false;
        }

        syslog(LOG_DEBUG, "FPM Looking to read %d bytes", need_len);
        bytes_read = read(this->sock, cur, need_len);

        if (bytes_read <= 0) {
            char error[BUFSIZ];
            strerror_r(errno, error, BUFSIZ);
            syslog(LOG_ERR, "FPM Error reading from socket: %s", error);
            return NULL;
        }

        syslog(LOG_DEBUG, "FPM Read %d bytes", bytes_read);
        cur += bytes_read;

        if (bytes_read < need_len) {
            continue;
        }

        assert(bytes_read == need_len);

        if (!malformed_msg) {
            return hdr;
        }

        if (!fpm_msg_ok(hdr, buf_len)) {
            syslog(LOG_ERR, "FPM Malformed fpm message");
            return NULL;
        }
    }
}

void FPMServer::print_nhlfe(const nhlfe_msg_t *msg) {
    const char *op = (msg->table_operation == ADD_LSP)? "ADD_NHLFE" :
                     (msg->table_operation == REMOVE_LSP)? "REMOVE_NHLFE" :
                     "UNKNOWN";
    const char *type = (msg->nhlfe_operation == PUSH)? "PUSH" :
                       (msg->nhlfe_operation == POP)? "POP" :
                       (msg->nhlfe_operation == SWAP)? "SWAP" :
                       "UNKNOWN";
    const uint8_t *data = reinterpret_cast<const uint8_t*>(&msg->next_hop_ip);
    IPAddress ip(msg->ip_version, data);

    syslog(LOG_INFO, "fpm->%s %s %s %d %d", op, ip.toString().c_str(), type,
           ntohl(msg->in_label), ntohl(msg->out_label));
}

/*
 * process_fpm_msg
 */
void FPMServer::process_fpm_msg(fpm_msg_hdr_t *hdr) {
    syslog(LOG_INFO, "FPM message - Type: %d, Length %d", hdr->msg_type,
            ntohs(hdr->msg_len));

    /**
     * Note: NHLFE and FTN are not standardised in Quagga 0.9.22. These are
     * subject to change, and correspond to the FIMSIM application found here:
     *
     * http://github.com/ofisher/FIMSIM
     */
    if (hdr->msg_type == FPM_MSG_TYPE_NETLINK) {
        struct nlmsghdr *n = (nlmsghdr *) fpm_msg_data(hdr);

        if (n->nlmsg_type == RTM_NEWROUTE || n->nlmsg_type == RTM_DELROUTE) {
            table->updateRouteTable(n);
        }
    } else if (hdr->msg_type == FPM_MSG_TYPE_NHLFE) {
        nhlfe_msg_t *lsp_msg = (nhlfe_msg_t *) fpm_msg_data(hdr);
        print_nhlfe(lsp_msg);
        table->updateNHLFE(lsp_msg);
    } else if (hdr->msg_type == FPM_MSG_TYPE_FTN) {
        syslog(LOG_WARNING, "FPM FTN not yet implemented");
    } else {
        syslog(LOG_WARNING, "FPM Unknown fpm message type %u", hdr->msg_type);
    }
}

/*
 * fpm_serve
 */
void FPMServer::fpm_serve() {
    char buf[FPM_MAX_MSG_LEN];
    fpm_msg_hdr_t *hdr;
    while (1) {
        hdr = this->read_fpm_msg(buf, sizeof(buf));
        if (!hdr) {
            return;
        }
        this->process_fpm_msg(hdr);
    }
}

FPMServer::FPMServer(FlowTable *ft) {
    this->table = ft;
}

void FPMServer::operator()() {
    if (this->create_listen_sock(FPM_DEFAULT_PORT, &this->server_sock)) {
        syslog(LOG_CRIT, "FPMServer couldn't open a server socket. Exiting.");
        exit(EXIT_FAILURE);
    }

    while (true) {
        this->sock = this->accept_conn(this->server_sock);
        this->fpm_serve();
        syslog(LOG_INFO, "FPM Done serving client");
    }
}
