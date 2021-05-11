/*
    Abhimanyu Singh
	Undergraduate BS CEEN
    Texas A&M University
    Date  : 2/11/21
 */
#include "common.h"
#include "FIFOreqchannel.h"
#include <string>
using namespace std;



int main(int argc, char *argv[]){
	//int pid is the return of the fork, meaning multiple returns
	int pid = fork();
	if(pid == 0) {//when fork is flagged 0, the child process has opened and now ./server will be run using same cmd line args
	    execvp("./server", argv);
	} else {//else when 1, the parent process runs ie the client

		//constructor for clientside FIFO pipeline called chan
		FIFORequestChannel chan ("control", FIFORequestChannel::CLIENT_SIDE);
		
		int opt;
		int p = 1;
		double t = 0.0;
		int e = 1;
		int bufcap = MAX_MESSAGE; //int that stores the max msg value in bytes, m stores the updated bufsize 
		int j = 0;//flag int that stores the conditional bit that chooses between 1 data pt, j datapoints, and filetransfer/newchan/fork
		int c = 0; bool NewChan = false;//flags and sets for a new chan
		string filename = "";

		while ((opt = getopt(argc, argv, "p:t:e:f:j:m:c")) != -1) {
			switch (opt) {
				case 'p':
					p = atoi (optarg);
					break;
				case 't':
					t = atof (optarg);
					break;
				case 'e':
					e = atoi (optarg);
					break;
				case 'f':
					filename = optarg;
					break;
				case 'j':
					j = atoi (optarg);
					break;
				case 'm':
					bufcap = atoi (optarg);
					std::cout << "Buffer capacity has been changed to " << bufcap << endl;
					break;
				case 'c':
					NewChan = true;
					break;
				default:
					break;
			}
		}	


		//user inputted variable checks 
		if ( (t < 0 || t > 59.996) && (t != -1) ) {//invalid t for time in seconds (ex .004 sec)
			EXITONERROR("invalid input for time");
		} 
		if (p < 1 || p > 15) {
			EXITONERROR("Invalid input for person");
		}
		if (e != 1 && e != 2) {
			EXITONERROR("Invalid input for ecgno");
		}

		/////////////////////SINGLE DATAPOINT REQUEST////////////
		if (j == 1)  {//requestiong only 1 datapoint
			//this datamsg will be constructed with values p, t, e incoming from user terminal input
			datamsg* x = new datamsg(p, t, e);//dynamic allocation of datamsg x
		
			chan.cwrite (x, sizeof (datamsg)); //sending to server a datamsg request x


			double* datarec = new double;//double value for ecg of requested patient @ time and ecgno type
			chan.cread (datarec, sizeof(double));//cread from server with data going to pointer to double datarec

			std::cout << "For person " << p <<", at time " << t << ", the value of ecg "<< e <<" is " << *datarec << endl;//outputting the result

			//free heap mem
			delete x;
			delete datarec;
		}
		////////////////////MULTIPLE DATAPOINTS REQUEST///////////
		else if (j > 1) { //request for multiple msgs to output to a csv file
			//timing analysis vars
			struct timeval start;
			struct timeval end;
			gettimeofday(&start, NULL);//start
			//need to open an output file 
			ofstream outfile;
			string person = to_string(p);//cast 
			string filename_mp = "received/x" + person + ".csv";//file + location dir
			outfile.open(filename_mp);//file open

			//now send cwrite to server to request the number of datapoints, with output of both ecgno's
			for (int i = 0; i < j; i++) {
				outfile << t << ",";
				//send request msg to server for ecg1
				datamsg* mp_datarq = new datamsg(p, t, 1);//since we are looking at first ecg and second one
				chan.cwrite(mp_datarq, sizeof(datamsg));
				//read the ecg double value from the buf 
				double* ecg = new double;
				chan.cread(ecg, sizeof(double));
				//send the first value to the x1.csv file
				outfile << *ecg << ",";

				//now repeat for ecgno 2
				datamsg* mp_datarq2 = new datamsg(p, t, 2);//ecg now 2
				chan.cwrite(mp_datarq2, sizeof(datamsg));
				double* ecg2 = new double;
				chan.cread(ecg2, sizeof(double));
				outfile << *ecg2 << endl;

				//free heap memory
				delete mp_datarq;
				delete mp_datarq2;
				delete ecg;
				delete ecg2;
				//increments
				if ( t > 59.996) t=0;//reset when past 59.996 
				t += .004; //next time index 

			}
			outfile.close();//close ofstream outfile 
			gettimeofday(&end, NULL);//end of timing 
			//runtime in us
			double time_taken = (end.tv_sec - start.tv_sec) * 1e6;//us to sec
			time_taken = (time_taken + (end.tv_usec - start.tv_usec)) * 1e-6;//sec to us
			std::cout << "Time taken to request " << j << " datapoints: " << time_taken << setprecision(6) << " seconds" << endl;
			std::cout << "This is equivalent to " << time_taken << setprecision(6) << "e6 microseconds" << endl;
		}
		//////////////////FILE REQUEST/NEW CHANNEL REQUEST/////////////////////	
		else if (j < 1) {	 //requesting file from server if j is not >= 1 then that means that no datapoints are being requested
			/////////////////FILE REQUEST//////////////////
			if (filename != "") {//if the string filename has been written to by the getopt, then enter 
				//timing analysis vars
				struct timeval start;
				struct timeval end;
				gettimeofday(&start, NULL);//start

				filemsg fm(0, 0);//declaring a filemesg with 0 offset and 0 length didnt dynamically allocate due to seg faults
				__int64_t filesize;//filesize to be read from server

				int size = (sizeof(filemsg) + filename.size() + 1);
				char* buf = new char[sizeof(filemsg) + filename.size() + 1]; //allocated a buffer of chars with size of filemsg + filename size + 1bit for c_str
				memcpy (buf, &fm, sizeof(filemsg));//copying filemsg to the buffer first using memcpy
				memcpy(buf + sizeof(filemsg), filename.c_str(), filename.size() + 1);; //copying filename to the buffer as a cstr

				chan.cwrite(buf, sizeof(filemsg) + filename.size() + 1);//send write msg to server entailing buffer and its size 
				chan.cread(&filesize, sizeof(__int64_t));//read in the filesize from the server while giving the buffer capacity 

				ofstream datafile;//io file stream myfile
				string file_name = "received/" + filename;//filename is now in the recived dir 
				datafile.open(file_name, ios::binary);//using i/os specifying out and file type binary 
				__int64_t offset = 0;//offset of buffer, starts at 0

				cout << filesize << endl;
				
				while(filesize > offset) {//while the offset length is less than that of the actual length of the filesize, there is data to be pulled
					int space = filesize-offset;//space variable that holds the length-current offset to represent the length of the file message

					if (space > bufcap) {//the space left is still more than 256, meaning there is a full buffer to send at least 
						filemsg data_fm(offset,bufcap);//data fm offset with length 256

						char* filebuf = new char[sizeof(filemsg) + filename.size() + 1];//data buffer to recieve data
						memcpy (filebuf, &data_fm, sizeof(filemsg));//giving databuffer the filemsg datafm and then the filename on top
						memcpy (filebuf+ sizeof(filemsg), filename.c_str(), filename.size() + 1);
						chan.cwrite(filebuf, sizeof(filemsg) + filename.size() + 1);
						char* datain = new char[bufcap]; //read data buffer of data with size of 256
						chan.cread(datain, bufcap);//read the data into the databuf 

						//now write this datain to the file
						datafile.write(datain, bufcap);

						//increment the offset by 256 bytes/bufcap
						offset += bufcap;

						//free the heap mem
						delete filebuf;
						delete datain;
				
					} else {//less than 256bytes remain, must be handled as a separate case with different bounds (space)bytes instead of bufsize
						filemsg data_fm(offset, space);//data fm len-space is less than 256, starting at offset

						char* filebuf = new char[sizeof(filemsg) + filename.size() + 1];//data buffer to recieve data
						memcpy (filebuf, &data_fm, sizeof(filemsg));//giving filebuf the filemsg datafm and then the filename on top
						memcpy (filebuf + sizeof(filemsg), filename.c_str(), filename.size() +1);
						chan.cwrite(filebuf, sizeof(filemsg) + filename.size() + 1);
						char* datain = new char[space]; //read data buffer of data with size of filespc
						chan.cread(datain, space);//read the data from server with same file size

						//now write this datain to the file
						datafile.write(datain, space);

						//increment the offset by the space (filesize-offset)
						offset += space; 

						//free the heap mem
						delete filebuf;
						delete datain;
						//break;
					}
				}
				//free mem
				delete buf;
				datafile.close();
				//cout << "pass test" << endl;

				//end timing 
				gettimeofday(&end, NULL);
				double time_taken = (end.tv_sec - start.tv_sec) * 1e6;//us to sec
				time_taken = (time_taken + (end.tv_usec - start.tv_usec)) * 1e-6;//sec to us
				cout << "Time taken to request a file transfer: " << time_taken << setprecision(6) << " seconds" << endl;
			}
			/////////////NEW CHANNEL REQUEST////////////////////////////////
			else if (NewChan) { //when -c is flagged, newchan is set true to open a new channel

			//create a msgtyp newchanmsg, wrote to server with the newchannel msg
			MESSAGE_TYPE newchanmsg = NEWCHANNEL_MSG;
			chan.cwrite (&newchanmsg, sizeof(MESSAGE_TYPE));
			//this will hold the new channel's name 
			char* chanbuf = new char[30];//buf is size 30 to match the buf in proc newchan request serverside
			chan.cread(chanbuf, 30);
			FIFORequestChannel chan2 (chanbuf, FIFORequestChannel::CLIENT_SIDE);//chan2 has been opened with name inside buf and clientside


			//Test the channel with some data requests, person 1, time 1.008, ecgno 1
			datamsg* test = new datamsg(1, 1.008, 1);//dynamic allocation of datamsg x
			chan.cwrite (test, sizeof (datamsg)); //sending to server a datamsg request x

			double* datarec = new double;//double value for ecg of requested patient @ time and ecgno type
			chan.cread (datarec, sizeof(double));//cread from server with data going to pointer to double datarec
			cout << "After opening a new channel, now to test for expected val: -0.395" << endl;
			cout << "For person 1 at time 1.008 sec, the value of ecg no 1 is: " << *datarec << endl;//outputting the result
			if(*datarec == -0.395) cout << "Data test on new channel has passed!" << endl;
				
			//free heap mem
			delete test;
			delete datarec;

			//test 2
			datamsg* test2 = new datamsg(10, 14.008, 2);//dynamic allocation of datamsg x
			chan.cwrite (test2, sizeof (datamsg)); //sending to server a datamsg request x

			double* datarec2 = new double;//double value for ecg of requested patient @ time and ecgno type
			chan.cread (datarec2, sizeof(double));//cread from server with data going to pointer to double datarec
			cout << "Now to test for another expected val: -0.195" << endl;
			cout << "For person 10 at time 14.008 sec, the value of ecg no 2 is: " << *datarec2 << endl;//outputting the result
			if(*datarec2 == -0.195) cout << "The second data test has passed" << endl;
				
			//free heap mem
			delete test2;
			delete datarec2;
			}

		}
		///////////CLOSING CHANNEL HANDLER//////////////////	
		MESSAGE_TYPE m = QUIT_MSG;
		chan.cwrite (&m, sizeof (MESSAGE_TYPE));
		usleep(1e6);//wait 1 second after killing server proc to stopping the client proc so that server can cleanup
	}
}
