/*
	Original author of the starter code
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date: 2/8/20
	
	Please include your Name, UIN, and the date below
	Name: Owen Zhang
	UIN: 934005910
	Date: 09/25/25
*/
#include "common.h"
#include "FIFORequestChannel.h"
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>

using namespace std;


int main (int argc, char *argv[]) {
	int opt;
	int p = 1;        // person number
	double t = 0.0;   // time
	int e = 1;        // ecg number
	
	string filename = "";
	// parse command line args
	while ((opt = getopt(argc, argv, "p:t:e:f:")) != -1) {
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
		}
	}

	// fork server process
	pid_t server_pid = fork();
	if (server_pid == 0) {
		// child runs server
		execl("./server", "server", (char*) NULL);
		exit(1);
	} else if (server_pid < 0) {
		exit(1);
	}
	
	// wait for server to start up
	sleep(1);

    FIFORequestChannel control_chan("control", FIFORequestChannel::CLIENT_SIDE);
	
	// ask server for new channel
	MESSAGE_TYPE new_channel_msg = NEWCHANNEL_MSG;
	control_chan.cwrite(&new_channel_msg, sizeof(MESSAGE_TYPE));
	
	// get channel name back
	char new_channel_name[30];
	control_chan.cread(new_channel_name, 30);
	cout << "Created new channel: " << new_channel_name << endl;
	
	// make new channel for data
	FIFORequestChannel* chan = new FIFORequestChannel(new_channel_name, FIFORequestChannel::CLIENT_SIDE);
	
	// file transfer
	if (!filename.empty()) {
		// first get file size
		filemsg fm(0, 0);
		int len = sizeof(filemsg) + (filename.size() + 1);
		char* buf = new char[len];
		memcpy(buf, &fm, sizeof(filemsg));
		strcpy(buf + sizeof(filemsg), filename.c_str());
		chan->cwrite(buf, len);
		
		// read file size back
		__int64_t file_length;
		chan->cread(&file_length, sizeof(__int64_t));
		cout << "File length: " << file_length << " bytes" << endl;
		
		// make sure received dir exists
		system("mkdir -p received");
		
		// open file for writing
		string output_path = "received/" + filename;
		ofstream outfile(output_path, ios::binary);
		if (!outfile.is_open()) {
			cerr << "Cannot open " << output_path << " for writing" << endl;
			delete[] buf;
			exit(1);
		}
		
		// transfer in pieces
		__int64_t bytes_received = 0;
		__int64_t chunk_size = MAX_MESSAGE - sizeof(filemsg) - filename.size() - 1;
		if (chunk_size <= 0) chunk_size = 1000;  // backup size
		
		while (bytes_received < file_length) {
			__int64_t remaining = file_length - bytes_received;
			__int64_t current_chunk = min((__int64_t)chunk_size, remaining);
			
			// ask for chunk
			filemsg chunk_req(bytes_received, current_chunk);
			memcpy(buf, &chunk_req, sizeof(filemsg));
			strcpy(buf + sizeof(filemsg), filename.c_str());
			chan->cwrite(buf, len);
			
			// get chunk data
			char* chunk_data = new char[current_chunk];
			chan->cread(chunk_data, current_chunk);
			
			// write to file
			outfile.write(chunk_data, current_chunk);
			bytes_received += current_chunk;
			
			delete[] chunk_data;
		}
		
		outfile.close();
		delete[] buf;
		cout << "File transfer complete: " << bytes_received << " bytes received" << endl;
	}
	// batch data collection (just -p flag)
	else if (t == 0.0 && e == 1) {
		// get 1000 points for both ecg1 and ecg2
		system("mkdir -p received");
		ofstream outfile("received/x1.csv");
		if (!outfile.is_open()) {
			cerr << "Cannot open received/x1.csv for writing" << endl;
			exit(1);
		}
		
		// loop through 1000 time points
		for (int i = 0; i < 1000; i++) {
			double time = i * 0.004;
			
			// get ecg1 data
			datamsg data_req1(p, time, 1);
			char buf1[MAX_MESSAGE];
			memcpy(buf1, &data_req1, sizeof(datamsg));
			chan->cwrite(buf1, sizeof(datamsg));
			double ecg1_reply;
			chan->cread(&ecg1_reply, sizeof(double));
			
			// get ecg2 data
			datamsg data_req2(p, time, 2);
			char buf2[MAX_MESSAGE];
			memcpy(buf2, &data_req2, sizeof(datamsg));
			chan->cwrite(buf2, sizeof(datamsg));
			double ecg2_reply;
			chan->cread(&ecg2_reply, sizeof(double));
			
			// save to csv
			outfile << time << "," << ecg1_reply << "," << ecg2_reply << endl;
		}
		outfile.close();
		cout << "Collected 1000 data points for person " << p << " and saved to x1.csv" << endl;
	} else {
		// single data point
		datamsg data_req(p, t, e);
		char buf[MAX_MESSAGE];
		memcpy(buf, &data_req, sizeof(datamsg));
		chan->cwrite(buf, sizeof(datamsg));
		double reply;
		chan->cread(&reply, sizeof(double));
		cout << "For person " << p << ", at time " << t << ", the value of ecg " << e << " is " << reply << endl;
	}
	
	// closing the channels
    MESSAGE_TYPE m = QUIT_MSG;
    chan->cwrite(&m, sizeof(MESSAGE_TYPE));
    
    // close control channel too
    control_chan.cwrite(&m, sizeof(MESSAGE_TYPE));
    
    // free the channel
    delete chan;
    
    // wait for server to finish
    int status;
    waitpid(server_pid, &status, 0);
    
    // clean up fifo files
    system("rm -f data*_*");
}
