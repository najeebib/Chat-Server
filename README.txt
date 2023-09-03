id: 319051066
name: najeeb ibraheem
exercise name: Event-Driven Chat Server

submited files:
1. chatServer.c: the chatserver implementation

Run the code: ./server <s> 
s is the port number of the server

This is a simple chat server, where clients can talk with others.

The program takes as input a number which will the port number for the server we made.
The program runs indiffently waiting for clients to connect, and when they do the program puts their sockets in a fdset,
and check if they are ready to read or write with select().
When a client is ready to read the server reads input from that socket and saves the message to th other connection, and when the other connection are ready to write
the server writes the messages to all other connection.


