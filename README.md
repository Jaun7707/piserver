A minor side project mimicking the functionality of Dropbox. I had this setup on my raspberry pi and my
Macbook in terminal. Simply run the make file on both machines, then run the piserver on the pi, and the piclient
on each of the client machines.

Current functionality includes:
• Multiway file synchronization between machines.
• Backup functionality for deleted files.
• File modification check.

Left off working on:
• Boot up file change check.
• Folder recursion.

Known issues:
• Under certain circumstances the clients will play hot potatoe with the file. One uploads, other downloads, repeat.
