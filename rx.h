//control
int audio_init(void); //initialize audio devices
void audio_fin(void); //close audio devices
//runtime
int rx(int type); //receiving

//int recvSamplesFromNetwork(short pcmSampleArrayInt[3240], int sock, struct sockaddr_in server_addr);