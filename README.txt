Read me about Link State Routing

Author: Yisha Wu

In this application, I have built a link state routing protocol that implements reliable flooding and Dijkstra's forward search algorithm.

To compile the source code, input "make all"
To run the application, input "./routed_LS <router ID> <log file> <init file>
To exit the application, just input "ctrl c" in any router application, and every other routers will exit with saved log file.

In my application, each LS router periodically advertise its link state to each of its neighbors every 5 seconds.
All the port numbers that routers using are declared in the given configure file.
To implement the synchronization of listening and connecting, I set all the sockets to non-block. And after the router launched, it try to connect all its neighbors. If it failed, the router knows that its neighbor hasn't started and it starts to listening and try to accept the potential connection.

The format of the log file is as follows:
<time when the routing table changed>
<the updated routing table>
<the lsp that cause the routing table changed>
<where this lsp came from>
To obtain the better display effect of the routing table, I prefer the tab size of 8 spaces. 

And you need to exit the application after at least 20 seconds when the last router launched, which is to make sure that every router has received all the other routers' LSP and finished its own routing table. 
I also print the content of the log file to stdout, so you can see the updating process of routing table more easily. And when the stdout of all the routers are not changing, which means that all the routing tables are converged properly, you can input "ctrl c" to exit.

Be careful that in my application, the operation of writing log file is incremental. That means that every time you run the application, the log of changing routing table will be saved in the same log file if you input the same log file name.
