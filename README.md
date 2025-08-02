This tool is designed to automatically monitor changes (such as file creation, modification, or deletion) within a specified source directory and replicate these changes to a target directory in real time or whenever connection is established. It leverages the Linux inotify API to efficiently detect file system events without polling, ensuring minimal resource usage and fast response times.

A. For sync between two folders on the same device in the same directory:
  1. Open a terminal window and navigate to the folder where you want to set up the client folder (Folder 1) and type:
     ```
     git clone https://github.com/davidthedosa/File-Synchronisation-Tool
     ```
     
     
