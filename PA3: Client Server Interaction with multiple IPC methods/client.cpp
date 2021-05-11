/*
    Abhimanyu Singh
    BS CEEN
    Texas A&M University
    Date  : 3/20/21
 */
#include "common.h"
#include <sys/wait.h>
#include "FIFOreqchannel.h"
#include "MQreqchannel.h"
#include "SHMreqchannel.h"
#include <sys/wait.h>

using namespace std;

int main(int argc, char *argv[]) {
    int c;
    int buffercap = MAX_MESSAGE;
    int p = 0, ecg = 1;
    double t = -1.0;
    bool isnewchan = false;
    bool isfiletransfer = false;
    string filename;
    string ipcmethod = "f"; // IPC method to use, 3 choices f, q, m
    int nchannels = 1;      // number of channels to create, control chan defaulted 
    double TotalTime = 0;
    struct timeval start, end;
    
    while ((c = getopt(argc, argv, "p:t:e:m:f:c:i:")) != -1)
    {
        switch (c)
        {
        case 'p':
            p = atoi(optarg);
            break;
        case 't':
            t = atof(optarg);
            break;
        case 'e':
            ecg = atoi(optarg);
            break;
        case 'm':
            buffercap = atoi(optarg);
            break;
        case 'c':
            isnewchan = true;
            nchannels = atoi(optarg);
            break;
        case 'f':
            isfiletransfer = true;
            filename = optarg;
            break;
        case 'i':
            ipcmethod = optarg;
            break;
        }
    }

    string outfilepath = string("received/") + filename;
    FILE* outfile = fopen (outfilepath.c_str(), "wb");  

    // fork part
    if (fork() == 0) { // child
        // passing all args to server
        char *args[] = {"./server", "-m", (char *)to_string(buffercap).c_str(), "-i", (char *)ipcmethod.c_str(), NULL};
        if (execvp(args[0], args) < 0)
        {
            perror("exec filed");
            exit(0);
        }
    }

    // supports all IPC types <f|q|m>

    RequestChannel* control_chan = NULL; //first channel that is opened regardless of -c
    //vector <RequestChannel*> channelStorage (nchannels, NULL); //vector of size nchannels with allnullptrs
    if (ipcmethod == "f") {
        control_chan = new FIFORequestChannel ("control", RequestChannel::CLIENT_SIDE);
    }else if (ipcmethod == "q")
        control_chan = new MQRequestChannel ("control", RequestChannel::CLIENT_SIDE);
    else if (ipcmethod == "m")
        control_chan = new SHMRequestChannel ("control", RequestChannel::CLIENT_SIDE, buffercap);

    RequestChannel *chan = control_chan;
    // creating a vector to hold all created channels for cleeanup
    vector <RequestChannel*> channelStorage (nchannels, NULL);

    //checking if -c is flagged leading to demand for more than 1 channel 
    if (isnewchan) {
        cout << "Using the new channel everything following" << endl;
        for (int i = 0; i < nchannels; i++) {//loop through 0-nchannels to call the correct ammount of channel constructors of type <f,q,m>
            MESSAGE_TYPE m = NEWCHANNEL_MSG;
            control_chan->cwrite(&m, sizeof(m));
            char newchanname[100];
            control_chan->cread(newchanname, sizeof(newchanname));
            // create new channel based on IPC method
            if (ipcmethod == "f")
                chan = new FIFORequestChannel(newchanname, RequestChannel::CLIENT_SIDE);
            else if (ipcmethod == "q") // MQ
                chan = new MQRequestChannel(newchanname, RequestChannel::CLIENT_SIDE);
            else if (ipcmethod == "s") // SHM
                chan = new SHMRequestChannel(newchanname, RequestChannel::CLIENT_SIDE, buffercap);
            channelStorage[i] = chan; // add channel ptr to array
            cout << "New channel by the name " << newchanname << " is created" << endl;
            cout << "All further communication will happen through each channel instead of the main channel" << endl;
        }
    }
    ////////////////now to iterate through each current channel and preform file io////////////////////
    for (int currChannel = 0; currChannel < nchannels; currChannel++) {
        /////////////////////////////////////////////////////////////////
        if (!isfiletransfer){   // requesting data msgs
            if (t >= 0){    // 1 data point
                datamsg d (p, t, ecg);
                chan->cwrite (&d, sizeof (d));
                double ecgvalue;
                chan->cread (&ecgvalue, sizeof (double));
                std::cout << "Ecg " << ecg << " value for patient "<< p << " at time " << t << " is: " << ecgvalue << endl;
            } else {   // bulk (i.e., 1K) data requests 
                struct timeval start, end;   
                gettimeofday(&start, NULL); // start timing
                double ts = 0;  
                datamsg d (p, ts, ecg);
                double ecgvalue;
                for (int i=0; i<1000; i++){
                    chan->cwrite (&d, sizeof (d));
                    chan->cread (&ecgvalue, sizeof (double));
                    d.seconds += 0.004; //increment the timestamp by 4ms
                    std::cout << ecgvalue << endl;
                }

                gettimeofday(&end, NULL); //end of timing anal
                // calculate the time period of 1k pts reception
                double Time;
                Time = (end.tv_sec - start.tv_sec) * 1e6;
                Time = (Time + (end.tv_usec - start.tv_usec)) * 1e-6;
                cout << "The time taken is: " << Time << " seconds." << endl;
                TotalTime += Time;
            }
        } 
        ///////////////////////////////////////////////////////////////////
        else if (isfiletransfer){
            struct timeval start;
            gettimeofday(&start, NULL);
            
            filemsg f (0,0);  // special first message to get file size
            int to_alloc = sizeof (filemsg) + filename.size() + 1; // extra byte for NULL
            char* buf = new char [to_alloc];
            memcpy (buf, &f, sizeof(filemsg));
            memcpy (buf + sizeof(filemsg), filename.c_str(), filename.size()+1);
            strcpy (buf + sizeof (filemsg), filename.c_str());
            chan->cwrite (buf, to_alloc);
            __int64_t filesize;
            chan->cread (&filesize, sizeof (__int64_t));
            std::cout << "File size: " << filesize << endl;

            //int transfers = ceil (1.0 * filesize / MAX_MESSAGE);
            filemsg* fm = (filemsg*) buf;
            double fs = 1.0*filesize;
            int front = round((fs)/ nchannels * currChannel);
            int back = round((fs)/nchannels*(currChannel+1));
            __int64_t rem = back - front;
            fm->offset = front;
            
            char* recv_buffer = new char [MAX_MESSAGE];
            while (rem > 0){ //
                fm->length = (int) min (rem, (__int64_t) MAX_MESSAGE);
                chan->cwrite (buf, to_alloc);
                chan->cread (recv_buffer, MAX_MESSAGE);
                fwrite (recv_buffer, 1, fm->length, outfile);
                rem -= fm->length;
                fm->offset += fm->length;
            }
            
            delete recv_buffer;
            delete buf;
            std::cout << "File transfer completed" << endl;
            //END TIMER
            struct timeval end;
            gettimeofday(&end, NULL); 
            double time_taken = 0; 
            time_taken = (end.tv_sec - start.tv_sec) * 1e6; //online resources 
            time_taken = (time_taken + (end.tv_usec -  start.tv_usec)) * 1e-6; //online resources
            TotalTime += time_taken; //increment the total time by the time taken by the specific channel
            std::cout << "Data exchange for specific channel took: " << time_taken << " seconds." << endl;
        }
    }
    fclose (outfile);

    if (TotalTime > 0) {
        cout << "The Total time taken for this process is: "  << TotalTime << " seconds." << endl;
    }
    MESSAGE_TYPE q = QUIT_MSG;
    for (int i = 0; i < nchannels; i++) {
        channelStorage[i]->cwrite(&q, sizeof(MESSAGE_TYPE));
        delete channelStorage[i];
    }
    // this means that the user requested a new channel, so the control_channel must be destroyed as well
    control_chan->cwrite(&q, sizeof(MESSAGE_TYPE));
    // wait for the child process running server
    // this will allow the server to properly do clean up
    // if wait is not used, the server may sometimes crash
    wait(0);
}