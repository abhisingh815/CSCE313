#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "FIFOreqchannel.h"
#include <thread>
#include <sys/wait.h>
using namespace std;


void patient_thread_function(int p, int n, BoundedBuffer* requestBuffer){
    /* What will the patient threads do? */
    datamsg d (p, 0.00, 1);
    for (int i = 0; i < n; i++) {
        requestBuffer->push ((char*) &d, sizeof (datamsg));
        d.seconds += .004;
    }
}
struct Response {
    int person;
    double ecgno;
};

void worker_thread_function(BoundedBuffer* requestBuffer, FIFORequestChannel* wchan, BoundedBuffer* responseBuffer, int mb) {
    char buf [1024];

    char recvbuf [mb];

    while (true) {
        requestBuffer->pop(buf, 1024);
        MESSAGE_TYPE* m = (MESSAGE_TYPE*) buf;
        if(*m == QUIT_MSG) {
            wchan->cwrite (m, sizeof(MESSAGE_TYPE));
            delete wchan; //dealocate
            break;
        }
        if (*m == DATA_MSG) {
            datamsg* d = (datamsg*) buf;
            wchan->cwrite(buf, sizeof(datamsg));
            double ecgno;
            wchan->cread(&ecgno, sizeof(double));
            Response r {d->person, ecgno};
            responseBuffer->push ((char*)&r, sizeof (r));
        } else if (*m == FILE_MSG) {
            /// 
            filemsg *fm = (filemsg *) buf;
            string filename = (char *) (fm+1); //filename with sizing for cstr
            int size = sizeof(filemsg) + filename.size() + 1;
            wchan->cwrite(buf, size);
            wchan->cread(recvbuf, mb);

            string recvfname = "recv/" + filename;

            FILE* fp = fopen (recvfname.c_str(), "r+");
            fseek (fp, fm->offset, SEEK_SET);
            fwrite (recvbuf, 1, fm->length, fp);
            fclose (fp);
        }
    }
    
}
void histogram_thread_function (BoundedBuffer* responseBuffer, HistogramCollection* hc){
    char buf [1024];
    while (true) {
        responseBuffer->pop (buf, 1024);
        Response* r = (Response*) buf;
        if (r->person == -1) {
            break;
        }
        hc->update (r->person, r->ecgno);
    }
}

void file_thread_function(string fname,BoundedBuffer* request_buffer, FIFORequestChannel* chan, int mb ){
    //1. Create the file
    string recvfname = "recv/" + fname;
    //Need to make the file as long as the original length
    char buf [1024];
    filemsg fm (0,0);
    memcpy(buf, &fm, sizeof(fm));
    strcpy (buf + sizeof(fm), fname.c_str());
    chan->cwrite (buf, sizeof(fm) + fname.size() + 1);
    __int64_t filelength;
    chan->cread (&filelength, sizeof(filelength));

    FILE* fp = fopen(recvfname.c_str(), "w");
    fseek (fp, filelength, SEEK_SET);
    fclose (fp);

    //2. Generate all of the required file messages
    filemsg* filem = (filemsg*) buf;

    while(filelength > 0){
        if (filelength < mb) { // once the file length is less than the msg size
            mb = filelength;
        }
        filem->length = mb;
        request_buffer->push (buf, sizeof(filemsg) + fname.size() + 1);
        filem->offset += mb;
        filelength -= mb;
    }
}


