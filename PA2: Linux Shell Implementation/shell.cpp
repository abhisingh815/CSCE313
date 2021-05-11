//Abhimanyu Singh
// 3/4/21
// Dr. Tanzir
// CSCE 313 - 506

#include <vector>
#include <string>
#include <cstring>
#include <stdio.h>
#include <iostream>
#include <sys/types.h> 
#include <unistd.h> 
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

using namespace std;

string trim (string input) {
    bool space =true;//bool to check if blank space
    while (space) {
        if (input[input.size()-1] == ' ') { //check end of string
            input = input.substr(0, input.size()-1); //removes the empty space
        } if (input[0] == ' ') {
            input = input.substr(1); //removes 1st char
        } else {
            space = false;
        }
    }
    return input;//string is now trimmed
}

char** vec_to_char_array (vector<string>& parts) { //makes an array of char pointers from a vector of string parts
    char **array = new char *[parts.size() + 1];
    for (int i = 0; i < parts.size(); i++)
    {
        array[i] = new char[parts[i].size()];
        strcpy(array[i], parts[i].c_str());
    }
    array[parts.size()] = NULL;
    return array;
}


vector<string> split (string line, string separator = " ") {
    vector<string> splitstring;
    int position = 0;
    position = line.find(separator); //index of the separator

    while (position != string::npos ) {
        string part = trim( line.substr(0, position) ); //string that will be pushed 
        splitstring.push_back(part); //pushed the appropriate string into the vector
        line.erase(0, position+1); //account for the separator as well from 0 to the separator inclusive
        position = line.find(separator);
    }
    splitstring.push_back( trim(line) );
    return splitstring;
}



int main () {
    char* dirbuf[100];
    vector<int> bps; //vector of background processes, needed to kill off zombie processes by keeping track of them 
    dup2(0, 10); //backup
    while (true) {
        cout << getenv("USER") << "$ "; //shell prompt with actual user's name
        
        dup2(10, 0); //retrive backup

        string inputLine;//string to hold shell user input
        getline (cin, inputLine);//gets command from user inputted line 
        char dirbuf[1000];
        string currentDir = getcwd(dirbuf, sizeof(dirbuf)); //string holds curr dir

        //exit shell
        if (inputLine == string("exit")) { //command to exit shell
            cout << "User Exiting Shell" << endl;
            return 0;
        }
        
       //zombie killer
        for (int i = 0; i < bps.size(); i++) {
            if (waitpid(bps[i], 0, WNOHANG) == bps[i]) { // kill the zombie
                cout << "Background Process: " << bps[i] << " has ended" << endl;   
                bps.erase(bps.begin() + i);
                i--;
            }
        }

        vector<string> cdpipe = split(inputLine, "|"); //vector that holds the split of the command line for each indivisual command
        for(int i = 0; i < cdpipe.size(); i++) { //go through each command in the vector 
            int fds[2]; //array of 2 integers used for multiple command pipe with fds[0] and fds[1]
            pipe(fds); //pipe for process communication, process 1 -> process 2
            int pid = fork(); //parent child process by fork

            bool bp = false; //background process check
            int pos = cdpipe[i].find("&");
            if (pos != string::npos) {
                cout << "Handling a Background Process" << endl;
                cdpipe[i] = trim(cdpipe[i].substr(0, pos-1));
                bp = true;
            }
            
            ///////////////////////////////
            if (pid == 0) { //child process
                inputLine = cdpipe[i]; 
                //cd
                if (trim(inputLine).find("cd") == 0) { //if inputline contains cd at the front    
                    vector<string> dir = split(inputLine, " ");
                    string PATH = dir[1]; //the target directory is the second index of the inputLine vector, space parsed
                    //cd
                    if (PATH == "-") { //goes to previous directory and prints directory
                        cout << dirbuf << endl; //print prev dir
                        chdir(dirbuf); //change to prev dir
                        continue; //continue to next loop/prompt user for new shell input
                    } else if (PATH == "~") { //go to home dir
                        char* Home = getenv("HOME");
                        cout << Home<<endl;
                        chdir(Home);
                        continue;
                    } else { //not -, change to new directory that is of path = string directory
                        getcwd(dirbuf, sizeof(dirbuf)); //puts current directory into the buf
                        chdir(PATH.c_str()); //convert to useable type in this case using c_str for char* 
                        continue; //new shell prompt
                    }
                }
                //pwd
                if (trim(inputLine).find("pwd") == 0) { //if inputline contains cd at the front    
                    getcwd(dirbuf, sizeof(dirbuf)); //puts current directory into the buf
                    cout << dirbuf << endl;
                    continue;
                }                
                
                
                //Output redirection >
                int writeIndex = inputLine.find(">");
                if (writeIndex >= 0) {
                    string cmd = trim(inputLine.substr(0, writeIndex));
                    string fileName = trim(inputLine.substr(writeIndex+1));
                    
                    inputLine = cmd; //now the vector index at i contains the string of the command only
                    //file out redirection
                    int fd = open(fileName.c_str(), O_WRONLY | O_CREAT | O_RDONLY, S_IWUSR | S_IRUSR);
                    dup2(fd, 1); //duplicate fd value to dt index 1, which is the std write index
                    close(fd);
                }
                //input redirection <
                int readIndex = inputLine.find("<");
                if (readIndex >= 0) {
                    string cmd = trim(inputLine.substr(0, readIndex));
                    string fileName = trim(inputLine.substr(readIndex+1));
                    inputLine = cmd; //now the vector index at i contains the string of the command only
                    //file in read redirection
                    int fd = open(fileName.c_str(), O_RDONLY | O_CREAT, S_IWUSR | S_IRUSR);
                    dup2(fd, 0); //duplicate fd value to dt index 0, which is the std read
                    close(fd);
                }

                //pipe communication 
                if(i < cdpipe.size()-1) { //last process doesnt communicate to another process via pipe since it is at the end of the pipeline 
                    dup2(fds[1], 1); //fds[1] is the write descriptor, copying to 1 for the communication pipe to b linked for writing
                }
                
                vector<string> arguments = split(inputLine);
                char** args = vec_to_char_array(arguments);
                execvp (args[0], args);
            }

            else { //parent process
                if (!bp) { //if no background process, waitpid "sleep"
                    if (i == cdpipe.size()-1) {//last child prcoess
                        waitpid (pid, 0,0);
                    } else {
                        bps.push_back(pid);
                    }
                } else { //if is a bp, push into the bps vector for later zombie reapiing
                        bps.push_back(pid); //pushes pid into the vector 
                }
                    dup2(fds[0], 0);
                    close(fds[1]);
            }
        }       
    }
}