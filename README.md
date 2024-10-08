# p2p-demo
C-based peer-to-peer network prototype that replicated the functionality of a blockchain ledger, ensuring consistent file synchronization across all connected nodes using POSIX standards. This prototype included establishing a socket and listening on a predefined port, handling incoming connections from other peers, transmitting and receiving data through active file descriptors, utilizing threads to monitor the ledger and manage concurrent connections, and implementing mutual exclusion mechanisms to prevent race conditions and ensure data integrity.

INSTRUCTIONS
*Will only work on linux*
Create a new file with the name of your choice and store the p2p.c file in that directory
once stored, run the command "gcc -o p2p p2p.c -lpthread" on the command line to compile the code
then run the code using "./p2p" on the command line. This will start up as a host peer waiting for connections.
If you want to run it as a client then run "./p2p <IP address of host>"
this should create a blockchain.txt file in the current directory
The blockchain.txt files on all peers on the network should be synchronized and if changes are detected, all peers connected will also update to the latest version.