int main(int argc, char *argv[])
{
    bool filetransfer = false;
    string filename = "";
    int n = 15000;    //default number of requests per "patient"
    int p = 10;     // number of patients [1,15]
    int h = 10;
    int w = 100;    //default number of worker threads
    int b = 1024; 	// default capacity of the request buffer, you should change this default
	int m = MAX_MESSAGE; 	// default capacity of the message buffer
    srand(time_t(NULL));
    
    int opt =-1;
    while ((opt = getopt(argc, argv, "m:n:b:w:p:h:f:")) != -1){
        switch(opt){
            case 'm':
                m = atoi(optarg);
                break;
            case 'n':
                n = atoi(optarg);
                break;
            case 'p':
                p = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 'w':
                w = atoi(optarg);
                break;
            case 'h':
                h = atoi(optarg);
                break;
            case 'f':
                filename = optarg;
                filetransfer = true;
                break;
        }
    }
    
    int pid = fork();
    if (pid == 0){
		// modify this to pass along m
        execl ("server", "server", (char *)NULL);
    }
    cout << "started" << endl;
	FIFORequestChannel* chan = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE);
    BoundedBuffer request_buffer(b);
    BoundedBuffer response_buffer(b);

	HistogramCollection hc;

    thread patients [p];
    thread workers [w];
	thread hists [h]; 
    FIFORequestChannel* wchans[w];

    //creating the p number of histograms 
    for (int i = 0; i < p; i++) { //number of patients to make histograms, so that for each patient a histogram is made
        Histogram* h = new Histogram (10, -2.0, 2.0); //create a new histogram with 10 bins from -2.0 to 2.0
        hc.add (h); //add the histogram to histogram collection hc
    }
    cout << h << " Histograms are made and added to the Histogram Collection" << endl;

    for (int i = 0; i < w; i++) {
        MESSAGE_TYPE m = NEWCHANNEL_MSG;
        chan->cwrite (&m, sizeof (m));
        char newchanname [100];
        chan->cread (newchanname, sizeof (newchanname));
        wchans [i] = new FIFORequestChannel (newchanname, FIFORequestChannel::CLIENT_SIDE);
    }
    cout << w << " worker chanels made" << endl;

    struct timeval start, end;
    gettimeofday (&start, 0);

    thread filethread;
    if (filetransfer) {
        filethread = thread (file_thread_function, filename, &request_buffer, chan, m);
        cout << "A File Thread is made" << endl;
    } else {
        for (int i = 0; i < p; i++) {
            patients[i] = thread (patient_thread_function, i+1, n, &request_buffer);
        }
        cout << p << " Patient Threads made" << endl;
    }

    for (int i = 0; i < w; i++) {
        workers[i] = thread (worker_thread_function, &request_buffer, wchans[i], &response_buffer, m);
    }
    cout << w << " Worker Threads made" << endl;

    for (int i = 0; i < h; i++) {
        hists[i] = thread (histogram_thread_function, &response_buffer, &hc);
    }
    cout << h << " Histogram Threads made" << endl;

    if (filetransfer) {
        filethread.join();
        cout << "The File Thread is completed and joined" << endl;
    } else {
        //wait for patient threads 
        for (int i = 0; i < p; i++) {
            patients[i].join();
            
        }
        cout << "Patient Threads are done and joined" << endl;
    }
    

	//push quit messages at the end of every request buffer after patients are done
    //this signals to worker threads 
    for (int i = 0; i < w; i++) {
        MESSAGE_TYPE q = QUIT_MSG;
        request_buffer.push ((char *)&q, sizeof (MESSAGE_TYPE));
    }
    cout << "Workers are signaled" << endl;

    //wait for worker threads now 
    for (int i = 0; i < w; i++) {
        workers[i].join();
    }

    //now workers are done
    //histogram threads can recognize kill signals (-1)
    Response r {-1, 0}; //kill response 
    for (int i = 0; i < w; i++) {
        response_buffer.push ( (char *) &r, sizeof(r) );
    }
    cout << "All Worker Threads are joined and Histogram threads are signaled" << endl;

    //once all the kill signals are sent
    //wait for the hist threads 
    for (int i = 0; i < h; i++) {
        hists[i].join();
    }
    cout << "All Threads including Histogram Threads are completed and joined" << endl << endl;
    gettimeofday (&end, 0);


    // print the results
    cout << "Printing a graph of all Histograms" << endl << endl;
	hc.print ();
    cout << endl << endl;
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took approximately " << secs << "." << usecs << " seconds" << endl;

    //clean up worker threads 
    for (int i = 0; i < w; i++) {
        MESSAGE_TYPE q = QUIT_MSG;
        wchans[i]->cwrite ( (char *) q, sizeof(MESSAGE_TYPE));
    }



    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    cout << "All Done!!!" << endl << endl;
    wait (0); // add wait statement to make sure so server can quit channels and exit successfully 
    delete chan;
    
}
