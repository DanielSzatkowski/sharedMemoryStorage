# sharedMemoryStorage
Storage for collecting user's messages using shared memory and semaphores.

## Usage
Firstly, You have to run compiled server with 2 arguments: existing file and maximum numbers of messages.
For example:
```
./server.out server.c 10
```
You can check messages from users using CTRL + Z and close program (the memory will be freed) using CTRL + C.

Now there is a possibility for store Your message in a shared memory. For that You should run client programm with argument: name of existing file, that was used for setting up server.
Example:
```
./client.out server.c
```
