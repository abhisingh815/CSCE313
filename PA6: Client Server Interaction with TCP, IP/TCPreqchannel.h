
#ifndef _TCPreqchannel_H_
#define _TCPreqchannel_H_

#include "common.h"
#include <sys/socket.h>
#include <netdb.h>
#include <string>
#include <thread>

class TCPRequestChannel
{
private:
	int sockfd;
	
public:
	TCPRequestChannel(const string hostname, const string port);

	TCPRequestChannel(int fd);

	~TCPRequestChannel();

	int cread (void* msgbuf, int bufcapacity);
	
	int cwrite(void *msgbuf , int msglen);

	int getfd ();
};

#endif
