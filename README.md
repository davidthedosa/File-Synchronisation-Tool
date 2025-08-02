This tool is designed to automatically monitor changes (such as file creation, modification, or deletion) within a specified source directory and replicate these changes to a target directory in real time or whenever connection is established. It leverages the Linux inotify API to efficiently detect file system events without polling, ensuring minimal resource usage and fast response times.

A. For sync between two folders on the same device in the same directory:
  1. Open a terminal window and navigate to the folder where you want to set up the parent directory and type:
     ```
     git clone https://github.com/davidthedosa/File-Synchronisation-Tool
     ```
  2. Change directory to move into the directory with the tool.
     ```
     cd File-Synchronisation-Tool
     ```
  3. Type the following to compile the server code.
     ```
     gcc tcp_server.c -o server1
     ```

     _Alternatively you can use the ```make``` command to skip steps 3 and 4_
  4. Type the following to compile the client code.
     ```
     gcc tcp_client.c -o client1
     ```
  5. Initialize the server
     ```
     ./server1
     ```
  6. Open another terminal window and navigate to the same directory to initialize the client.
     ```
     ./client1
     ```
  7. Type 'y' to connect locally and customize the log file name if you want to in both server and client windows.
     <img width="1360" height="768" alt="initialization" src="https://github.com/user-attachments/assets/2af4cdb3-d135-4794-9054-ceec7158d690" />
     Connection has been established at this step.
          
  9. Open another terminal window and navigate to the same directory and change to the client_dir.
      ```
      cd client_dir
      ```
  10. Open another terminal window and navigate to the same directory and change to the server_dir.
      ```
      cd server_dir
      ```
  11. Now you can work with your files in any folder of your choice and the same will be synced in the other folder automatically in real time.
      <img width="1360" height="768" alt="example of sync of 1 file" src="https://github.com/user-attachments/assets/01163c16-956c-487c-b2b1-f169bc98fb95" />
      
  12. You can check the logs by navigating to the logs folder in File-Synchronisation-Tool

      <img width="1037" height="457" alt="log file" src="https://github.com/user-attachments/assets/5640aaf3-96f3-4df9-9b41-4103f040b4e2" />

B. For sync across two folders in two different machines connected on same local network.
  
  I. For Device 1 (Server):
  1. Follow the same steps described in A from A.1-A.3
  2. Open another terminal and note down the ip address of the server system by typing the following command:
     ```
     ip address
     ```

  <img width="1050" height="477" alt="ip address of server system" src="https://github.com/user-attachments/assets/f0ce6dc0-90ab-480f-9d5b-26873320a3f3" />
  
  3. Initialize the server
     ```
     ./server1
     ```

  II. For Device 2 (Client):
  
  1. Follow the same steps described in A from A.1-A.2, A.4.
  2. Initialize the client
     ```
     ./client1
     ```
  3. Type 'n' to connect to server on local network and enter the server ip address noted down earlier and customize the log file name if you want to in both server and client machines.

  <img width="1017" height="286" alt="connection establishment between two devices" src="https://github.com/user-attachments/assets/3c81d876-b25e-4ac5-ab15-5e3b386c5673" />

  4. Replicate A.9 and A.10 on respective machines to sync effectively.
     <img width="1360" height="768" alt="Client machine" src="https://github.com/user-attachments/assets/3a6d21f3-05f7-4466-9b66-0c0f5b4f4018" />
     <img width="1672" height="515" alt="server machine" src="https://github.com/user-attachments/assets/d506cfe7-10c8-43af-9390-c66c1a388b8d" />


    
