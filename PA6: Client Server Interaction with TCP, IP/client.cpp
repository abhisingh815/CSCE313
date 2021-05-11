/*
    Abhimanyu Singh with the help of Dr. Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date  : 4/19/21
 */
#include "common.h"
#include <sys/wait.h>
#include "TCPreqchannel.h"
#include "BoundedBuffer.h"
#include "HistogramCollection.h"
#include <thread>
#include <iostream>
#include <sys/epoll.h>

using namespace std;



struct Response{
    int person;
    double ecgval;
    Response (int _p, double _e): person (_p), ecgval (_e){;}

};

void timediff (struct timeval& start, struct timeval& end){
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << "." << usecs << " seconds" << endl;
}


void patient_thread_function (int n, int p, BoundedBuffer* reqbuffer){
    double t = 0;
    datamsg d (p, t, 1);
    for (int i=0; i<n; i++){
        reqbuffer->push ((char*) &d, sizeof (d));
        d.seconds += 0.004;
    }
    
}

void histogram_thread_function (BoundedBuffer* responseBuffer, HistogramCollection* hc){
    char buf [1024];
    Response* r = (Response *) buf;
    while (true){
        responseBuffer->pop (buf, 1024);
        if (r->person < 1){ // it means quit
            break;
        }
        hc->update (r->person, r->ecgval);
    }
}

void file_thread_function(string filename, TCPRequestChannel* wchan, BoundedBuffer* requestBuffer, int mb){
    //create the file
    string recvfilename = "received/" + filename;
    //get the length as file
    char buf [1024];
    filemsg f(0,0);
    memcpy(buf, &f, sizeof(f));
    strcpy(buf + sizeof(f), filename.c_str());
    wchan->cwrite(buf, sizeof(f) + filename.size() + 1);
    __int64_t filelen;
    wchan->cread(&filelen, sizeof(filelen));

    FILE* fp = fopen(recvfilename.c_str(), "w");
    fseek (fp, filelen, SEEK_SET);
    fclose(fp);

    filemsg* fm = (filemsg *) buf;
    __int64_t remlen = filelen;

    while(remlen > 0){
        fm->length = min (remlen, (__int64_t) mb);
        requestBuffer->push (buf, sizeof(filemsg) + filename.size() + 1);
        fm->offset += fm->length;
        remlen -= fm->length;
    }
    
}

void event_polling_function (TCPRequestChannel** wchans, BoundedBuffer* requestBuffer, BoundedBuffer* responseBuffer, HistogramCollection* hc, int n, int p, int w, int mb){    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //event poller thread 
    char buf [1024];
    char recvbuf [mb];

    struct epoll_event ev;
    struct epoll_event events[w];

    //creates empty epoll list
    int epollfd = epoll_create1 (0); //returns fd that is the epoll list
    if (epollfd == -1) {
        EXITONERROR ("epoll_create1");
    }

    //need to make a map that holds information on the index of the wchan that is returned back for more work
    unordered_map<int, int> fd_to_index; //map the indexes to the file descriptiors for ease of access when calling wchans [fd]
    vector<vector<char>> state (w);
    bool quit_recv = false; //boolean variable set to false, when this flag is raised, a quit msg is recieved and needs to be handled, using the unordered map

    //priming + adding each fd to the list
    int nsent = 0, nrecv = 0;
    for(int i = 0; i < w; i++){
        int sz = requestBuffer->pop (buf, 1024);
        MESSAGE_TYPE* m = (MESSAGE_TYPE* ) buf;

        if(*m == QUIT_MSG){ //if quit message, boolean variable
            MESSAGE_TYPE q = QUIT_MSG;
            requestBuffer->push((char*) &q, sizeof(q));
        } else {
             wchans[i]->cwrite (buf, sz);
            state [i] = vector<char>(buf, buf+sz); //record the state
            nsent++;
            int rfd = wchans[i]->getfd();
            fcntl(rfd, F_SETFL, O_NONBLOCK);
        
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = rfd;
            fd_to_index [rfd] = i;
                    

            if(epoll_ctl(epollfd, EPOLL_CTL_ADD, rfd, &ev) == -1 ){
                EXITONERROR("epoll_ctl: list_sock");
            }
        }   
    }

    //wait and see
    //this is a infinite loop that will break once a quit is recivied from the buffer and the number of recieved is equal to number of sent, ie done
    while(true){
        //break statement
        if(quit_recv && nrecv == nsent){
            break;
        }

        //epoll wait will wait and give -1 when it is waiting for something to pop up in the eventqueue
        int nfds = epoll_wait(epollfd, events, w, -1);
        if(nfds == -1){
            EXITONERROR("epoll_wait"); //epoll wait
        }
        //goes through each nfds and maps the channel data to an rfd and index, which lets us send these quits since we now know the index
        for(int i = 0; i < nfds; i++){
            int rfd = events[i].data.fd;
            int index = fd_to_index [rfd];

            int resp_sz = wchans[index]->cread(recvbuf, mb);
            nrecv++;

            //processing (recvbuf)
            vector<char> req = state [index];
            char* request = req.data();
            //message type of request typecast
            MESSAGE_TYPE* m = (MESSAGE_TYPE* ) request;


            if(*m == DATA_MSG){
                //push the identifier to the responsebuffer, in the case that it is a datamsg
                //create a response object that will be pushed to the buf, this is the indentifier
                Response r{((datamsg *)request)->person, *(double*) recvbuf}; 
                responseBuffer->push((char* ) &r, sizeof(r));
            } else if (*m ==  FILE_MSG){ // if a file message then use PA4 code for the file message hadling 
                filemsg* fm = (filemsg* ) request;
                string filename = (char *)(fm + 1);
                string recvfilename = "received/" + filename;
                FILE* fp = fopen(recvfilename.c_str(), "r+");  //need to open a file, in read+ mode
                fseek(fp, fm->offset, SEEK_SET);
                fwrite(recvbuf, 1, fm->length, fp);
                fclose(fp);
            } 
            if(!quit_recv){ //if a quit message still hasnt been recieved, then pop the request buffer since there is still some in it, check if it is a quitmsg,
                //if a quit msg then the next while loop will be primed if the nsent == nerecv
                int req_sz = requestBuffer->pop(buf, sizeof(buf));
                if(*(MESSAGE_TYPE*) buf == QUIT_MSG){
                    quit_recv = true;
                } else { //if not a quit message then write the buffer of size req_sz to the wchannel of correct index not i
                    wchans [index]->cwrite (buf, req_sz);
                    state [index] = vector<char> (buf, buf+req_sz); //save the state by writing a vector of chars that contains the size of the reqsize and starts at buf
                    nsent++; //increment the sent by 1
                }            
            }
        }
    }
}




