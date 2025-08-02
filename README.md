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
     <img width="1360" height="768" alt="VirtualBox_Ubuntu_VM_Dosa_02_08_2025_14_07_22" src="https://github.com/user-attachments/assets/2af4cdb3-d135-4794-9054-ceec7158d690" />
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
      <img width="1360" height="768" alt="VirtualBox_Ubuntu_VM_Dosa_02_08_2025_14_18_29" src="https://github.com/user-attachments/assets/01163c16-956c-487c-b2b1-f169bc98fb95" />
      
  12. You can check the logs by navigating to the logs folder in File-Synchronisation-Tool

      <img width="1037" height="457" alt="VirtualBox_Ubuntu_VM_Dosa_02_08_2025_14_20_59" src="https://github.com/user-attachments/assets/5640aaf3-96f3-4df9-9b41-4103f040b4e2" />



     
     
