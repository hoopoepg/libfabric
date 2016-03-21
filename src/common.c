/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2006 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2013 Intel Corp., Inc.  All rights reserved.
 * Copyright (c) 2015 Los Alamos Nat. Security, LLC. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include <complex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/time.h>

#include <fi_signal.h>
#include <rdma/fi_errno.h>
#include <fi.h>


int fi_wait_cond(pthread_cond_t *cond, pthread_mutex_t *mut, int timeout)
{
#ifndef _WIN32
	struct timespec ts;

	if (timeout < 0)
		return pthread_cond_wait(cond, mut);

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout / 1000;
	ts.tv_nsec += (timeout % 1000) * 1000000;
	return pthread_cond_timedwait(cond, mut, &ts);
#else /* _WIN32 */
	return !SleepConditionVariableCS(cond, mut, (DWORD)timeout);
#endif /* _WIN32 */
}

int fi_read_file(const char *dir, const char *file, char *buf, size_t size)
{
#ifndef _WIN32
	char *path;
	int fd, len;

	if (asprintf(&path, "%s/%s", dir, file) < 0)
		return -1;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		free(path);
		return -1;
	}

	len = read(fd, buf, size);
	close(fd);
	free(path);

	if (len > 0 && buf[len - 1] == '\n')
		buf[--len] = '\0';

	return len;
#else /* _WIN32 */
	char *path;
	DWORD len;

	if (asprintf(&path, "%s/%s", dir, file) < 0)
		return -1;

	HANDLE fd = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	free(path);
	if (fd == INVALID_HANDLE_VALUE) {
		return -1;
	}

	BOOL res = ReadFile(fd, buf, (DWORD)size, &len, 0);
	CloseHandle(fd);

	if (!res) {
		return -1;
	}

	return (int)len;
#endif /* _WIN32 */
}

int fi_poll_fd(int fd, int timeout)
{
	struct pollfd fds;
	int ret;

	fds.fd = fd;
	fds.events = POLLIN;
	ret = poll(&fds, 1, timeout);
	return ret == -1 ? -errno : ret;
}

uint64_t fi_tag_bits(uint64_t mem_tag_format)
{
	return UINT64_MAX >> (ffsll(htonll(mem_tag_format)) -1);
}

uint64_t fi_tag_format(uint64_t tag_bits)
{
	return FI_TAG_GENERIC >> (ffsll(htonll(tag_bits)) - 1);
}

static const size_t fi_datatype_size_table[] = {
	[FI_INT8]   = sizeof(int8_t),
	[FI_UINT8]  = sizeof(uint8_t),
	[FI_INT16]  = sizeof(int16_t),
	[FI_UINT16] = sizeof(uint16_t),
	[FI_INT32]  = sizeof(int32_t),
	[FI_UINT32] = sizeof(uint32_t),
	[FI_INT64]  = sizeof(int64_t),
	[FI_UINT64] = sizeof(uint64_t),
	[FI_FLOAT]  = sizeof(float),
	[FI_DOUBLE] = sizeof(double),
	[FI_FLOAT_COMPLEX]  = sizeof(float complex),
	[FI_DOUBLE_COMPLEX] = sizeof(double complex),
	[FI_LONG_DOUBLE]    = sizeof(long double),
	[FI_LONG_DOUBLE_COMPLEX] = sizeof(long double complex),
};

size_t fi_datatype_size(enum fi_datatype datatype)
{
	if (datatype >= FI_DATATYPE_LAST) {
		errno = FI_EINVAL;
		return 0;
	}
	return fi_datatype_size_table[datatype];
}

int fi_send_allowed(uint64_t caps)
{
	if (caps & FI_MSG ||
		caps & FI_TAGGED) {
		if (caps & FI_SEND)
			return 1;
		if (caps & FI_RECV)
			return 0;
		return 1;
	}

	return 0;
}

int fi_recv_allowed(uint64_t caps)
{
	if (caps & FI_MSG ||
		caps & FI_TAGGED) {
		if (caps & FI_RECV)
			return 1;
		if (caps & FI_SEND)
			return 0;
		return 1;
	}

	return 0;
}

int fi_rma_initiate_allowed(uint64_t caps)
{
	if (caps & FI_RMA ||
		caps & FI_ATOMICS) {
		if (caps & FI_WRITE ||
			caps & FI_READ)
			return 1;
		if (caps & FI_REMOTE_WRITE ||
			caps & FI_REMOTE_READ)
			return 0;
		return 1;
	}

	return 0;
}

int fi_rma_target_allowed(uint64_t caps)
{
	if (caps & FI_RMA ||
		caps & FI_ATOMICS) {
		if (caps & FI_REMOTE_WRITE ||
			caps & FI_REMOTE_READ)
			return 1;
		if (caps & FI_WRITE ||
			caps & FI_READ)
			return 0;
		return 1;
	}

	return 0;
}

uint64_t fi_gettime_ms(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return now.tv_sec * 1000 + now.tv_usec / 1000;
}

#ifndef _WIN32
int fi_fd_nonblock(int fd)
{
	long flags = 0;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		return -errno;
	}

	if(fcntl(fd, F_SETFL, flags | O_NONBLOCK))
		return -errno;

	return 0;
}
#endif /* _WIN32 */

#ifndef HAVE_EPOLL

int fi_epoll_create(struct fi_epoll **ep)
{
	*ep = calloc(1, sizeof(struct fi_epoll));
	return *ep ? 0 : -FI_ENOMEM;
}

int fi_epoll_add(struct fi_epoll *ep, int fd, void *context)
{
	struct pollfd *fds;
	void *contexts;

	if (ep->nfds == ep->size) {
		fds = calloc(ep->size + 64,
			     sizeof(*ep->fds) + sizeof(*ep->context));
		if (!fds)
			return -FI_ENOMEM;

		ep->size += 64;
		contexts = fds + ep->size;

		memcpy(fds, ep->fds, ep->nfds * sizeof(*ep->fds));
		memcpy(contexts, ep->context, ep->nfds * sizeof(*ep->context));
		free(ep->fds);
		ep->fds = fds;
		ep->context = contexts;
	}

	ep->fds[ep->nfds].fd = fd;
	ep->fds[ep->nfds].events = POLLIN;
	ep->context[ep->nfds++] = context;
	return 0;
}

int fi_epoll_del(struct fi_epoll *ep, int fd)
{
	int i;

	for (i = 0; i < ep->nfds; i++) {
		if (ep->fds[i].fd == fd) {
			ep->fds[i].fd = ep->fds[ep->nfds - 1].fd;
			ep->context[i] = ep->context[--ep->nfds];
      			return 0;
		}
  	}
	return -FI_EINVAL;
}

void *fi_epoll_wait(struct fi_epoll *ep, int timeout)
{
	int i, ret;

	ret = poll(ep->fds, ep->nfds, timeout);
	if (ret <= 0)
		return NULL;

	for (i = ep->index; i < ep->nfds; i++) {
		if (ep->fds[i].revents)
			goto found;
	}
	for (i = 0; i < ep->index; i++) {
		if (ep->fds[i].revents)
			goto found;
	}
	return NULL;
found:
	ep->index = i;
	return ep->context[i];
}

void fi_epoll_close(struct fi_epoll *ep)
{
	if (ep) {
		free(ep->fds);
		free(ep);
	}
}

#endif
