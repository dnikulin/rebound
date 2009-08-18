// Copyright (c) 2009 Dmitri Nikulin, Jeffrey Parsons
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#define PROG "rebound: "
#define ENVVAR "REBOUND_IP"

// For RTLD_NEXT
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Cached reference to real bind() function
typedef int (*bindfunc)(int, const struct sockaddr *, socklen_t);
static bindfunc realbind = NULL;

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	if (realbind == NULL) {
		// Attempt to find real bind
		// This may even be another preload
		void *sym = dlsym(RTLD_NEXT, "bind");

		if (sym == NULL) {
			// Report error and return an error code bind()
			// is permitted to return
			perror(PROG "dlsym");
			return EINVAL;
		}

		// Cache function pointer
		realbind = (bindfunc) sym;
	}

	// Only attempt to use IPv4 addresses
	if (addrlen != sizeof(struct sockaddr_in)) {
		fprintf(stderr, PROG "Unknown addrlen %d, want %d\n",
				(int) addrlen, (int) sizeof(struct sockaddr_in));
		return realbind(sockfd, addr, addrlen);
	}

	// Interpret input address as IPv4
	const struct sockaddr_in *addrin = (const struct sockaddr_in *) addr;

	// Duplicate address structure
	struct sockaddr_in naddr;
	memcpy(&naddr, addrin, sizeof(naddr));

	// Retrieve configuration environment variable
	const char *ip = getenv(ENVVAR);

	// Report error if the environment variable is absent
	if (ip == NULL) {
		fprintf(stderr, PROG "No " ENVVAR " environment variable\n");
		return realbind(sockfd, addr, addrlen);
	}

	// Decode as IPv4
	if (inet_pton(naddr.sin_family, ip, &naddr) != 1) {
		fprintf(stderr, PROG ENVVAR " (%s) is not an IPv4\n", ip);
		return realbind(sockfd, addr, addrlen);
	}

	// Recreate address fields after inet_pton's overrides
	naddr.sin_family = addrin->sin_family;
	naddr.sin_port = addrin->sin_port;

	// Report the effetive call being made
	fprintf(stderr, PROG "Calling bind(%d, %s, %d)\n",
			sockfd, ip, (int) ntohs(naddr.sin_port));

	// Call real bind with new address
	socklen_t nlen = sizeof(naddr);
	int ret = realbind(sockfd, (const struct sockaddr *) &naddr, nlen);

	// Report error if any
	if (ret != 0)
		perror(PROG "bind");
	return ret;
}