int main(int argc, char *argv[]){
    
    int c;
    int buffercap = MAX_MESSAGE;
    int p = 10, ecg = 1;
    double t = -1.0;
    bool isnewchan = false;
    bool isfiletransfer = false;
    string filename;
    int b = 1024;
    int w = 100; //number of request channels in this PA
    int n = 10000;
    int m = MAX_MESSAGE;
    int h = 3;
    string host; //
    string port;



    while ((c = getopt (argc, argv, "p:t:e:m:f:b:cw:n:h:r:")) != -1){
        switch (c){
            case 'p':
                p = atoi (optarg);
                break;
            case 't':
                t = atof (optarg);
                break;
            case 'e':
                ecg = atoi (optarg);
                break;
            case 'm':
                buffercap = atoi (optarg);
                m = buffercap;
                break;
            case 'c':
                isnewchan = true;
                break;
            case 'f':
                isfiletransfer = true;
                filename = optarg;
                break;
            case 'b':
                b = atoi (optarg);
                break;
            case 'w':
                w = atoi (optarg);
                break;
            case 'n':
                n = atoi (optarg);
                break;
            case 'h':
                host = optarg;
                break;
            case 'r':
                port = optarg;
                break;
        }
    }
    
    // fork part
    BoundedBuffer requestBuffer (b);
	BoundedBuffer responseBuffer (b);
    HistogramCollection hc;


    MESSAGE_TYPE q = QUIT_MSG;

    

    cout << "Creating " << w << " request channels." << endl;
    // make w worker channels (make sure to do it sequentially in the main)
    
    //dynamixally allocation of fiforeqchannels for ease of deletion
    TCPRequestChannel** wchans = new TCPRequestChannel* [w];
    for(int i=0; i<w; i++){
        wchans[i] = new TCPRequestChannel (host, port);
    }
	
	thread hists[h];
    thread patient[p];
    struct timeval start, end;
    gettimeofday (&start, 0);
    //Start all threads here 
    

    if (!isfiletransfer) {
        cout << "Not a File Transfer." << endl;

        cout << "Creating " << h << " Histogram Threads." << endl;
        // making histograms and adding to the histogram collection hc
        for (int i = 0; i < p; i++){
            Histogram* h = new Histogram (10, -2.0, 2.0);
            hc.add (h);
        }

        cout << "Creating " << p << " Patient Threads." << endl;
        for (int i = 0; i < p; i++){
            patient[i] = thread (patient_thread_function, n, i+1, &requestBuffer);
        }

        cout << "Creating the Event Polling Thread." << endl;
        //single thread 
        thread evp (event_polling_function, wchans, &requestBuffer, &responseBuffer, &hc, n, p, w, m);

        cout << "Creating " << h << " Histogram Threads." << endl;
        for (int i =0; i < h; i++) {
            hists[i] = thread (histogram_thread_function, &responseBuffer, &hc);
        }

        cout << "All threads are created, now joining." << endl;
        ///now wait to join threads
        //patients
        for (int i=0; i<p; i++){
            patient[i].join ();
        }
        //evp function quit msg
        requestBuffer.push((char*) &q, sizeof(q));
        evp.join();

        //send quits to histogram collection
        Response r {-1, 0};
        for(int i = 0; i < h; i++){
            responseBuffer.push ((char* ) &r, sizeof(r));
        }

        for(int i = 0; i < h; i++){
            hists [i].join();
        }
        cout << "All threads are joined and done." << endl;

    } else { //file transfer, use ft thread and evp thread only, no hist
        cout << "File Transfer." << endl;
        cout << "Creating the File Thread." << endl;
        thread filethread (file_thread_function, filename, wchans[0], &requestBuffer, m);

        //evp thread 
        cout << "Creating the Event Polling Thread." << endl;
        thread evp (event_polling_function, wchans, &requestBuffer, &responseBuffer, &hc, n, p, w, m);

        cout << "All threads are created, now joining." << endl;
        filethread.join();
        //pushing quits to request buffer for evp
        requestBuffer.push((char*) &q, sizeof(q));
        evp.join();

        cout << "All threads are joined and done." << endl << endl;
    }

    gettimeofday (&end, 0);
    // print time difference
    timediff (start, end);
    cout << "   " << endl;


    //now to deallocate memory from objects that were created in the process of the event thread and other threads 
    //this will primarialy be the channels created that need to be deleted (w chans)
    for (int i = 0; i < w; i++) {
        wchans[i]->cwrite ( (char*) &q, sizeof(MESSAGE_TYPE)); //write this to the w channel
        delete wchans[i];
    }
    //delete the fynamic array
    delete [] wchans;
	
    // print the results
	hc.print ();
    //delete all the histograms 
    hc.deleteHists ();
    // cleaning the main channel

    cout << "All Done!!!" << endl;
}




