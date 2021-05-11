
#ifndef _requestchannel_H_
#define _requestchannel_H_
//abstract class rq channel
#include "common.h"

class RequestChannel
{
public:
	enum Side {SERVER_SIDE, CLIENT_SIDE};
	enum Mode {READ_MODE, WRITE_MODE};
	
protected:
	string my_name;
	Side my_side;
	
	int wfd;
	int rfd;
	
	string s1, s2;
	virtual int open_ipc (string _pipe_name, int mode) {;} //virtual method so this function can be overridden
	
public:
	RequestChannel(const string _name, const Side _side): my_name (_name), my_side (_side) {}
	virtual ~RequestChannel() {} //nneds to be virtual because of derived classes constructor to be called, since they are all different
	virtual int cread (void* msgbuf, int bufcapacity) = 0;
	virtual int cwrite(void *msgbuf , int msglen) = 0;
	
 
	string name() {
		return my_name;
	} 
	
};

#endif
