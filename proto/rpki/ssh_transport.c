/*
 *	BIRD -- The Resource Public Key Infrastructure (RPKI) to Router Protocol
 *
 *	(c) 2015 CZ.NIC
 *
 *	This file was part of RTRlib: http://rpki.realmv6.org/
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "utils.h"
#include "ssh_transport.h"
#include "lib/libssh.h"

#include "rpki.h"

static int tr_ssh_open(void *tr_ssh_sock);
static void tr_ssh_close(void *tr_ssh_sock);
static void tr_ssh_free(struct tr_socket *tr_sock);
static const char *tr_ssh_ident(void *socket);

int tr_ssh_open(void *socket)
{
  struct tr_ssh_socket *ssh_socket = socket;
  struct rpki_cache *cache = ssh_socket->cache;

  const char *err_msg;
  if ((err_msg = load_libssh()) != NULL)
  {
    CACHE_TRACE(D_EVENTS, cache, "%s", err_msg);
    return TR_ERROR;
  }

  sock *sk = cache->sk;
  sk->type = SK_SSH_ACTIVE;
  sk->ssh = mb_allocz(sk->pool, sizeof(struct ssh_sock));
  sk->ssh->username = cache->cfg->ssh->username;
  sk->ssh->client_privkey_path = cache->cfg->ssh->bird_private_key;
  sk->ssh->server_hostkey_path = cache->cfg->ssh->cache_public_key;
  sk->ssh->subsystem = "rpki-rtr";
  sk->ssh->state = BIRD_SSH_CONNECT;

  if (sk_open(sk) != 0)
    return TR_ERROR;

  return TR_SUCCESS;
}

void tr_ssh_close(void *tr_ssh_sock)
{
  struct tr_ssh_socket *socket = tr_ssh_sock;
  struct rpki_cache *cache = socket->cache;

  sock *sk = cache->sk;
  if (sk && sk->ssh)
  {
    if (sk->ssh->channel)
    {
      if (ssh_channel_is_open(sk->ssh->channel))
	ssh_channel_close(sk->ssh->channel);
      ssh_channel_free(sk->ssh->channel);
      sk->ssh->channel = NULL;
    }

    if (sk->ssh->session)
    {
      ssh_disconnect(sk->ssh->session);
      ssh_free(sk->ssh->session);
      sk->ssh->session = NULL;
    }
    mb_free(sk->ssh);
    sk->ssh = NULL;
  }
}

void tr_ssh_free(struct tr_socket *tr_sock)
{
  struct tr_ssh_socket *tr_ssh_sock = tr_sock->socket;

  if (tr_ssh_sock)
  {
    if (tr_ssh_sock->ident != NULL)
      mb_free(tr_ssh_sock->ident);
    mb_free(tr_ssh_sock);
    tr_sock->socket = NULL;
  }
}

const char *tr_ssh_ident(void *socket)
{
  ASSERT(socket != NULL);

  struct tr_ssh_socket *ssh = socket;
  struct rpki_cache *cache = ssh->cache;

  if (ssh->ident != NULL)
    return ssh->ident;

  const char *username = cache->cfg->ssh->username;
  const char *host = cache->cfg->hostname;

  size_t len = strlen(username) + 1 + strlen(host) + 1 + 5 + 1; /* <user> + '@' + <host> + ':' + <port> + '\0' */
  ssh->ident = mb_alloc(cache->p->p.pool, len);
  if (ssh->ident == NULL)
    return NULL;
  snprintf(ssh->ident, len, "%s@%s:%u", username, host, cache->cfg->port);
  return ssh->ident;
}

int tr_ssh_init(struct rpki_cache *cache)
{
  struct rpki_proto *p = cache->p;
  struct tr_socket *tr_socket = cache->rtr_socket->tr_socket;

  tr_socket->close_fp = &tr_ssh_close;
  tr_socket->free_fp = &tr_ssh_free;
  tr_socket->open_fp = &tr_ssh_open;
  tr_socket->ident_fp = &tr_ssh_ident;

  tr_socket->socket = mb_allocz(p->p.pool, sizeof(struct tr_ssh_socket));
  struct tr_ssh_socket *ssh = tr_socket->socket;
  ssh->cache = cache;

  return TR_SUCCESS;
}